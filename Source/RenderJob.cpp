#include "RenderJob.h"

RenderJob::RenderJob() : juce::Thread ("render") {}

RenderJob::~RenderJob() { cancel(); }

void RenderJob::start (Request r)
{
    cancel();
    request = std::move (r);
    progress = 0.0f;
    state = State::running;
    startThread (juce::Thread::Priority::low);
}

void RenderJob::cancel()
{
    stopThread (-1);   // render checks threadShouldExit after every chunk, so this waits one chunk at most
    state = State::idle;
}

juce::File RenderJob::getDest() const
{
    return request.dest;
}

void RenderJob::run()
{
    const auto& r = request;
    const bool ok = StemPlayer::render (r.stems, r.gains, r.startSec, r.endSec, r.speed, r.dest,
                                        [this] (float f) { progress = f; return ! threadShouldExit(); });
    if (ok) progress = 1.0f;
    state = ok ? State::done : threadShouldExit() ? State::idle : State::failed;
}
