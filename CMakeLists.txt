cmake_minimum_required(VERSION 3.15)
project(LruCacheGrpc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Dependencies ---
find_package(Protobuf REQUIRED COMPONENTS gRPC CONFIG)
find_package(gRPC CONFIG REQUIRED)
# Attempt to find Abseil - Protobuf depends on it
# This might require Abseil to be installed with CMake support in your prefix
find_package(absl REQUIRED COMPONENTS strings # Add other components if needed later
    # Add hints if necessary, e.g.: HINTS /home/kai/.local/lib/cmake/absl
)


# --- Debug Messages ---
# (Keep your debug messages)
# message(STATUS "--- After finding packages ---")
# ... (protobuf, grpc messages) ...
# if(TARGET absl::strings) message(STATUS "Abseil target absl::strings found.") 
# else() 
# message(WARNING "Abseil target absl::strings NOT found.") 
# endif()


# --- Proto File Location ---
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/protos/cache.proto)
get_filename_component(PROTO_IMPORT_DIR ${PROTO_FILES} DIRECTORY)

# --- Generated Code Output Directory ---
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

# --- Create an OBJECT library for proto generation ---
# Changed from INTERFACE to OBJECT
add_library(cache_proto_obj OBJECT) # Use OBJECT library type

# --- Generate BASE C++ code ---
protobuf_generate(
    TARGET cache_proto_obj          # Associate with OBJECT library
    PROTOS ${PROTO_FILES}
    IMPORT_DIRS ${PROTO_IMPORT_DIR}
    PROTOC_OUT_DIR ${PROTO_GENERATED_DIR}
)

# --- Generate gRPC C++ code ---
protobuf_generate(
    TARGET cache_proto_obj          # Associate with the SAME OBJECT library
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    PROTOS ${PROTO_FILES}
    IMPORT_DIRS ${PROTO_IMPORT_DIR}
    PROTOC_OUT_DIR ${PROTO_GENERATED_DIR}
)

# --- Configure the OBJECT library ---
# Public includes needed by users of this object library
target_include_directories(cache_proto_obj PUBLIC
    $<BUILD_INTERFACE:${PROTO_GENERATED_DIR}/protos> # Specific dir for #include "cache.pb.h"
    $<BUILD_INTERFACE:${PROTO_GENERATED_DIR}>        # Parent dir for #include "protos/cache.pb.h"
)

# Public link libraries needed by users of this object library
# This ensures targets linking cache_proto_obj also link these.
target_link_libraries(cache_proto_obj PUBLIC
    protobuf::libprotobuf # Should pull in its dependencies (like absl)
    gRPC::grpc++
    # Explicitly add Abseil components if protobuf::libprotobuf doesn't pull them in
    absl::strings
    # Add other absl components like absl::base, absl::synchronization if linker errors persist
)

# --- Your LRU Cache Source Files ---
set(CACHE_LIB_SRCS
    src/lru_cache.cpp
    #src/node.cpp
)
# Create a library for your cache code (optional but good practice)
add_library(lru_cache_lib ${CACHE_LIB_SRCS})
target_include_directories(lru_cache_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # Headers for the cache lib
)
target_link_libraries(lru_cache_lib PUBLIC Threads::Threads) # Cache might use threads


# --- Server Executable ---
add_executable(cache_server src/cache_server.cpp)

# Include directories needed specifically by cache_server.cpp (if any beyond cache lib)
# target_include_directories(cache_server PRIVATE ...)

# Link server against the object library and other dependencies
target_link_libraries(cache_server PRIVATE
    # Link the object files compiled from generated sources
    cache_proto_obj
    # Link your cache library
    lru_cache_lib
    # Link specific gRPC/other libs needed by the server executable itself
    gRPC::grpc++_reflection
    # Note: protobuf::libprotobuf, gRPC::grpc++, absl::*, Threads::Threads
    # should be pulled in transitively from cache_proto_obj and lru_cache_lib
)

# --- Client Executable ---
add_executable(cache_client src/cache_client.cpp)
# target_include_directories(cache_client PRIVATE ...) # Includes needed by client
target_link_libraries(cache_client PRIVATE
    cache_proto_obj   # Link generated code object files
    lru_cache_lib     # Client might need cache headers/types? If not, remove.
    # No reflection needed for client usually
    # Transitive dependencies (protobuf, grpc++, absl, threads) should be pulled in
)

# --- Installation (Optional) ---
# ...