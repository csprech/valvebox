cmake_minimum_required(VERSION 3.22)
project(Valvebox VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found. Run scripts/bootstrap.sh first.")
endif()
add_subdirectory(external/JUCE)

# The instrument without its window: compiled into both the app and the test runner.
set(VALVEBOX_CORE_SOURCES
  src/core/Model.cpp
  src/core/Waves.cpp
  src/core/Voices.cpp
  src/core/Effects.cpp
  src/core/Rack.cpp
  src/core/Edit.cpp
  src/core/Machine.cpp
  src/core/MachinePlay.cpp
  src/core/Factory.cpp
)

# (switches over enums use `default:` on purpose; JUCE's warning set flags every one)
set_source_files_properties(${VALVEBOX_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-switch-enum;-Wno-float-equal")

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
  ${VALVEBOX_CORE_SOURCES}
  src/engine/Engine.cpp
  src/ui/Materials.cpp
  src/ui/Widgets.cpp
  src/ui/MainComponent.cpp
  src/Main.cpp)
target_include_directories(Valvebox PRIVATE src)
target_compile_definitions(Valvebox PRIVATE ${VALVEBOX_JUCE_DEFS} JUCE_APPLICATION_NAME_STRING="Valvebox"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(Valvebox PRIVATE -Wno-float-equal)
target_link_libraries(Valvebox PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
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
