cmake_minimum_required(VERSION 3.22)

project(SamsiBassline VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: Universal Binary (Apple Silicon + Intel), Mindest-macOS 10.15
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
    endif()
endif()

# Windows: statische MSVC-Laufzeit, damit das Plugin ohne VC-Redistributable läuft
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# JUCE: entweder lokaler Ordner (-DSAMSI_JUCE_DIR=...) oder automatischer Download
# Geprüfte Version: JUCE 9.0.4
# ---------------------------------------------------------------------------
set(SAMSI_JUCE_DIR "" CACHE PATH "Pfad zu einer lokalen JUCE-9.0.4-Kopie (leer = automatisch laden)")

if(SAMSI_JUCE_DIR)
    add_subdirectory(${SAMSI_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(SAMSI_BUILD_TESTS "Automatisierte Tests bauen" ON)
option(SAMSI_COPY_AFTER_BUILD "Plugin nach dem Build in den VST3-Systemordner kopieren" OFF)
option(SAMSI_MINIMAL_LINUX_DEPS "Linux: optionale X11-Erweiterungen abschalten (nur fuer Test-Builds)" OFF)

juce_add_plugin(SamsiBassline
    COMPANY_NAME            "Samsi Audio"
    COMPANY_WEBSITE         ""
    BUNDLE_ID               "com.samsiaudio.samsibassline"
    PLUGIN_MANUFACTURER_CODE Smsi          # 4 Zeichen, mind. ein Großbuchstabe
    PLUGIN_CODE             SmBl           # stabil lassen – identifiziert das Plugin in Projekten
    PRODUCT_NAME            "Samsi Bassline"
    VERSION                 ${PROJECT_VERSION}
    FORMATS                 VST3
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${SAMSI_COPY_AFTER_BUILD})

# Eingebettete Grafik (SAMSARA-Logo)
juce_add_binary_data(SamsiAssets
    HEADER_NAME SamsiBinaryData.h
    NAMESPACE SamsiBinary
    SOURCES Resources/samsara_logo.png)
set_target_properties(SamsiAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SAMSI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/dsp/Settings.cpp
    Source/dsp/OutputUtility.cpp
    Source/dsp/Glue.cpp
    Source/analysis/LiveAnalyzer.cpp
    Source/dsp/KickEngine.cpp
    Source/dsp/BassEngine.cpp
    Source/dsp/Sequencer.cpp
    Source/dsp/SamsiEngine.cpp
    Source/fit/OfflineRenderer.cpp
    Source/fit/FitAnalyzer.cpp
    Source/fit/AutoFit.cpp
    Source/fit/AnalysisService.cpp
    Source/fit/SampleAlign.cpp
    Source/content/WavetableFactory.cpp
    Source/content/PitchDetect.cpp
    Source/content/SampleLoader.cpp
    Source/content/KeyAnalysis.cpp
    Source/content/Presets.cpp
    Source/ui/KeyView.cpp
    Source/ui/FlowerOrb.cpp
    Source/ui/AnalyzerView.cpp
    Source/ui/GluePanel.cpp
    Source/ui/SamsiLookAndFeel.cpp
    Source/ui/KickPanel.cpp
    Source/ui/BassPanel.cpp
    Source/ui/MixPanel.cpp
    Source/ui/SequencerPanel.cpp
    Source/ui/FitPanel.cpp)

target_sources(SamsiBassline PRIVATE ${SAMSI_SOURCES})
target_include_directories(SamsiBassline PRIVATE Source)

set(SAMSI_DEFINITIONS
    SAMSI_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(SAMSI_MINIMAL_LINUX_DEPS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SAMSI_DEFINITIONS
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XRENDER=0
        JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
endif()

target_compile_definitions(SamsiBassline PUBLIC ${SAMSI_DEFINITIONS})
# Gleiche Rechenergebnisse auf allen Systemen: keine automatische FMA-Zusammenfassung
# (sonst rechnet z. B. Apple Silicon MIX = KICK + BASS um eine Rundungsstelle anders).
set(SAMSI_FP_FLAGS $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>)
target_compile_options(SamsiBassline PRIVATE ${SAMSI_FP_FLAGS})

target_link_libraries(SamsiBassline
    PRIVATE
        SamsiAssets
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests: Konsolenprogramm, das den echten Prozessor ohne Host betreibt
# ---------------------------------------------------------------------------
if(SAMSI_BUILD_TESTS)
    juce_add_console_app(SamsiTests PRODUCT_NAME "SamsiTests")
    target_sources(SamsiTests PRIVATE tests/TestMain.cpp ${SAMSI_SOURCES})
    target_include_directories(SamsiTests PRIVATE Source)
    target_compile_options(SamsiTests PRIVATE ${SAMSI_FP_FLAGS})
    target_compile_definitions(SamsiTests PRIVATE
        ${SAMSI_DEFINITIONS}
        SAMSI_TEST_BUILD=1
        JucePlugin_Name="Samsi Bassline"
        JucePlugin_Build_VST3=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SamsiTests PRIVATE
        SamsiAssets
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()
