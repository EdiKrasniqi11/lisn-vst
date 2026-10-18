#!/bin/bash
# Builds LISN's private AI engine: uv-managed CPython 3.11 plus the locked Demucs stack, with the models prefetched.
# setup-engine.sh "/Library/Application Support/LISN/engine"   (uv, requirements-macos.txt and sitecustomize.py sit next to it)
# Exit 0: ready, and EngineDir/ready.txt matches requirements-macos.txt + sitecustomize.py. Anything else: failed, see EngineDir/setup.log.
[ $# -eq 1 ] || { echo "usage: setup-engine.sh <EngineDir>" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
engine=$1
uv=$here/uv
req=$here/requirements-macos.txt
site=$here/sitecustomize.py
venv=$engine/venv
py=$venv/bin/python3
stamp=$engine/ready.txt
umask 022   # normal users run the engine, and the installer runs this as root
want=$(shasum -a 256 "$req" "$site" | awk '{print $1}' | paste -sd- -)

mkdir -p "$engine" || exit 1
[ "$(cat "$stamp" 2>/dev/null)" = "$want" ] && exit 0   # this exact engine is already here

setup() {
    set -eu
    free=$(df -k "$engine" | awk 'NR==2 {print $4}')
    [ "$free" -ge $((3 * 1024 * 1024)) ] || { echo "Needs 3 GB free on the system drive, found $((free / 1024 / 1024)) GB."; exit 1; }

    export UV_PYTHON_INSTALL_DIR=$engine/python
    export UV_NO_CACHE=1
    rm -f "$stamp"
    rm -rf "$venv"

    "$uv" venv --managed-python --python 3.11 "$venv"
    "$uv" pip install --python "$py" --require-hashes --compile-bytecode -r "$req"
    cp "$site" "$venv/lib/python3.11/site-packages/sitecustomize.py"

    models="from demucs.pretrained import get_model; [get_model(m) for m in ('htdemucs', 'htdemucs_6s')]"
    HF_HUB_OFFLINE=0 "$py" -c "$models"   # the one step that goes online: download both models into the engine; after it, sitecustomize.py's offline default applies, as in a split

    # Harmony Split's quick model (Resources/harmony_split.py looks in venv/models/harmony), checked against its hash.
    # Best quality (913 MB) is a download in the app, the first time someone picks it.
    mkdir -p "$venv/models/harmony"
    kara=$venv/models/harmony/UVR_MDXNET_KARA_2.onnx
    curl -fsSL --retry 3 -o "$kara" https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR_MDXNET_KARA_2.onnx
    [ "$(shasum -a 256 "$kara" | awk '{print $1}')" = bf32e15105a09c0f7dddd2b67346146334d6f3ecb399ed7638eba2ab07cbf5f4 ] \
        || { echo "The Harmony Split model download is damaged."; exit 1; }

    # Loads torch too, so a broken wheel fails here; and Harmony Split's packages.
    "$py" -c "$models; import demucs.separate, onnxruntime, rotary_embedding_torch"

    chmod -R a+rX "$engine"   # nothing is written here at split time
    echo "$want" > "$stamp"
    chmod a+r "$stamp"
}

set -o pipefail
setup 2>&1 | tee "$engine/setup.log"
code=${PIPESTATUS[0]}
[ "$code" -eq 0 ] || echo "FAILED (exit $code), see $engine/setup.log" | tee -a "$engine/setup.log"
exit "$code"
