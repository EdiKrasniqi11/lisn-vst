"""LISN Harmony Split: a split's vocals.wav -> <outDir>/lead.wav + <outDir>/harmonies.wav, with a karaoke model.

Usage: python -I harmony_split.py <vocals.wav> <outDir>

The app (Source/HarmonyJob.cpp) writes this file next to the stems cache and runs it on LISN's engine Python. It goes
easy on the PC: below-normal priority and half the cores, so the DAW keeps what it needs. Progress is tqdm's "NN%|" on
stderr, which the app reads. Exit 3 with a "NEEDS SETUP" line when the packages or the model aren't installed.

ponytail: audio-separator (MIT) does the model work for this demo; it pulls in librosa & co and wants ffmpeg, which
this script sidesteps. Before a release: a small own MDX inference on onnxruntime + the engine's numpy.
"""
import ctypes
import os
import sys

MODEL = "UVR_MDXNET_KARA_2.onnx"   # karaoke: "Vocals" = the lead, "Instrumental" = everything else (the harmonies)


def needs_setup(why):
    print("NEEDS SETUP:", why, flush=True)
    sys.exit(3)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: harmony_split.py <vocals.wav> <outDir>")
    src, out = sys.argv[1], sys.argv[2]

    # Below normal priority (the pseudo-handle must go in as a pointer: a plain int -1 is truncated and the call fails).
    ctypes.windll.kernel32.SetPriorityClass(ctypes.c_void_p(-1), 0x4000)
    threads = max(1, (os.cpu_count() or 2) // 2)

    model_dir = os.path.join(sys.prefix, "models", "harmony")
    if not os.path.isfile(os.path.join(model_dir, MODEL)):
        needs_setup("no model in " + model_dir)
    try:
        import onnxruntime as ort
        import torch
        from audio_separator.separator import Separator
    except ImportError as e:
        needs_setup(e)

    torch.set_num_threads(threads)
    make_options = ort.SessionOptions

    def capped_options():
        o = make_options()
        o.intra_op_num_threads, o.inter_op_num_threads = threads, 1
        return o

    ort.SessionOptions = capped_options
    Separator.check_ffmpeg_installed = lambda self: None   # WAVs go through soundfile; ffmpeg is never used

    sep = Separator(output_dir=out, model_file_dir=model_dir, output_format="WAV", use_soundfile=True, log_level=30)
    sep.load_model(model_filename=MODEL)
    # Full paths: audio-separator's soundfile writer ignores output_dir.
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    sep.separate(src, custom_output_names={"Vocals": os.path.join(out, "lead"), "Instrumental": os.path.join(out, "harmonies")})
    print("100%|", flush=True)


if __name__ == "__main__":
    main()
