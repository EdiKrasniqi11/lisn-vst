#pragma once
#include "HarmonyFlow.h"
#include "SeparationJob.h"

class StemSplitterProcessor;

enum class Screen { Drop, Splitting, Stems, Error, HarmonyDrop, HarmonySplitting, Harmonies };

// What the editor shows. Built from the processor and its job on every timer tick.
struct UiState
{
    Screen screen = Screen::Drop;
    double progress = 0.0;                   // Splitting
    ErrorKind error = ErrorKind::None;       // Error
    juce::String errorText, pythonExe;       // Error
    juce::File stemDir;                      // Stems
    juce::String songName;                   // lastInput file name
    bool sixStems = false;

    // Harmonies (its own product): the three Harmony* screens, and Error when its flow failed.
    bool harmoniesProduct = false;
    int harmonyStep = 0;                     // HarmonySplitting: 1 = finding the vocals, 2 = splitting the harmonies
    bool downloading = false;                // HarmonySplitting step 2 is fetching Best's model
    juce::String offerSong;                  // HarmonyDrop: the StemSplitter song offered, file name without extension; empty = none
    juce::File harmonyStemDir;               // Harmonies
    juce::String harmonyQuality;             // Harmonies: the result's quality ("quick" / "best")

    bool sameScreenAs (const UiState& o) const;   // every field except progress
};

// Pure mapping, testable without a running job. savedStemDir is empty unless it holds stems (the processor's cached check).
UiState uiStateFor (JobState, ErrorKind, double progress, const juce::String& errorText, const juce::String& pythonExe,
                    const juce::File& jobStemDir, const juce::File& savedStemDir, const juce::File& lastInput, bool sixStems);
// Harmonies' mapping. harmonyStemDir is where the result lives (when hasResult); stemSplitterSong is the Stems product's song
// (empty = none) and stemSplitterSongHasStems says it is already split.
UiState harmoniesStateFor (HarmonyFlow::Step, float progress, bool downloading, ErrorKind, const juce::String& errorText,
                           const juce::String& pythonExe, const juce::File& harmonyInput, const juce::File& harmonyStemDir,
                           bool hasResult, const juce::File& stemSplitterSong, bool stemSplitterSongHasStems, const juce::String& quality);
UiState uiStateFor (const StemSplitterProcessor&);   // reads the job + processor fields
