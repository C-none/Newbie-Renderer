cmake_minimum_required(VERSION 3.25)
project(nr_lua VERSION 5.5.0 LANGUAGES C)

include(CMakePackageConfigHelpers)

set(lua_sources
    src/lapi.c
    src/lauxlib.c
    src/lbaselib.c
    src/lcode.c
    src/lcorolib.c
    src/lctype.c
    src/ldblib.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/linit.c
    src/liolib.c
    src/llex.c
    src/lmathlib.c
    src/lmem.c
    src/loadlib.c
    src/lobject.c
    src/lopcodes.c
    src/loslib.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/lstrlib.c
    src/ltable.c
    src/ltablib.c
    src/ltm.c
    src/lundump.c
    src/lutf8lib.c
    src/lvm.c
    src/lzio.c
)

add_library(lua ${lua_sources})
add_library(lua::lua ALIAS lua)
target_include_directories(lua
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)
if(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS} m)
endif()

install(
    TARGETS lua
    EXPORT luaTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(
    FILES
        src/lua.h
        src/luaconf.h
        src/lauxlib.h
        src/lualib.h
        src/lua.hpp
    DESTINATION include
)
install(
    EXPORT luaTargets
    FILE luaTargets.cmake
    NAMESPACE lua::
    DESTINATION lib/cmake/lua
)
configure_package_config_file(
    luaConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/luaConfig.cmake"
    INSTALL_DESTINATION lib/cmake/lua
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/luaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/luaConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/luaConfigVersion.cmake"
    DESTINATION lib/cmake/lua
)
