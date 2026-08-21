function(slotdeck_enable_sanitizers target)
    if(NOT SLOTDECK_ENABLE_SANITIZERS)
        return()
    endif()

    target_compile_definitions(${target} PRIVATE SLOTDECK_SANITIZERS_ENABLED)

    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        return()
    endif()

    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
