#include "FakeHarmony.h"
#include "../Source/PluginProcessor.h"

struct ProcessorStateTests : juce::UnitTest
{
    ProcessorStateTests() : juce::UnitTest ("ProcessorState") {}

    static void load (StemSplitterProcessor& p, const juce::XmlElement& xml)
    {
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (xml, blob);
        p.setStateInformation (blob.getData(), (int) blob.getSize());
    }

    void runTest() override
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("lisn-state", {});
        const auto input = root.getChildFile ("song.wav");
        const auto stems = root.getChildFile ("stems");
        expect (stems.getChildFile ("vocals.wav").create().wasOk());

        beginTest ("round trip");
        {
            juce::XmlElement xml ("StemSplitter");
            xml.setAttribute ("model", "htdemucs_6s");
            xml.setAttribute ("input", input.getFullPathName());
            xml.setAttribute ("stems", stems.getFullPathName());
            StemSplitterProcessor a;
            load (a, xml);
            a.setTheme ("midnight");
            a.setPythonPath ("C:\\Py\\python.exe");

            juce::MemoryBlock blob;
            a.getStateInformation (blob);
            StemSplitterProcessor b;
            b.setStateInformation (blob.getData(), (int) blob.getSize());
            expectEquals (b.getTheme(), juce::String ("midnight"));
            expect (b.isSixStems());
            expectEquals (b.getPythonPath(), juce::String ("C:\\Py\\python.exe"));
            expect (b.getLastInput() == input);
            expect (b.getStemDir() == stems);
            expect (b.hasStems());
            expect (a.job.getState() == JobState::Idle && b.job.getState() == JobState::Idle);
        }

        beginTest ("version-1 state");
        {
            StemSplitterProcessor p;
            load (p, *juce::parseXML ("<StemSplitter model=\"htdemucs\" python=\"python\" input=\"\" stems=\"\"/>"));
            expectEquals (p.getTheme(), juce::String ("dusk"));
            expectEquals (p.getPythonPath(), juce::String ("python"));
            expect (! p.isSixStems());
            expect (p.getLastInput() == juce::File() && ! p.hasStems());
            expectEquals (p.getProduct(), juce::String ("stemsplitter"));
            expectEquals (p.getHarmonyQuality(), juce::String ("quick"));
            expect (p.getHarmonyInput() == juce::File());
        }

        beginTest ("product, quality and Harmonies' song are saved");
        {
            FakeHarmony::CachedSong song;
            StemSplitterProcessor a;
            a.harmonyFlow.harmonyJobForTests().testCommand = FakeHarmony::okPass (song);
            a.setProduct ("harmonies");
            a.setHarmonyQuality ("best");
            a.startHarmonies (song.file);
            for (int i = 0; i < 100 && a.harmonyFlow.getStep() != HarmonyFlow::Step::done; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            expect (a.harmonyFlow.getStep() == HarmonyFlow::Step::done, a.harmonyFlow.getError());

            juce::MemoryBlock blob;
            a.getStateInformation (blob);
            StemSplitterProcessor b;
            b.setStateInformation (blob.getData(), (int) blob.getSize());
            expectEquals (b.getProduct(), juce::String ("harmonies"));
            expectEquals (b.getHarmonyQuality(), juce::String ("best"));
            expect (b.getHarmonyInput() == song.file);

            song.stems.getParentDirectory().deleteRecursively();   // its cache is gone: restoring still works
            StemSplitterProcessor c;
            c.setStateInformation (blob.getData(), (int) blob.getSize());
            expect (c.getHarmonyInput() == song.file);
            expect (c.harmonyFlow.getStep() == HarmonyFlow::Step::idle);
        }

        beginTest ("setSixStems without a song");
        {
            StemSplitterProcessor p;
            p.setSixStems (true);
            expect (p.isSixStems());
            expect (p.job.getState() == JobState::Idle);
            p.setSixStems (false);
            expect (! p.isSixStems());
        }
        root.deleteRecursively();
    }
};

static ProcessorStateTests processorStateTests;
