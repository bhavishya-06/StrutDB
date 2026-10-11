cmake_minimum_required(VERSION 3.24)

project(StrutDB
  VERSION 0.1.0
  DESCRIPTION "High-performance in-memory graph database"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Let cache-forced options win inside FetchContent subprojects (CMP0077).
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_policy(SET CMP0135 NEW)

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# Warnings / flags — applied only to StrutDB targets, never to FetchContent.
# -----------------------------------------------------------------------------
add_library(strutdb_warnings INTERFACE)
add_library(strutdb::warnings ALIAS strutdb_warnings)

target_compile_options(strutdb_warnings INTERFACE
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor -Woverloaded-virtual
  >
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:
    /W4 /permissive- /Zc:__cplusplus
  >
)

add_library(strutdb_threads INTERFACE)
add_library(strutdb::threads ALIAS strutdb_threads)
target_link_libraries(strutdb_threads INTERFACE Threads::Threads)
target_compile_options(strutdb_threads INTERFACE
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-pthread>
)
target_link_options(strutdb_threads INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-pthread>
)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Each dependency is declared independently, MakeAvailable'd in isolation,
# then linked only onto the StrutDB module that actually consumes it.
# Core never links parser / RocksDB / sol2 — that keeps the include DAG acyclic.

# -----------------------------------------------------------------------------
# Lua (companion of sol2 — sol2 is bindings only)
# -----------------------------------------------------------------------------
FetchContent_Declare(lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG        v5.4.7
  GIT_SHALLOW    TRUE
  PATCH_COMMAND  ${CMAKE_COMMAND}
                   -DOVERLAY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlays/lua
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlays/lua/apply_overlay.cmake
)

# -----------------------------------------------------------------------------
# sol2 — Lua C++ bindings (header-only)
# -----------------------------------------------------------------------------
set(SOL2_ENABLE_INSTALL OFF)
set(SOL2_BUILD_LUA OFF)
set(SOL2_TESTS OFF)
set(SOL2_EXAMPLES OFF)
set(SOL2_DOCS OFF)
FetchContent_Declare(sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG        v3.3.1
  GIT_SHALLOW    TRUE
)

# -----------------------------------------------------------------------------
# RocksDB — tiered / durable storage (CMake native)
# -----------------------------------------------------------------------------
set(WITH_GFLAGS OFF)
set(WITH_SNAPPY OFF)
set(WITH_LZ4 OFF)
set(WITH_ZLIB OFF)
set(WITH_ZSTD OFF)
set(WITH_BZ2 OFF)
set(WITH_TESTS OFF)
set(WITH_BENCHMARK_TOOLS OFF)
set(WITH_TOOLS OFF)
set(WITH_CORE_TOOLS OFF)
set(WITH_TRACE_TOOLS OFF)
set(WITH_JNI OFF)
set(WITH_EXAMPLES OFF)
set(WITH_LIBURING OFF)
set(FAIL_ON_WARNINGS OFF)
set(ROCKSDB_BUILD_SHARED OFF)
set(ROCKSDB_INSTALL OFF)
set(PORTABLE ON)
set(USE_RTTI ON)
FetchContent_Declare(rocksdb
  GIT_REPOSITORY https://github.com/facebook/rocksdb.git
  GIT_TAG        v9.10.0
  GIT_SHALLOW    TRUE
)

# -----------------------------------------------------------------------------
# libcypher-parser — Cypher AST (autotools upstream; overlay CMakeLists)
# Use release tarball which includes pre-generated parser files
# -----------------------------------------------------------------------------
FetchContent_Declare(libcypher_parser
  URL https://github.com/cleishm/libcypher-parser/releases/download/v0.6.2/libcypher-parser-0.6.2.tar.gz
  PATCH_COMMAND  ${CMAKE_COMMAND}
                   -DOVERLAY_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlays/libcypher-parser
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlays/libcypher-parser/apply_overlay.cmake
)

FetchContent_MakeAvailable(lua sol2 rocksdb libcypher_parser)

# -----------------------------------------------------------------------------
# StrutDB module targets
#
# Dependency DAG (one-way only):
#   network -> execution -> parser -> core
#                   \-> storage -> core
#                   \-> scripting -> core
#   core -> (STL / Threads only)
# -----------------------------------------------------------------------------
add_library(strutdb_core STATIC
  src/core/node.cpp
  src/core/edge.cpp
  src/core/adjacency.cpp
  src/core/graph.cpp
  src/core/hashmap.cpp
)
add_library(strutdb::core ALIAS strutdb_core)
target_include_directories(strutdb_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(strutdb_core PUBLIC
  strutdb::warnings
  strutdb::threads
)
target_compile_features(strutdb_core PUBLIC cxx_std_20)

add_library(strutdb_parser STATIC
  src/parser/parser.cpp
)
add_library(strutdb::parser ALIAS strutdb_parser)
target_include_directories(strutdb_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(strutdb_parser
  PUBLIC  strutdb::core
  PRIVATE cypher-parser
)
target_compile_features(strutdb_parser PUBLIC cxx_std_20)

add_library(strutdb_storage INTERFACE)
add_library(strutdb::storage ALIAS strutdb_storage)
target_include_directories(strutdb_storage INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(strutdb_storage INTERFACE
  strutdb::core
  rocksdb
)

add_library(strutdb_scripting STATIC
  src/scripting/lua_engine.cpp
)
add_library(strutdb::scripting ALIAS strutdb_scripting)
target_include_directories(strutdb_scripting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(strutdb_scripting PUBLIC
  strutdb::core
  sol2::sol2
  lua_static
)

add_library(strutdb_execution STATIC
  src/execution/executor.cpp
)
add_library(strutdb::execution ALIAS strutdb_execution)
target_include_directories(strutdb_execution PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(strutdb_execution PUBLIC
  strutdb::core
  strutdb::parser
  strutdb::storage
  strutdb::scripting
)

add_library(strutdb_network STATIC
  src/network/resp.cpp
  src/network/server.cpp
)
add_library(strutdb::network ALIAS strutdb_network)
target_include_directories(strutdb_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(strutdb_network PUBLIC
  strutdb::execution
  strutdb::threads
)

add_library(strutdb INTERFACE)
add_library(strutdb::strutdb ALIAS strutdb)
target_link_libraries(strutdb INTERFACE strutdb::network)

# -----------------------------------------------------------------------------
# Executable target
# -----------------------------------------------------------------------------
add_executable(strutdb-server
  src/main.cpp
)
target_link_libraries(strutdb-server PRIVATE
  strutdb::network
)
target_compile_features(strutdb-server PRIVATE cxx_std_20)
