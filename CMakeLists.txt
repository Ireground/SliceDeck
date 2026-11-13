cmake_minimum_required(VERSION 3.22)

project(SliceDeck VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static runtime so the plugin doesn't need extra Visual C++ DLLs
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Signalsmith Stretch (MIT licence) - time-stretching / pitch-shifting
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        88061fd4eda56a2353c5d65c74aa3f4ccf76f9da)
FetchContent_MakeAvailable(signalsmith_stretch)

juce_add_plugin(SliceDeck
    COMPANY_NAME            "Ireground"
    PLUGIN_MANUFACTURER_CODE Irgd
    PLUGIN_CODE             Slcd
    FORMATS                 VST3
    PRODUCT_NAME            "SliceDeck"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Instrument Sampler)

juce_generate_juce_header(SliceDeck)

target_sources(SliceDeck PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReaperBridge.cpp)

# Needed for the REAPER extension interface (Steinberg headers shipped with JUCE)
target_include_directories(SliceDeck PRIVATE
    ${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK)

target_compile_definitions(SliceDeck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(MSVC)
    target_compile_options(SliceDeck PRIVATE /bigobj)
endif()

target_link_libraries(SliceDeck
    PRIVATE
        signalsmith-stretch
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
