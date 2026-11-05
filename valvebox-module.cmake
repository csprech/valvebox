# Valvebox as a module for hosts (the Tabletop studio): the sources to compile into the host's own target,
# which brings JUCE (juce_audio_utils, juce_audio_processors, juce_gui_extra). The app and the tests use the
# same lists.
set(VALVEBOX_DIR ${CMAKE_CURRENT_LIST_DIR})
set(VALVEBOX_CORE_SOURCES
  ${VALVEBOX_DIR}/src/core/Model.cpp
  ${VALVEBOX_DIR}/src/core/Factory.cpp
  ${VALVEBOX_DIR}/src/core/Waves.cpp
  ${VALVEBOX_DIR}/src/core/Voices.cpp
  ${VALVEBOX_DIR}/src/core/Effects.cpp
  ${VALVEBOX_DIR}/src/core/Rack.cpp
  ${VALVEBOX_DIR}/src/core/Edit.cpp
  ${VALVEBOX_DIR}/src/core/Machine.cpp
  ${VALVEBOX_DIR}/src/core/MachinePlay.cpp)
set(VALVEBOX_UI_SOURCES
  ${VALVEBOX_DIR}/src/engine/Engine.cpp
  ${VALVEBOX_DIR}/src/ui/Materials.cpp
  ${VALVEBOX_DIR}/src/ui/Widgets.cpp
  ${VALVEBOX_DIR}/src/ui/MainComponent.cpp)
set(VALVEBOX_MODULE_SOURCES ${VALVEBOX_CORE_SOURCES} ${VALVEBOX_UI_SOURCES} ${VALVEBOX_DIR}/src/plugin/ValveboxProcessor.cpp)
# (switches over enums use `default:` on purpose; JUCE's warning set flags every one)
set_source_files_properties(${VALVEBOX_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-switch-enum;-Wno-float-equal")
