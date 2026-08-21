set(SLOTDECK_TERMINAL_ENGINE_SOURCES
    GhosttyTerminalAdapter.cpp
    GhosttyHeaders.h
    GhosttyTerminalAdapter.h
    ShellProfile.cpp
    ShellProfile.h
    TerminalDimensions.h
    TerminalSession.cpp
    TerminalSession.h
    TerminalShortcuts.h
    TerminalThemeCatalog.cpp
    TerminalThemeCatalog.h
    platform/IPtyBackend.h
    ../domain/TerminalSessionState.h
    ../domain/TerminalTheme.h
    ../ui/TerminalWidget.cpp
    ../ui/TerminalWidget.h
)

set(SLOTDECK_TERMINAL_ENGINE_PLATFORM_SOURCES ${SLOTDECK_TERMINAL_PLATFORM_SOURCES})
list(TRANSFORM SLOTDECK_TERMINAL_ENGINE_PLATFORM_SOURCES REPLACE "^terminal/" "")

qt_add_library(
    SlotDeckTerminalEngine
    STATIC
        ${SLOTDECK_TERMINAL_ENGINE_SOURCES}
        ${SLOTDECK_TERMINAL_ENGINE_PLATFORM_SOURCES}
)
target_include_directories(SlotDeckTerminalEngine PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_BINARY_DIR}/generated)
if(WIN32)
    target_compile_definitions(SlotDeckTerminalEngine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_link_libraries(
    SlotDeckTerminalEngine
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        SlotDeckUi
        ghostty-vt-static
        ${SLOTDECK_TERMINAL_PLATFORM_LIBRARIES}
)
set_target_properties(
    SlotDeckTerminalEngine
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

slotdeck_set_warnings(SlotDeckTerminalEngine)
slotdeck_enable_coverage(SlotDeckTerminalEngine)
slotdeck_enable_sanitizers(SlotDeckTerminalEngine)
