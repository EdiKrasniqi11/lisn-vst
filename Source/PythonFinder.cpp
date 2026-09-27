#include "PythonFinder.h"
#include <algorithm>

PythonSearchDirs PythonSearchDirs::fromEnvironment()
{
    // juce::File asserts on relative or empty paths, so unset or odd variables become File().
    auto dir = [] (const char* name, const char* suffix = "")
    {
        auto v = juce::SystemStats::getEnvironmentVariable (name, {}).trim();
        if (v.isNotEmpty()) v << suffix;
        return juce::File::isAbsolutePath (v) ? juce::File (v) : juce::File();
    };

    PythonSearchDirs d;
    d.pathEnv      = juce::SystemStats::getEnvironmentVariable ("PATH", {});
    d.localAppData = dir ("LOCALAPPDATA");
    d.programFiles = dir ("ProgramFiles");
    d.systemDrive  = dir ("SystemDrive", "\\");
    d.userProfile  = dir ("USERPROFILE");
    return d;
}

juce::StringArray pythonCandidates (const juce::String& hint, const PythonSearchDirs& dirs)
{
    juce::StringArray out;
    auto addIfFile = [&out] (const juce::File& f)
    {
        if (f.existsAsFile()) out.addIfNotAlreadyThere (f.getFullPathName(), true);
    };
    // Only absolute paths: juce::File asserts on relative ones, and a bare "python" hint could resolve to the Store stub.
    if (auto h = hint.trim().unquoted(); juce::File::isAbsolutePath (h))
        addIfFile (juce::File (h));

    for (auto entry : juce::StringArray::fromTokens (dirs.pathEnv, ";", "\""))
    {
        entry = entry.trim().unquoted();
        if (juce::File::isAbsolutePath (entry) && ! entry.containsIgnoreCase ("WindowsApps"))
            addIfFile (juce::File (entry).getChildFile ("python.exe"));
    }

    auto versioned = [&] (const juce::File& base)   // base\Python3*\python.exe, newest first
    {
        if (! base.isDirectory()) return;
        auto found = base.findChildFiles (juce::File::findDirectories, false, "Python3*");
        auto version = [] (const juce::File& f) { return f.getFileName().substring (6).getIntValue(); };
        std::stable_sort (found.begin(), found.end(), [&] (const juce::File& a, const juce::File& b) { return version (a) > version (b); });
        for (auto& f : found) addIfFile (f.getChildFile ("python.exe"));
    };
    if (dirs.localAppData != juce::File()) versioned (dirs.localAppData.getChildFile ("Programs\\Python"));
    versioned (dirs.programFiles);
    versioned (dirs.systemDrive);

    if (dirs.userProfile != juce::File())
    {
        addIfFile (dirs.userProfile.getChildFile ("anaconda3\\python.exe"));
        addIfFile (dirs.userProfile.getChildFile ("miniconda3\\python.exe"));
    }
    return out;
}

PythonStatus checkPython (const juce::String& exe, int timeoutMs, const std::function<bool()>& shouldAbort)
{
    juce::ChildProcess p;
    if (! p.start (juce::StringArray { exe, "-c", "import importlib.util,sys;sys.exit(0 if importlib.util.find_spec('demucs') else 3)" }, 0))
        return PythonStatus::NotPython;

    const auto started = juce::Time::getMillisecondCounter();
    while (p.isRunning())
    {
        if ((shouldAbort && shouldAbort()) || juce::Time::getMillisecondCounter() - started >= (juce::uint32) timeoutMs)
        {
            p.kill();
            return PythonStatus::NotPython;
        }
        juce::Thread::sleep (50);
    }

    switch (p.getExitCode())
    {
        case 0:  return PythonStatus::Ready;
        case 3:  return PythonStatus::NoDemucs;
        default: return PythonStatus::NotPython;
    }
}

PythonFound findPython (const juce::String& hint, const PythonSearchDirs& dirs, const std::function<bool()>& shouldAbort)
{
    PythonFound noDemucs;
    for (auto& exe : pythonCandidates (hint, dirs))
    {
        if (shouldAbort && shouldAbort()) break;
        const auto status = checkPython (exe, 10000, shouldAbort);
        if (status == PythonStatus::Ready) return { exe, status };
        if (status == PythonStatus::NoDemucs && noDemucs.exe.isEmpty()) noDemucs = { exe, status };
    }
    return noDemucs;
}
