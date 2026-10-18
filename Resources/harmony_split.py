"""LISN Harmony Split: a split's vocals.wav -> <outDir>/lead.wav + <outDir>/harmonies.wav, with a karaoke model.

Usage: python -I harmony_split.py <vocals.wav> <outDir> [quick|best]

The app (Source/HarmonyJob.cpp) writes this file next to the stems cache and runs it on LISN's engine Python. It goes
easy on the PC: below-normal priority and half the cores, so the DAW keeps what it needs. Progress is tqdm's "NN%|" on
stderr, which the app reads. Exit 3 with a "NEEDS SETUP" line when the packages or the model aren't installed.

ponytail: audio-separator (MIT) does the model work for this demo; it pulls in librosa & co and wants ffmpeg, which
this script sidesteps. Before a release: a small own MDX inference on onnxruntime + the engine's numpy.
"""
import ctypes
import os
import sys

# Karaoke models: "Vocals" = the lead, "Instrumental" = everything else (the harmonies). Best is about 3x the time.
MODELS = {
    "quick": "UVR_MDXNET_KARA_2.onnx",                                      # MDX-Net, 53 MB
    "best": "mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt",     # Mel-Band RoFormer, 913 MB
}


def needs_setup(why):
    print("NEEDS SETUP:", why, flush=True)
    sys.exit(3)


def main():
    if len(sys.argv) not in (3, 4) or sys.argv[3:] not in ([], ["quick"], ["best"]):
        sys.exit("usage: harmony_split.py <vocals.wav> <outDir> [quick|best]")
    src, out = sys.argv[1], sys.argv[2]
    quality = (sys.argv[3:] or ["quick"])[0]
    model = MODELS[quality]

    # Below normal priority (the pseudo-handle must go in as a pointer: a plain int -1 is truncated and the call fails).
    ctypes.windll.kernel32.SetPriorityClass(ctypes.c_void_p(-1), 0x4000)
    threads = max(1, (os.cpu_count() or 2) // 2)

    model_dir = os.path.join(sys.prefix, "models", "harmony")
    if not os.path.isfile(os.path.join(model_dir, model)):
        needs_setup("no model in " + model_dir)
    try:
        import onnxruntime as ort
        import soundfile as sf
        import torch
        from audio_separator.separator import Separator
        from audio_separator.separator.common_separator import CommonSeparator
    except ImportError as e:
        needs_setup(e)

    torch.set_num_threads(threads)
    make_options = ort.SessionOptions

    def capped_options():
        o = make_options()
        o.intra_op_num_threads, o.inter_op_num_threads = threads, 1
        return o

    ort.SessionOptions = capped_options
    # Our own WAV writer: audio-separator's soundfile path casts the float audio to int16 (silence) and writes the
    # interleaved channels as one mono track twice as long, and its other path needs ffmpeg. Stereo 16-bit, like demucs.
    Separator.check_ffmpeg_installed = lambda self: None
    CommonSeparator.write_audio = lambda self, path, source: sf.write(path, source, self.sample_rate, subtype="PCM_16")

    # normalization_threshold 1: keep the vocal's level (lead + harmonies = vocals) unless it would clip.
    sep = Separator(output_dir=out, model_file_dir=model_dir, output_format="WAV", normalization_threshold=1.0, log_level=30)
    sep.load_model(model_filename=model)
    # Full paths: our writer gets the stem's path as audio-separator builds it, without output_dir.
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    sep.separate(src, custom_output_names={"Vocals": os.path.join(out, "lead"), "Instrumental": os.path.join(out, "harmonies")})

    # Both parts must match the vocal's length and channels (a broken writer once made them mono, twice as long, silent).
    want = sf.info(src)
    for name in ("lead", "harmonies"):
        got = sf.info(os.path.join(out, name + ".wav"))
        if (got.frames, got.channels, got.samplerate) != (want.frames, want.channels, want.samplerate):
            sys.exit(f"{name}.wav came out wrong: {got.frames} frames x {got.channels} ch, the vocal is {want.frames} x {want.channels}")
    with open(os.path.join(out, "quality.txt"), "w") as f:   # the app ticks this quality in its menu
        f.write(quality)
    print("100%|", flush=True)


if __name__ == "__main__":
    main()
