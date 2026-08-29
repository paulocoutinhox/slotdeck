qt_add_library(
    SlotDeckSystemInformationObjects
    OBJECT
    SystemInformation.cpp
    SystemInformation.h
    SystemInformationPlugin.cpp
    SystemInformationPlugin.h
    SystemInformationTranslations.h
    SystemInformationView.cpp
    SystemInformationView.h
)
target_include_directories(SlotDeckSystemInformationObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(SlotDeckSystemInformationObjects PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets SlotDeckUi lfreist-hwinfo::hwinfo)
set_target_properties(SlotDeckSystemInformationObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)

slotdeck_set_warnings(SlotDeckSystemInformationObjects)
slotdeck_enable_coverage(SlotDeckSystemInformationObjects)
slotdeck_enable_sanitizers(SlotDeckSystemInformationObjects)

qt_add_library(SlotDeckSystemInformationPlugin SHARED)
target_link_libraries(SlotDeckSystemInformationPlugin PRIVATE SlotDeckSystemInformationObjects)
set_target_properties(SlotDeckSystemInformationPlugin PROPERTIES OUTPUT_NAME slotdeck-system-information)
if(APPLE)
    set_target_properties(SlotDeckSystemInformationPlugin PROPERTIES BUILD_RPATH "@loader_path/../Frameworks")
elseif(UNIX)
    set_target_properties(SlotDeckSystemInformationPlugin PROPERTIES BUILD_RPATH "$ORIGIN/.." INSTALL_RPATH "$ORIGIN/../../lib;$ORIGIN/..")
endif()

slotdeck_set_warnings(SlotDeckSystemInformationPlugin)
slotdeck_enable_coverage(SlotDeckSystemInformationPlugin)
slotdeck_enable_sanitizers(SlotDeckSystemInformationPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckSystemInformationPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
