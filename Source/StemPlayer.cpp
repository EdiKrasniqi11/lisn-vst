#include "StemPlayer.h"

// ponytail: while playing, the read-ahead thread can briefly wait on a disk read and the audio thread on the buffer's lock;
// idle blocks take no lock. Upgrade path: decode the stems into memory on load and play them lock-free.

namespace
{
    // Stereo from any reader: mono is copied to both sides; beyond the file's end the reader returns zeros.
    void readStereo (juce::AudioFormatReader& r, float* l, float* rgt, juce::int64 pos, int n)
    {
        float* dest[] = { l, rgt };
        r.read (dest, juce::jmin (2, (int) r.numChannels), pos, n);
        if (r.numChannels == 1)
            juce::FloatVectorOperations::copy (rgt, l, n);
    }
}

// Every stem read at the same position into channels 2i / 2i+1. The read-ahead buffer counts positions linearly, so this
// source reports linear positions and maps them into the song itself (songPosition), wrapping inside the loop.
class StemPlayer::Stack : public juce::PositionableAudioSource
{
public:
    Stack (const StemPlayer& p, juce::OwnedArray<juce::AudioFormatReader>& r) : owner (p)
    {
        readers.swapWith (r);
        for (auto* reader : readers)
            length = juce::jmax (length, reader->lengthInSamples);
    }

    juce::int64 songLength() const { return length; }
    int numStems() const { return readers.size(); }

    void prepareToPlay (int, double) override {}
    void releaseResources() override {}
    void setNextReadPosition (juce::int64 p) override { pos = p; }
    juce::int64 getNextReadPosition() const override { return pos; }
    // While the loop can wrap, the stream must not end: the read-ahead buffer and the transport stop at this length.
    juce::int64 getTotalLength() const override { return owner.wraps() ? std::numeric_limits<juce::int64>::max() / 4 : length; }
    bool isLooping() const override { return false; }   // true would make the buffer wrap at the song length itself

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();
        for (int done = 0; done < info.numSamples;)
        {
            const auto linear = pos.load();
            const auto song = owner.songPosition (linear);
            const auto limit = owner.wraps() ? owner.loopEnd.load() : length;   // read up to the wrap or the song end
            const int n = (int) juce::jmin ((juce::int64) (info.numSamples - done), juce::jmax ((juce::int64) 0, limit - song));
            if (n == 0)
                break;                                   // past the end of the song: the rest stays silent
            for (int i = 0; i < readers.size() && 2 * i + 1 < info.buffer->getNumChannels(); ++i)
                readStereo (*readers[i], info.buffer->getWritePointer (2 * i, info.startSample + done),
                            info.buffer->getWritePointer (2 * i + 1, info.startSample + done), song, n);
            pos = linear + n;
            done += n;
        }
    }

private:
    const StemPlayer& owner;
    juce::OwnedArray<juce::AudioFormatReader> readers;
    juce::int64 length = 0;
    std::atomic<juce::int64> pos { 0 };
};

StemPlayer::StemPlayer()
{
    formats.registerBasicFormats();
    readAhead.startThread();
}

StemPlayer::~StemPlayer()
{
    wanted = false;
    transport.setSource (nullptr);
    readAhead.stopThread (2000);
}

void StemPlayer::prepare (double sampleRate, int maxBlockSize)
{
    prepared = false;
    scratch.setSize (2 * maxStems, juce::jmax (1, maxBlockSize));   // a 0-sample scratch would make addTo's chunk loop spin
    mix.setSize (2, juce::jmax (1, maxBlockSize));
    hostRate = sampleRate;
    transport.prepareToPlay (maxBlockSize, sampleRate);
    transport.prepareToPlay (maxBlockSize, sampleRate);   // again: sizes the resampler for the ratio the first call set
    prepared = true;
}

void StemPlayer::release()
{
    prepared = false;
    transport.releaseResources();
}

bool StemPlayer::load (const juce::Array<juce::File>& stems)
{
    unload();
    juce::OwnedArray<juce::AudioFormatReader> readers;
    juce::Array<juce::File> loaded;
    for (const auto& f : stems)
    {
        if (readers.size() == maxStems)
            break;                                   // ponytail: stems past 8 are ignored; demucs makes 4 or 6
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || (! readers.isEmpty() && ! juce::exactlyEqual (r->sampleRate, readers[0]->sampleRate)))
            return false;
        readers.add (r.release());
        loaded.add (f);
    }
    if (readers.isEmpty())
        return false;

    sourceRate = readers[0]->sampleRate;
    const int count = readers.size();
    stack = std::make_unique<Stack> (*this, readers);
    lengthSamples = stack->songLength();
    lengthSec = (double) lengthSamples / sourceRate;
    transport.setSource (stack.get(), 32768, &readAhead, sourceRate, 2 * count);
    files = loaded;
    stemCount = count;
    seekSource (0);
    transport.start();   // only sets flags under the lock; audio is gated by `wanted`
    return true;
}

void StemPlayer::unload()
{
    wanted = false;
    stemCount = 0;
    transport.setSource (nullptr);
    stack.reset();
    files.clear();
    for (auto& m : muted) m = false;
    looping = false;
    loop = {};
    loopStart = loopEnd = origin = 0;
    lengthSamples = 0;
    lengthSec = 0;
    readPos = 0;
    reachedEnd = false;   // an end not yet taken belonged to the previous song
}

