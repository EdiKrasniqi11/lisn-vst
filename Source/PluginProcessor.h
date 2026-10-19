#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SeparationJob.h"
#include "HarmonyFlow.h"
#include "StemPlayer.h"

class StemSplitterProcessor : public juce::AudioProcessor
{
public:
    StemSplitterProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay (double sampleRate, int maxBlock) override { player.prepare (sampleRate, maxBlock); }
    void releaseResources() override { player.release(); }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { player.addTo (buffer); }   // pass-through plus the stem player

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;   // read-only: never writes members
    void setStateInformation (const void*, int) override;

    SeparationJob job;
    HarmonyFlow harmonyFlow;                 // Harmonies' two steps; lives here so closing the window doesn't stop it
    StemPlayer player;
    bool introShown = false;                 // the opening animation played for this instance (message thread; not saved)

    // Settings. The host may save state from any thread, so every access goes through settingsLock.
    juce::String getTheme() const      { const juce::ScopedLock sl (settingsLock); return theme; }
    juce::String getPythonPath() const { const juce::ScopedLock sl (settingsLock); return pythonPath; }
    juce::File getLastInput() const    { const juce::ScopedLock sl (settingsLock); return lastInput; }
    juce::File getStemDir() const      { const juce::ScopedLock sl (settingsLock); return stemDir; }
    bool hasStems() const              { const juce::ScopedLock sl (settingsLock); return stemDirHasWav; }   // cached, no disk scan
    bool isSixStems() const            { const juce::ScopedLock sl (settingsLock); return model == "htdemucs_6s"; }
    juce::String getProduct() const        { const juce::ScopedLock sl (settingsLock); return product; }          // "stems" or "harmonies"
    juce::String getHarmonyQuality() const { const juce::ScopedLock sl (settingsLock); return harmonyQuality; }   // "quick" or "best"
    juce::File getHarmonyInput() const     { const juce::ScopedLock sl (settingsLock); return harmonyInput; }

    void setTheme (const juce::String& id);
    void setPythonPath (const juce::String& exe) { const juce::ScopedLock sl (settingsLock); pythonPath = exe; }
    void setSixStems (bool six);             // re-splits the last song when its stems exist for the other model
    void startSplit (const juce::File& input);
    void setProduct (const juce::String& id);          // anything but "harmonies" means "stems"
    void setHarmonyQuality (const juce::String& q);    // anything but "best" means "quick"
    void startHarmonies (const juce::File& song, bool keepExistingResult = true);   // Harmonies' flow for this song
    void syncWithJob();                      // adopts a finished job's stem dir and working Python

    // What the Harmonies screens need from disk, re-read when its inputs change and otherwise at most once a second (the
    // editor asks 30 times a second). Message thread only.
    struct HarmonyResults
    {
        HarmonyFlow::Step step = HarmonyFlow::Step::idle;
        juce::File input, offer;             // the keys: Harmonies' song, and the Stems product's song
        juce::File dir;                      // Harmonies' song's 4-stem folder
        juce::String quality;                // its harmony result's quality; empty = no result
        bool offerHasResult = false;         // the Stems product's song already has a harmony result
        bool valid = false;
        juce::uint32 at = 0;
    };
    const HarmonyResults& harmonyResults() const;

private:
    void applyJobResults (juce::File& dir, juce::String& python) const;   // what syncWithJob adopts, applied to copies

    juce::CriticalSection settingsLock;
    juce::String theme = "dusk";
    juce::String model = "htdemucs";
    juce::String pythonPath;   // a hint (the user's pick or the last Python that worked); empty = search
    juce::File lastInput;      // last dropped file
    juce::String product = "stems";
    juce::String harmonyQuality = "quick";
    juce::File harmonyInput;   // Harmonies' own song (its stems are the 4-stem split's cache, whatever model says)
    juce::File stemDir;        // folder of finished stems, empty if none
    bool stemDirHasWav = false;
    mutable HarmonyResults harmonyResultsCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitterProcessor)
};
