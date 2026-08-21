qt_add_library(DonatePluginObjects OBJECT DonatePlugin.cpp DonatePlugin.h DonateTranslations.h DonateView.cpp DonateView.h)
qt_add_resources(DonatePluginObjects "donate-assets" PREFIX "/slotdeck/donate" FILES assets/profile.png)
target_include_directories(DonatePluginObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(DonatePluginObjects PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets SlotDeckUi)
set_target_properties(DonatePluginObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)

slotdeck_set_warnings(DonatePluginObjects)
slotdeck_enable_coverage(DonatePluginObjects)
slotdeck_enable_sanitizers(DonatePluginObjects)

qt_add_library(SlotDeckDonatePlugin SHARED)
target_link_libraries(SlotDeckDonatePlugin PRIVATE DonatePluginObjects)
set_target_properties(SlotDeckDonatePlugin PROPERTIES OUTPUT_NAME slotdeck-donate)

slotdeck_set_warnings(SlotDeckDonatePlugin)
slotdeck_enable_coverage(SlotDeckDonatePlugin)
slotdeck_enable_sanitizers(SlotDeckDonatePlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckDonatePlugin LIBRARY DESTINATION plugins RUNTIME DESTINATION plugins)
endif()
