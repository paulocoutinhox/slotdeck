find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

include(GoogleTest)

# The Windows offscreen platform plugin exposes no font database, so the native plugin runs the interface tests there.
set(SLOTDECK_TEST_PLATFORM "offscreen")
if(WIN32)
    set(SLOTDECK_TEST_PLATFORM "windows")
endif()

# A case chains many asynchronous conditions and each one owns the scaled wait budget, so the case budget stays far above that budget and only a stuck process is killed.
set(SLOTDECK_TEST_TIMEOUT 300)
if(SLOTDECK_ENABLE_SANITIZERS)
    set(SLOTDECK_TEST_TIMEOUT 900)
endif()

set(SLOTDECK_TEST_INCLUDE_DIRECTORIES
    ${googletest_SOURCE_DIR}/googletest/include
    ${googletest_SOURCE_DIR}/googlemock/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/generated
)

# The entry point carries the fixture servers every suite starts, so it is compiled once and linked by all of them.
qt_add_library(SlotDeckTestMain OBJECT TestMain.cpp)
target_include_directories(SlotDeckTestMain BEFORE PRIVATE ${SLOTDECK_TEST_INCLUDE_DIRECTORIES})
target_link_libraries(SlotDeckTestMain PUBLIC GTest::gtest GTest::gmock Qt6::Test Qt6::Widgets)
slotdeck_set_warnings(SlotDeckTestMain)
slotdeck_enable_coverage(SlotDeckTestMain)
slotdeck_enable_sanitizers(SlotDeckTestMain)

function(slotdeck_add_test_suite target)
    qt_add_executable(${target} ${ARGN})
    target_include_directories(${target} BEFORE PRIVATE ${SLOTDECK_TEST_INCLUDE_DIRECTORIES})
    target_link_libraries(${target} PRIVATE SlotDeckTestMain GTest::gtest GTest::gmock Qt6::Test)
    slotdeck_set_warnings(${target})
    slotdeck_enable_coverage(${target})
    slotdeck_enable_sanitizers(${target})
    # a suite that links Qt WebEngine needs seconds to answer the discovery listing on a cold runner.
    gtest_discover_tests(
        ${target}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 120
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${SLOTDECK_TEST_PLATFORM};QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu;QTWEBENGINE_DISABLE_SANDBOX=1"
            TIMEOUT ${SLOTDECK_TEST_TIMEOUT}
    )
endfunction()

slotdeck_add_test_suite(
    SlotDeckCoreTests
    core/CoreTests.cpp
)
target_link_libraries(SlotDeckCoreTests PRIVATE SlotDeckCore)
add_dependencies(SlotDeckCoreTests SlotDeckBrowserPlugin SlotDeckCodeEditorPlugin SlotDeckDonatePlugin SlotDeckLogsPlugin SlotDeckAiPlugin SlotDeckSystemInformationPlugin SlotDeckTerminalPlugin SlotDeckWebServerPlugin)
if(APPLE)
    set(SLOTDECK_TEST_PLUGIN_DIRECTORY "${CMAKE_BINARY_DIR}/PlugIns")
else()
    set(SLOTDECK_TEST_PLUGIN_DIRECTORY "$<TARGET_FILE_DIR:SlotDeckCoreTests>/plugins")
endif()
add_custom_target(
    SlotDeckTestPlugins ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SLOTDECK_TEST_PLUGIN_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckLogsPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckSystemInformationPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckDonatePlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckAiPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckBrowserPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckCodeEditorPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckTerminalPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SlotDeckWebServerPlugin>"
            "${SLOTDECK_TEST_PLUGIN_DIRECTORY}/"
    DEPENDS SlotDeckBrowserPlugin SlotDeckCodeEditorPlugin SlotDeckDonatePlugin SlotDeckLogsPlugin SlotDeckAiPlugin SlotDeckSystemInformationPlugin SlotDeckTerminalPlugin SlotDeckWebServerPlugin
)
add_dependencies(SlotDeckCoreTests SlotDeckTestPlugins)
if(APPLE)
    set(SLOTDECK_TEST_HWINFO_DIRECTORY "${CMAKE_BINARY_DIR}/Frameworks")
else()
    set(SLOTDECK_TEST_HWINFO_DIRECTORY "$<TARGET_FILE_DIR:SlotDeckCoreTests>")
endif()
add_custom_command(
    TARGET SlotDeckCoreTests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SLOTDECK_TEST_HWINFO_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hwinfo_battery>" "$<TARGET_FILE:hwinfo_cpu>" "$<TARGET_FILE:hwinfo_disk>" "$<TARGET_FILE:hwinfo_gpu>" "$<TARGET_FILE:hwinfo_mainboard>" "$<TARGET_FILE:hwinfo_os>" "$<TARGET_FILE:hwinfo_ram>" "$<TARGET_FILE:hwinfo_network>" "${SLOTDECK_TEST_HWINFO_DIRECTORY}/"
)

slotdeck_add_test_suite(
    SlotDeckTerminalTests
    terminal/TerminalTests.cpp
)
target_link_libraries(SlotDeckTerminalTests PRIVATE SlotDeckTerminalObjects SlotDeckCore)

slotdeck_add_test_suite(
    SlotDeckWebServerTests
    web-server/WebServerTests.cpp
)
target_link_libraries(SlotDeckWebServerTests PRIVATE SlotDeckWebServerObjects SlotDeckCore)

slotdeck_add_test_suite(
    SlotDeckLogsTests
    logs/LogsTests.cpp
)
target_link_libraries(SlotDeckLogsTests PRIVATE SlotDeckLogsObjects SlotDeckCore)

slotdeck_add_test_suite(
    SlotDeckSystemInformationTests
    system-information/SystemInformationTests.cpp
)
target_link_libraries(SlotDeckSystemInformationTests PRIVATE SlotDeckSystemInformationObjects SlotDeckCore)
add_custom_command(TARGET SlotDeckSystemInformationTests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:hwinfo_battery>" "$<TARGET_FILE:hwinfo_cpu>" "$<TARGET_FILE:hwinfo_disk>" "$<TARGET_FILE:hwinfo_gpu>" "$<TARGET_FILE:hwinfo_mainboard>" "$<TARGET_FILE:hwinfo_os>" "$<TARGET_FILE:hwinfo_ram>" "$<TARGET_FILE:hwinfo_network>" "$<TARGET_FILE_DIR:SlotDeckSystemInformationTests>/")

slotdeck_add_test_suite(
    SlotDeckAiTests
    ai/AiTests.cpp
    ai/AiToolTests.cpp
    ai/AiRuntimeTests.cpp
    ai/AiTestSupport.h
)
target_link_libraries(SlotDeckAiTests PRIVATE SlotDeckAiObjects SlotDeckCore)

slotdeck_add_test_suite(
    SlotDeckDonateTests
    donate/DonateTests.cpp
)
target_link_libraries(SlotDeckDonateTests PRIVATE DonatePluginObjects SlotDeckCore)

slotdeck_add_test_suite(
    SlotDeckBrowserTests
    browser/BrowserTests.cpp
)
target_link_libraries(SlotDeckBrowserTests PRIVATE BrowserPluginObjects SlotDeckCore)

slotdeck_add_test_suite(
    SlotDeckCodeEditorTests
    code-editor/CodeEditorTests.cpp
)
target_link_libraries(SlotDeckCodeEditorTests PRIVATE CodeEditorPluginObjects SlotDeckCore)
