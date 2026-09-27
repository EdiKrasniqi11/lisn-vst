#pragma once
#include <juce_core/juce_core.h>
#include <functional>

enum class PythonStatus { NotPython, NoDemucs, Ready };

struct PythonSearchDirs
{
    juce::String pathEnv;                                // PATH
    juce::File localAppData, programFiles, systemDrive, userProfile;
    static PythonSearchDirs fromEnvironment();           // skips unset/relative variables
};

// python.exe paths to try, first to last, without duplicates (case-insensitive):
// 1. hint, if it is an absolute path to an existing file (a bare name such as "python" is dropped:
//    CreateProcess could resolve it to the Microsoft Store stub)
// 2. <dir>\python.exe for each absolute PATH entry (quotes removed) that exists, skipping any containing "WindowsApps"
// 3. localAppData\Programs\Python\Python3*, programFiles\Python3*, systemDrive\Python3* (each newest first by the number after "Python")
// 4. userProfile\anaconda3\python.exe, userProfile\miniconda3\python.exe
juce::StringArray pythonCandidates (const juce::String& hint, const PythonSearchDirs& dirs);

// Runs: exe -c "import importlib.util,sys;sys.exit(0 if importlib.util.find_spec('demucs') else 3)"
// exit 0 -> Ready, 3 -> NoDemucs; failure to start, any other exit code, timeout or abort -> NotPython (process killed).
PythonStatus checkPython (const juce::String& exe, int timeoutMs, const std::function<bool()>& shouldAbort = {});

struct PythonFound { juce::String exe; PythonStatus status = PythonStatus::NotPython; };

// First Ready candidate; else the first NoDemucs one; else { "", NotPython }. Stops early when shouldAbort() is true.
PythonFound findPython (const juce::String& hint, const PythonSearchDirs& dirs, const std::function<bool()>& shouldAbort = {});
