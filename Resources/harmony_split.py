"""LISN Harmony Split: a split's vocals.wav -> <outDir>/lead.wav + <outDir>/harmonies.wav, with a karaoke model.

Usage: python -I harmony_split.py <vocals.wav> <outDir> [quick|best]

The app (Source/HarmonyJob.cpp) writes this file next to the stems cache and runs it on LISN's engine Python. It goes
easy on the PC: below-normal priority and half the cores, so the DAW keeps what it needs. Progress is "NN%|" lines,
which the app reads. Exit 3 with a "NEEDS SETUP" line when a package or the model isn't there.

Needs only what the engine has (torch, numpy, sphn, einops) plus onnxruntime (quick) and rotary-embedding-torch (best).
The two models, karaoke models from Ultimate Vocal Remover's public set:
  quick: UVR_MDXNET_KARA_2.onnx, MDX-Net (53 MB), installed with the engine in <engine>/venv/models/harmony
  best:  the aufr33 & viperx Mel-Band RoFormer karaoke model (913 MB), downloaded by the app on first use into
         %LOCALAPPDATA%/LISN/models/harmony (~/Library/Application Support/LISN/models/harmony on Mac)
The inference follows python-audio-separator (MIT, Andrew Beveridge) and its RoFormer code from lucidrains'
BS-RoFormer (MIT, Phil Wang), cut down to what these two models need.
"""
import ctypes
import math
import os
import sys
import wave

MODELS = {
    "quick": "UVR_MDXNET_KARA_2.onnx",
    "best": "mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt",
}
RATE = 44100


def needs_setup(why):
    print("NEEDS SETUP:", why, flush=True)
    sys.exit(3)


def progress(fraction):
    print(f"{int(fraction * 100)}%|", flush=True)


def write_wav(path, audio):
    """(2, n) float32 -> 16-bit stereo PCM, like demucs' stems."""
    import numpy as np
    pcm = (np.clip(audio.T, -1.0, 1.0) * 32767.0).round().astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(audio.shape[0])
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


# --- quick: MDX-Net Karaoke 2 (ONNX) -----------------------------------------------------------------------------------
# Its model data (UVR's mdx_model_data.json): n_fft 5120, dim_f 2048, dim_t 2^8, compensate 1.065. It outputs the
# Instrumental (everything but the lead); the lead is the input minus that.

