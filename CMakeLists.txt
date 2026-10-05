cmake_minimum_required(VERSION 3.24)
project(StemSplitter VERSION 0.2.0)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Signalsmith Stretch (MIT): keep-the-key time-stretch for the speed control. Linear (MIT) is its FFT library, declared
# first at a pinned commit so Stretch's own CMakeLists uses this one instead of fetching its tag.
FetchContent_Declare(signalsmith-linear GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG de55e6a50ffcf6f8f43f649692d94691c7025151)   # 0.6.4
FetchContent_Declare(signalsmith-stretch GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df)   # 1.4.0
FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)

# The fonts, and the header's symbol straight from the logo kit (BinaryData::lisnsymbolglass_svg).
juce_add_binary_data(LisnAssets SOURCES
    Resources/fonts/Unbounded-Bold.ttf
    Resources/fonts/DMSans-Regular.ttf
    Resources/fonts/DMSans-Medium.ttf
    Resources/fonts/DMSans-Bold.ttf
    docs/design/logo/kit/lisn-symbol-glass.svg)

# Shared by the plugin and lisn_tests; later .cpp files go inside this call.
set(LISN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SeparationJob.cpp
    Source/PythonFinder.cpp
    Source/Theme.cpp
    Source/StemPlayer.cpp
    Source/Peaks.cpp
    Source/WaveGeometry.cpp
    Source/Frost.cpp
    Source/WaveBackground.cpp
    Source/Controls.cpp
    Source/StemRow.cpp
    Source/UiState.cpp
    Source/Screens.cpp
    Source/RenderJob.cpp)

juce_add_plugin(StemSplitter
    COMPANY_NAME "LISN"
    PLUGIN_MANUFACTURER_CODE Lisn
    PLUGIN_CODE Stsp
    FORMATS VST3 Standalone
    PRODUCT_NAME "LISN StemSplitter"
    # The logo kit's app icon. juceaide turns these into the exe's .ico (16/32/48/256) when CMake configures, so
    # re-run cmake after changing them. The 32 px render keeps the 32 px frame exact: Windows draws the taskbar
    # icon from it at 100% and 125% scaling.
    ICON_SMALL docs/design/logo/kit/app/lisn-app-icon-16.png
    ICON_BIG docs/design/logo/kit/app/lisn-app-icon-1024.png
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{USERPROFILE}/VST3")   # no admin needed; add this folder to the DAW's scan paths

# Source/StandaloneApp.cpp is LISN, the desktop app: JUCE's Standalone wrapper runs it instead of its own app class.
target_sources(StemSplitter PRIVATE ${LISN_SOURCES} Source/StandaloneApp.cpp)

target_compile_definitions(StemSplitter PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(StemSplitter
    PRIVATE LisnAssets juce::juce_audio_utils signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(lisn_tests PRODUCT_NAME "lisn_tests")

target_sources(lisn_tests PRIVATE
    ${LISN_SOURCES}
    tests/TestMain.cpp
    tests/ThemeTests.cpp
    tests/PythonFinderTests.cpp
    tests/ProcessorStateTests.cpp
    tests/StemPlayerTests.cpp
    tests/PeaksTests.cpp
    tests/WaveGeometryTests.cpp
    tests/FrostTests.cpp
    tests/WaveBackgroundTests.cpp
    tests/ControlsTests.cpp
    tests/StemRowTests.cpp
    tests/UiStateTests.cpp
    tests/OpenWithTests.cpp
    tests/RenderJobTests.cpp
    tests/Snapshots.cpp)

target_compile_definitions(lisn_tests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_Name=\"LISN StemSplitter\"")

target_link_libraries(lisn_tests PRIVATE
    LisnAssets juce::juce_audio_utils signalsmith-stretch juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_executable(progress_test tests/progress_test.cpp)
target_compile_definitions(progress_test PRIVATE
    SAMPLE_OUTPUT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/demucs_sample_output.txt")
add_test(NAME progress_test COMMAND progress_test)
add_test(NAME lisn_tests COMMAND lisn_tests)
