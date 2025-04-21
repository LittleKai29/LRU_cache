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
#include <sstream>   // For parsing config and splitting strings
#include <algorithm> // For std::find, std::remove, std::stoi, std::stoul
#include <cctype>    // For std::isspace

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
            std::cout << "[CacheService] Received GET request for key: " << request->key() << std::endl;
            std::optional<std::string> value_opt = lru_cache_.get(request->key());

            // *** ADD EXTRA DEBUG LOGGING ***
            if (value_opt.has_value()) {
                std::cout << "  DEBUG: LRUCache::get returned value: '" << value_opt.value() << "'" << std::endl;
            } else {
                std::cout << "  DEBUG: LRUCache::get returned std::nullopt" << std::endl;
            }
            // *** END EXTRA DEBUG LOGGING ***


            if (value_opt.has_value()) {
                response->set_value(value_opt.value());
                response->set_found(true);
                std::cout << "  Found value: " << value_opt.value() << std::endl;
            } else {
                response->set_value("");
                response->set_found(false);
                std::cout << "  Key not found or expired." << std::endl;
            }
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

// --- Helper function to trim whitespace ---
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first) {
        return str;
    }
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
}

// --- Configuration Structure ---
struct ServerConfig {
    std::string listen_address = "0.0.0.0:50051";
    std::size_t capacity = 10;
    int ttl_seconds = 60;
    std::string wal_file = "cache.wal";
    std::vector<std::string> replica_addresses; // Empty means replica mode
};

// --- Configuration Parsing Function ---
bool loadConfig(const std::string& filename, ServerConfig& config) {
    std::ifstream config_file(filename);
    if (!config_file.is_open()) {
        std::cerr << "Warning: Could not open config file '" << filename << "'. Using default settings." << std::endl;
        return true;
    }

    std::cout << "Loading configuration from: " << filename << std::endl;
    std::string line;
    int line_num = 0;
    while (std::getline(config_file, line)) {
        line_num++;

        // *** ADD: Remove comments first ***
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos); // Keep only the part before '#'
        }

        line = trim(line); // Trim whitespace AFTER removing comment
        if (line.empty()) { // Skip empty lines (including lines that were only comments)
            continue;
        }

        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            std::cerr << "Warning: Skipping malformed line " << line_num << " in config: " << line << std::endl;
            continue;
        }

        std::string key = trim(line.substr(0, equals_pos));
        std::string value = trim(line.substr(equals_pos + 1));

        // --- Rest of the parsing logic remains the same ---
        if (key == "listen_address") {
            config.listen_address = value;
        } else if (key == "capacity") {
            try {
                config.capacity = std::stoul(value);
                if (config.capacity == 0) { /* handle 0 capacity */ config.capacity = 1; }
            } catch (const std::exception& e) { /* handle error */ }
        } else if (key == "ttl_seconds") {
             try {
                config.ttl_seconds = std::stoi(value);
            } catch (const std::exception& e) { /* handle error */ }
        } else if (key == "wal_file") {
            config.wal_file = value;
        } else if (key == "replica_addresses") {
            config.replica_addresses.clear(); // Clear previous entries if key is found again
            if (!value.empty()) {
                std::stringstream ss(value);
                std::string segment;
                while (std::getline(ss, segment, ',')) {
                    std::string trimmed_addr = trim(segment);
                    if (!trimmed_addr.empty()) {
                        config.replica_addresses.push_back(trimmed_addr);
                    }
                }
            }
        } else {
             std::cerr << "Warning: Skipping unknown configuration key '" << key << "' at line " << line_num << std::endl;
        }
    }
    return true;
}


// --- Server Runner (Modified) ---
// No longer takes config parameters directly, uses the global config struct implicitly or explicitly
void RunServer(LRUCache& cache_instance, const ServerConfig& config) { // Pass config struct
    CacheServiceImpl service(cache_instance, config.replica_addresses); // Pass replicas to service

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    builder.AddListeningPort(config.listen_address, grpc::InsecureServerCredentials()); // Use config listen address

    builder.RegisterService(static_cast<CacheService::Service*>(&service));
    builder.RegisterService(static_cast<ReplicationService::Service*>(&service));

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << config.listen_address << std::endl; // Log configured address
    std::cout << "Using WAL file: " << config.wal_file << std::endl;
    if (!config.replica_addresses.empty()) {
         std::cout << "Operating in PRIMARY mode." << std::endl;
         for(const auto& addr : config.replica_addresses) {
             std::cout << "  - Replicating to: " << addr << std::endl;
         }
    } else {
         std::cout << "Operating in REPLICA mode." << std::endl;
    }

    server->Wait();
}

// --- Main Server Entry Point (Modified) ---
int main(int argc, char** argv) {
    // --- Load Configuration ---
    ServerConfig config;
    std::string config_filename = "cache_config.cfg"; // Default config file name
    // Optional: Allow overriding config file path via command line argument
    if (argc > 1) {
        config_filename = argv[1];
        std::cout << "Using configuration file specified on command line: " << config_filename << std::endl;
    }
    if (!loadConfig(config_filename, config)) {
        std::cerr << "FATAL: Failed to load configuration. Exiting." << std::endl;
        return 1;
    }

    // --- Create Cache Instance using loaded config ---
    LRUCache shared_cache(config.capacity, config.ttl_seconds);
    std::cout << "LRU Cache initialized (Capacity: " << config.capacity << ", TTL: " << config.ttl_seconds << "s)" << std::endl;

    // --- Load State from WAL using loaded config ---
    if (!LRUCache::loadFromWAL(config.wal_file, shared_cache)) {
         std::cerr << "FATAL: Failed to load state from WAL '" << config.wal_file << "'. Exiting." << std::endl;
         return 1;
    }
    std::cout << "Cache state after WAL recovery: ";
    shared_cache.print();

    // --- Open WAL File for Appending using loaded config ---
    std::ofstream wal_file_stream(config.wal_file, std::ios::app);
    if (!wal_file_stream.is_open()) {
        std::cerr << "FATAL: Could not open WAL file '" << config.wal_file << "' for appending." << std::endl;
        return 1;
    }
    shared_cache.setWalStream(&wal_file_stream);
    std::cout << "WAL stream attached to cache instance." << std::endl;

    // --- Run the gRPC server using loaded config ---
    RunServer(shared_cache, config); // Pass the config struct

    std::cout << "Server shutting down." << std::endl;
    return 0;
}
