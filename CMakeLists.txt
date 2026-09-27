cmake_minimum_required(VERSION 3.22)
project(StemSplitter VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(LisnFonts SOURCES
    Resources/fonts/Unbounded-Bold.ttf
    Resources/fonts/DMSans-Regular.ttf
    Resources/fonts/DMSans-Medium.ttf
    Resources/fonts/DMSans-Bold.ttf)

# Shared by the plugin and lisn_tests; later .cpp files go inside this call.
set(LISN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SeparationJob.cpp
    Source/PythonFinder.cpp
    Source/Theme.cpp
    Source/StemPreview.cpp)

juce_add_plugin(StemSplitter
    COMPANY_NAME "LISN"
    PLUGIN_MANUFACTURER_CODE Lisn
    PLUGIN_CODE Stsp
    FORMATS VST3 Standalone
    PRODUCT_NAME "LISN StemSplitter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{USERPROFILE}/VST3")   # no admin needed; add this folder to the DAW's scan paths

target_sources(StemSplitter PRIVATE ${LISN_SOURCES})

target_compile_definitions(StemSplitter PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemSplitter
    PRIVATE LisnFonts juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(lisn_tests PRODUCT_NAME "lisn_tests")

target_sources(lisn_tests PRIVATE
    ${LISN_SOURCES}
    tests/TestMain.cpp
    tests/ThemeTests.cpp
    tests/PythonFinderTests.cpp
    tests/ProcessorStateTests.cpp
    tests/StemPreviewTests.cpp)

target_compile_definitions(lisn_tests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_Name=\"LISN StemSplitter\"")

target_link_libraries(lisn_tests PRIVATE
    LisnFonts juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_executable(progress_test tests/progress_test.cpp)
target_compile_definitions(progress_test PRIVATE
    SAMPLE_OUTPUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/demucs_sample_output.txt")
add_test(NAME progress_test COMMAND progress_test)
add_test(NAME lisn_tests COMMAND lisn_tests)
