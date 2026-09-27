#include "SeparationJob.h"
#include "DemucsProgress.h"
#include "PythonFinder.h"

juce::File SeparationJob::stemDirFor (juce::File input, juce::String model)
{
    auto key = juce::String::toHexString ((input.getFullPathName()
                    + juce::String (input.getSize())
                    + juce::String (input.getLastModificationTime().toMilliseconds())).hashCode64());
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("StemSplitter/cache").getChildFile (key).getChildFile (model);
}

juce::Array<juce::File> SeparationJob::stemsIn (juce::File dir)
{
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();
    return files;
}

void SeparationJob::start (juce::File input, juce::String model, juce::String hint)
{
    cancel();
    inputFile = input; modelName = model; pythonHint = hint;
    outDir = stemDirFor (input, model);
    pythonExe.clear();
    error.clear();
    errorKind = ErrorKind::None;
    progress = 0.0;

    if (! stemsIn (outDir).isEmpty()) { progress = 1.0; state = JobState::Done; return; } // cache hit

    state = JobState::Running;
    startThread();
}

void SeparationJob::run()
{
    auto fail = [this] (ErrorKind kind, const juce::String& message)   // error fields first: readers check state
    {
        errorKind = kind;
        error = message;
        state = JobState::Failed;
    };

    const auto found = findPython (pythonHint, PythonSearchDirs::fromEnvironment(), [this] { return threadShouldExit(); });
    if (threadShouldExit()) { state = JobState::Cancelled; return; }
    if (found.status == PythonStatus::NotPython) { fail (ErrorKind::PythonMissing, "Python not found."); return; }
    pythonExe = found.exe;
    if (found.status == PythonStatus::NoDemucs) { fail (ErrorKind::DemucsMissing, "Demucs is not installed for \"" + pythonExe + "\"."); return; }

    auto root = outDir.getParentDirectory();   // demucs writes <root>/<model>/{stem}.wav
    root.createDirectory();

    juce::StringArray cmd { pythonExe, "-m", "demucs", "-n", modelName,
                            "-o", root.getFullPathName(), "--filename", "{stem}.{ext}",
                            inputFile.getFullPathName() };

    bool started = false;
    {
        const juce::ScopedLock sl (procLock);   // cancel() kills under this lock; start() replaces the process object
        if (threadShouldExit()) { state = JobState::Cancelled; return; }
        started = proc.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);
    }
    if (! started)
    {
        fail (ErrorKind::PythonMissing, "Could not start Python (\"" + pythonExe + "\").");
        return;
    }

    auto handleCancel = [this]
    {
        proc.kill();
        proc.waitForProcessToFinish (2000);
        outDir.deleteRecursively();       // don't cache a half-written result
        state = JobState::Cancelled;
    };

    std::string output;
    char buf[64];
    for (;;)
    {
        if (threadShouldExit())
        {
            handleCancel();
            return;
        }
        auto n = proc.readProcessOutput (buf, sizeof (buf));
        if (n > 0)
        {
            output.append (buf, (size_t) n);
            if (auto p = parseDemucsProgress (output); p >= 0)
                progress = p / 100.0;
        }
        else if (! proc.isRunning())
            break;
        else
            wait (50);
    }

    if (threadShouldExit())
    {
        handleCancel();
        return;
    }

    auto exitCode = proc.getExitCode();
    if (exitCode != 0 || stemsIn (outDir).isEmpty())
    {
        outDir.deleteRecursively();
        if (exitCode == 9009)
        {
            fail (ErrorKind::PythonMissing, "Could not start Python (\"" + pythonExe + "\").");
        }
        else
        {
            auto tail = juce::String (output).trim();
            fail (ErrorKind::Failed, "demucs failed:\n" + tail.substring (juce::jmax (0, tail.length() - 600)));
        }
        return;
    }
    progress = 1.0;
    state = JobState::Done;
}
