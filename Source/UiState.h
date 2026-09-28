#pragma once
#include "SeparationJob.h"

class StemSplitterProcessor;

enum class Screen { Drop, Splitting, Stems, Error };

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

    bool sameScreenAs (const UiState& o) const;   // every field except progress
};

// Pure mapping, testable without a running job. savedStemDir is empty unless it holds stems (the processor's cached check).
UiState uiStateFor (JobState, ErrorKind, double progress, const juce::String& errorText, const juce::String& pythonExe,
                    const juce::File& jobStemDir, const juce::File& savedStemDir, const juce::File& lastInput, bool sixStems);
UiState uiStateFor (const StemSplitterProcessor&);   // reads the job + processor fields
