cmake_minimum_required(VERSION 3.22)
project(GlitchLab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(GLITCHLAB_BUILD_PLUGIN "Build the VST3/AU plugin (needs JUCE)" ON)
option(GLITCHLAB_BUILD_TOOLS  "Build engine tests and the glitch_cli tool (no JUCE needed)" ON)
option(GLITCHLAB_STANDALONE   "Also build a standalone app next to the plugin" OFF)
set(GLITCHLAB_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (otherwise it is downloaded)")

# ---------------------------------------------------------------- tools ---
if(GLITCHLAB_BUILD_TOOLS)
  add_executable(engine_tests tests/test_engine.cpp)
  add_executable(glitch_cli tools/glitch_cli.cpp)
  enable_testing()
  add_test(NAME engine_tests COMMAND engine_tests)
endif()

# --------------------------------------------------------------- plugin ---
if(GLITCHLAB_BUILD_PLUGIN)
  if(GLITCHLAB_JUCE_PATH)
    add_subdirectory(${GLITCHLAB_JUCE_PATH} JUCE)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.9
      GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
  endif()

  set(GL_FORMATS VST3)
  if(APPLE)
    list(APPEND GL_FORMATS AU)
  endif()
  if(GLITCHLAB_STANDALONE)
    list(APPEND GL_FORMATS Standalone)
  endif()

  juce_add_plugin(GlitchLab
    COMPANY_NAME "GlitchLab"
    PLUGIN_MANUFACTURER_CODE Glch
    PLUGIN_CODE Gl01
    FORMATS ${GL_FORMATS}
    PRODUCT_NAME "GlitchLab"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect)

  target_sources(GlitchLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

  target_compile_definitions(GlitchLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

  target_link_libraries(GlitchLab
    PRIVATE
      juce::juce_audio_utils
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endif()
