cmake_minimum_required(VERSION 3.25)
project(XTONE VERSION 0.15.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(XTONE_BUILD_PLUGIN "Plugin bauen (braucht JUCE-Submodule)" ON)
option(XTONE_BUILD_TESTS  "DSP-Tests bauen" ON)

add_subdirectory(dsp)                 # pure C++20, no JUCE

if (XTONE_BUILD_PLUGIN)
    add_subdirectory(JUCE)
    set(xtone_formats VST3 Standalone)
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND xtone_formats AU)
    endif()
    juce_add_plugin(Xtone
        COMPANY_NAME             "XTONE Dev"      # TODO: decide before v0.2.0 (see HANDOFF 12.4)
        BUNDLE_ID                com.terreursquad.xtone
        PLUGIN_MANUFACTURER_CODE Xtdv             # 4 chars, >= 1 upper case - NEVER change after release
        PLUGIN_CODE              Xtn1             # 4 chars, unique - NEVER change after release
        PRODUCT_NAME             "XTONE"
        VERSION                  ${PROJECT_VERSION}
        FORMATS                  ${xtone_formats}
        AU_MAIN_TYPE             kAudioUnitType_MusicDevice
        IS_SYNTH                 TRUE
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD  FALSE
        VST3_CATEGORIES          Instrument Sampler)
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_GENERATOR STREQUAL "Xcode")
        # Sign the finished bundles after JUCE generates resources such as the VST3 manifest.
        foreach (format IN LISTS xtone_formats)
            add_custom_command(TARGET Xtone_${format} POST_BUILD
                COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:Xtone_${format}>"
                VERBATIM)
        endforeach()
    endif()
    juce_add_binary_data(XtoneFaceplate
        HEADER_NAME XtoneFaceplateData.h
        NAMESPACE XtoneFaceplateData
        SOURCES
            plugin/assets/HammerDrillFaceplate.png
            plugin/assets/MakitaFaceplate.jpg
            plugin/assets/FestoolFaceplate.jpg
            plugin/assets/HiltiFaceplate.jpg
            plugin/assets/DeWaltKnob.png
            plugin/assets/FestoolKnob.png
            plugin/assets/HiltiKnob.png
            plugin/assets/MakitaKnob.png
            plugin/assets/TeddyBellyPlush.jpg
            plugin/assets/TeddyBellyQuilt.jpg
            plugin/assets/TeddyBellyFaceplate.png
            plugin/assets/TeddyBellyHeart.png)
    set_property(SOURCE plugin/gui/FaceplateArtwork.cpp plugin/gui/CottonCandyArtwork.cpp APPEND PROPERTY
        INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:XtoneFaceplate,INTERFACE_INCLUDE_DIRECTORIES>")
    target_sources(Xtone PRIVATE
        plugin/gui/CottonCandyArtwork.cpp
        plugin/gui/FaceplateArtwork.cpp
        plugin/gui/ModulationPanels.cpp
        plugin/gui/ParameterControl.cpp
        plugin/PluginProcessor.cpp
        plugin/Parameters.cpp
        plugin/SampleLoader.cpp
        plugin/Presets.cpp
        plugin/gui/PluginEditor.cpp
        plugin/gui/HeaderBar.cpp
        plugin/gui/HammerLookAndFeel.cpp
        plugin/gui/RatePanel.cpp
        plugin/gui/WaveformView.cpp
        plugin/gui/SequencerGrid.cpp
        plugin/gui/ParamPanel.cpp)
    target_include_directories(Xtone PRIVATE plugin)
    if (MSVC)
        target_compile_options(Xtone PRIVATE /utf-8)
    endif()
    target_compile_definitions(Xtone PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
    target_link_libraries(Xtone
        PRIVATE xtone_dsp "$<LINK_ONLY:XtoneFaceplate>" juce::juce_audio_utils juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
endif()

if (XTONE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                                GIT_TAG v3.8.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    enable_testing()
    add_subdirectory(tests)
endif()
