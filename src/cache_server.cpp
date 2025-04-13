// src/cache_server.cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector> // Include vector

// gRPC Headers
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

// Generated Proto Headers (adjust path based on generation output)
#include "cache.grpc.pb.h" // Use "" for local includes

// Your LRU Cache Header
#include "lru_cache.h" // Use "" for local includes

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

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
    // The actual LRU Cache instance doing the work
    LRUCache& lru_cache_; // Use a reference to a shared cache instance

public:
    // Constructor takes a reference to the cache it will manage
    explicit CacheServiceImpl(LRUCache& cache) : lru_cache_(cache) {}

    // --- Implement the RPC methods defined in the .proto file ---

    Status Get(ServerContext* context, const GetRequest* request,
               GetResponse* response) override {
        std::cout << "Server received GET request for key: " << request->key() << std::endl;
        std::string value = lru_cache_.get(request->key()); // Call your cache's get

        if (!value.empty()) {
            response->set_value(value);
            response->set_found(true);
            std::cout << "  Found value: " << value << std::endl;
        } else {
            response->set_value(""); // Explicitly set empty
            response->set_found(false);
            std::cout << "  Key not found or expired." << std::endl;
        }
        return Status::OK; // Indicate successful processing of the request
    }

    Status Put(ServerContext* context, const PutRequest* request,
               PutResponse* response) override {
         std::cout << "Server received PUT request for key: " << request->key()
                   << " value: " << request->value() << std::endl;
        lru_cache_.put(request->key(), request->value()); // Call your cache's put
        response->set_success(true); // Indicate success
        std::cout << "  Put successful." << std::endl;
        return Status::OK;
    }

     Status Delete(ServerContext* context, const DeleteRequest* request,
                   DeleteResponse* response) override {
        std::cout << "Server received DELETE request for key: " << request->key() << std::endl;
        lru_cache_.remove(request->key()); // Call your cache's remove
        response->set_success(true); // Indicate success
        std::cout << "  Delete successful." << std::endl;
        return Status::OK;
    }
};

void RunServer(LRUCache& cache_instance) {
    std::string server_address("0.0.0.0:50051"); // Listen on all interfaces, port 50051
    CacheServiceImpl service(cache_instance);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin(); // Optional: for server reflection
    ServerBuilder builder;

    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);

    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

int main(int argc, char** argv) {
    // --- Create your LRU Cache instance ---
    // These values could come from command-line args, config file, etc.
    const std::size_t cache_capacity = 10;
    const int ttl_seconds = 60;
    LRUCache shared_cache(cache_capacity, ttl_seconds);
    std::cout << "LRU Cache initialized (Capacity: " << cache_capacity << ", TTL: " << ttl_seconds << "s)" << std::endl;

    // --- Run the gRPC server, passing the shared cache instance ---
    RunServer(shared_cache); // Pass by reference

    return 0;
}