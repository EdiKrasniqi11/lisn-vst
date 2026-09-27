#include "StemPreview.h"

// ponytail: while previewing, the audio thread can briefly wait on BufferingAudioSource's lock during a
// 2048-frame disk read, and load() swaps sources without a fade (a click is possible when switching rows);
// idle blocks take no lock. Upgrade path: decode the stem into memory on load and play it lock-free.

StemPreview::StemPreview()
{
    formats.registerBasicFormats();
    readAhead.startThread();
}

StemPreview::~StemPreview()
{
    wanted = false;
    transport.setSource (nullptr);
    readAhead.stopThread (2000);
}

void StemPreview::prepare (double sampleRate, int maxBlockSize)
{
    prepared = false;
    scratch.setSize (2, juce::jmax (1, maxBlockSize));   // a 0-sample scratch would make addTo's chunk loop spin forever
    hostRate = sampleRate;
    transport.prepareToPlay (maxBlockSize, sampleRate);
    transport.prepareToPlay (maxBlockSize, sampleRate);   // again: sizes the resampler for the ratio the first call set
    prepared = true;
}

void StemPreview::release()
{
    prepared = false;
    transport.releaseResources();
}

bool StemPreview::load (const juce::File& f)
{
    unload();
    auto* reader = formats.createReaderFor (f);
    if (reader == nullptr) return false;
    const double srcRate = reader->sampleRate;
    lengthSec = (double) reader->lengthInSamples / srcRate;
    source = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (source.get(), 32768, &readAhead, srcRate, 2);
    transport.setPosition (0);
    transport.start();   // only sets flags under the lock; audio is gated by `wanted`
    file = f;
    return true;
}

void StemPreview::unload()
{
    wanted = false;
    transport.setSource (nullptr);
    source.reset();
    file = {};
    lengthSec = 0;
    readPos = 0;
    reachedEnd = false;   // an end not yet taken belonged to the previous stem
}

void StemPreview::play()
{
    if (source == nullptr) return;
    if (! transport.isPlaying())   // the transport stops itself only at end of file; a seek made after that is kept
    {
        if (transport.hasStreamFinished()) { transport.setPosition (0); readPos = 0; }
        transport.start();
    }
    wanted = true;
}

void StemPreview::pause()
{
    wanted = false;
}

double StemPreview::getPositionFraction() const
{
    return lengthSec > 0 ? juce::jlimit (0.0, 1.0, (double) readPos.load() / (lengthSec * hostRate)) : 0.0;
}

void StemPreview::setPositionFraction (double f)
{
    transport.setPosition (f * lengthSec);
    readPos = (juce::int64) (f * lengthSec * hostRate);
}

void StemPreview::addTo (juce::AudioBuffer<float>& buffer)
{
    const bool want = wanted.load (std::memory_order_relaxed);
    if ((! want && ! sounding) || ! prepared.load (std::memory_order_relaxed)) return;   // idle: nothing else runs
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    float blockPeak = 0.0f;
    for (int start = 0; start < total; start += scratch.getNumSamples())               // chunks if the host exceeds its block size
    {
        const int n = juce::jmin (scratch.getNumSamples(), total - start);
        juce::AudioSourceChannelInfo info (&scratch, 0, n);
        transport.getNextAudioBlock (info);
        if (! want) scratch.applyGainRamp (0, n, 1.0f, 0.0f);                          // the one faded block after pause()
        blockPeak = juce::jmax (blockPeak, scratch.getMagnitude (0, n));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, start, scratch, juce::jmin (ch, scratch.getNumChannels() - 1), 0, n);
        if (! want) break;
    }
    sounding = want && transport.isPlaying();
    if (want && ! transport.isPlaying() && wanted.exchange (false)) reachedEnd = true;    // played to the end (not a concurrent load/pause)
    readPos.store (transport.getNextReadPosition(), std::memory_order_relaxed);
    if (blockPeak > peak.load (std::memory_order_relaxed)) peak.store (blockPeak, std::memory_order_relaxed);   // benign race with takePeak
}
