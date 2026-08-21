function(slotdeck_enable_coverage target)
    if(NOT SLOTDECK_ENABLE_COVERAGE)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
        return()
    endif()

    message(FATAL_ERROR "Coverage requires a GCC toolchain with gcov support")
endfunction()
