// src/cache_server.cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <thread>        // For replication worker
#include <queue>         // For replication queue
#include <mutex>         // For queue mutex
#include <condition_variable> // For queue CV
#include <atomic>        // For stopping workers
#include <chrono>        // For sleep/timeouts

// gRPC Headers
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

// Generated Proto Headers (now includes replication service)
#include "cache.grpc.pb.h"

// Your LRU Cache Header
#include "lru_cache.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

// Using the generated C++ namespace
using cache::CacheService;
using cache::DeleteRequest;
using cache::DeleteResponse;
using cache::GetRequest;
using cache::GetResponse;
using cache::PutRequest;
using cache::PutResponse;
// Replication types
using cache::ReplicationService;
using cache::ReplicationRequest;
using cache::ReplicationResponse;


// --- Structure for Replication Task ---
struct ReplicationTask {
    ReplicationRequest request;
};

// --- Combined Service Implementation ---
// Implements BOTH CacheService (for clients) AND ReplicationService (for primary)
class CacheServiceImpl final : public CacheService::Service, public ReplicationService::Service {
private:
    LRUCache& lru_cache_; // Reference to the local cache instance

    // --- Replication Members (only used if this server is PRIMARY) ---
    std::vector<std::unique_ptr<ReplicationService::Stub>> replica_stubs_;
    std::queue<ReplicationTask> replication_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> replication_workers_;
    std::atomic<bool> stop_replication_{false};

    // --- Replication Worker Logic ---
    void ReplicationWorkerLoop() {
        while (!stop_replication_) {
            ReplicationTask task;
            { // --- Dequeue Task ---
                std::unique_lock<std::mutex> lock(queue_mutex_);
                // Wait until queue is not empty OR stop is requested
                queue_cv_.wait(lock, [this] { return !replication_queue_.empty() || stop_replication_; });

                if (stop_replication_ && replication_queue_.empty()) {
                    return; // Exit if stopped and queue is drained
                }

                task = std::move(replication_queue_.front());
                replication_queue_.pop();
            } // Unlock queue mutex

            // --- Send to Replicas ---
            // Simple approach: send to all replicas sequentially in this worker
            // More advanced: use multiple workers, handle failures better
            for (const auto& stub : replica_stubs_) {
                ReplicationResponse reply;
                ClientContext context;
                // Set a deadline for the RPC call
                auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(500); // 500ms timeout
                context.set_deadline(deadline);

                std::cout << "[Replicator] Sending " << (task.request.op_type() == ReplicationRequest::PUT ? "PUT" : "DEL")
                          << " key=" << task.request.key() << " to replica..." << std::endl;

                Status status = stub->ApplyOperation(&context, task.request, &reply);

                if (!status.ok()) {
                    std::cerr << "[Replicator] ERROR replicating key=" << task.request.key()
                              << ": " << status.error_code() << ": " << status.error_message() << std::endl;
                    // TODO: Implement retry logic or mark replica as down?
                } else if (!reply.success()) {
                     std::cerr << "[Replicator] ERROR: Replica failed to apply key=" << task.request.key() << std::endl;
                } else {
                     std::cout << "[Replicator] Successfully replicated key=" << task.request.key() << std::endl;
                }
            }
        }
         std::cout << "[Replicator] Worker thread exiting." << std::endl;
    }

public:
    // Constructor now takes replica addresses (empty if this is not a primary)
    explicit CacheServiceImpl(LRUCache& cache, const std::vector<std::string>& replica_addrs)
        : lru_cache_(cache)
    {
        if (!replica_addrs.empty()) {
            std::cout << "Initializing primary mode with " << replica_addrs.size() << " replicas." << std::endl;
            stop_replication_ = false;
            for (const auto& addr : replica_addrs) {
                std::cout << "  - Creating stub for replica at: " << addr << std::endl;
                auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
                replica_stubs_.push_back(ReplicationService::NewStub(channel));
            }
            // Start replication worker thread(s)
            // For simplicity, start one worker thread
            replication_workers_.emplace_back(&CacheServiceImpl::ReplicationWorkerLoop, this);
             std::cout << "Replication worker thread started." << std::endl;
        } else {
             std::cout << "Initializing replica mode (no replication targets)." << std::endl;
        }
    }

