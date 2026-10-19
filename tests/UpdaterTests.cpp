#include "../Source/Updater.h"
#include <juce_cryptography/juce_cryptography.h>

struct UpdaterTests : juce::UnitTest
{
    UpdaterTests() : juce::UnitTest ("Updater") {}

    using Step = Updater::Step;
    using Failure = Updater::Failure;

    static void pump (const std::function<bool()>& until)   // the thread posts to the message loop
    {
        for (int i = 0; i < 100 && ! until(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    static void waitFor (const std::function<bool()>& cond)   // bounded: 2 s
    {
        for (int i = 0; i < 200 && ! cond(); ++i)
            juce::Thread::sleep (10);
    }

    static ReleaseInfo release (const juce::String& hash = {})
    {
        ReleaseInfo r;
        r.version = "9.9.9";
        r.installerUrl = "https://example.invalid/dl/LISN-Setup.exe";
        r.installerSha256 = hash;
        return r;
    }

    struct Rig
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-updater-test", "");
        Updater u;
        std::atomic<int> downloads { 0 };
        Rig()
        {
            u.downloadDir = dir;
            u.downloadFn = [this] (const juce::URL&, const juce::String&, const juce::File& dest, const std::function<bool (float)>& progress)
            {
                ++downloads;
                dest.getParentDirectory().createDirectory();
                progress (0.5f);
                dest.replaceWithText ("installer");
                progress (1.0f);
                return true;
            };
        }
        ~Rig() { dir.deleteRecursively(); }
        juce::File file() const { return dir.getChildFile ("LISN-Setup.exe"); }
        void toReady() { u.startDownload (release()); pump ([this] { return u.getStep() == Step::ready; }); }
    };

    void runTest() override
    {
        beginTest ("a download ends ready, and a second start while downloading is ignored");
        {
            Rig r;
            r.u.startDownload (release());
            expect (r.u.getStep() == Step::downloading);
            r.u.startDownload (release());
            pump ([&] { return r.u.getStep() == Step::ready; });
            expect (r.u.getStep() == Step::ready);
            expectEquals (r.u.getProgress(), 1.0f);
            expect (r.file().existsAsFile());
            expectEquals (r.downloads.load(), 1);
        }

        beginTest ("cancel returns to idle with nothing left, and a new start downloads again");
        {
            Rig r;
            r.u.downloadFn = [&] (const juce::URL&, const juce::String&, const juce::File& dest, const std::function<bool (float)>& progress)
            {
                ++r.downloads;
                dest.getParentDirectory().createDirectory();
                dest.getSiblingFile (dest.getFileName() + ".part").replaceWithText ("x");
                while (progress (0.1f)) juce::Thread::sleep (10);
                return false;
            };
            r.u.startDownload (release());
            waitFor ([&] { return r.downloads.load() == 1; });
            r.u.cancel();
            expect (r.u.getStep() == Step::idle);   // at once; the thread tidies up behind
            r.u.startDownload (release());   // waits for the old thread to finish
            waitFor ([&] { return r.downloads.load() == 2; });
            expectEquals (r.downloads.load(), 2);
            r.u.cancel();
            waitFor ([&] { return r.dir.getNumberOfChildFiles (juce::File::findFiles) == 0; });
            expectEquals (r.dir.getNumberOfChildFiles (juce::File::findFiles), 0);
        }

        beginTest ("a failing download is failed/download until reset");
        {
            Rig r;
            r.u.downloadFn = [] (const juce::URL&, const juce::String&, const juce::File&, const std::function<bool (float)>&) { return false; };
            r.u.startDownload (release());
            pump ([&] { return r.u.getStep() == Step::failed; });
            expect (r.u.getStep() == Step::failed && r.u.getFailure() == Failure::download);
            r.u.reset();
            expect (r.u.getStep() == Step::idle && r.u.getFailure() == Failure::none);
        }

        beginTest ("a file already there is reused only when its hash matches");
        {
            Rig r;
            r.dir.createDirectory();
            r.file().replaceWithText ("installer");
            const auto hash = juce::SHA256 (r.file()).toHexString();
            r.u.startDownload (release (hash));
            expect (r.u.getStep() == Step::ready);
            expectEquals (r.downloads.load(), 0);

            Rig bad;
            bad.dir.createDirectory();
            bad.file().replaceWithText ("something else");
            bad.u.startDownload (release (hash));
            pump ([&] { return bad.u.getStep() == Step::ready; });
            expectEquals (bad.downloads.load(), 1);

            Rig noHash;   // no digest: can't prove it's complete
            noHash.dir.createDirectory();
            noHash.file().replaceWithText ("installer");
            noHash.u.startDownload (release());
            pump ([&] { return noHash.u.getStep() == Step::ready; });
            expectEquals (noHash.downloads.load(), 1);
        }

        beginTest ("the installer's exit code decides the result");
        {
            struct Case { int code; Step step; Failure failure; };
            for (const auto& c : { Case { 0, Step::installed, Failure::none }, Case { 101, Step::failed, Failure::engine },
                                   Case { 7, Step::failed, Failure::inUse }, Case { 1223, Step::failed, Failure::install },
                                   Case { -2, Step::waitingForInstaller, Failure::none } })
            {
                Rig r;
                r.toReady();
                r.u.launchFn = [code = c.code] (const juce::File&) { return code; };
                r.u.install (r.dir.getChildFile ("stamp"));
                expect (r.u.getStep() == Step::installing);
                pump ([&] { return r.u.getStep() != Step::installing; });
                expect (r.u.getStep() == c.step && r.u.getFailure() == c.failure, "code " + juce::String (c.code));
            }
        }

        beginTest ("install is ignored unless ready");
        {
            Rig r;
            r.u.launchFn = [] (const juce::File&) { return 0; };
            r.u.install ({});
            expect (r.u.getStep() == Step::idle);
        }

        beginTest ("engineChanged follows the stamp, and the installer is deleted after installed");
        {
            Rig r;
            const auto stamp = r.dir.getChildFile ("ready.txt");
            r.toReady();
            stamp.replaceWithText ("a");
            r.u.launchFn = [stamp] (const juce::File&) { stamp.replaceWithText ("b"); return 0; };
            r.u.install (stamp);
            pump ([&] { return r.u.getStep() == Step::installed; });
            expect (r.u.getStep() == Step::installed);
            expect (r.u.engineChanged());
            expect (! r.file().existsAsFile());

            Rig same;
            const auto s2 = same.dir.getChildFile ("ready.txt");
            same.toReady();
            s2.replaceWithText ("a");
            same.u.launchFn = [] (const juce::File&) { return 0; };
            same.u.install (s2);
            pump ([&] { return same.u.getStep() == Step::installed; });
            expect (same.u.getStep() == Step::installed);
            expect (! same.u.engineChanged());
        }

        beginTest ("a listener removed mid-download doesn't stop it");
        {
            struct L : juce::ChangeListener { void changeListenerCallback (juce::ChangeBroadcaster*) override {} };
            Rig r;
            L l;
            r.u.addChangeListener (&l);
            r.u.startDownload (release());
            r.u.removeChangeListener (&l);
            pump ([&] { return r.u.getStep() == Step::ready; });
            expect (r.u.getStep() == Step::ready);
        }
    }
};

static UpdaterTests updaterTests;
