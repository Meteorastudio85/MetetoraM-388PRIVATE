cmake_minimum_required(VERSION 3.22)
project(M388 VERSION 0.3.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(M388Assets SOURCES
    Assets/bg_main.jpg
    Assets/fader_cap.png
    Assets/sw_mic_a.png Assets/sw_mic_b.png
    Assets/sw_byp_a.png Assets/sw_byp_b.png)
set_target_properties(M388Assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

juce_add_plugin(M388
    COMPANY_NAME "M388"
    PLUGIN_MANUFACTURER_CODE Mtap
    PLUGIN_CODE M388
    FORMATS VST3
    PRODUCT_NAME "M-388 Tape Console"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(M388 PRIVATE
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h
    Source/TapeCore.h Source/ParamTable.h Source/Geometry.h Source/SegmentFont.h)

target_compile_definitions(M388 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(M388 PRIVATE M388Assets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
