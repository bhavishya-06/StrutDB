cmake_minimum_required(VERSION 3.24)
project(libcypher_parser_overlay C)

include(CheckIncludeFile)

check_include_file(unistd.h   HAVE_UNISTD_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(strings.h  HAVE_STRINGS_H)

set(PACKAGE_VERSION "0.6.2")
set(PACKAGE_STRING "libcypher-parser ${PACKAGE_VERSION}")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/config.h"
  @ONLY
)

file(GLOB LIBCYPHER_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/src/*.c"
)

add_library(cypher-parser STATIC ${LIBCYPHER_SOURCES})
add_library(libcypher-parser::cypher-parser ALIAS cypher-parser)

target_include_directories(cypher-parser
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
target_compile_definitions(cypher-parser PRIVATE HAVE_CONFIG_H)
set_target_properties(cypher-parser PROPERTIES
  C_STANDARD 11
  POSITION_INDEPENDENT_CODE ON
  C_EXTENSIONS ON
)

# Upstream is autotools-era C; do not promote its warnings onto StrutDB.
if(MSVC)
  target_compile_options(cypher-parser PRIVATE /W0)
else()
  target_compile_options(cypher-parser PRIVATE -w)
endif()
