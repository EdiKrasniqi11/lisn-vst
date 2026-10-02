#pragma once
#include "StemPlayer.h"
#include <atomic>

// StemPlayer::render on its own thread. Every input is copied at start(); the owner polls the state on the message thread.
// The destructor cancels and joins.
class RenderJob : private juce::Thread
{
public:
    struct Request { juce::Array<juce::File> stems; std::vector<float> gains; double startSec = 0, endSec = 0, speed = 1.0; juce::File dest; };
    enum class State { idle, running, done, failed };
    RenderJob();
    ~RenderJob() override;                       // cancel()
    void start (Request);                        // cancels any running render first
    void cancel();                               // stops within one chunk, joins; the state goes back to idle
    State getState() const { return state.load(); }
    float getProgress() const { return progress.load(); }   // 0..1
    juce::File getDest() const;                  // the current request's dest (message thread)

private:
    void run() override;
    Request request;
    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
};
