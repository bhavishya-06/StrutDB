cmake_minimum_required(VERSION 3.24)
project(lua_overlay C)

set(LUA_CORE_SOURCES
  lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldblib.c
  ldebug.c ldo.c ldump.c lfunc.c lgc.c linit.c liolib.c llex.c
  lmathlib.c lmem.c loadlib.c lobject.c lopcodes.c loslib.c lparser.c
  lstate.c lstring.c lstrlib.c ltable.c ltablib.c ltm.c lundump.c
  lutf8lib.c lvm.c lzio.c
)

add_library(lua_static STATIC ${LUA_CORE_SOURCES})
add_library(Lua::Lua ALIAS lua_static)

target_include_directories(lua_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

set_target_properties(lua_static PROPERTIES
  C_STANDARD 11
  POSITION_INDEPENDENT_CODE ON
)

if(WIN32)
  target_compile_definitions(lua_static PRIVATE LUA_USE_WINDOWS)
else()
  target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
  target_link_libraries(lua_static PUBLIC ${CMAKE_DL_LIBS} m)
endif()
