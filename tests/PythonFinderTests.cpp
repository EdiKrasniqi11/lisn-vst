#include "../Source/PythonFinder.h"

struct PythonFinderTests : juce::UnitTest
{
    PythonFinderTests() : juce::UnitTest ("PythonFinder") {}

    void runTest() override
    {
        beginTest ("candidate order");
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-pyfinder", {});
        auto fakePython = [&] (const char* dir)   // empty file standing in for python.exe
        {
            auto f = root.getChildFile (dir).getChildFile ("python.exe");
            expect (f.create().wasOk());
            return f.getFullPathName();
        };
        const auto path1  = fakePython ("path1");
        const auto path2  = fakePython ("path2");
        const auto store  = fakePython ("Microsoft/WindowsApps");
        const auto lad310 = fakePython ("lad/Programs/Python/Python310");
        const auto lad312 = fakePython ("lad/Programs/Python/Python312");
        const auto pf311  = fakePython ("pf/Python311");
        const auto sd39   = fakePython ("sd/Python39");
        const auto mini   = fakePython ("home/miniconda3");
        auto dirOf = [] (const juce::String& exe) { return juce::File (exe).getParentDirectory().getFullPathName(); };

        PythonSearchDirs dirs;
        dirs.pathEnv = dirOf (path1) + ";" + dirOf (store) + ";relative\\dir;;" + dirOf (path1).quoted() + ";" + dirOf (path2).quoted();
        dirs.localAppData = root.getChildFile ("lad");
        dirs.programFiles = root.getChildFile ("pf");
        dirs.systemDrive  = root.getChildFile ("sd");
        dirs.userProfile  = root.getChildFile ("home");

        const juce::StringArray expected { pf311, path1, path2, lad312, lad310, sd39, mini };
        expectEquals (pythonCandidates (pf311, dirs).joinIntoString ("\n"), expected.joinIntoString ("\n"));

        beginTest ("bare hint is dropped");
        expect (pythonCandidates ("python", dirs) == pythonCandidates ("", dirs));
        expectEquals (pythonCandidates ("", dirs)[0], path1);
        root.deleteRecursively();

        beginTest ("checkPython rejects non-Python programs");
        expect (checkPython ("C:\\definitely\\missing.exe", 2000) == PythonStatus::NotPython);
        expect (checkPython ("C:\\Windows\\System32\\where.exe", 5000) == PythonStatus::NotPython);

        if (juce::SystemStats::getEnvironmentVariable ("LISN_INTEGRATION", {}) == "1")
        {
            beginTest ("finds this machine's Python (LISN_INTEGRATION=1)");
            const auto found = findPython ("", PythonSearchDirs::fromEnvironment());
            logMessage ("found Python: " + found.exe);
            expect (found.status == PythonStatus::Ready, found.exe);
        }
    }
};

static PythonFinderTests pythonFinderTests;
