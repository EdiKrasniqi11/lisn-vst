#include "StemPlayer.h"
#if JUCE_MSVC
 #pragma warning (push, 0)
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#if JUCE_MSVC
 #pragma warning (pop)
#endif

struct StemPlayer::Stretch { signalsmith::stretch::SignalsmithStretch<float> st; };

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

    // Every reader times its level, summed into sum's first n samples (stereo) from file position pos; one is scratch.
    void readMix (juce::OwnedArray<juce::AudioFormatReader>& readers, const std::vector<float>& levels,
                  juce::AudioBuffer<float>& sum, juce::AudioBuffer<float>& one, juce::int64 pos, int n)
    {
        sum.clear (0, n);
        for (int k = 0; k < readers.size(); ++k)
        {
            readStereo (*readers[k], one.getWritePointer (0), one.getWritePointer (1), pos, n);
            for (int ch = 0; ch < 2; ++ch)
                sum.addFrom (ch, 0, one, ch, 0, n, levels[(size_t) k]);
        }
    }

    constexpr int renderChunk = 65536;

    // The plain mix of [first, last) into w, a chunk at a time; onProgress after each (false cancels).
    bool writeMix (juce::AudioFormatWriter& w, juce::OwnedArray<juce::AudioFormatReader>& readers, const std::vector<float>& levels,
                   juce::int64 first, juce::int64 last, const std::function<bool (float)>& onProgress)
    {
        juce::AudioBuffer<float> sum (2, renderChunk), one (2, renderChunk);
        for (auto pos = first; pos < last; pos += renderChunk)
        {
            const int n = (int) juce::jmin ((juce::int64) renderChunk, last - pos);
            readMix (readers, levels, sum, one, pos, n);
            if (! w.writeFromAudioSampleBuffer (sum, 0, n)) return false;
            if (onProgress && ! onProgress ((float) (pos + n - first) / (float) (last - first))) return false;
        }
        return true;
    }

    // [first, last) mixed and time-stretched by speed (keeping the key) into w: always llround ((last - first) / speed) samples.
    // The stretcher's exact-length recipe (its exact(), a chunk at a time so progress and cancel work): outputSeek over
    // the first outputSeekLength samples, process the rest into all but the tail, then flush the tail. A range no longer
    // than that look-ahead is padded with silence to just past it and the output trimmed back to the exact length.
    bool writeStretched (juce::AudioFormatWriter& w, juce::OwnedArray<juce::AudioFormatReader>& readers, const std::vector<float>& levels,
                         juce::int64 first, juce::int64 last, double speed, int semitones, double rate, const std::function<bool (float)>& onProgress)
    {
        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (2, (float) rate);
        st.setTransposeSemitones ((float) semitones);
        const auto inLen = last - first, outLen = (juce::int64) std::llround ((double) inLen / speed);
        const int seekLen = st.outputSeekLength ((float) speed);
        const auto padded = juce::jmax (inLen, (juce::int64) seekLen + 1);
        const auto outPadded = (juce::int64) std::llround ((double) padded / speed);
        juce::AudioBuffer<float> sum (2, renderChunk), one (2, renderChunk), out (2, 2 * renderChunk + 2);
        // n samples of the clip from offset (relative to first) into sum; silence past last
        auto readClip = [&] (juce::int64 offset, int n)
        {
            const int real = (int) juce::jlimit ((juce::int64) 0, (juce::int64) n, inLen - offset);
            if (real > 0) readMix (readers, levels, sum, one, first + offset, real);
            sum.clear (real, n - real);
        };
        juce::int64 written = 0;
        auto emit = [&] (int m)   // the first m samples of out, never past outLen in all
        {
            const int k = (int) juce::jmin ((juce::int64) m, outLen - written);
            if (k > 0 && ! w.writeFromAudioSampleBuffer (out, 0, k)) return false;
            written += juce::jmax (0, k);
            return true;
        };
        readClip (0, seekLen);
        st.outputSeek (sum.getArrayOfReadPointers(), seekLen);
        const auto bodyIn = padded - seekLen;
        const auto bodyOut = outPadded - (juce::int64) (seekLen / speed);   // as exact(): the tail is what flush() returns
        juce::int64 inDone = 0, outDone = 0;
        while (inDone < bodyIn)
        {
            const int n = (int) juce::jmin ((juce::int64) renderChunk, bodyIn - inDone);
            readClip (seekLen + inDone, n);
            inDone += n;
            const auto target = (juce::int64) std::llround ((double) bodyOut * (double) inDone / (double) bodyIn);
            const int m = (int) (target - outDone);
            st.process (sum.getArrayOfReadPointers(), n, out.getArrayOfWritePointers(), m);
            if (! emit (m)) return false;
            outDone = target;
            if (onProgress && ! onProgress (0.99f * (float) (seekLen + inDone) / (float) padded)) return false;
        }
        const int tail = (int) (outPadded - outDone);
        st.flush (out.getArrayOfWritePointers(), tail, (float) speed);
        if (! emit (tail)) return false;
        return ! onProgress || onProgress (1.0f);
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

StemPlayer::StemPlayer() : stretch (std::make_unique<Stretch>())
{
    for (auto& v : volumes) v = 1.0f;
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
    const int block = juce::jmax (1, maxBlockSize), inMax = (int) std::ceil (block * 2.0) + 1;   // a stretched block reads up to 2x
    scratch.setSize (2 * maxStems, inMax);
    mix.setSize (2, block);
    mixIn.setSize (2, inMax);
    stretch->st.presetDefault (2, (float) sampleRate);
    {   // size the stretcher's working buffers now, so resets and blocks on the audio thread never allocate
        juce::AudioBuffer<float> zeros (2, stretch->st.seekLength());
        zeros.clear();
        stretch->st.seek (zeros.getArrayOfReadPointers(), zeros.getNumSamples(), 1.0);
        stretch->st.reset();
    }
    stretching = false;
    carry = 0.0;
    outGain = 1.0f;
    hostRate = sampleRate;
    // The transport is prepared for the largest read, a stretched block's 2x, so its resampler's ring never grows on the audio thread.
    transport.prepareToPlay (inMax, sampleRate);
    transport.prepareToPlay (inMax, sampleRate);          // again: sizes the resampler for the ratio the first call set
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
    for (auto& v : volumes) v = 1.0f;
    speed = 1.0;
    pitch = 0;
    looping = false;
    loop = {};
    loopStart = loopEnd = origin = base = 0;
    lengthSamples = 0;
    lengthSec = 0;
    readPos = 0;
    reachedEnd = false;   // an end not yet taken belonged to the previous song
    restretch = true;     // a new song never inherits the stretcher
}

void StemPlayer::play()
{
    if (stack == nullptr) return;
    if (! transport.isPlaying())   // the transport stops itself only at the end; a seek made after that is kept
    {
        if (transport.hasStreamFinished()) seekSource (0, looping.load());
        transport.start();
    }
    restretch = true;
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
    const auto linear = linearPosition();
    const bool moves = songSample != songPosition (linear);   // a loop edit re-bases in place: the stretcher keeps going
    const auto fresh = juce::jmax (base.load(), linear) + 2 * readAheadSamples;
    base = fresh;
    origin = songSample;
    looping = loopOn;
    transport.setPosition ((double) fresh / sourceRate);   // a no-op until prepare(), which repeats the seek
    readPos = (juce::int64) ((double) fresh * hostRate / sourceRate);
    if (moves) restretch = true;
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

void StemPlayer::setVolume (int stem, float level)
{
    if (juce::isPositiveAndBelow (stem, maxStems))
        volumes[(size_t) stem] = juce::jlimit (0.0f, 1.0f, level);
}

float StemPlayer::getVolume (int stem) const
{
    return juce::isPositiveAndBelow (stem, maxStems) ? volumes[(size_t) stem].load() : 1.0f;
}

std::vector<float> StemPlayer::mixGains() const
{
    std::vector<float> g ((size_t) files.size());
    for (size_t i = 0; i < g.size(); ++i)
        g[i] = muted[i].load() ? 0.0f : volumes[i].load();
    return g;
}

void StemPlayer::setSpeed (double s)
{
    speed = std::round (juce::jlimit (0.5, 2.0, s) * 10000.0) / 10000.0;   // 4 decimals (whole BPM steps land exactly), as renderName names it
}

void StemPlayer::setPitch (int semitones)
{
    pitch = juce::jlimit (-12, 12, semitones);
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
    const double rate = speed.load (std::memory_order_relaxed);
    const int semitones = pitch.load (std::memory_order_relaxed);
    if (restretch.exchange (false, std::memory_order_relaxed))
        stretching = false;                                                    // a play or seek: at 1x the stems play directly
    bool fadeDirect = false;                                                   // the first chunk after engaging mid-play
    if (! stretching && (! juce::exactlyEqual (rate, 1.0) || semitones != 0))
    {
        stretch->st.reset();                                                   // engages; its first ~60 ms fade in
        stretching = true;
        carry = 0.0;
        outGain = 1.0f;
        fadeDirect = sounding;
    }
    float blockPeak = 0.0f;
    for (int start = 0; start < total; start += mix.getNumSamples())          // chunks if the host exceeds its block size
    {
        const int n = juce::jmin (mix.getNumSamples(), total - start);
        int in = n;
        if (stretching)
        {
            const double owed = n * rate + carry;
            in = juce::jlimit (1, scratch.getNumSamples(), (int) owed);
            carry = owed - in;
        }
        juce::AudioSourceChannelInfo info (&scratch, 0, in);
        transport.getNextAudioBlock (info);
        auto& sum = stretching ? mixIn : mix;
        sum.clear (0, in);
        for (int i = 0; i < stems; ++i)
        {
            // A mute or volume change (or, playing directly, the one faded block after pause()) ramps across this block:
            // heard at once, never a click. Stretched, pause fades the output below instead.
            const float to = (want || stretching) && ! muted[(size_t) i].load (std::memory_order_relaxed)
                               ? volumes[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                sum.addFromWithRamp (ch, 0, scratch.getReadPointer (2 * i + ch), in, gains[(size_t) i], to);
            gains[(size_t) i] = to;
        }
        if (stretching)
        {
            // ponytail: what is heard trails the playhead by the stretcher's latency (~0.1 s). Upgrade path: report the
            // position minus that latency times the speed.
            stretch->st.setTransposeSemitones ((float) semitones);   // no allocation: it stores a factor
            stretch->st.process (mixIn.getArrayOfReadPointers(), in, mix.getArrayOfWritePointers(), n);
            if (fadeDirect)   // over the stretcher's silent pre-roll, the direct audio that would have played next fades out
            {
                for (int ch = 0; ch < 2; ++ch)
                    mix.addFromWithRamp (ch, 0, mixIn.getReadPointer (ch), juce::jmin (n, in), 1.0f, 0.0f);
                fadeDirect = false;
            }
            const float fade = want ? 1.0f : 0.0f;
            mix.applyGainRamp (0, n, outGain, fade);
            outGain = fade;
            if (! want) gains.fill (0.0f);   // a later resume at 1x ramps in from silence, like a direct pause
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

bool StemPlayer::render (const juce::Array<juce::File>& stems, const std::vector<float>& gains, double startSec, double endSec,
                         double speed, const juce::File& dest, const std::function<bool (float)>& onProgress, int semitones)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::OwnedArray<juce::AudioFormatReader> readers;
    std::vector<float> levels;
    for (int i = 0; i < stems.size() && i < (int) gains.size(); ++i)
        if (gains[(size_t) i] > 0.0f)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (stems[i]));
            if (r == nullptr) return false;
            readers.add (r.release());
            levels.push_back (gains[(size_t) i]);
        }
    if (readers.isEmpty()) return false;

    const double rate = readers[0]->sampleRate;
    const auto first = (juce::int64) (startSec * rate), last = (juce::int64) (endSec * rate);
    if (last <= first) return false;

    // Written next to dest, then renamed: a failed or cancelled render never leaves a file that FL could reference.
    dest.getParentDirectory().createDirectory();
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part");
    part.deleteFile();
    bool ok = false;
    {
        auto out = part.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (out != nullptr ? juce::WavAudioFormat().createWriterFor (out.get(), rate, 2, 24, {}, 0)
                                                                   : nullptr);
        if (w != nullptr)
        {
            out.release();   // the writer owns the stream now
            const double s = juce::jlimit (0.5, 2.0, speed);
            const int st = juce::jlimit (-12, 12, semitones);
            ok = juce::exactlyEqual (s, 1.0) && st == 0 ? writeMix (*w, readers, levels, first, last, onProgress)
                                                        : writeStretched (*w, readers, levels, first, last, s, st, rate, onProgress);
        }
    }
    if (ok && part.moveFileTo (dest)) return true;   // the move fails if dest is open in another app
    part.deleteFile();
    return false;
}

juce::String StemPlayer::renderName (const juce::String& songFile, const juce::StringArray& stemNames,
                                     const std::vector<float>& gains, double speed, juce::Range<double> loopSeconds, int semitones)
{
    juce::StringArray parts;
    for (int i = 0; i < stemNames.size() && i < (int) gains.size(); ++i)
    {
        const float g = gains[(size_t) i];
        if (g > 0.0f)
            parts.add (g < 1.0f ? stemNames[i] + " " + juce::String (juce::roundToInt (g * 100.0f)) : stemNames[i]);
    }
    if (parts.isEmpty()) return {};
    auto clock = [] (double sec)
    {
        const auto cs = juce::jmax (0, (int) (sec * 100));
        return juce::String (cs / 6000) + "." + juce::String (cs / 100 % 60).paddedLeft ('0', 2) + "." + juce::String (cs % 100).paddedLeft ('0', 2);
    };
    auto name = songFile.upToLastOccurrenceOf (".", false, false) + " - " + parts.joinIntoString ("+");
    if (! juce::exactlyEqual (speed, 1.0)) name << " x" << juce::String (speed, 4).trimCharactersAtEnd ("0");
    if (semitones != 0) name << " " << (semitones > 0 ? "+" : "") << semitones << "st";
    if (! loopSeconds.isEmpty()) name << " (" << clock (loopSeconds.getStart()) << "-" << clock (loopSeconds.getEnd()) << ")";
    return juce::File::createLegalFileName (name + ".wav");
}
