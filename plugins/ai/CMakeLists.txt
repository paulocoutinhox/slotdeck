qt_add_library(
    SlotDeckAiObjects
    OBJECT
    AiAgentPrompt.cpp
    AiAgentPrompt.h
    AiAgentSettingsView.cpp
    AiAgentSettingsView.h
    AiChatClient.cpp
    AiChatClient.h
    AiCliChatClient.cpp
    AiCliChatClient.h
    AiCommandRunner.cpp
    AiCommandRunner.h
    AiConnectionDialog.cpp
    AiConnectionDialog.h
    AiConnectionSettingsView.cpp
    AiConnectionSettingsView.h
    AiConversationView.cpp
    AiConversationView.h
    AiMcpClient.cpp
    AiMcpClient.h
    AiMcpSettingsView.cpp
    AiMcpSettingsView.h
    AiMcpTransport.cpp
    AiMcpTransport.h
    AiModelConnection.cpp
    AiModelConnection.h
    AiModelDiscovery.cpp
    AiModelDiscovery.h
    AiPlugin.cpp
    AiPlugin.h
    AiProviderCatalog.cpp
    AiProviderCatalog.h
    AiProviderScope.cpp
    AiProviderScope.h
    AiRateLimitSettingsView.cpp
    AiRateLimitSettingsView.h
    AiRequestGate.cpp
    AiRequestGate.h
    AiSecret.cpp
    AiSecret.h
    AiServiceSettingsView.cpp
    AiServiceSettingsView.h
    AiSkillCatalog.cpp
    AiSkillCatalog.h
    AiTaskDialog.cpp
    AiTaskDialog.h
    AiTaskInfoDialog.cpp
    AiTaskInfoDialog.h
    AiTaskRepository.cpp
    AiTaskRepository.h
    AiTasksView.cpp
    AiTasksView.h
    AiToolContract.cpp
    AiToolContract.h
    AiToolRegistry.cpp
    AiToolRegistry.h
    AiTranslations.h
    CronExpression.cpp
    CronExpression.h
)
qt_add_resources(SlotDeckAiObjects "ai-assets" PREFIX "/slotdeck/ai" FILES assets/models.json assets/providers.json)
target_include_directories(SlotDeckAiObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(SlotDeckAiObjects PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets SlotDeckUi)
set_target_properties(SlotDeckAiObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)
slotdeck_set_warnings(SlotDeckAiObjects)
slotdeck_enable_coverage(SlotDeckAiObjects)
slotdeck_enable_sanitizers(SlotDeckAiObjects)

qt_add_library(SlotDeckAiPlugin SHARED)
target_link_libraries(SlotDeckAiPlugin PRIVATE SlotDeckAiObjects)
set_target_properties(SlotDeckAiPlugin PROPERTIES OUTPUT_NAME slotdeck-ai)
slotdeck_set_warnings(SlotDeckAiPlugin)
slotdeck_enable_coverage(SlotDeckAiPlugin)
slotdeck_enable_sanitizers(SlotDeckAiPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckAiPlugin LIBRARY DESTINATION plugins RUNTIME DESTINATION plugins)
endif()