def quick(mix, model_path, threads):
    import numpy as np
    import onnxruntime as ort
    import torch

    n_fft, hop, dim_f, dim_t, compensate, overlap = 5120, 1024, 2048, 256, 1.065, 0.25
    trim = n_fft // 2
    chunk = hop * (dim_t - 1)
    gen = chunk - 2 * trim
    window = torch.hann_window(n_fft, periodic=True)

    options = ort.SessionOptions()
    options.intra_op_num_threads, options.inter_op_num_threads = threads, 1
    options.log_severity_level = 3
    session = ort.InferenceSession(model_path, sess_options=options, providers=["CPUExecutionProvider"])

    def stft(x):                                   # (1, 2, chunk) -> (1, 4, dim_f, frames): re and im per channel
        spec = torch.stft(x.reshape(-1, x.shape[-1]), n_fft=n_fft, hop_length=hop, window=window, center=True, return_complex=False)
        spec = spec.permute(0, 3, 1, 2).reshape(1, 4, -1, spec.shape[2])
        return spec[:, :, :dim_f, :]

    def istft(spec):                               # the inverse, the cut bins back as zeros
        spec = torch.cat([spec, torch.zeros(1, 4, n_fft // 2 + 1 - dim_f, spec.shape[-1])], 2)
        spec = spec.reshape(2, 2, n_fft // 2 + 1, -1).permute(0, 2, 3, 1)
        return torch.istft(spec[..., 0] + spec[..., 1] * 1j, n_fft=n_fft, hop_length=hop, window=window, center=True).reshape(1, 2, -1)

    pad = gen + trim - mix.shape[-1] % gen
    mixture = np.concatenate([np.zeros((2, trim), "float32"), mix, np.zeros((2, pad), "float32")], 1)
    step = int((1 - overlap) * chunk)
    result = np.zeros((1, 2, mixture.shape[-1]), "float32")
    divider = np.zeros_like(result)
    starts = range(0, mixture.shape[-1], step)
    with torch.no_grad():
        for k, start in enumerate(starts):
            end = min(start + chunk, mixture.shape[-1])
            part = mixture[:, start:end]
            if end - start < chunk:
                part = np.concatenate([part, np.zeros((2, chunk - (end - start)), "float32")], 1)
            spec = stft(torch.from_numpy(np.ascontiguousarray(part))[None])
            spec[:, :, :3, :] *= 0
            out = istft(torch.from_numpy(session.run(None, {"input": spec.numpy()})[0])).numpy()
            win = np.hanning(end - start)[None, None, :]
            result[..., start:end] += out[..., : end - start] * win
            divider[..., start:end] += win
            progress((k + 1) / len(starts) * 0.99)
    harmonies = (result / np.where(divider > 0, divider, 1))[0][:, trim:-trim][:, : mix.shape[-1]] * compensate
    return mix - harmonies, harmonies


# --- best: Mel-Band RoFormer karaoke (torch) -----------------------------------------------------------------------------

def mel_bands(n_mels, n_fft):
    """Which STFT bins each mel band covers: librosa.filters.mel (Slaney scale) > 0, without librosa."""
    import numpy as np
    f_sp, min_log_hz = 200.0 / 3, 1000.0
    min_log_mel, logstep = min_log_hz / f_sp, math.log(6.4) / 27.0

    def hz_to_mel(f):
        return min_log_mel + math.log(f / min_log_hz) / logstep if f >= min_log_hz else f / f_sp

    mels = np.linspace(hz_to_mel(0.0), hz_to_mel(RATE / 2), n_mels + 2)
    mel_f = np.where(mels >= min_log_mel, min_log_hz * np.exp(logstep * (mels - min_log_mel)), f_sp * mels)
    fft_f = np.fft.rfftfreq(n_fft, 1.0 / RATE)
    ramps = np.subtract.outer(mel_f, fft_f)
    fdiff = np.diff(mel_f)
    weights = np.zeros((n_mels, len(fft_f)), "float32")
    for i in range(n_mels):
        weights[i] = np.maximum(0, np.minimum(-ramps[i] / fdiff[i], ramps[i + 2] / fdiff[i + 1]))
    weights *= (2.0 / (mel_f[2 : n_mels + 2] - mel_f[:n_mels]))[:, None]
    weights[0, 0] = weights[-1, -1] = 1.0
    return weights > 0


def roformer(n_fft, hop, n_mels, dim, depth, heads, dim_head, mask_depth):
    """The aufr33 & viperx karaoke model's MelBandRoformer: stereo, one stem, one time and one freq layer per depth."""
    import torch
    import torch.nn.functional as F
    from einops import pack, rearrange, repeat, unpack
    from rotary_embedding_torch import RotaryEmbedding
    from torch import nn

    class RMSNorm(nn.Module):
        def __init__(self, d):
            super().__init__()
            self.scale, self.gamma = d ** 0.5, nn.Parameter(torch.ones(d))

        def forward(self, x):
            return F.normalize(x, dim=-1) * self.scale * self.gamma

    class Attention(nn.Module):
        def __init__(self, rotary):
            super().__init__()
            inner = heads * dim_head
            self.rotary_embed = rotary
            self.norm = RMSNorm(dim)
            self.to_qkv = nn.Linear(dim, inner * 3, bias=False)
            self.to_gates = nn.Linear(dim, heads)
            self.to_out = nn.Sequential(nn.Linear(inner, dim, bias=False), nn.Dropout(0.0))

        def forward(self, x):
            x = self.norm(x)
            q, k, v = rearrange(self.to_qkv(x), "b n (qkv h d) -> qkv b h n d", qkv=3, h=heads)
            q, k = self.rotary_embed.rotate_queries_or_keys(q), self.rotary_embed.rotate_queries_or_keys(k)
            out = F.scaled_dot_product_attention(q, k, v)
            out = out * rearrange(self.to_gates(x), "b n h -> b h n 1").sigmoid()
            return self.to_out(rearrange(out, "b h n d -> b n (h d)"))

    class Transformer(nn.Module):
        def __init__(self, rotary):
            super().__init__()
            ff = nn.Sequential(RMSNorm(dim), nn.Linear(dim, dim * 4), nn.GELU(), nn.Dropout(0.0), nn.Linear(dim * 4, dim), nn.Dropout(0.0))
            self.layers = nn.ModuleList([nn.ModuleList([Attention(rotary), nn.Module()])])
            self.layers[0][1].net = ff             # the checkpoint's names: layers.0.1.net.*
            self.norm = RMSNorm(dim)

        def forward(self, x):
            for attn, ff in self.layers:
                x = attn(x) + x
                x = ff.net(x) + x
            return self.norm(x)

    def mlp(d_in, d_out, d_hidden):
        dims = (d_in, *((d_hidden,) * mask_depth), d_out)
        net = []
        for i, (a, b) in enumerate(zip(dims[:-1], dims[1:])):
            net.append(nn.Linear(a, b))
            if i < len(dims) - 2:
                net.append(nn.Tanh())
        return nn.Sequential(*net)

    class BandSplit(nn.Module):
        def __init__(self, dims_in):
            super().__init__()
            self.dims_in = dims_in
            self.to_features = nn.ModuleList([nn.Sequential(RMSNorm(d), nn.Linear(d, dim)) for d in dims_in])

        def forward(self, x):
            return torch.stack([f(p) for p, f in zip(x.split(self.dims_in, dim=-1), self.to_features)], dim=-2)

    class MaskEstimator(nn.Module):
        def __init__(self, dims_in):
            super().__init__()
            self.to_freqs = nn.ModuleList([nn.Sequential(mlp(dim, d * 2, dim * 4), nn.GLU(dim=-1)) for d in dims_in])

        def forward(self, x):
            return torch.cat([f(b) for b, f in zip(x.unbind(dim=-2), self.to_freqs)], dim=-1)

    class MelBandRoformer(nn.Module):
        def __init__(self):
            super().__init__()
            time_rotary, freq_rotary = RotaryEmbedding(dim=dim_head), RotaryEmbedding(dim=dim_head)
            self.layers = nn.ModuleList([nn.ModuleList([Transformer(time_rotary), Transformer(freq_rotary)]) for _ in range(depth)])
            bands = torch.from_numpy(mel_bands(n_mels, n_fft))
            indices = repeat(torch.arange(n_fft // 2 + 1), "f -> b f", b=n_mels)[bands]
            indices = rearrange(repeat(indices, "f -> f s", s=2) * 2 + torch.arange(2), "f s -> (f s)")
            self.register_buffer("freq_indices", indices, persistent=False)
            self.register_buffer("num_bands_per_freq", bands.sum(0), persistent=False)
            dims_in = tuple(2 * f * 2 for f in bands.sum(1).tolist())
            self.band_split = BandSplit(dims_in)
            self.mask_estimators = nn.ModuleList([MaskEstimator(dims_in)])
            self.window = torch.hann_window(n_fft)

        def forward(self, audio):                  # (1, 2, t) -> (1, 2, t): the lead
            stft_kw = dict(n_fft=n_fft, hop_length=hop, win_length=n_fft, window=self.window)
            spec = torch.view_as_real(torch.stft(audio.reshape(2, -1), **stft_kw, return_complex=True))
            spec = rearrange(spec[None], "b s f t c -> b (f s) t c")
            x = rearrange(spec[torch.arange(1)[..., None], self.freq_indices], "b f t c -> b t (f c)")
            x = self.band_split(x)
            for time_t, freq_t in self.layers:
                x = rearrange(x, "b t f d -> b f t d")
                x, ps = pack([x], "* t d")
                (x,) = unpack(time_t(x), ps, "* t d")
                x = rearrange(x, "b f t d -> b t f d")
                x, ps = pack([x], "* f d")
                (x,) = unpack(freq_t(x), ps, "* f d")
            masks = rearrange(self.mask_estimators[0](x)[:, None], "b n t (f c) -> b n f t c", c=2)
            spec = torch.view_as_complex(rearrange(spec, "b f t c -> b 1 f t c").contiguous())
            masks = torch.view_as_complex(masks.contiguous()).type(spec.dtype)
            index = repeat(self.freq_indices, "f -> b n f t", b=1, n=1, t=spec.shape[-1])
            summed = torch.zeros_like(spec).scatter_add_(2, index, masks)
            spec = spec * summed / repeat(self.num_bands_per_freq, "f -> (f r) 1", r=2).clamp(min=1e-8)
            spec = rearrange(spec, "b n (f s) t -> (b n s) f t", s=2)
            return torch.istft(spec, **stft_kw).reshape(1, 2, -1)

    return MelBandRoformer()


def best(mix, model_path):
    import numpy as np
    import torch

    # The model's config (mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956_config.yaml).
    model = roformer(n_fft=2048, hop=441, n_mels=60, dim=384, depth=6, heads=8, dim_head=64, mask_depth=2)
    model.load_state_dict(torch.load(model_path, map_location="cpu", weights_only=True))
    model.eval()

    chunk = 441 * (801 - 1)                        # 8 s chunks, back to back: audio-separator's default for RoFormers
    audio = torch.from_numpy(mix)
    window = torch.from_numpy(np.hamming(chunk).astype("float32"))
    result, counter = torch.zeros_like(audio), torch.zeros_like(audio)
    starts = range(0, audio.shape[1], chunk)
    with torch.no_grad():
        for k, i in enumerate(starts):
            at = i if i + chunk <= audio.shape[1] else max(0, audio.shape[1] - chunk)   # the last chunk ends at the end
            part = audio[:, at : at + chunk]
            n = part.shape[-1]
            if n < chunk:
                part = torch.nn.functional.pad(part, (0, chunk - n))
            out = model(part[None])[0][:, :n]
            result[:, at : at + n] += out * window[:n]
            counter[:, at : at + n] += window[:n]
            progress((k + 1) / len(starts) * 0.99)
    lead = (result / counter.clamp(min=1e-10)).numpy()
    return lead, mix - lead


def main():
    if len(sys.argv) not in (3, 4) or sys.argv[3:] not in ([], ["quick"], ["best"]):
        sys.exit("usage: harmony_split.py <vocals.wav> <outDir> [quick|best]")
    src, out = sys.argv[1], os.path.abspath(sys.argv[2])
    quality = (sys.argv[3:] or ["quick"])[0]

    # Below normal priority (the pseudo-handle goes in as a pointer: a plain int -1 is truncated and the call fails).
    if sys.platform == "win32":
        ctypes.windll.kernel32.SetPriorityClass(ctypes.c_void_p(-1), 0x4000)
    else:
        os.nice(10)
    threads = max(1, (os.cpu_count() or 2) // 2)  # half the threads: on 8 cores x 2, 16 was slower than 8 anyway

    user_dir = (os.path.expanduser("~/Library/Application Support/LISN/models/harmony") if sys.platform == "darwin"
                else os.path.join(os.environ.get("LOCALAPPDATA", ""), "LISN", "models", "harmony"))
    dirs = [os.path.join(sys.prefix, "models", "harmony"), user_dir]
    model_path = next((p for p in (os.path.join(d, MODELS[quality]) for d in dirs) if os.path.isfile(p)), None)
    if model_path is None:
        needs_setup(f"no {MODELS[quality]} in {' or '.join(dirs)}")
    try:
        import numpy as np
        import sphn
        import torch
        __import__("onnxruntime" if quality == "quick" else "rotary_embedding_torch")
    except ImportError as e:
        needs_setup(e)
    torch.set_num_threads(threads)

    mix, rate = sphn.read(src)
    if rate != RATE or mix.shape[0] != 2:
        sys.exit(f"{src}: expected 44.1 kHz stereo, got {rate} Hz x {mix.shape[0]}")
    mix = np.ascontiguousarray(mix, dtype="float32")
    lead, harmonies = quick(mix, model_path, threads) if quality == "quick" else best(mix, model_path)

    os.makedirs(out, exist_ok=True)
    write_wav(os.path.join(out, "lead.wav"), lead)
    write_wav(os.path.join(out, "harmonies.wav"), harmonies)
    # Both as long as the vocal, stereo (a broken writer once made them mono, twice as long, silent).
    for name in ("lead", "harmonies"):
        with wave.open(os.path.join(out, name + ".wav")) as w:
            if (w.getnframes(), w.getnchannels()) != (mix.shape[1], 2):
                sys.exit(f"{name}.wav came out wrong: {w.getnframes()} frames x {w.getnchannels()} ch, the vocal is {mix.shape[1]} x 2")
    with open(os.path.join(out, "quality.txt"), "w") as f:   # the app ticks this quality in its menu
        f.write(quality)
    progress(1.0)


if __name__ == "__main__":
    main()