void StemPlayer::play()
{
    if (stack == nullptr) return;
    if (! transport.isPlaying())   // the transport stops itself only at the end; a seek made after that is kept
    {
        if (transport.hasStreamFinished()) seekSource (0);
        transport.start();
    }
    wanted = true;
}

void StemPlayer::pause()
{
    wanted = false;
}

juce::int64 StemPlayer::songPosition (juce::int64 linear) const
{
    const auto s = loopStart.load(), e = loopEnd.load();
    if (! looping.load() || s >= e || origin.load() >= e || linear < e)
        return linear;
    return s + (linear - e) % (e - s);
}

bool StemPlayer::wraps() const
{
    return looping.load() && loopStart.load() < loopEnd.load() && origin.load() < loopEnd.load();
}

juce::int64 StemPlayer::linearPosition() const
{
    return hostRate > 0 ? (juce::int64) ((double) readPos.load() * sourceRate / hostRate) : 0;
}

double StemPlayer::getPositionFraction() const
{
    return lengthSamples > 0 ? juce::jlimit (0.0, 1.0, (double) songPosition (linearPosition()) / (double) lengthSamples) : 0.0;
}

void StemPlayer::seekSource (juce::int64 sourceSample)
{
    origin = sourceSample;
    transport.setPosition ((double) sourceSample / sourceRate);   // also flushes the read-ahead
    readPos = (juce::int64) ((double) sourceSample * hostRate / sourceRate);
}

void StemPlayer::setPositionFraction (double f)
{
    seekSource ((juce::int64) (juce::jlimit (0.0, 1.0, f) * (double) lengthSamples));
}

void StemPlayer::setMuted (int stem, bool m)
{
    if (juce::isPositiveAndBelow (stem, maxStems))
        muted[(size_t) stem] = m;
}

bool StemPlayer::isMuted (int stem) const
{
    return juce::isPositiveAndBelow (stem, maxStems) && muted[(size_t) stem].load();
}

juce::uint32 StemPlayer::audibleMask() const
{
    juce::uint32 mask = 0;
    for (int i = 0; i < files.size(); ++i)
        if (! muted[(size_t) i].load())
            mask |= 1u << i;
    return mask;
}

void StemPlayer::solo (int stem)
{
    if (! juce::isPositiveAndBelow (stem, files.size())) return;
    const bool alone = audibleMask() == (1u << stem);
    for (int i = 0; i < files.size(); ++i)
        muted[(size_t) i] = ! alone && i != stem;
}

void StemPlayer::setLoop (double a, double b)
{
    loop = { juce::jlimit (0.0, 1.0, juce::jmin (a, b)), juce::jlimit (0.0, 1.0, juce::jmax (a, b)) };
    loopStart = (juce::int64) (loop.getStart() * (double) lengthSamples);
    loopEnd = (juce::int64) (loop.getEnd() * (double) lengthSamples);
    setLooping (true);
}

void StemPlayer::setLooping (bool on)
{
    const auto song = songPosition (linearPosition());
    looping = on && ! loop.isEmpty();
    // Turning a loop on jumps into it; every change re-seeks, which drops read-ahead audio from the old setting.
    seekSource (looping.load() && (song < loopStart.load() || song >= loopEnd.load()) ? loopStart.load() : song);
}

void StemPlayer::addTo (juce::AudioBuffer<float>& buffer)
{
    const bool want = wanted.load (std::memory_order_relaxed);
    if ((! want && ! sounding) || ! prepared.load (std::memory_order_relaxed)) return;   // idle: nothing else runs
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples(), stems = stemCount.load (std::memory_order_relaxed);
    float blockPeak = 0.0f;
    for (int start = 0; start < total; start += scratch.getNumSamples())               // chunks if the host exceeds its block size
    {
        const int n = juce::jmin (scratch.getNumSamples(), total - start);
        juce::AudioSourceChannelInfo info (&scratch, 0, n);
        transport.getNextAudioBlock (info);
        mix.clear (0, n);
        for (int i = 0; i < stems; ++i)
        {
            // A mute (or the one faded block after pause()) ramps across this block: heard at once, never a click.
            const float to = want && ! muted[(size_t) i].load (std::memory_order_relaxed) ? 1.0f : 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                mix.addFromWithRamp (ch, 0, scratch.getReadPointer (2 * i + ch), n, gains[(size_t) i], to);
            gains[(size_t) i] = to;
        }
        blockPeak = juce::jmax (blockPeak, mix.getMagnitude (0, n));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, start, mix, juce::jmin (ch, 1), 0, n);
        if (! want) break;
    }
    sounding = want && transport.isPlaying();
    if (want && ! transport.isPlaying() && wanted.exchange (false)) reachedEnd = true;    // played to the end
    readPos.store (transport.getNextReadPosition(), std::memory_order_relaxed);
    if (blockPeak > peak.load (std::memory_order_relaxed)) peak.store (blockPeak, std::memory_order_relaxed);   // benign race with takePeak
}
