vcpkg_download_distfile(
    LUA_ARCHIVE
    URLS "https://www.lua.org/ftp/lua-5.5.0.tar.gz"
    FILENAME "lua-5.5.0.tar.gz"
    SHA512 3253d2cdc929da6438095a30d66ef16a1abdbb0ada8fee238705b3b38492f14be9553640fdca6b25661e01155ba5582032e0a2ef064e4c283e85efc0a128cabe
)

# These patches are the eight fixes linked from the official Lua 5.5.0
# bug page, in its published order:
# 632a71b, 45c7ae5, 10eb89d, f1bb277, efddc23, 3228a97, bc4bbce, b996f8f.
vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${LUA_ARCHIVE}"
    PATCHES
        0001-gc-step-overflow.patch
        0002-packsize-overflow.patch
        0003-utf8-shift-overflow.patch
        0004-binary-load-gc.patch
        0005-gmatch-state.patch
        0006-lua-load-stack.patch
        0007-newmetatable-incomplete.patch
        0008-newindex-write-barrier.patch
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/luaConfig.cmake.in" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME lua CONFIG_PATH lib/cmake/lua)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/doc/readme.html"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
