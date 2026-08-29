qt_add_library(
    SlotDeckCodeEditorObjects
    OBJECT
    CodeDocument.cpp
    CodeDocument.h
    CodeEditorPlugin.cpp
    CodeEditorPlugin.h
    CodeEditorRepository.cpp
    CodeEditorRepository.h
    CodeEditorState.h
    CodeEditorTranslations.h
    CodeEditorView.cpp
    CodeEditorView.h
    CodeEditorWidget.cpp
    CodeEditorWidget.h
    CodeColorScheme.cpp
    CodeColorScheme.h
    CodeSyntaxHighlighter.cpp
    CodeSyntaxHighlighter.h
    CodeWorkspaceView.cpp
    CodeWorkspaceView.h
    EditorConfig.cpp
    EditorConfig.h
    FileFinder.cpp
    FileFinder.h
    FileSystemFailure.cpp
    FileSystemFailure.h
    FileWatch.cpp
    FileWatch.h
    LanguageRegistry.cpp
    LanguageRegistry.h
    LanguageServerClient.cpp
    LanguageServerClient.h
    LanguageServerTransport.cpp
    LanguageServerTransport.h
)
qt_add_resources(SlotDeckCodeEditorObjects "code-editor-assets" PREFIX "/slotdeck/code-editor" FILES assets/languages.json assets/schemes.json)
target_include_directories(SlotDeckCodeEditorObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(SlotDeckCodeEditorObjects PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets SlotDeckUi)
set_target_properties(SlotDeckCodeEditorObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)
slotdeck_set_warnings(SlotDeckCodeEditorObjects)
slotdeck_enable_coverage(SlotDeckCodeEditorObjects)
slotdeck_enable_sanitizers(SlotDeckCodeEditorObjects)

qt_add_library(SlotDeckCodeEditorPlugin SHARED)
target_link_libraries(SlotDeckCodeEditorPlugin PRIVATE SlotDeckCodeEditorObjects)
set_target_properties(SlotDeckCodeEditorPlugin PROPERTIES OUTPUT_NAME slotdeck-code-editor)
slotdeck_set_warnings(SlotDeckCodeEditorPlugin)
slotdeck_enable_coverage(SlotDeckCodeEditorPlugin)
slotdeck_enable_sanitizers(SlotDeckCodeEditorPlugin)

if(NOT APPLE)
    install(TARGETS SlotDeckCodeEditorPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
