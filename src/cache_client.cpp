// src/cache_client.cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector> // Include vector

// gRPC Headers
#include <grpcpp/grpcpp.h>

// Generated Proto Headers (adjust path based on generation output)
#include "cache.grpc.pb.h" // Use "" for local includes

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// Using the generated C++ namespace
using cache_rpc::CacheService;
using cache_rpc::DeleteRequest;
using cache_rpc::DeleteResponse;
using cache_rpc::GetRequest;
using cache_rpc::GetResponse;
using cache_rpc::PutRequest;
using cache_rpc::PutResponse;

class CacheClient {
public:
    // Constructor: Establishes connection and creates stub
    CacheClient(std::shared_ptr<Channel> channel)
        : stub_(CacheService::NewStub(channel)) {} // Create stub from channel

    // --- Client-side methods to interact with the RPCs ---

    // Wrapper for the Get RPC
    bool GetValue(const std::string& key, std::string& out_value) {
        GetRequest request;
        request.set_key(key);

        GetResponse response;
        ClientContext context; // Context for the RPC call

        // The actual RPC call
        Status status = stub_->Get(&context, request, &response);

        // Check the status of the RPC
        if (status.ok()) {
            if (response.found()) {
                out_value = response.value();
                return true; // Found
            } else {
                out_value = ""; // Not found or expired
                return false; // Not found
            }
        } else {
            std::cerr << "gRPC Get failed: " << status.error_code() << ": "
                      << status.error_message() << std::endl;
            out_value = "";
            return false; // RPC error
        }
    }

    // Wrapper for the Put RPC
    bool PutValue(const std::string& key, const std::string& value) {
        PutRequest request;
        request.set_key(key);
        request.set_value(value);

        PutResponse response;
        ClientContext context;

        // The actual RPC call
        Status status = stub_->Put(&context, request, &response);

        if (status.ok()) {
            return response.success(); // Return success flag from server
        } else {
            std::cerr << "gRPC Put failed: " << status.error_code() << ": "
                      << status.error_message() << std::endl;
            return false; // RPC error
        }
    }

     // Wrapper for the Delete RPC
    bool DeleteValue(const std::string& key) {
        DeleteRequest request;
        request.set_key(key);

        DeleteResponse response;
        ClientContext context;

        // The actual RPC call
        Status status = stub_->Delete(&context, request, &response);

        if (status.ok()) {
            return response.success(); // Return success flag from server
        } else {
            std::cerr << "gRPC Delete failed: " << status.error_code() << ": "
                      << status.error_message() << std::endl;
            return false; // RPC error
        }
    }

private:
    // The gRPC stub for making calls
    std::unique_ptr<CacheService::Stub> stub_;
};

int main(int argc, char** argv) {
    // --- Connect to the server ---
    // This could come from command-line args, config file, etc.
    std::string target_str = "localhost:50051";

    // Create a client connected to the server address.
    // Use InsecureChannelCredentials for simplicity (no encryption/auth)
    CacheClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

    std::cout << "Cache Client connected to " << target_str << std::endl;

    // --- Example Usage ---
    std::string key1 = "apple";
    std::string value1 = "red_fruit";
    std::string key2 = "banana";
    std::string value2 = "yellow_fruit";
    std::string retrieved_value;

    // Put some values
    std::cout << "\nPutting '" << key1 << "' -> '" << value1 << "'" << std::endl;
    if (client.PutValue(key1, value1)) {
        std::cout << "  Put successful." << std::endl;
    } else {
        std::cout << "  Put failed." << std::endl;
    }

    std::cout << "Putting '" << key2 << "' -> '" << value2 << "'" << std::endl;
     if (client.PutValue(key2, value2)) {
        std::cout << "  Put successful." << std::endl;
    } else {
        std::cout << "  Put failed." << std::endl;
    }

    // Get a value
    std::cout << "\nGetting '" << key1 << "'" << std::endl;
    if (client.GetValue(key1, retrieved_value)) {
        std::cout << "  Got value: " << retrieved_value << std::endl;
    } else {
        std::cout << "  Key '" << key1 << "' not found." << std::endl;
    }

    // Get a non-existent value
    std::cout << "\nGetting 'grape'" << std::endl;
    if (client.GetValue("grape", retrieved_value)) {
        std::cout << "  Got value: " << retrieved_value << std::endl;
    } else {
        std::cout << "  Key 'grape' not found." << std::endl;
    }

    // Delete a value
    std::cout << "\nDeleting '" << key1 << "'" << std::endl;
    if (client.DeleteValue(key1)) {
         std::cout << "  Delete successful." << std::endl;
    } else {
         std::cout << "  Delete failed." << std::endl;
    }

    // Try getting the deleted value
    std::cout << "\nGetting '" << key1 << "' again" << std::endl;
    if (client.GetValue(key1, retrieved_value)) {
        std::cout << "  Got value: " << retrieved_value << std::endl;
    } else {
        std::cout << "  Key '" << key1 << "' not found." << std::endl;
    }


    return 0;
}