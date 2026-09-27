#include "SeparationJob.h"
#include "DemucsProgress.h"

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

void SeparationJob::start (juce::File input, juce::String model, juce::String python)
{
    cancel();
    inputFile = input; modelName = model; pythonExe = python;
    outDir = stemDirFor (input, model);
    error.clear();
    progress = 0.0;

    if (! stemsIn (outDir).isEmpty()) { progress = 1.0; state = JobState::Done; return; } // cache hit

    state = JobState::Running;
    startThread();
}

void SeparationJob::run()
{
    auto root = outDir.getParentDirectory();   // demucs writes <root>/<model>/{stem}.wav
    root.createDirectory();

    juce::ChildProcess proc;
    juce::StringArray cmd { pythonExe, "-m", "demucs", "-n", modelName,
                            "-o", root.getFullPathName(), "--filename", "{stem}.{ext}",
                            inputFile.getFullPathName() };

    if (! proc.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "Could not start Python (\"" + pythonExe + "\"). Set the Python path in settings.";
        state = JobState::Failed;
        return;
    }

    std::string output;
    char buf[1024];
    for (;;)
    {
        if (threadShouldExit())
        {
            proc.kill();
            outDir.deleteRecursively();       // don't cache a half-written result
            state = JobState::Cancelled;
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

    if (proc.getExitCode() != 0 || stemsIn (outDir).isEmpty())
    {
        auto tail = juce::String (output).trim();
        error = "demucs failed:\n" + tail.substring (juce::jmax (0, tail.length() - 600));
        outDir.deleteRecursively();
        state = JobState::Failed;
        return;
    }
    progress = 1.0;
    state = JobState::Done;
}
