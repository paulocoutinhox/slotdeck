set(TERMINAL_PLATFORM_ICON)
if(APPLE)
    set(TERMINAL_PLATFORM_ICON "${PROJECT_SOURCE_DIR}/extras/images/logo.icns")
    set_source_files_properties(
        "${TERMINAL_PLATFORM_ICON}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )
elseif(WIN32)
    set(TERMINAL_PLATFORM_ICON "${CMAKE_CURRENT_SOURCE_DIR}/SlotDeck.rc")
endif()

set(SLOTDECK_CORE_SOURCES
    app/Application.cpp
    app/Application.h
    app/ConfigurationManager.cpp
    app/ConfigurationManager.h
    app/ApplicationSettingsStore.cpp
    app/ApplicationSettingsStore.h
    domain/ApplicationLanguage.cpp
    domain/ApplicationLanguage.h
    domain/ApplicationSettings.h
    filesystem/FileSystemService.cpp
    filesystem/FileSystemService.h
    persistence/DatabaseExecutor.cpp
    persistence/DatabaseExecutor.h
    persistence/ConfigurationTransfer.cpp
    persistence/ConfigurationTransfer.h
    persistence/CoreDatabaseSchema.cpp
    persistence/CoreDatabaseSchema.h
    persistence/PluginDatabase.h
    persistence/StateStore.cpp
    persistence/StateStore.h
    persistence/StoredValues.h
    plugins/LocalizationService.cpp
    plugins/LocalizationService.h
    plugins/CoreTranslations.h
    plugins/PluginInterface.h
    plugins/PluginManager.cpp
    plugins/PluginManager.h
    ui/AppStyle.cpp
    ui/AppStyle.h
    ui/ApplicationSettingsView.cpp
    ui/ApplicationSettingsView.h
    ui/ConfirmationDialog.cpp
    ui/ConfirmationDialog.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/ModeBar.cpp
    ui/ModeBar.h
    ui/SettingsView.cpp
    ui/SettingsView.h
    ui/ToastOverlay.cpp
    ui/ToastOverlay.h
    utils/Result.h
)

qt_add_library(SlotDeckCore STATIC ${SLOTDECK_CORE_SOURCES})
target_include_directories(
    SlotDeckCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/../generated
)
target_link_libraries(SlotDeckCore PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Sql Qt6::Widgets SlotDeckUi)

slotdeck_set_warnings(SlotDeckCore)
slotdeck_enable_coverage(SlotDeckCore)
slotdeck_enable_sanitizers(SlotDeckCore)

qt_add_executable(SlotDeck MACOSX_BUNDLE WIN32 main.cpp ${TERMINAL_PLATFORM_ICON})

# Qt names an application com.yourcompany when nothing declares it, and that name reaches the macOS bundle and the Windows manifest.
set_target_properties(
    SlotDeck
    PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "${SLOTDECK_BUNDLE_IDENTIFIER}"
        QT_WINDOWS_APP_PROJECT_IDENTIFIER "${SLOTDECK_BUNDLE_IDENTIFIER}"
)

if(APPLE)
    set_target_properties(SlotDeck PROPERTIES MACOSX_BUNDLE_ICON_FILE "logo.icns")
endif()

set(SLOTDECK_APP_ICON "${PROJECT_SOURCE_DIR}/extras/images/logo.png")
set_source_files_properties(
    "${SLOTDECK_APP_ICON}"
    PROPERTIES QT_RESOURCE_ALIAS "images/logo.png"
)
qt_add_resources(
    SlotDeck
    "slotdeck_assets"
    PREFIX "/"
    FILES "${SLOTDECK_APP_ICON}"
)

target_link_libraries(SlotDeck PRIVATE SlotDeckCore)

slotdeck_set_warnings(SlotDeck)
slotdeck_enable_coverage(SlotDeck)
slotdeck_enable_sanitizers(SlotDeck)

if(APPLE)
    find_program(SLOTDECK_DEVELOPMENT_CODESIGN_EXECUTABLE codesign REQUIRED)
    add_custom_target(
        SlotDeckBundledPlugins ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:hwinfo_battery>"
                "$<TARGET_FILE:hwinfo_cpu>"
                "$<TARGET_FILE:hwinfo_disk>"
                "$<TARGET_FILE:hwinfo_gpu>"
                "$<TARGET_FILE:hwinfo_mainboard>"
                "$<TARGET_FILE:hwinfo_os>"
                "$<TARGET_FILE:hwinfo_ram>"
                "$<TARGET_FILE:hwinfo_network>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/Frameworks/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckTerminalPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckWebServerPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckLogsPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckSystemInformationPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckDonatePlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckAiPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckBrowserPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckCodeEditorPlugin>"
                "$<TARGET_BUNDLE_DIR:SlotDeck>/Contents/PlugIns/"
        COMMAND "${SLOTDECK_DEVELOPMENT_CODESIGN_EXECUTABLE}"
                --force
                --deep
                --sign -
                "$<TARGET_BUNDLE_DIR:SlotDeck>"
        DEPENDS SlotDeck SlotDeckAiPlugin SlotDeckBrowserPlugin SlotDeckCodeEditorPlugin SlotDeckDonatePlugin SlotDeckLogsPlugin SlotDeckSystemInformationPlugin SlotDeckTerminalPlugin SlotDeckWebServerPlugin ${SLOTDECK_HWINFO_TARGETS}
    )
else()
    add_custom_target(
        SlotDeckBundledPlugins ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SlotDeck>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:hwinfo_battery>"
                "$<TARGET_FILE:hwinfo_cpu>"
                "$<TARGET_FILE:hwinfo_disk>"
                "$<TARGET_FILE:hwinfo_gpu>"
                "$<TARGET_FILE:hwinfo_mainboard>"
                "$<TARGET_FILE:hwinfo_os>"
                "$<TARGET_FILE:hwinfo_ram>"
                "$<TARGET_FILE:hwinfo_network>"
                "$<TARGET_FILE_DIR:SlotDeck>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckTerminalPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckWebServerPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckLogsPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckSystemInformationPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckDonatePlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckAiPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckBrowserPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SlotDeckCodeEditorPlugin>"
                "$<TARGET_FILE_DIR:SlotDeck>/plugins/"
        DEPENDS SlotDeck SlotDeckAiPlugin SlotDeckBrowserPlugin SlotDeckCodeEditorPlugin SlotDeckDonatePlugin SlotDeckLogsPlugin SlotDeckSystemInformationPlugin SlotDeckTerminalPlugin SlotDeckWebServerPlugin ${SLOTDECK_HWINFO_TARGETS}
    )
endif()

install(TARGETS SlotDeck BUNDLE DESTINATION . RUNTIME DESTINATION bin)

if(WIN32)
    # A Windows loader resolves a dependent library beside the executable, and nothing else installs these.
    install(
        FILES
            "$<TARGET_FILE:hwinfo_battery>"
            "$<TARGET_FILE:hwinfo_cpu>"
            "$<TARGET_FILE:hwinfo_disk>"
            "$<TARGET_FILE:hwinfo_gpu>"
            "$<TARGET_FILE:hwinfo_mainboard>"
            "$<TARGET_FILE:hwinfo_os>"
            "$<TARGET_FILE:hwinfo_ram>"
            "$<TARGET_FILE:hwinfo_network>"
        DESTINATION bin
    )
endif()

if(APPLE)
    find_program(SLOTDECK_MACDEPLOYQT_EXECUTABLE macdeployqt REQUIRED)
    find_program(SLOTDECK_CODESIGN_EXECUTABLE codesign REQUIRED)
    find_program(SLOTDECK_QTPATHS_EXECUTABLE qtpaths6 REQUIRED)
    execute_process(
        COMMAND "${SLOTDECK_QTPATHS_EXECUTABLE}" --plugin-dir
        OUTPUT_VARIABLE SLOTDECK_QT_PLUGIN_DIRECTORY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    file(REAL_PATH "${SLOTDECK_QT_PLUGIN_DIRECTORY}/platforms/libqcocoa.dylib"
         SLOTDECK_MACOS_PLATFORM_PLUGIN)
    set(SLOTDECK_CODESIGN_HARDENING)
    if(NOT SLOTDECK_CODESIGN_IDENTITY STREQUAL "-")
        set(SLOTDECK_CODESIGN_HARDENING --options runtime --timestamp)
    endif()
    install(CODE "
        set(app_bundle \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/SlotDeck.app\")
        execute_process(
            COMMAND \"${SLOTDECK_MACDEPLOYQT_EXECUTABLE}\"
                    \"\${app_bundle}\"
                    -no-plugins
                    -no-codesign
                    -appstore-compliant
                    -always-overwrite
            RESULT_VARIABLE deploy_result
        )
        if(NOT deploy_result EQUAL 0)
            message(FATAL_ERROR \"macdeployqt failed with exit code \${deploy_result}\")
        endif()

        file(MAKE_DIRECTORY \"\${app_bundle}/Contents/PlugIns/platforms\")
        file(COPY \"${SLOTDECK_MACOS_PLATFORM_PLUGIN}\"
             DESTINATION \"\${app_bundle}/Contents/PlugIns/platforms\")

        execute_process(
            COMMAND \"${SLOTDECK_CODESIGN_EXECUTABLE}\"
                    --force
                    --deep
                    ${SLOTDECK_CODESIGN_HARDENING}
                    --sign \"${SLOTDECK_CODESIGN_IDENTITY}\"
                    \"\${app_bundle}\"
            RESULT_VARIABLE codesign_result
        )
        if(NOT codesign_result EQUAL 0)
            message(FATAL_ERROR \"codesign failed with exit code \${codesign_result}\")
        endif()
    ")
else()
    qt_generate_deploy_script(
        TARGET SlotDeck
        OUTPUT_SCRIPT slotdeck_deploy_script
        CONTENT "
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:SlotDeck>\"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    ADDITIONAL_LIBRARIES
        \"$<TARGET_FILE:SlotDeckAiPlugin>\"
        \"$<TARGET_FILE:SlotDeckBrowserPlugin>\"
        \"$<TARGET_FILE:SlotDeckCodeEditorPlugin>\"
        \"$<TARGET_FILE:SlotDeckDonatePlugin>\"
        \"$<TARGET_FILE:SlotDeckLogsPlugin>\"
        \"$<TARGET_FILE:SlotDeckSystemInformationPlugin>\"
        \"$<TARGET_FILE:SlotDeckTerminalPlugin>\"
        \"$<TARGET_FILE:SlotDeckWebServerPlugin>\"
)
"
    )
    install(SCRIPT ${slotdeck_deploy_script})
endif()
