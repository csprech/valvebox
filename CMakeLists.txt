cmake_minimum_required(VERSION 3.22)
project(Valvebox VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found. Run scripts/bootstrap.sh first.")
endif()
add_subdirectory(external/JUCE)

# The source lists (the instrument without its window, the engine and panel, the processor for hosts) live in
# valvebox-module.cmake, so the Tabletop studio compiles exactly what the app does.
include(valvebox-module.cmake)

set(VALVEBOX_JUCE_DEFS
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1
)

option(VALVEBOX_BUILD_APP "Build the GUI app" ON)

if(VALVEBOX_BUILD_APP)
juce_add_gui_app(Valvebox
  PRODUCT_NAME "Valvebox"
  COMPANY_NAME "Valvebox Research"
  BUNDLE_ID "org.valvebox.app"
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Valvebox listens to its AUDIO IN (audio input 1) for AUDIO IN THRU, AUTO BPM SCAN and the audio-in oscillator.")

target_sources(Valvebox PRIVATE
  ${VALVEBOX_MODULE_SOURCES}
  src/Main.cpp)
target_include_directories(Valvebox PRIVATE src)
target_compile_definitions(Valvebox PRIVATE ${VALVEBOX_JUCE_DEFS} JUCE_APPLICATION_NAME_STRING="Valvebox"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(Valvebox PRIVATE -Wno-float-equal)
target_link_libraries(Valvebox PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- the GPUI front end's bridge
# A static library with the instrument (engine, and the JUCE panel used off-screen as controller and renderer)
# behind the C interface in bridge/PanelBridge.h. gpui/build.rs builds and links it.
option(VALVEBOX_BUILD_BRIDGE "Build the bridge for the GPUI front end" OFF)
if(VALVEBOX_BUILD_BRIDGE)
  add_library(valvebox_bridge STATIC ${VALVEBOX_CORE_SOURCES} ${VALVEBOX_UI_SOURCES} bridge/PanelBridge.cpp bridge/ValveboxAdapter.cpp)
  target_include_directories(valvebox_bridge PRIVATE src)
  target_compile_definitions(valvebox_bridge PRIVATE ${VALVEBOX_JUCE_DEFS} JUCE_STANDALONE_APPLICATION=0
    JUCE_APPLICATION_NAME_STRING="Valvebox" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
  target_link_libraries(valvebox_bridge PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------- tests
juce_add_console_app(ValveboxTests PRODUCT_NAME "ValveboxTests")
target_sources(ValveboxTests PRIVATE
  ${VALVEBOX_CORE_SOURCES}
  tests/TestMain.cpp
  tests/SmokeTests.cpp
  tests/PanelTests.cpp
  tests/SoundTests.cpp)
target_include_directories(ValveboxTests PRIVATE src)
target_compile_definitions(ValveboxTests PRIVATE ${VALVEBOX_JUCE_DEFS})
target_compile_options(ValveboxTests PRIVATE -Wno-float-equal) # (the model's defaulted == compares floats exactly, on purpose)
target_link_libraries(ValveboxTests PRIVATE
  juce::juce_core
  juce::juce_audio_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME ValveboxTests COMMAND ValveboxTests)
