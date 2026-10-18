#include "HarmonyJob.h"
#include "DemucsProgress.h"
#include "PythonFinder.h"
#include "BinaryData.h"
#include <juce_cryptography/juce_cryptography.h>

namespace
{
    // The two karaoke models (Resources/harmony_split.py's MODELS). Best is fetched from Ultimate Vocal Remover's public
    // model set and must match its hash.
    // ponytail: the URL is UVR's release; host a copy on LISN's own releases once the authors have said yes.
    const char* const quickModel = "UVR_MDXNET_KARA_2.onnx";
    const char* const bestModel = "mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt";
    const char* const bestUrl = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/"
                                "mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt";
    const char* const bestSha256 = "1de20d459332fe8869aeb01327a31df0032262706e1365114e852dc271779813";
}

bool HarmonyJob::hasResult (const juce::File& stemDir)
{
    const auto d = dirFor (stemDir);
    return d.getChildFile ("lead.wav").existsAsFile() && d.getChildFile ("harmonies.wav").existsAsFile();
}

juce::String HarmonyJob::qualityOf (const juce::File& stemDir)
{
    if (! hasResult (stemDir)) return {};
    const auto q = dirFor (stemDir).getChildFile ("quality.txt").loadFileAsString().trim();   // the script writes it
    return q == "best" ? q : "quick";
}

juce::File HarmonyJob::userModelDir()
{
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("LISN/models/harmony");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/LISN/models/harmony");   // on Mac that special location is ~/Library
   #endif
}

juce::File HarmonyJob::findModel (const juce::String& q, const juce::String& pythonExe)
{
    const auto name = q == "best" ? bestModel : quickModel;
    juce::Array<juce::File> dirs { userModelDir() };
    if (juce::File::isAbsolutePath (pythonExe))   // <venv>\Scripts\python.exe -> <venv>\models\harmony
        dirs.insert (0, juce::File (pythonExe).getParentDirectory().getParentDirectory().getChildFile ("models/harmony"));
    for (const auto& d : dirs)
        if (d.getChildFile (name).existsAsFile())
            return d.getChildFile (name);
    return {};
}

bool HarmonyJob::download (const juce::URL& url, const juce::String& sha256, const juce::File& dest,
                           const std::function<bool (float)>& onProgress)
{
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    if (! dest.getParentDirectory().createDirectory()) return false;
    part.deleteFile();
    bool ok = false;
    {
        int status = 0;
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (20000).withNumRedirectsToFollow (5).withStatusCode (&status);
        auto in = url.createInputStream (options);
        juce::FileOutputStream out (part);
        if (in != nullptr && out.openedOk() && (url.isLocalFile() || status == 200))
        {
            const auto total = in->getTotalLength();
            juce::HeapBlock<char> buffer (1 << 16);
            ok = true;
            while (ok && ! in->isExhausted())
            {
                const auto n = in->read (buffer, 1 << 16);
                if (n < 0 || (n > 0 && ! out.write (buffer, (size_t) n)))
                    ok = false;
                else if (! onProgress (total > 0 ? (float) ((double) out.getPosition() / (double) total) : 0.0f))
                    ok = false;
            }
            out.flush();
            ok = ok && out.getStatus().wasOk() && (total <= 0 || out.getPosition() == total);
        }
    }
    ok = ok && juce::SHA256 (part).toHexString().equalsIgnoreCase (sha256);
    ok = ok && (! dest.exists() || dest.deleteFile()) && part.moveFileTo (dest);
    if (! ok) part.deleteFile();
    return ok;
}

void HarmonyJob::start (const juce::File& dir, const juce::String& hint, const juce::String& q)
{
    cancel();
    stemDir = dir;
    pythonHint = hint;
    quality = q;
    error.clear();
    progress = 0.0f;
    downloading = false;
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
    downloading = false;
    state = State::idle;
}

void HarmonyJob::run()
{
    const auto fail = [this] (const juce::String& message) { error = message; state = State::failed; };

    auto cmd = testCommand;
    if (cmd.isEmpty())
    {
        const auto found = findPython (pythonHint, PythonSearchDirs::fromEnvironment(), [this] { return threadShouldExit(); });
        if (threadShouldExit()) return;
        if (found.status != PythonStatus::Ready) { fail ("LISN's engine isn't installed."); return; }

        // Best quality's model, the first time: into the user's folder (no admin), checked against its hash.
        if (quality == "best" && findModel ("best", found.exe) == juce::File())
        {
            downloading = true;
            const auto ok = download (juce::URL (bestUrl), bestSha256, userModelDir().getChildFile (bestModel),
                                      [this] (float f) { progress = f; return ! threadShouldExit(); });
            downloading = false;
            progress = 0.0f;
            if (threadShouldExit()) return;
            if (! ok) { fail ("Couldn't download the Best quality model."); return; }
        }

        // The script lives in the app; it goes next to the stems cache, rewritten every pass so it always matches.
        const auto script = stemDir.getParentDirectory().getParentDirectory().getSiblingFile ("harmony_split.py");
        if (! script.replaceWithData (BinaryData::harmony_split_py, (size_t) BinaryData::harmony_split_pySize))
        {
            fail ("Can't write " + script.getFullPathName());
            return;
        }
        cmd = { found.exe, "-I", "-W", "ignore", script.getFullPathName(), stemDir.getChildFile ("vocals.wav").getFullPathName() };
    }

    const auto part = stemDir.getChildFile ("harmony.part");
    part.deleteRecursively();
    if (! part.createDirectory()) { fail ("Can't write to " + stemDir.getFullPathName()); return; }
    cmd.add (part.getFullPathName());
    cmd.add (quality);

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
