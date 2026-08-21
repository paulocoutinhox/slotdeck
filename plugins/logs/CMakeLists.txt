qt_add_library(SlotDeckLogsObjects OBJECT LogsPlugin.cpp LogsPlugin.h LogsTranslations.h)
target_include_directories(SlotDeckLogsObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(SlotDeckLogsObjects PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets SlotDeckUi)
set_target_properties(SlotDeckLogsObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)

slotdeck_set_warnings(SlotDeckLogsObjects)
slotdeck_enable_coverage(SlotDeckLogsObjects)
slotdeck_enable_sanitizers(SlotDeckLogsObjects)

qt_add_library(SlotDeckLogsPlugin SHARED)
target_link_libraries(SlotDeckLogsPlugin PRIVATE SlotDeckLogsObjects)
set_target_properties(SlotDeckLogsPlugin PROPERTIES OUTPUT_NAME slotdeck-logs)

slotdeck_set_warnings(SlotDeckLogsPlugin)
slotdeck_enable_coverage(SlotDeckLogsPlugin)
slotdeck_enable_sanitizers(SlotDeckLogsPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckLogsPlugin LIBRARY DESTINATION plugins RUNTIME DESTINATION plugins)
endif()
