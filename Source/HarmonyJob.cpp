#include "HarmonyJob.h"
#include "DemucsProgress.h"
#include "PythonFinder.h"
#include "BinaryData.h"

bool HarmonyJob::hasResult (const juce::File& stemDir)
{
    const auto d = dirFor (stemDir);
    return d.getChildFile ("lead.wav").existsAsFile() && d.getChildFile ("harmonies.wav").existsAsFile();
}

void HarmonyJob::start (const juce::File& dir, const juce::String& hint)
{
    cancel();
    stemDir = dir;
    pythonHint = hint;
    error.clear();
    progress = 0.0f;
    state = State::running;
    startThread (juce::Thread::Priority::low);
}

void HarmonyJob::cancel()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl (procLock);   // run() starts proc under this lock, so kill() never races start()
        proc.kill();
    }
    stopThread (5000);
    state = State::idle;
}

void HarmonyJob::run()
{
    const auto fail = [this] (const juce::String& message) { error = message; state = State::failed; };
    const auto part = stemDir.getChildFile ("harmony.part");
    part.deleteRecursively();
    if (! part.createDirectory()) { fail ("Can't write to " + stemDir.getFullPathName()); return; }

    auto cmd = testCommand;
    if (cmd.isEmpty())
    {
        const auto found = findPython (pythonHint, PythonSearchDirs::fromEnvironment(), [this] { return threadShouldExit(); });
        if (threadShouldExit()) return;
        if (found.status != PythonStatus::Ready) { fail ("LISN's engine isn't installed."); return; }
        // The script lives in the app; it goes next to the stems cache, rewritten every pass so it always matches.
        const auto script = stemDir.getParentDirectory().getParentDirectory().getSiblingFile ("harmony_split.py");
        if (! script.replaceWithData (BinaryData::harmony_split_py, (size_t) BinaryData::harmony_split_pySize))
        {
            fail ("Can't write " + script.getFullPathName());
            return;
        }
        cmd = { found.exe, "-I", script.getFullPathName(), stemDir.getChildFile ("vocals.wav").getFullPathName() };
    }
    cmd.add (part.getFullPathName());

    bool started = false;
    {
        const juce::ScopedLock sl (procLock);
        if (! threadShouldExit())
            started = proc.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);
    }
    std::string output;
    if (started)
    {
        char buf[64];
        while (! threadShouldExit())
        {
            if (const auto n = proc.readProcessOutput (buf, sizeof (buf)); n > 0)
            {
                output.append (buf, (size_t) n);
                if (const auto p = parseDemucsProgress (output); p >= 0)
                    progress = (float) p / 100.0f;
            }
            else if (! proc.isRunning())
                break;
            else
                wait (50);
        }
    }
    if (threadShouldExit())
    {
        proc.kill();
        proc.waitForProcessToFinish (2000);
        part.deleteRecursively();
        return;   // cancel() sets idle
    }

    const auto out = dirFor (stemDir);
    if (! started || proc.getExitCode() != 0 || ! part.getChildFile ("lead.wav").existsAsFile()
        || ! part.getChildFile ("harmonies.wav").existsAsFile())
    {
        part.deleteRecursively();
        const auto tail = juce::String (output).trim();
        fail (started ? tail.substring (juce::jmax (0, tail.length() - 400)) : juce::String ("Couldn't start Python."));
        DBG ("Harmony Split failed: " << error);
        return;
    }
    out.deleteRecursively();
    if (! part.moveFileTo (out)) { part.deleteRecursively(); fail ("Can't write " + out.getFullPathName()); return; }
    progress = 1.0f;
    state = State::done;
}
