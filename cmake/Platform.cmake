if(APPLE)
    set(SLOTDECK_TERMINAL_PLATFORM_SOURCES
        terminal/platform/posix/PosixPtyBackend.cpp
        terminal/platform/posix/PosixPtyBackend.h
        terminal/platform/posix/PosixShellIntegration.cpp
        terminal/platform/posix/PosixShellIntegration.h
    )
    set(SLOTDECK_TERMINAL_PLATFORM_LIBRARIES "-framework CoreFoundation" "-framework Security" util)
elseif(WIN32)
    set(SLOTDECK_TERMINAL_PLATFORM_SOURCES
        terminal/platform/windows/ConPtyBackend.cpp
        terminal/platform/windows/ConPtyBackend.h
    )
    set(SLOTDECK_TERMINAL_PLATFORM_LIBRARIES userenv)
else()
    set(SLOTDECK_TERMINAL_PLATFORM_SOURCES
        terminal/platform/posix/PosixPtyBackend.cpp
        terminal/platform/posix/PosixPtyBackend.h
        terminal/platform/posix/PosixShellIntegration.cpp
        terminal/platform/posix/PosixShellIntegration.h
    )
    set(SLOTDECK_TERMINAL_PLATFORM_LIBRARIES util)
endif()
