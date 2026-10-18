# Builds LISN's private AI engine: uv-managed CPython 3.11 plus the locked Demucs stack, with the models prefetched.
# setup-engine.ps1 -EngineDir C:\ProgramData\LISN\engine   (uv.exe, requirements.txt and sitecustomize.py sit next to it)
# Exit 0: ready, and EngineDir\ready.txt matches this requirements.txt + sitecustomize.py. Anything else: failed, see EngineDir\setup.log.
param([Parameter(Mandatory = $true)][string]$EngineDir)
$ErrorActionPreference = 'Stop'

$uv    = Join-Path $PSScriptRoot 'uv.exe'
$req   = Join-Path $PSScriptRoot 'requirements.txt'
$site  = Join-Path $PSScriptRoot 'sitecustomize.py'
$venv  = Join-Path $EngineDir 'venv'
$py    = Join-Path $venv 'Scripts\python.exe'
$stamp = Join-Path $EngineDir 'ready.txt'
$want  = (Get-FileHash $req, $site -Algorithm SHA256 | ForEach-Object Hash) -join '-'

New-Item -ItemType Directory -Force $EngineDir | Out-Null
if ((Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $want)) { exit 0 }   # this exact engine is already here

function Invoke-Native([string]$exe, [string[]]$argv) {
    $ErrorActionPreference = 'Continue'            # PowerShell 5.1 turns native stderr into errors; the exit code decides
    & $exe @argv 2>&1 | ForEach-Object { "$_" }   # through the pipeline so the transcript records it
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path $exe -Leaf) $($argv[0]) exited with $LASTEXITCODE" }
}

$code = 1
Start-Transcript -Path (Join-Path $EngineDir 'setup.log') -Force | Out-Null
try {
    $free = (Get-Item $EngineDir).PSDrive.Free
    if ($free -lt 3GB) { throw ('Needs 3 GB free on the system drive, found {0:N1} GB.' -f ($free / 1GB)) }

    $env:UV_PYTHON_INSTALL_DIR = Join-Path $EngineDir 'python'
    $env:UV_NO_CACHE = '1'
    Remove-Item $stamp -ErrorAction SilentlyContinue
    if (Test-Path $venv) { Remove-Item -Recurse -Force $venv }

    Invoke-Native $uv @('venv', '--managed-python', '--python', '3.11', $venv)
    Invoke-Native $uv @('pip', 'install', '--python', $py, '--require-hashes', '--compile-bytecode', '-r', $req)
    Copy-Item $site (Join-Path $venv 'Lib\site-packages\sitecustomize.py')

    $models = "from demucs.pretrained import get_model; [get_model(m) for m in ('htdemucs', 'htdemucs_6s')]"
    $env:HF_HUB_OFFLINE = '0'                      # the one step that goes online: download both models into the engine
    Invoke-Native $py @('-c', $models)
    Remove-Item Env:HF_HUB_OFFLINE                 # from here on, sitecustomize.py's offline default applies, as in a split

    # Harmony Split's quick model (Resources/harmony_split.py looks in venv\models\harmony), checked against its hash.
    # Best quality (913 MB) is a download in the app, the first time someone picks it.
    $harmony = Join-Path $venv 'models\harmony'
    New-Item -ItemType Directory -Force $harmony | Out-Null
    $kara = Join-Path $harmony 'UVR_MDXNET_KARA_2.onnx'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'       # PowerShell 5.1's progress bar slows the download to a crawl
    Invoke-WebRequest -UseBasicParsing -OutFile $kara `
        -Uri 'https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR_MDXNET_KARA_2.onnx'
    if ((Get-FileHash $kara -Algorithm SHA256).Hash -ne 'BF32E15105A09C0F7DDDD2B67346146334D6F3ECB399ED7638EBA2AB07CBF5F4') {
        throw 'The Harmony Split model download is damaged.'
    }

    # Loads torch's DLLs too, so a missing VC++ runtime fails here; and Harmony Split's packages.
    Invoke-Native $py @('-c', "$models; import demucs.separate, onnxruntime, rotary_embedding_torch")

    Set-Content -Path $stamp -Value $want -Encoding Ascii
    $code = 0
}
catch { Write-Output "FAILED: $_" }
Stop-Transcript | Out-Null
exit $code
