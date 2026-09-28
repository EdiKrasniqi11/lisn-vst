# Usage: python tests/sum_check.py <original> <stem_dir>
# The sum of the stems should reconstruct the original (residual well below the signal).
import sys, pathlib, torch, soundfile, subprocess, tempfile, os
import numpy as np

def load_audio(path):
    """Load audio, converting mp3 to wav via ffmpeg if needed."""
    path = str(path)
    if path.lower().endswith('.mp3'):
        # Use ffmpeg to convert mp3 to wav
        with tempfile.TemporaryDirectory() as tmpdir:
            wav_path = os.path.join(tmpdir, 'temp.wav')
            subprocess.run(['ffmpeg', '-i', path, '-q:a', '9', wav_path],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            audio_data, sr = soundfile.read(wav_path)
    else:
        audio_data, sr = soundfile.read(path)

    # Convert to torch tensor (channels, samples)
    if len(audio_data.shape) == 1:
        audio_data = audio_data[np.newaxis, :]
    else:
        audio_data = audio_data.T
    return torch.from_numpy(audio_data.astype(np.float32)), sr

orig, sr = load_audio(sys.argv[1])
stems = [load_audio(p)[0] for p in sorted(pathlib.Path(sys.argv[2]).glob("*.wav"))]
assert stems, "no stems found"
total = sum(stems)
if sr != 44100:
    # Use torch.functional.interpolate for resampling since torchaudio may not be available
    orig = torch.nn.functional.interpolate(orig.unsqueeze(0), size=int(orig.shape[-1] * 44100 / sr), mode='linear', align_corners=False).squeeze(0)
n = min(orig.shape[-1], total.shape[-1])
residual = (orig[..., :n] - total[..., :n]).pow(2).mean()
snr = 10 * torch.log10(orig[..., :n].pow(2).mean() / residual)
print(f"reconstruction SNR: {snr:.1f} dB")
assert snr > 20, "stems do not sum back to the original"
print("sum_check OK")
