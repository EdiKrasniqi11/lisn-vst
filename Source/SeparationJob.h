#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

enum class JobState { Idle, Running, Done, Failed, Cancelled };

class SeparationJob : private juce::Thread
{
public:
    SeparationJob() : Thread ("demucs") {}
    ~SeparationJob() override { cancel(); }

    void start (juce::File input, juce::String model, juce::String python);
    void cancel()
    {
        signalThreadShouldExit();
        if (isThreadRunning())
            proc.kill();   // unblocks the blocking readProcessOutput() in run()
        stopThread (5000);
    }

    JobState getState() const      { return state.load(); }
    double getProgress() const     { return progress.load(); }
    juce::String getError() const  { return error; }   // valid when Failed
    juce::File getStemDir() const  { return outDir; }  // valid when Done

    // cache key = path + size + mtime, so edited files re-separate
    static juce::File stemDirFor (juce::File input, juce::String model);
    static juce::Array<juce::File> stemsIn (juce::File dir);

private:
    void run() override;

    juce::File inputFile, outDir;
    juce::String modelName, pythonExe, error;
    juce::ChildProcess proc;
    std::atomic<JobState> state { JobState::Idle };
    std::atomic<double> progress { 0.0 };
};
