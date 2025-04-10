cmake_minimum_required(VERSION 3.15) # Need a reasonably modern CMake
project(LruCacheGrpc CXX)

set(CMAKE_CXX_STANDARD 17) # Or 11, 14
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Protobuf ---
# CMake's FindProtobuf module can generate code
find_package(Protobuf REQUIRED)

# --- Find gRPC ---
# This requires gRPC's CMake config files to be installed correctly
# You might need to set CMAKE_PREFIX_PATH if installed in a non-standard location
find_package(gRPC REQUIRED)

# --- Proto File Location ---
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/protos/cache.proto)

# --- Generated Code Output Directory ---
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

# --- Generate C++ code from .proto file ---
# This command uses protoc and the gRPC plugin internally
protobuf_generate_grpc_cpp(PROTO_SRCS PROTO_HDRS
    ${PROTO_FILES}
    PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>" # Use CMake target for plugin
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/protos # Where to find imports if any
    OUT_DIR ${PROTO_GENERATED_DIR}
)

# --- Include Directories ---
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include # For lru_cache.h, node.h
    ${PROTO_GENERATED_DIR}             # For generated headers
    ${Protobuf_INCLUDE_DIRS}           # For protobuf headers
    ${gRPC_INCLUDE_DIRS}               # For gRPC headers (usually handled by target_link_libraries)
)

# --- Your LRU Cache Source Files ---
set(CACHE_LIB_SRCS
    src/lru_cache.cpp
    src/node.cpp
)

# --- Server Executable ---
add_executable(cache_server src/cache_server.cpp ${CACHE_LIB_SRCS} ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(cache_server
    PRIVATE
    gRPC::grpc++        # gRPC C++ library
    gRPC::grpc++_reflection # For server reflection (optional)
    protobuf::libprotobuf # Protobuf library
    Threads::Threads    # For std::thread (used by cache and maybe gRPC internals)
)
# Add C++17/etc flags if not set globally
# target_compile_features(cache_server PRIVATE cxx_std_17)

# --- Client Executable ---
add_executable(cache_client src/cache_client.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(cache_client
    PRIVATE
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)
# Add C++17/etc flags if not set globally
# target_compile_features(cache_client PRIVATE cxx_std_17)

# --- Installation (Optional) ---
# install(TARGETS cache_server cache_client DESTINATION bin)
# install(FILES ${PROTO_HDRS} DESTINATION include) # Example if making a library