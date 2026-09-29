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

    void prepareToPlay (int, double) override {}
    void releaseResources() override {}
    void setNextReadPosition (juce::int64 p) override { pos = p; }
    juce::int64 getNextReadPosition() const override { return pos; }
    // While the loop can wrap, the stream must not end: the read-ahead buffer and the transport stop at this length.
    // Otherwise it ends where the song ends, in linear positions (see songPosition).
    juce::int64 getTotalLength() const override
    {
        return owner.wraps() ? std::numeric_limits<juce::int64>::max() / 4
                             : owner.base.load() + (length - owner.origin.load());
    }
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
    const auto song = stack != nullptr ? songPosition (linearPosition()) : 0;   // before hostRate changes
    scratch.setSize (2 * maxStems, juce::jmax (1, maxBlockSize));   // a 0-sample scratch would make addTo's chunk loop spin
    mix.setSize (2, juce::jmax (1, maxBlockSize));
    hostRate = sampleRate;
    transport.prepareToPlay (maxBlockSize, sampleRate);
    transport.prepareToPlay (maxBlockSize, sampleRate);   // again: sizes the resampler for the ratio the first call set
    if (stack != nullptr)
        seekSource (song, looping.load());   // a seek made before this was a no-op (no sample rate yet); also re-bases readPos
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
        if (r == nullptr || r->lengthInSamples <= 0 || (! readers.isEmpty() && ! juce::exactlyEqual (r->sampleRate, readers[0]->sampleRate)))
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
    transport.setSource (stack.get(), readAheadSamples, &readAhead, sourceRate, 2 * count);
    files = loaded;
    stemCount = count;
    seekSource (0, false);
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
    loopStart = loopEnd = origin = base = 0;
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
        if (transport.hasStreamFinished()) seekSource (0, looping.load());
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
    const auto song = origin.load() + (linear - base.load());   // unwrapped: the last seek target plus what played since
    const auto s = loopStart.load(), e = loopEnd.load();
    if (! looping.load() || s >= e || origin.load() >= e || song < e)
        return song;
    return s + (song - e) % (e - s);
}

bool StemPlayer::wraps() const
{
    return looping.load() && loopStart.load() < loopEnd.load() && origin.load() < loopEnd.load();
}

juce::int64 StemPlayer::linearPosition() const
{
    return hostRate > 0 ? juce::jmax (base.load(), (juce::int64) ((double) readPos.load() * sourceRate / hostRate)) : 0;
}

double StemPlayer::getPositionFraction() const
{
    return lengthSamples > 0 ? juce::jlimit (0.0, 1.0, (double) songPosition (linearPosition()) / (double) lengthSamples) : 0.0;
}

void StemPlayer::seekSource (juce::int64 songSample, bool loopOn)
{
    // BufferingAudioSource keeps its buffered audio when a seek lands inside it, and that audio was mapped with the old loop
    // setting. So every seek jumps to a fresh linear range past anything it can have buffered; songPosition() maps it back.
    // base and origin are published before `looping`, so getTotalLength() never falls behind the playing position.
    const auto fresh = juce::jmax (base.load(), linearPosition()) + 2 * readAheadSamples;
    base = fresh;
    origin = songSample;
    looping = loopOn;
    transport.setPosition ((double) fresh / sourceRate);   // a no-op until prepare(), which repeats the seek
    readPos = (juce::int64) ((double) fresh * hostRate / sourceRate);
}

void StemPlayer::setPositionFraction (double f)
{
    seekSource ((juce::int64) (juce::jlimit (0.0, 1.0, f) * (double) lengthSamples), looping.load());
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
    // Re-base first: the bounds change below, and the transport must never see a finished stream meanwhile.
    seekSource (songPosition (linearPosition()), looping.load());
    loop = { juce::jlimit (0.0, 1.0, juce::jmin (a, b)), juce::jlimit (0.0, 1.0, juce::jmax (a, b)) };
    loopStart = (juce::int64) (loop.getStart() * (double) lengthSamples);
    loopEnd = (juce::int64) (loop.getEnd() * (double) lengthSamples);
    setLooping (true);
}

void StemPlayer::setLooping (bool on)
{
    // ponytail: every loop change re-seeks, so the read-ahead refills and the resampler restarts: a small click is possible.
    // Upgrade path: crossfade across the seek.
    const auto song = songPosition (linearPosition());
    const bool loopOn = on && ! loop.isEmpty();
    seekSource (loopOn && (song < loopStart.load() || song >= loopEnd.load()) ? loopStart.load() : song, loopOn);   // turning a loop on jumps into it
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

bool StemPlayer::render (const juce::Array<juce::File>& stems, juce::uint32 audibleMask, double startSec, double endSec,
                         const juce::File& dest)
{
    if (dest.existsAsFile()) return true;            // renders are cached by name (FL keeps pointing at them)
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::OwnedArray<juce::AudioFormatReader> readers;
    for (int i = 0; i < stems.size() && i < 32; ++i)
        if ((audibleMask >> i) & 1u)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (stems[i]));
            if (r == nullptr) return false;
            readers.add (r.release());
        }
    if (readers.isEmpty()) return false;

    const double rate = readers[0]->sampleRate;
    const auto first = (juce::int64) (startSec * rate), last = (juce::int64) (endSec * rate);
    if (last <= first) return false;

    // Written next to dest, then renamed: a failed render never leaves a half file that FL could reference.
    dest.getParentDirectory().createDirectory();
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    part.deleteFile();
    {
        auto out = part.createOutputStream();
        if (out == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (out.get(), rate, 2, 24, {}, 0));
        if (w == nullptr) return false;
        out.release();   // the writer owns the stream now
        constexpr int chunk = 65536;
        juce::AudioBuffer<float> sum (2, chunk), one (2, chunk);
        for (auto pos = first; pos < last; pos += chunk)
        {
            const int n = (int) juce::jmin ((juce::int64) chunk, last - pos);
            sum.clear();
            for (auto* r : readers)
            {
                readStereo (*r, one.getWritePointer (0), one.getWritePointer (1), pos, n);
                for (int ch = 0; ch < 2; ++ch)
                    sum.addFrom (ch, 0, one, ch, 0, n);
            }
            if (! w->writeFromAudioSampleBuffer (sum, 0, n)) return false;
        }
    }
    return part.moveFileTo (dest);
}
