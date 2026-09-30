# setup-engine.ps1 copies this into the LISN engine's venv (Lib\site-packages); Python runs it at every start.
# The models live inside the engine, so every Windows user shares them and uninstall removes them.
import os
import sys

os.environ["HF_HOME"] = os.path.join(sys.prefix, "models", "hf")        # demucs 4.1 loads htdemucs from Hugging Face
os.environ["TORCH_HOME"] = os.path.join(sys.prefix, "models", "torch")  # and falls back to torch hub

# Splits use the models setup-engine.ps1 downloaded and never go online: they work offline, and an update
# upstream can't make a normal user rewrite files the admin installer created (that would fail the split).
os.environ.setdefault("HF_HUB_OFFLINE", "1")
