// LISN, the desktop app: the plugin's editor in its own window, playing through the default output.
// CMakeLists.txt sets JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP, so JUCE's Standalone wrapper calls juce_CreateApplication()
// below instead of using its own app class (which has an Options menu, an "input muted" banner and no single instance).
#if JucePlugin_Build_Standalone

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "OpenWith.h"

namespace
{
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::AudioProcessorEditor* editor)
        : DocumentWindow (name, juce::Colours::black, minimiseButton | closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        setContentOwned (editor, true);   // the window deletes the editor, before the holder deletes the processor
        centreWithSize (getWidth(), getHeight());
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class LisnApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "LISN"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "LISN";        // %APPDATA%\LISN\LISN.settings
        options.filenameSuffix  = ".settings";
        settings.setStorageParameters (options);

        // Output only: no mic, no "input muted" banner. The processor's input bus just gets silence.
        const juce::StandalonePluginHolder::PluginInOuts outputOnly[] { { 0, 2 } };
        holder = std::make_unique<juce::StandalonePluginHolder> (settings.getUserSettings(), false, juce::String(), nullptr,
                                                                 juce::Array<juce::StandalonePluginHolder::PluginInOuts> (outputOnly, 1));

        window = std::make_unique<MainWindow> (getApplicationName(), holder->processor->createEditorAndMakeActive());
        window->setVisible (true);
        window->getContentComponent()->grabKeyboardFocus();   // Space and the other keys work before any click
        open (commandLine);
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        window->toFront (true);
        open (commandLine);
    }

    void systemRequestedQuit() override
    {
        holder->savePluginState();   // theme, 4 or 6 stems, the last song and its stems
        quit();
    }

    void shutdown() override
    {
        window = nullptr;   // the editor goes before the processor
        holder = nullptr;
        settings.saveIfNeeded();
    }

private:
    void open (const juce::String& commandLine)   // a file from "Open with" goes the same way as a drop on the window
    {
        const auto file = fileToOpen (commandLine);
        const juce::StringArray files { file.getFullPathName() };
        auto* target = dynamic_cast<juce::FileDragAndDropTarget*> (window->getContentComponent());
        if (file.existsAsFile() && target != nullptr && target->isInterestedInFileDrag (files))
            target->filesDropped (files, 0, 0);
    }

    juce::ApplicationProperties settings;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<MainWindow> window;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication() { return new LisnApp(); }

#endif
