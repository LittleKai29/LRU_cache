# gRPC LRU Cache with WAL and Asynchronous Replication

This project implements a thread-safe Least Recently Used (LRU) cache in C++ with Time-To-Live (TTL) support. It exposes a simple Get/Put/Delete API over gRPC and includes features for persistence using a Write-Ahead Log (WAL) and basic high availability through asynchronous primary-replica replication. 

## Features

*   **LRU Cache:** Core cache eviction based on least recent usage.
*   **Time-To-Live (TTL):** Items expire after a configurable duration of inactivity.
*   **Thread-Safe:** Internal locking ensures safe concurrent access.
*   **gRPC API:** Network interface defined using Protocol Buffers and gRPC.
    *   `Get(key)`: Retrieve a value.
    *   `Put(key, value)`: Insert or update a value.
    *   `Delete(key)`: Remove a value.
*   **Write-Ahead Log (WAL):** Provides persistence by logging Put/Delete operations before applying them to memory. Allows state recovery after restarts.
*   **Asynchronous Replication:** A primary server can replicate Put/Delete operations asynchronously to one or more replica servers.
*   **Configuration File:** Server behavior (port, capacity, TTL, WAL file, replication role/targets) is managed via a configuration file (`cache_config.cfg`).

## Prerequisites

Before building, ensure you have the following installed:

*   **C++ Compiler:** A modern C++ compiler supporting C++17 (like GCC or Clang).
*   **CMake:** Version 3.15 or higher.
*   **Protocol Buffers:** `protobuf` library, development files, and the `protoc` compiler (version 3.x recommended).
*   **gRPC:** `gRPC` C++ library, development files, and the `grpc_cpp_plugin` for `protoc`.

Refer to the official gRPC C++ installation guide for detailed instructions: [gRPC C++ Quick Start](https://grpc.io/docs/languages/cpp/quickstart/)

## Building

1.  **Clone the repository (if applicable):**
    ```bash
    git clone <your-repo-url>
    cd <your-repo-directory>
    ```

2.  **Create a build directory:**
    ```bash
    mkdir build
    cd build
    ```

3.  **Run CMake:**
    ```bash
    # If gRPC/Protobuf are installed in non-standard locations, specify the prefix:
    # cmake .. -DCMAKE_PREFIX_PATH=/path/to/grpc_install
    cmake ..
    ```

4.  **Compile:**
    ```bash
    make -j $(nproc)
    # Or: cmake --build . -j $(nproc)
    ```

This will generate the executables (`cache_server`, `cache_client`) in the `build/` directory.

## Configuration (`cache_config.cfg`)

The server's behavior is controlled by a configuration file, typically named `cache_config.cfg`. Create this file in the directory where you intend to run the server.

**Example `cache_config.cfg`:**

```ini
# cache_config.cfg

# --- Server Settings ---
# Address and port for the server to listen on
listen_address=0.0.0.0:50051

# --- Cache Settings ---
capacity=100
ttl_seconds=300 # 5 minutes
wal_file=cache_server.wal # Path relative to execution dir, or absolute

# --- Replication Settings ---
# List of replica addresses (comma-separated).
# If this line is commented out or empty, the server runs in REPLICA mode.
# If addresses are present, the server runs in PRIMARY mode.
# replica_addresses=localhost:50052,192.168.1.100:50051
replica_addresses=
```

## Key Settings:

listen_address: The IP address and port the gRPC server binds to. Must be unique for each server instance running on the same machine.

capacity: Maximum number of items in the LRU cache.

ttl_seconds: Item expiration time in seconds.

wal_file: Path to the Write-Ahead Log file for persistence. Should be unique for each server instance.

replica_addresses: Comma-separated list of host:port for replica servers. If present and non-empty, the server acts as a primary. If empty or commented out, it acts as a replica.

## Running
Prepare Configuration Files: Create separate .cfg files for the primary and each replica, ensuring unique listen_address and wal_file settings in each. Set replica_addresses appropriately for the primary.

Start Replicas: In separate terminals or as background processes, run:

./build/cache_server /path/to/replica_config.cfg


Start Primary: In a separate terminal or as a background process, run:

./build/cache_server /path/to/primary_config.cfg

(The server will look for the specified config file. If no argument is given, it defaults to cache_config.cfg in the current directory).

## Usage / Interaction
You can interact with the running cache server (typically the primary) using a gRPC client or a tool like grpcurl.

Using grpcurl (Recommended):

Ensure grpcurl is installed and the server has reflection enabled (it is by default in the code).

Put:

grpcurl -plaintext -d '{"key": "mykey", "value": "myvalue"}' <primary_host>:<primary_port> cache.CacheService.Put

Get:

grpcurl -plaintext -d '{"key": "mykey"}' <primary_host>:<primary_port> cache.CacheService.Get

Delete:

grpcurl -plaintext -d '{"key": "mykey"}' <primary_host>:<primary_port> cache.CacheService.Delete

Replace <primary_host>:<primary_port> with the actual address from the primary's configuration (e.g., localhost:50051).

Using the Included Client:

A basic client executable (cache_client) is also built. It demonstrates simple interactions:

./build/cache_client

## Project Structure
.
├── CMakeLists.txt          # Main CMake build script
├── README.md               # This file
├── include/                # Header files (.h)
│   ├── lru_cache.h
│   └── node.h
├── protos/                 # Protocol Buffer definitions (.proto)
│   └── cache.proto
├── src/                    # Source files (.cpp)
│   ├── cache_client.cpp    # Example client implementation
│   ├── cache_server.cpp    # Server implementation (gRPC service)
│   ├── lru_cache.cpp       # LRU Cache logic implementation
│   └── node.cpp            # Node implementation
├── build/                  # Build directory (created by CMake)
├── cache_config.cfg        # Example configuration file
└── test_replication.sh     # Example test script (if you kept it)

## Future Improvements / TODO
More robust error handling in replication (retries, failure detection).

Mechanism for replicas to perform a full state sync from the primary on startup.

More efficient WAL format (binary) and durability options (fsync).

WAL segmentation and cleanup/compaction.

Security: Add TLS encryption to gRPC communication.

Monitoring: Expose metrics (cache hits/misses, queue length, etc.).

Dockerization (initial attempt exists, needs refinement).

More comprehensive unit and integration tests.

Leader election with Raft