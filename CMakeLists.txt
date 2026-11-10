cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

project(Duckwerk VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(DUCKWERK_JUCE_TAG "9.0.5" CACHE STRING "JUCE version to fetch")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${DUCKWERK_JUCE_TAG}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(DUCKWERK_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_plugin(Duckwerk
    COMPANY_NAME "Duckwerk"
    PLUGIN_MANUFACTURER_CODE Dkwk
    PLUGIN_CODE Dkw1
    FORMATS VST3
    PRODUCT_NAME "Duckwerk"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Duckwerk PRIVATE ${DUCKWERK_SOURCES})

set(DUCKWERK_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(Duckwerk PUBLIC ${DUCKWERK_DEFS})
target_link_libraries(Duckwerk
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# Headless test: runs the DSP with a fake kick/bass and renders the editor to PNG.
juce_add_console_app(DuckwerkTest PRODUCT_NAME "DuckwerkTest")
target_sources(DuckwerkTest PRIVATE ${DUCKWERK_SOURCES} Test/Main.cpp)
target_compile_definitions(DuckwerkTest PRIVATE ${DUCKWERK_DEFS})
target_link_libraries(DuckwerkTest
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
