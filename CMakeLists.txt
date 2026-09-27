cmake_minimum_required(VERSION 3.22)
project(WaveTree VERSION 2.0.0 LANGUAGES C CXX)

# foleys_gui_magic requires C++20 (v1 core was C++17 and compiles fine as 20).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
else()
    message(FATAL_ERROR "No local JUCE found — set -DJUCE_DIR=/path/to/JUCE")
endif()

# ---- build switches ---------------------------------------------------------
# GUI_EDITOR OFF = no foleys WYSIWYG window next to the plugin (builds to hand out).
# INSTALL_AFTER_BUILD OFF = leave the bundles in the build dir, touch no plugin folder.
option(WAVETREE_GUI_EDITOR "Open the foleys WYSIWYG editor window with the plugin" ON)
option(WAVETREE_INSTALL_AFTER_BUILD "Copy the built plugins into ~/Library/Audio/Plug-Ins" ON)

# ---- WaveTree II (v2 identity — v1 bundles must never be overwritten) -------
juce_add_plugin(WaveTree
    PLUGIN_MANUFACTURER_CODE Wvtr
    PLUGIN_CODE              Wtr2
    COMPANY_NAME             "WaveTree"
    BUNDLE_ID                com.wavetree.v2
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  ${WAVETREE_INSTALL_AFTER_BUILD}
    PLUGIN_NAME              "WaveTree II"
    PRODUCT_NAME             "WaveTree II"
    FORMATS                  VST3 Standalone AU)

juce_generate_juce_header(WaveTree)
target_sources(WaveTree PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(WaveTree PRIVATE Source Source/dsp Source/common)
target_compile_definitions(WaveTree PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)

# Default WYSIWYG layout, embedded (also shipped editable at Resources/magic.xml).
juce_add_binary_data(WaveTreeData SOURCES Resources/magic.xml)
set_target_properties(WaveTreeData PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- foleys_gui_magic (WYSIWYG GUI layer; module only, no example CMake) ----
if(DEFINED FOLEYS_DIR AND EXISTS "${FOLEYS_DIR}/modules/foleys_gui_magic/foleys_gui_magic.h")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic/modules/foleys_gui_magic/foleys_gui_magic.h")
    set(FOLEYS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic")
else()
    message(FATAL_ERROR "No foleys_gui_magic checkout found — set -DFOLEYS_DIR=...")
endif()
juce_add_module("${FOLEYS_DIR}/modules/foleys_gui_magic")
target_compile_definitions(WaveTree PUBLIC
    FOLEYS_SHOW_GUI_EDITOR_PALLETTE=$<BOOL:${WAVETREE_GUI_EDITOR}>
    FOLEYS_ENABLE_BINARY_DATA=0
    FOLEYS_ENABLE_OPEN_GL_CONTEXT=0)

target_link_libraries(WaveTree PRIVATE
    WaveTreeData
    foleys_gui_magic
    juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ---- offline DSP checks (JUCE-free CLI, wired as CTest) ---------------------
add_executable(wavetree_check tools/wavetree_check.cpp)
target_include_directories(wavetree_check PRIVATE Source/common)
add_executable(acv2_check tools/acv2_check.cpp)
target_include_directories(acv2_check PRIVATE Source/common)
enable_testing()
add_test(NAME wavetree_dwt_check COMMAND wavetree_check)
add_test(NAME acv2_shared_check COMMAND acv2_check)

# ---- offline editor screenshot tool (links shared code, builds NO bundle) ----
# Deliberately NOT a CTest: it spins a real message loop and writes PNGs.
add_executable(wavetree_shot tools/wavetree_shot.cpp)
target_link_libraries(wavetree_shot PRIVATE WaveTree)
target_include_directories(wavetree_shot PRIVATE
    $<TARGET_PROPERTY:WaveTree,INCLUDE_DIRECTORIES>)
target_compile_definitions(wavetree_shot PRIVATE JucePlugin_Build_Standalone=0)
