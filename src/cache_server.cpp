// src/cache_server.cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <fstream> // For std::ofstream

// gRPC Headers
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

// Generated Proto Headers
#include "cache.grpc.pb.h"

// Your LRU Cache Header
#include "lru_cache.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode; // For specific error codes

// Using the generated C++ namespace
using cache::CacheService;
using cache::DeleteRequest;
using cache::DeleteResponse;
using cache::GetRequest;
using cache::GetResponse;
using cache::PutRequest;
using cache::PutResponse;

// Logic and data behind the server's behavior.
class CacheServiceImpl final : public CacheService::Service {
private:
    LRUCache& lru_cache_; // Reference to the cache instance

public:
    explicit CacheServiceImpl(LRUCache& cache) : lru_cache_(cache) {}

    Status Get(ServerContext* context, const GetRequest* request,
               GetResponse* response) override {
        std::cout << "Server received GET request for key: " << request->key() << std::endl;
        // Use the public get method which calls get_sync internally
        std::optional<std::string> value_opt = lru_cache_.get(request->key());

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
         std::cout << "Server received PUT request for key: " << request->key()
                   << " value: " << request->value() << std::endl;

        // Use the public put method which calls put_sync and handles WAL
        if (lru_cache_.put(request->key(), request->value())) {
            response->set_success(true);
            std::cout << "  Put successful." << std::endl;
            return Status::OK;
        } else {
            // WAL write likely failed
            response->set_success(false);
             std::cout << "  Put failed (likely WAL error)." << std::endl;
            return Status(StatusCode::INTERNAL, "Operation failed, potentially due to WAL error.");
        }
    }

     Status Delete(ServerContext* context, const DeleteRequest* request,
                   DeleteResponse* response) override {
        std::cout << "Server received DELETE request for key: " << request->key() << std::endl;

        // Use the public remove method which calls remove_sync and handles WAL
        if (lru_cache_.remove(request->key())) {
            response->set_success(true);
            std::cout << "  Delete successful." << std::endl;
            return Status::OK;
        } else {
            // WAL write likely failed
            response->set_success(false);
            std::cout << "  Delete failed (likely WAL error)." << std::endl;
            return Status(StatusCode::INTERNAL, "Operation failed, potentially due to WAL error.");
        }
    }
};

// --- Server Runner (Unchanged) ---
void RunServer(LRUCache& cache_instance, const std::string& wal_filename) { // Pass WAL filename for info
    std::string server_address("0.0.0.0:50051");
    CacheServiceImpl service(cache_instance); // Service uses the prepared cache

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    std::cout << "Using WAL file: " << wal_filename << std::endl; // Log WAL file name
    server->Wait();
}

// --- Main Server Entry Point ---
int main(int argc, char** argv) {
    // --- Configuration ---
    const std::size_t cache_capacity = 10;
    const int ttl_seconds = 60;
    const std::string wal_filename = "cache.wal"; // Define WAL file name

    // --- Create Cache Instance ---
    LRUCache shared_cache(cache_capacity, ttl_seconds);
    std::cout << "LRU Cache initialized (Capacity: " << cache_capacity << ", TTL: " << ttl_seconds << "s)" << std::endl;

    // --- Load State from WAL ---
    if (!LRUCache::loadFromWAL(wal_filename, shared_cache)) {
         std::cerr << "FATAL: Failed to load state from WAL. Exiting." << std::endl;
         return 1; // Exit if recovery fails critically
    }
    // Print state after recovery
    std::cout << "Cache state after WAL recovery: ";
    shared_cache.print();


    // --- Open WAL File for Appending ---
    // IMPORTANT: Keep the stream object alive for the duration of the server!
    std::ofstream wal_file_stream(wal_filename, std::ios::app);
    if (!wal_file_stream.is_open()) {
        std::cerr << "FATAL: Could not open WAL file '" << wal_filename << "' for appending." << std::endl;
        return 1;
    }

    // --- Attach WAL Stream to Cache ---
    shared_cache.setWalStream(&wal_file_stream);
    std::cout << "WAL stream attached to cache instance." << std::endl;

    // --- Run the gRPC server ---
    RunServer(shared_cache, wal_filename); // Pass cache and WAL filename for logging

    // --- Cleanup (wal_file_stream closes automatically via RAII) ---
    std::cout << "Server shutting down." << std::endl;

    return 0;
}