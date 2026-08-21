set(SLOTDECK_UI_SOURCES
    ApplicationShortcuts.h
    Components.cpp
    Components.h
    FindBar.cpp
    FindBar.h
    Icons.cpp
    Icons.h
    TabBar.cpp
    TabBar.h
    Theme.cpp
    Theme.h
)

qt_add_library(SlotDeckUi STATIC ${SLOTDECK_UI_SOURCES})
target_include_directories(SlotDeckUi PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(SlotDeckUi PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(
    SlotDeckUi
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

slotdeck_set_warnings(SlotDeckUi)
slotdeck_enable_coverage(SlotDeckUi)
slotdeck_enable_sanitizers(SlotDeckUi)
