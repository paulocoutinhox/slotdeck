qt_add_library(
    SlotDeckBrowserObjects
    OBJECT
    BrowserBookmarksView.cpp
    BrowserBookmarksView.h
    BrowserPlugin.cpp
    BrowserPlugin.h
    BrowserTranslations.h
    BrowserView.cpp
    BrowserView.h
)
target_include_directories(SlotDeckBrowserObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(SlotDeckBrowserObjects PUBLIC Qt6::Core Qt6::Gui Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::Widgets SlotDeckUi)
set_target_properties(SlotDeckBrowserObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)
slotdeck_set_warnings(SlotDeckBrowserObjects)
slotdeck_enable_coverage(SlotDeckBrowserObjects)
slotdeck_enable_sanitizers(SlotDeckBrowserObjects)

qt_add_library(SlotDeckBrowserPlugin SHARED)
target_link_libraries(SlotDeckBrowserPlugin PRIVATE SlotDeckBrowserObjects)
set_target_properties(SlotDeckBrowserPlugin PROPERTIES OUTPUT_NAME slotdeck-browser)
slotdeck_set_warnings(SlotDeckBrowserPlugin)
slotdeck_enable_coverage(SlotDeckBrowserPlugin)
slotdeck_enable_sanitizers(SlotDeckBrowserPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckBrowserPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
