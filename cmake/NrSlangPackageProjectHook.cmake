function(nr_apply_slang_package_project_hook)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        foreach(slang_tool IN ITEMS
            slang-bootstrap
            slangc
            slang-dispatcher
        )
            if(TARGET ${slang_tool})
                target_link_options(${slang_tool} PRIVATE -municode)
            endif()
        endforeach()
    endif()
endfunction()

cmake_language(DEFER CALL nr_apply_slang_package_project_hook)
