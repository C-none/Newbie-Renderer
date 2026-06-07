if(NOT _NR_MSYS2_CLANGXX_TOOLCHAIN)
    set(_NR_MSYS2_CLANGXX_TOOLCHAIN 1)

    if(POLICY CMP0056)
        cmake_policy(SET CMP0056 NEW)
    endif()
    if(POLICY CMP0066)
        cmake_policy(SET CMP0066 NEW)
    endif()
    if(POLICY CMP0067)
        cmake_policy(SET CMP0067 NEW)
    endif()
    if(POLICY CMP0137)
        cmake_policy(SET CMP0137 NEW)
    endif()

    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_C_COMPILER_TARGET
        CMAKE_CXX_COMPILER_TARGET
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_RC_COMPILER
        VCPKG_CRT_LINKAGE
        VCPKG_TARGET_ARCHITECTURE
        VCPKG_C_FLAGS
        VCPKG_CXX_FLAGS
        VCPKG_C_FLAGS_DEBUG
        VCPKG_CXX_FLAGS_DEBUG
        VCPKG_C_FLAGS_RELEASE
        VCPKG_CXX_FLAGS_RELEASE
        VCPKG_LINKER_FLAGS
        VCPKG_LINKER_FLAGS_DEBUG
        VCPKG_LINKER_FLAGS_RELEASE
    )

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(CMAKE_CROSSCOMPILING OFF CACHE BOOL "" FORCE)
    endif()

    set(CMAKE_SYSTEM_NAME Windows CACHE STRING "" FORCE)

    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
        set(_nr_system_processor "i686")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(_nr_system_processor "x86_64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm")
        set(_nr_system_processor "armv7")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_nr_system_processor "aarch64")
    else()
        message(FATAL_ERROR "Unsupported VCPKG_TARGET_ARCHITECTURE='${VCPKG_TARGET_ARCHITECTURE}' for the MSYS2 clang++ toolchain.")
    endif()

    set(CMAKE_SYSTEM_PROCESSOR "${_nr_system_processor}" CACHE STRING "" FORCE)
    set(_nr_target_triple "${CMAKE_SYSTEM_PROCESSOR}-w64-windows-gnu")

    find_program(_nr_clang_compiler NAMES clang.exe clang REQUIRED)
    find_program(_nr_clangxx_compiler NAMES clang++.exe clang++ REQUIRED)
    find_program(_nr_llvm_ar NAMES llvm-ar.exe llvm-ar REQUIRED)
    find_program(_nr_llvm_ranlib NAMES llvm-ranlib.exe llvm-ranlib REQUIRED)
    find_program(_nr_llvm_rc NAMES llvm-rc.exe llvm-rc)
    if(NOT _nr_llvm_rc)
        find_program(_nr_llvm_rc NAMES windres.exe windres)
    endif()

    execute_process(
        COMMAND "${_nr_clang_compiler}" -dumpmachine
        OUTPUT_VARIABLE _nr_clang_triple
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${_nr_clangxx_compiler}" -dumpmachine
        OUTPUT_VARIABLE _nr_clangxx_triple
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )

    if(NOT _nr_clang_triple STREQUAL _nr_target_triple)
        message(FATAL_ERROR
            "clang resolves to '${_nr_clang_triple}', but the MSYS2 clang++ toolchain requires '${_nr_target_triple}'. "
            "Ensure MSYS2 clang64/bin is ahead of other LLVM installations on PATH."
        )
    endif()

    if(NOT _nr_clangxx_triple STREQUAL _nr_target_triple)
        message(FATAL_ERROR
            "clang++ resolves to '${_nr_clangxx_triple}', but the MSYS2 clang++ toolchain requires '${_nr_target_triple}'. "
            "Ensure MSYS2 clang64/bin is ahead of other LLVM installations on PATH."
        )
    endif()

    # The top-level project is C++-only, but vcpkg ports still need a C compiler.
    set(CMAKE_C_COMPILER "${_nr_clang_compiler}" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${_nr_clangxx_compiler}" CACHE FILEPATH "" FORCE)
    set(CMAKE_C_COMPILER_TARGET "${_nr_target_triple}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER_TARGET "${_nr_target_triple}" CACHE STRING "" FORCE)
    set(CMAKE_AR "${_nr_llvm_ar}" CACHE FILEPATH "" FORCE)
    set(CMAKE_RANLIB "${_nr_llvm_ranlib}" CACHE FILEPATH "" FORCE)
    if(_nr_llvm_rc)
        set(CMAKE_RC_COMPILER "${_nr_llvm_rc}" CACHE FILEPATH "" FORCE)
    endif()

    get_filename_component(_nr_clang_bin_dir "${_nr_clangxx_compiler}" DIRECTORY)
    get_filename_component(_nr_clang_root_dir "${_nr_clang_bin_dir}" DIRECTORY)
    set(_nr_libcxx_modules_json "${_nr_clang_root_dir}/lib/libc++.modules.json")
    if(EXISTS "${_nr_libcxx_modules_json}")
        set(CMAKE_CXX_STANDARD_LIBRARY "libc++" CACHE STRING "" FORCE)
        set(CMAKE_CXX_STDLIB_MODULES_JSON "${_nr_libcxx_modules_json}" CACHE FILEPATH "" FORCE)
    endif()

    string(APPEND CMAKE_C_FLAGS_INIT " ${VCPKG_C_FLAGS} ")
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${VCPKG_CXX_FLAGS} ")
    string(APPEND CMAKE_C_FLAGS_DEBUG_INIT " ${VCPKG_C_FLAGS_DEBUG} ")
    string(APPEND CMAKE_CXX_FLAGS_DEBUG_INIT " ${VCPKG_CXX_FLAGS_DEBUG} ")
    string(APPEND CMAKE_C_FLAGS_RELEASE_INIT " ${VCPKG_C_FLAGS_RELEASE} ")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE_INIT " ${VCPKG_CXX_FLAGS_RELEASE} ")

    string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} -fuse-ld=lld ")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} -fuse-ld=lld ")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} -fuse-ld=lld ")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT " ${VCPKG_LINKER_FLAGS_DEBUG} ")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT " ${VCPKG_LINKER_FLAGS_DEBUG} ")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT " ${VCPKG_LINKER_FLAGS_DEBUG} ")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE_INIT " ${VCPKG_LINKER_FLAGS_RELEASE} ")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT " ${VCPKG_LINKER_FLAGS_RELEASE} ")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT " ${VCPKG_LINKER_FLAGS_RELEASE} ")

    if(VCPKG_CRT_LINKAGE STREQUAL "static")
        string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT "-static ")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT "-static ")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT "-static ")
    endif()
endif()
