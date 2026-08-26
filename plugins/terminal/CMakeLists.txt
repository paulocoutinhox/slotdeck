qt_add_library(
    SlotDeckTerminalObjects
    OBJECT
    TerminalPlugin.cpp
    TerminalPlugin.h
    TerminalSettingsStore.cpp
    TerminalSettingsStore.h
    TerminalTranslations.h
    TerminalView.cpp
    TerminalView.h
    TerminalWorkspaceRepository.cpp
    TerminalWorkspaceRepository.h
    domain/MainTab.h
    domain/SlotLayoutState.h
    domain/Workspace.h
    ui/SessionDrag.h
    ui/ShelfSessionChip.cpp
    ui/ShelfSessionChip.h
    ui/TerminalPane.cpp
    ui/TerminalPane.h
    ui/WorkspaceView.cpp
    ui/WorkspaceView.h
    workspace/LayoutManager.cpp
    workspace/LayoutManager.h
    workspace/WorkspaceManager.cpp
    workspace/WorkspaceManager.h
)
target_include_directories(
    SlotDeckTerminalObjects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(
    SlotDeckTerminalObjects
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        SlotDeckUi
        SlotDeckTerminalEngine
)
set_target_properties(
    SlotDeckTerminalObjects
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

slotdeck_set_warnings(SlotDeckTerminalObjects)
slotdeck_enable_coverage(SlotDeckTerminalObjects)
slotdeck_enable_sanitizers(SlotDeckTerminalObjects)

qt_add_library(SlotDeckTerminalPlugin SHARED)
target_link_libraries(SlotDeckTerminalPlugin PRIVATE SlotDeckTerminalObjects)
set_target_properties(SlotDeckTerminalPlugin PROPERTIES OUTPUT_NAME slotdeck-terminal)

slotdeck_set_warnings(SlotDeckTerminalPlugin)
slotdeck_enable_coverage(SlotDeckTerminalPlugin)
slotdeck_enable_sanitizers(SlotDeckTerminalPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckTerminalPlugin LIBRARY DESTINATION plugins RUNTIME DESTINATION plugins)
endif()