    // Destructor to stop replication workers
    ~CacheServiceImpl() {
        if (!replica_stubs_.empty()) { // Only if primary
             std::cout << "Stopping replication workers..." << std::endl;
            stop_replication_ = true;
            queue_cv_.notify_all(); // Wake up workers so they can check stop flag
            for (auto& worker : replication_workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
             std::cout << "Replication workers stopped." << std::endl;
        }
    }

    // --- CacheService Implementation (Client-facing) ---

    Status Get(ServerContext* context, const GetRequest* request,
               GetResponse* response) override {
        // No changes needed for Get
        std::cout << "[CacheService] Received GET request for key: " << request->key() << std::endl;
        std::optional<std::string> value_opt = lru_cache_.get(request->key());
        // ... (rest of Get implementation as before) ...
        if (value_opt.has_value()) { /* set response */ } else { /* set response */ }
        return Status::OK;
    }

    Status Put(ServerContext* context, const PutRequest* request,
               PutResponse* response) override {
         std::cout << "[CacheService] Received PUT request for key: " << request->key()
                   << " value: " << request->value() << std::endl;

        // 1. Apply locally (writes to WAL)
        if (!lru_cache_.put(request->key(), request->value())) {
            response->set_success(false);
            std::cout << "  Local Put failed (likely WAL error)." << std::endl;
            return Status(StatusCode::INTERNAL, "Local operation failed, potentially due to WAL error.");
        }

        // 2. If primary, enqueue for asynchronous replication
        if (!replica_stubs_.empty()) {
            ReplicationTask task;
            task.request.set_op_type(ReplicationRequest::PUT);
            task.request.set_key(request->key());
            task.request.set_value(request->value());

            { // Enqueue task
                std::lock_guard<std::mutex> lock(queue_mutex_);
                replication_queue_.push(std::move(task));
                 std::cout << "  Enqueued PUT key=" << request->key() << " for replication." << std::endl;
            }
            queue_cv_.notify_one(); // Notify a worker thread
        }

        // 3. Return success to client immediately
        response->set_success(true);
        std::cout << "  Local Put successful. Acknowledged client." << std::endl;
        return Status::OK;
    }

     Status Delete(ServerContext* context, const DeleteRequest* request,
                   DeleteResponse* response) override {
        std::cout << "[CacheService] Received DELETE request for key: " << request->key() << std::endl;

        // 1. Apply locally (writes to WAL)
        if (!lru_cache_.remove(request->key())) {
            response->set_success(false);
            std::cout << "  Local Delete failed (likely WAL error)." << std::endl;
            return Status(StatusCode::INTERNAL, "Local operation failed, potentially due to WAL error.");
        }

         // 2. If primary, enqueue for asynchronous replication
        if (!replica_stubs_.empty()) {
            ReplicationTask task;
            task.request.set_op_type(ReplicationRequest::DEL);
            task.request.set_key(request->key());
            // Value is not needed for DEL

            { // Enqueue task
                std::lock_guard<std::mutex> lock(queue_mutex_);
                replication_queue_.push(std::move(task));
                 std::cout << "  Enqueued DEL key=" << request->key() << " for replication." << std::endl;
            }
            queue_cv_.notify_one(); // Notify a worker thread
        }

        // 3. Return success to client immediately
        response->set_success(true);
        std::cout << "  Local Delete successful. Acknowledged client." << std::endl;
        return Status::OK;
    }

    // --- ReplicationService Implementation (Called by Primary on Replicas) ---

    Status ApplyOperation(ServerContext* context, const ReplicationRequest* request,
                          ReplicationResponse* response) override {
        std::cout << "[ReplicationService] Received ApplyOperation: "
                  << (request->op_type() == ReplicationRequest::PUT ? "PUT" : "DEL")
                  << " key=" << request->key() << std::endl;

        bool success = false;
        if (request->op_type() == ReplicationRequest::PUT) {
            // Apply PUT locally, using 'is_recovery=true' to prevent WAL write/re-replication
            success = lru_cache_.applyReplicatedPut(request->key(), request->value());
        } else if (request->op_type() == ReplicationRequest::DEL) {
            // Apply DEL locally, using 'is_recovery=true'
            success = lru_cache_.applyReplicatedRemove(request->key());
        } else {
            std::cerr << "  ERROR: Unknown operation type received." << std::endl;
            response->set_success(false);
            return Status(StatusCode::INVALID_ARGUMENT, "Unknown operation type");
        }

        if (success) {
            std::cout << "  Successfully applied replicated operation locally." << std::endl;
            response->set_success(true);
            return Status::OK;
        } else {
             std::cerr << "  ERROR: Failed to apply replicated operation locally." << std::endl;
            response->set_success(false);
            // Return OK status but indicate failure in response, or return INTERNAL error?
            // Let's return OK but success=false, primary will log it.
            return Status::OK;
        }
    }
};

// --- Server Runner ---
// Now takes replica addresses
void RunServer(LRUCache& cache_instance, const std::string& wal_filename, const std::vector<std::string>& replica_addrs) {
    std::string server_address("0.0.0.0:50051"); // Make this configurable?
    CacheServiceImpl service(cache_instance, replica_addrs); // Pass replicas to service

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    // *** Register BOTH services ***
    builder.RegisterService(static_cast<CacheService::Service*>(&service));
    builder.RegisterService(static_cast<ReplicationService::Service*>(&service));

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    std::cout << "Using WAL file: " << wal_filename << std::endl;
    if (!replica_addrs.empty()) {
         std::cout << "Operating in PRIMARY mode." << std::endl;
    } else {
         std::cout << "Operating in REPLICA mode." << std::endl;
    }

    server->Wait(); // Blocks here
    // Service destructor runs when server is shut down, stopping workers
}

// --- Main Server Entry Point ---
int main(int argc, char** argv) {
    // --- Configuration ---
    const std::size_t cache_capacity = 10;
    const int ttl_seconds = 60;
    const std::string wal_filename = "cache.wal"; // Base WAL name

    // --- Parse Replica Addresses (Simple Example) ---
    std::vector<std::string> replica_addrs;
    // Example: ./cache_server replica1:50051 replica2:50051
    // Skip argv[0] (program name)
    for (int i = 1; i < argc; ++i) {
        replica_addrs.push_back(argv[i]);
    }

    // --- Create Cache Instance ---
    LRUCache shared_cache(cache_capacity, ttl_seconds);
    std::cout << "LRU Cache initialized (Capacity: " << cache_capacity << ", TTL: " << ttl_seconds << "s)" << std::endl;

    // --- Load State from WAL ---
    if (!LRUCache::loadFromWAL(wal_filename, shared_cache)) {
         std::cerr << "FATAL: Failed to load state from WAL. Exiting." << std::endl;
         return 1;
    }
    std::cout << "Cache state after WAL recovery: ";
    shared_cache.print();

    // --- Open WAL File for Appending ---
    std::ofstream wal_file_stream(wal_filename, std::ios::app);
    if (!wal_file_stream.is_open()) {
        std::cerr << "FATAL: Could not open WAL file '" << wal_filename << "' for appending." << std::endl;
        return 1;
    }
    shared_cache.setWalStream(&wal_file_stream);
    std::cout << "WAL stream attached to cache instance." << std::endl;

    // --- Run the gRPC server ---
    // Pass replica addresses. If empty, server acts as a replica.
    RunServer(shared_cache, wal_filename, replica_addrs);

    std::cout << "Server shutting down." << std::endl;
    return 0;
}