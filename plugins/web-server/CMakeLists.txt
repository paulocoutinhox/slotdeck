qt_add_library(
    SlotDeckWebServerObjects
    OBJECT
    RequestLogModel.cpp
    RequestLogModel.h
    StaticFileResolver.cpp
    StaticFileResolver.h
    WebServerInstance.cpp
    WebServerInstance.h
    WebServerPlugin.cpp
    WebServerPlugin.h
    WebServerTranslations.h
    WebServerView.cpp
    WebServerView.h
)

target_include_directories(
    SlotDeckWebServerObjects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(
    SlotDeckWebServerObjects
    PUBLIC
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        SlotDeckUi
)
set_target_properties(
    SlotDeckWebServerObjects
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

slotdeck_set_warnings(SlotDeckWebServerObjects)
slotdeck_enable_coverage(SlotDeckWebServerObjects)
slotdeck_enable_sanitizers(SlotDeckWebServerObjects)

qt_add_library(SlotDeckWebServerPlugin SHARED)
target_link_libraries(SlotDeckWebServerPlugin PRIVATE SlotDeckWebServerObjects)
set_target_properties(SlotDeckWebServerPlugin PROPERTIES OUTPUT_NAME slotdeck-web-server)

slotdeck_set_warnings(SlotDeckWebServerPlugin)
slotdeck_enable_coverage(SlotDeckWebServerPlugin)
slotdeck_enable_sanitizers(SlotDeckWebServerPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckWebServerPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
