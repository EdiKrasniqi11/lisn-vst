# LISN StemSplitter: installer and landing site Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A public user opens a landing page, clicks Download, runs one installer, and finds a working LISN StemSplitter in FL Studio or Ableton, with no Python knowledge needed.

**Architecture:** lisn-vst gets an Inno Setup installer and one GitHub Actions workflow that builds, tests and, on a `v*` tag, drafts a GitHub Release whose asset name never changes. The installer puts the VST3 in the standard folder, installs the VC++ runtime, and runs a script that uses a bundled `uv.exe` to build a pinned Python + Demucs engine in `C:\ProgramData\LISN\engine`; `PythonFinder` checks that engine right after the saved path. A new public repo, lisn-site, is a static page on GitHub Pages whose Download button points at `releases/latest/download/LISN-StemSplitter-Setup.exe`, so shipping a new version never touches the site.

**Tech Stack:** JUCE 8.0.15 (CMake, MSVC 2022), Inno Setup 6, Windows PowerShell 5.1, uv 0.12.21, CPython 3.11, demucs 4.1.0 / torch 2.14.0 / torchaudio 2.11.0 / soundfile 0.12.1 (the versions splitting works with on this PC today), GitHub Actions (`windows-latest`), GitHub Releases, GitHub Pages, plain HTML + CSS.

## Status (2026-10-01)

- **Tasks 1 and 8 are done.** The site is built from the Claude Design export, published at https://github.com/EdiKrasniqi11/lisn-site (first commit `fab60b4`) and served at https://edikrasniqi11.github.io/lisn-site/. lisn-vst's repo homepage points at it. The placeholder page was skipped because the real design was ready. The fonts are the export's woff2 files, not the lisn-vst TTFs. The page is pixel-identical to the export at 1280 and 375 px.
- **The site's Download buttons 404 until Task 7 publishes the first release.** Once Task 4 measures the real engine download, update "about 450 MB" in `lisn-site/index.html` (Install step 3).
- **Remaining:** Task 0 and Tasks 2 to 7 (the installer).
- **Where the installer work happens:** the git worktree `C:\Projects\lisn-vst-installer` on branch `feature/installer`, not `C:\Projects\lisn-vst`. Another session uses that checkout, and switching its branch would switch it for that session too. The two folders share one repository: build, test and commit in the worktree, while `C:\Projects\lisn-vst` stays on `master`.

## Context

You asked three things. Short answers, found by reading `C:\Projects\lisn-vst` (public, JUCE 8.0.4, `FORMATS VST3 Standalone`) and your GitHub account:

1. **A landing site where people install the VST: yes.** Static page on GitHub Pages, free. The Download button links to the newest installer on lisn-vst's GitHub Releases through GitHub's fixed `latest/download` URL.
2. **New repo? Yes, `lisn-site`, public.** Pages is free for public repos. The site is the front door for LISN (the VST now, the desktop app from `docs/superpowers/plans/2026-09-29-desktop-app.md` later), so it shouldn't live inside one product's repo, and it keeps HTML commits out of the plugin's history. Hosting is: create repo, push, Settings > Pages > "Deploy from a branch" > `main` / root. Every push goes live in about a minute. Task 1 does exactly this.
3. **Is lisn-vst installer-ready? No.** What's missing:
   - **No installer.** The build only copies the plugin to `%USERPROFILE%\VST3`, a folder DAWs don't scan unless told to. Plugins belong in `C:\Program Files\Common Files\VST3`, which every DAW scans.
   - **The engine.** The plugin runs `python -m demucs` with a Python it searches for. Public users have no Python, so today they'd land on "Python isn't set up yet". The installer must set up a private engine.
   - **VC++ runtime.** torch's DLLs (and the plugin, built with the default dynamic runtime) need the Microsoft Visual C++ runtime, which a fresh PC may lack.
   - **Licensing.** JUCE requires a licence choice before anyone else gets a build. JUCE 8.0.4 also bundles the pre-MIT VST3 SDK; 8.0.11+ bundles VST3 SDK 3.8, which is MIT with nothing to sign.
   - **No CI, no release process, no licence notices.**
   - **Unsigned.** Windows SmartScreen will warn. Accepted for the beta (see Decisions).

## Decisions (2026-09-30)

- **Licence:** free, closed source under JUCE 8 **Starter** (free up to $20k/yr revenue from JUCE work, no splash screen; the older desktop plan's $50k figure is stale). lisn-vst stays public so its Releases are downloadable by anyone; `LICENSE` says all rights reserved. If lisn-vst ever goes private, release downloads stop working for the public: move releases to lisn-site then.
- **Site:** `https://edikrasniqi11.github.io/lisn-site/`. Custom domain later (optional section at the end).
- **Engine at install time** (installer about 50 MB, then about 450 MB downloaded while installing). Not bundled offline (a ~400 MB installer re-downloaded for every plugin fix) and not left to users (Python + pip is too much to ask of the public).
- **Signing:** v0.1 ships unsigned as a beta and the site explains the "More info > Run anyway" click. Revisit with traction (a cost decision).
- **Scope:** Windows 10/11 x64, VST3 only. The desktop app is a later plan that reuses this engine and gets a second button on the site.

## Global Constraints

- No em dashes in anything written: site copy, installer text, docs, commit messages.
- Installer asset name is always `LISN-StemSplitter-Setup.exe`. The site links to `https://github.com/EdiKrasniqi11/lisn-vst/releases/latest/download/LISN-StemSplitter-Setup.exe`.
- Plugin install path: `C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3` (Inno `{commoncf64}\VST3`). The installer needs admin, like every plugin installer.
- Engine: `C:\ProgramData\LISN\engine` (Inno `{commonappdata}\LISN\engine`, env `%ProgramData%`). Python: `engine\venv\Scripts\python.exe`. Models: `engine\venv\models` (TORCH_HOME).
- The version lives only in `project(StemSplitter VERSION x.y.z)` in `CMakeLists.txt` (JUCE takes the plugin version from it). Tags are `vx.y.z`; CI refuses a mismatch.
- This plan changes no UI, so lisn-vst's "Must hold" list (redesign spec) stays true untouched.
- Site: static HTML + CSS, no build step, relative URLs only (it's served under `/lisn-site/`), fonts self-hosted, no cookies or analytics.
- Stage explicit paths in every commit, never `git add .`.
- lisn-vst work happens on branch `feature/installer`. Commits, pushes, tags, merges and publishing a release each need the owner's go-ahead.

## Files

**lisn-vst** (`C:\Projects\lisn-vst`):
- Modify `CMakeLists.txt` (JUCE `GIT_TAG`), `Source/PythonFinder.h`, `Source/PythonFinder.cpp`, `tests/PythonFinderTests.cpp`, `.gitignore`, `docs/superpowers/plans/2026-09-29-desktop-app.md` (a note)
- Create `LICENSE`, `README.md`, `installer/requirements.in`, `installer/requirements.txt` (generated), `installer/setup-engine.ps1`, `installer/lisn-vst.iss`, `installer/THIRD-PARTY-NOTICES.txt`, `.github/workflows/build.yml`, `docs/superpowers/plans/2026-09-30-installer-and-landing-site.md` (this plan)

**lisn-site** (new, `C:\Projects\lisn-site`): `index.html`, `styles.css`, `.nojekyll`, `README.md`, `assets/fonts/`, `assets/img/`, `assets/og.png`, `assets/favicon.svg`

---

### Task 0: Set up

- [ ] **Step 1:** Create the worktree, so `C:\Projects\lisn-vst` stays on `master` for other sessions: `git -C C:/Projects/lisn-vst worktree add ../lisn-vst-installer -b feature/installer`. Do all lisn-vst work below inside `C:\Projects\lisn-vst-installer`. It needs its own first build, and that build doubles as the baseline for Task 2.
- [ ] **Step 2:** Save this plan as `docs/superpowers/plans/2026-09-30-installer-and-landing-site.md`. Under the title of `docs/superpowers/plans/2026-09-29-desktop-app.md`, add:
  > **Updated 2026-09-30 (installer plan):** the licence is decided: JUCE 8 Starter, closed source (Starter's cap is now $20k, not $50k). The AI engine lives at `C:\ProgramData\LISN\engine`, set up by lisn-vst's installer, and `PythonFinder` checks it first; Phase 6 should reuse it, not build a second engine. VST3 SDK 3.8 is MIT (JUCE 8.0.11+), so the Steinberg licence question is gone.
- [ ] **Step 3:** `git add docs/superpowers/plans/2026-09-30-installer-and-landing-site.md docs/superpowers/plans/2026-09-29-desktop-app.md` then `git commit -m "docs: plan for the installer and landing site"`

### Task 1: lisn-site on GitHub Pages (placeholder, live today)

Independent of everything else; do it first so hosting is proven while you design.

**Files:** Create `C:\Projects\lisn-site\index.html`, `.nojekyll` (empty: serve files as-is, so a folder starting with `_` isn't hidden), `README.md`.

- [ ] **Step 1:** `index.html`:
```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LISN StemSplitter</title>
<meta name="description" content="Split any song into stems inside FL Studio or Ableton. Free VST3 for Windows.">
<style>
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; text-align: center;
         background: #6F7679; color: #FFF4EA; font: 16px/1.5 system-ui, sans-serif; }
</style>
</head>
<body>
<main>
  <h1>LISN StemSplitter</h1>
  <p>Split any song into stems, right inside your DAW. Coming soon.</p>
</main>
</body>
</html>
```
  `README.md`: `# lisn-site` + one line: "The LISN StemSplitter website. GitHub Pages serves `main` at https://edikrasniqi11.github.io/lisn-site/. Plain HTML and CSS, no build: edit, commit, push, live in about a minute."
- [ ] **Step 2:** `cd C:/Projects/lisn-site && git init -b main && git add index.html .nojekyll README.md && git commit -m "chore: placeholder page"`
- [ ] **Step 3 (owner OK, creates a public repo):** `gh repo create EdiKrasniqi11/lisn-site --public --source . --push --description "Website for LISN StemSplitter"`
- [ ] **Step 4:** Turn on Pages. Same as Settings > Pages > Build and deployment > "Deploy from a branch" > `main` + `/ (root)` > Save:
  `gh api -X POST repos/EdiKrasniqi11/lisn-site/pages -f "source[branch]=main" -f "source[path]=/"`
- [ ] **Step 5:** Verify: `gh api repos/EdiKrasniqi11/lisn-site/pages --jq '.html_url, .status'` prints the URL and `built` (re-run after a minute if `building`), then `curl -sI https://edikrasniqi11.github.io/lisn-site/ | head -1` prints `HTTP/2 200`. Each push shows as a "pages build and deployment" run in the repo's Actions tab.
- [ ] **Step 6:** `gh repo edit EdiKrasniqi11/lisn-vst --homepage https://edikrasniqi11.github.io/lisn-site/`

### Task 2: Licence, README, JUCE 8.0.15

**Owner first (blocking any public release):** at juce.com, create an account and take the free **Starter** licence (accept the JUCE 8 EULA).

**Files:** Modify `CMakeLists.txt:6`. Create `LICENSE`, `README.md`.

- [ ] **Step 1:** In `CMakeLists.txt`, change `GIT_TAG 8.0.4` to `GIT_TAG 8.0.15`.
- [ ] **Step 2:** Make the comparison baseline before the bump: build current `master` on JUCE 8.0.4, run `lisn_tests.exe --snapshots snapshots` and `--bench`, and copy `snapshots` somewhere outside the repo. `.gitignore` only ignores a folder named `snapshots/`, so a `snapshots-8.0.4` folder inside the repo would show up as untracked. Don't reuse old PNGs: they must come from the same commit you're about to change.
- [ ] **Step 3:** Rebuild and test (the reconfigure re-fetches JUCE):
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
C:\Tools\pluginval\pluginval.exe --strictness-level 5 --validate "build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3"
build\lisn_tests_artefacts\Release\lisn_tests.exe --snapshots snapshots
build\lisn_tests_artefacts\Release\lisn_tests.exe --bench
```
  Expected: 100% tests passed; pluginval ends in SUCCESS; every PNG in `snapshots` looks the same as its twin in `snapshots-8.0.4` (read them: fonts, waves, rows); bench `process_block_us_idle` about 0 and the motion frame at most 4 ms. Fix compile errors in the smallest way; if anything renders or behaves differently, stop and ask.
- [ ] **Step 4:** FL Studio and Ableton smoke test with the dev copy: rescan, drop a song, split, play, mute, loop, drag a stem to a track, save and reopen the project.
- [ ] **Step 5:** `LICENSE` (a starting point, not legal advice; have it reviewed before any commercial launch):
```text
LISN StemSplitter
Copyright (c) 2026 Edi Krasniqi. All rights reserved.

This source code is published for reference only. It is not open source:
no permission is granted to copy, modify, or redistribute it.

The LISN StemSplitter plugin and installer are free to download and use,
including for commercial music production. You may not sell, rent, or
redistribute them; point people to the official download page instead.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY ARISING FROM THE USE OF THE SOFTWARE.

Third-party components and their licences are listed in THIRD-PARTY-NOTICES.txt.
```
  `README.md`:
~~~markdown
# LISN StemSplitter

A VST3 for Windows that splits a song into stems with Demucs, plays them in sync and lets you drag them into your DAW.

**Download:** https://edikrasniqi11.github.io/lisn-site/

This source is published for reference and is not open source (see [LICENSE](LICENSE)). Built with JUCE 8 under the JUCE Starter licence.

## Build (Windows, Visual Studio 2022, CMake 3.22+)

    cmake -S . -B build -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release

Releases: bump `project(StemSplitter VERSION ...)`, then push a matching `v*` tag. `.github/workflows/build.yml` drafts the GitHub Release with the installer.
~~~
- [ ] **Step 6:** `git add CMakeLists.txt LICENSE README.md` then `git commit -m "chore: JUCE 8.0.15 (MIT VST3 SDK), licence and README"`

### Task 3: PythonFinder tries the LISN engine right after the saved path (TDD)

**Files:** Modify `Source/PythonFinder.h`, `Source/PythonFinder.cpp`, `tests/PythonFinderTests.cpp`.

**Interfaces:** Produces `PythonSearchDirs::programData` (`juce::File`, from `%ProgramData%`) and candidate 2 = `programData\LISN\engine\venv\Scripts\python.exe`. Task 5's installer creates that file.

- [ ] **Step 1: Failing test.** In `tests/PythonFinderTests.cpp`, "candidate order": after `const auto mini = ...;` add
```cpp
        const auto engine = fakePython ("pd/LISN/engine/venv/Scripts");
```
  after `dirs.userProfile = ...;` add
```cpp
        dirs.programData  = root.getChildFile ("pd");
```
  and change the expectation to
```cpp
        const juce::StringArray expected { pf311, engine, path1, path2, lad312, lad310, sd39, mini };
```
  In "bare hint is dropped", change `expectEquals (pythonCandidates ("", dirs)[0], path1);` to `expectEquals (pythonCandidates ("", dirs)[0], engine);`
- [ ] **Step 2: See it fail.** `cmake --build build --config Release --target lisn_tests`. Expected: error C2039, `'programData': is not a member of 'PythonSearchDirs'`.
- [ ] **Step 3: Implement.** `Source/PythonFinder.h`: the member line becomes
```cpp
    juce::File localAppData, programFiles, systemDrive, userProfile, programData;
```
  and the candidate list comment gains a new item 2 (renumber the rest to 3 to 5):
```cpp
// 2. programData\LISN\engine\venv\Scripts\python.exe, the private engine the LISN installer sets up
```
  `Source/PythonFinder.cpp`, in `fromEnvironment()` after `d.userProfile = ...;`:
```cpp
    d.programData  = dir ("ProgramData");
```
  In `pythonCandidates()`, right after the hint block and before the PATH loop:
```cpp
    if (dirs.programData != juce::File())   // the private engine the LISN installer sets up
        addIfFile (dirs.programData.getChildFile ("LISN\\engine\\venv\\Scripts\\python.exe"));
```
- [ ] **Step 4: See it pass.** `cmake --build build --config Release` then `ctest --test-dir build -C Release --output-on-failure`. Expected: 100% tests passed.
- [ ] **Step 5:** `git add Source/PythonFinder.h Source/PythonFinder.cpp tests/PythonFinderTests.cpp` then `git commit -m "feat: PythonFinder tries the LISN engine in ProgramData right after the saved path"`

### Task 4: The engine setup script (pinned, locked, re-runnable)

**Files:** Create `installer/requirements.in`, `installer/requirements.txt` (generated), `installer/setup-engine.ps1`. Modify `.gitignore`.

**Interfaces:** `setup-engine.ps1 -EngineDir <dir>`, with `uv.exe` and `requirements.txt` next to the script. Exit 0 means ready and `<dir>\ready.txt` holds the SHA-256 of `requirements.txt` (a later run with the same file exits at once). Any other exit means failed; the log is `<dir>\setup.log`. Used by Task 5 and Task 6.

- [ ] **Step 1:** Append to `.gitignore`:
```text
installer/uv.exe
installer/vc_redist.x64.exe
installer/Output/
```
- [ ] **Step 2:** Get the pinned uv (not committed):
```powershell
Invoke-WebRequest https://github.com/astral-sh/uv/releases/download/0.12.21/uv-x86_64-pc-windows-msvc.zip -OutFile $env:TEMP\uv.zip
if ((Get-FileHash $env:TEMP\uv.zip).Hash -ne '5d223efa0bf00208c3853246af09420419dfbd352536aa6bb8163d6170e23890') { throw 'uv checksum mismatch' }
Expand-Archive $env:TEMP\uv.zip -DestinationPath $env:TEMP\uv -Force
Copy-Item $env:TEMP\uv\uv.exe installer\
```
- [ ] **Step 3:** `installer/requirements.in` (copied from `C:\Python311\python.exe -m pip show`, where splitting works):
```text
demucs==4.1.0
torch==2.14.0
torchaudio==2.11.0
soundfile==0.12.1
```
  Lock every transitive package with hashes (PyPI's Windows torch wheels are CPU-only, which is what we want, so no extra index):
  `installer\uv.exe pip compile installer\requirements.in -o installer\requirements.txt --python-version 3.11 --generate-hashes`
- [ ] **Step 4:** `installer/setup-engine.ps1`:
```powershell
# Builds LISN's private AI engine: uv-managed CPython 3.11 plus the locked Demucs stack, with the models prefetched.
# setup-engine.ps1 -EngineDir C:\ProgramData\LISN\engine   (uv.exe and requirements.txt sit next to this script)
# Exit 0: ready, and EngineDir\ready.txt holds requirements.txt's SHA-256. Anything else: failed, see EngineDir\setup.log.
param([Parameter(Mandatory = $true)][string]$EngineDir)
$ErrorActionPreference = 'Stop'

$uv    = Join-Path $PSScriptRoot 'uv.exe'
$req   = Join-Path $PSScriptRoot 'requirements.txt'
$venv  = Join-Path $EngineDir 'venv'
$py    = Join-Path $venv 'Scripts\python.exe'
$stamp = Join-Path $EngineDir 'ready.txt'
$want  = (Get-FileHash $req -Algorithm SHA256).Hash

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

    # Models live inside the engine so uninstall removes them; a TORCH_HOME the user set still wins.
    Set-Content -Path (Join-Path $venv 'Lib\site-packages\sitecustomize.py') -Encoding Ascii `
        -Value 'import os, sys; os.environ.setdefault("TORCH_HOME", os.path.join(sys.prefix, "models"))'

    Invoke-Native $py @('-c', "from demucs.pretrained import get_model; [get_model(m) for m in ('htdemucs', 'htdemucs_6s')]")
    Invoke-Native $py @('-c', 'import torch, torchaudio, demucs.separate')   # loads torch's DLLs, so a missing VC++ runtime fails here

    Set-Content -Path $stamp -Value $want -Encoding Ascii
    $code = 0
}
catch { Write-Output "FAILED: $_" }
Stop-Transcript | Out-Null
exit $code
```
- [ ] **Step 5: Test it into a temp folder** (no admin needed):
```powershell
Measure-Command { powershell -NoProfile -ExecutionPolicy Bypass -File installer\setup-engine.ps1 -EngineDir $env:TEMP\lisn-engine-test }
$LASTEXITCODE
& "$env:TEMP\lisn-engine-test\venv\Scripts\python.exe" -m demucs -n htdemucs -o $env:TEMP\lisn-split-test "<any song on this PC>"
powershell -NoProfile -ExecutionPolicy Bypass -File installer\setup-engine.ps1 -EngineDir $env:TEMP\lisn-engine-test
```
  Expected: first run exits 0 in a few minutes; `venv\models\hub\checkpoints` holds two `.th` files; the split writes 4 WAVs with no model download; the second run exits 0 in about a second. Note the real download size (network monitor) and time for the site copy, which currently says "about 450 MB, a few minutes". If a uv flag is rejected, check `installer\uv.exe venv --help` / `pip install --help` for the pinned version and adjust. Then `Remove-Item -Recurse -Force $env:TEMP\lisn-engine-test, $env:TEMP\lisn-split-test`.
- [ ] **Step 6:** `git add .gitignore installer/requirements.in installer/requirements.txt installer/setup-engine.ps1` then `git commit -m "feat: installer script that builds the pinned Demucs engine with uv"`

### Task 5: The Inno Setup installer

**Files:** Create `installer/lisn-vst.iss`, `installer/THIRD-PARTY-NOTICES.txt`.

**Interfaces:** Consumes the Release bundle `build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3`, `installer\uv.exe`, `installer\vc_redist.x64.exe`, `installer\requirements.txt`, `installer\setup-engine.ps1`, `LICENSE`. Produces `installer\Output\LISN-StemSplitter-Setup.exe`. `ISCC /DAppVersion=x.y.z` sets the version.

- [ ] **Step 1 (owner OK, installs software):** `winget install --id JRSoftware.InnoSetup -e`. ISCC is in `C:\Program Files (x86)\Inno Setup 6\` (or `%LOCALAPPDATA%\Programs\Inno Setup 6\` for a per-user install).
- [ ] **Step 2:** `Invoke-WebRequest https://aka.ms/vs/17/release/vc_redist.x64.exe -OutFile installer\vc_redist.x64.exe` (Microsoft's permanent link; redistributing it is allowed).
- [ ] **Step 3:** `installer/lisn-vst.iss`. The AppId below was generated for this plan; never change it, it's how upgrades find the old install.
```iss
; LISN StemSplitter installer. From the repo root, after a Release build:
;   ISCC /DAppVersion=0.1.0 installer\lisn-vst.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Engine "{commonappdata}\LISN\engine"

[Setup]
AppId={{E866911A-0569-4928-8FF5-A1B24278423C}
AppName=LISN StemSplitter
AppVersion={#AppVersion}
AppPublisher=LISN
AppPublisherURL=https://edikrasniqi11.github.io/lisn-site/
AppSupportURL=https://github.com/EdiKrasniqi11/lisn-vst/issues
DefaultDirName={autopf}\LISN StemSplitter
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=LISN-StemSplitter-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=LISN StemSplitter

[Messages]
FinishedLabelNoIcons=LISN StemSplitter is installed. Open FL Studio or Ableton Live and rescan your plugins to find it.

[Files]
Source: "..\build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3\*"; DestDir: "{commoncf64}\VST3\LISN StemSplitter.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "uv.exe"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "setup-engine.ps1"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "requirements.txt"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"

[UninstallDelete]
Type: filesandordirs; Name: "{#Engine}"
Type: dirifempty; Name: "{commonappdata}\LISN"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  WizardForm.StatusLabel.Caption := 'Installing the Microsoft Visual C++ runtime...';
  // 0 installed, 1638 newer one present, 3010 reboot pending: all fine. A real failure shows up in the engine check.
  Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  WizardForm.StatusLabel.Caption := 'Setting up the AI engine (about 450 MB download, a few minutes)...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
              ExpandConstant('-NoProfile -ExecutionPolicy Bypass -File "{app}\engine-setup\setup-engine.ps1" -EngineDir "{#Engine}"'),
              '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    MsgBox('LISN StemSplitter is installed, but its AI engine could not be set up.' + #13#10#13#10 +
           'Check your internet connection and run this installer again.' + #13#10 +
           'Details: ' + ExpandConstant('{#Engine}\setup.log'), mbError, MB_OK);
  WizardForm.ProgressGauge.Style := npbstNormal;
end;
```
- [ ] **Step 4:** `installer/THIRD-PARTY-NOTICES.txt`, assembled from the real texts, one section each:
  - **VST 3 SDK** (MIT): the SDK's `LICENSE.txt` from the fetched JUCE tree (`Get-ChildItem build\_deps\juce-src -Recurse -Filter LICENSE.txt | Where-Object FullName -match 'VST3_SDK\\LICENSE.txt$'`), plus "VST is a registered trademark of Steinberg Media Technologies GmbH."
  - **JUCE 8:** "Built with JUCE (https://juce.com) under the JUCE 8 End User Licence Agreement (Starter)."
  - **DM Sans, Unbounded** (SIL OFL 1.1, embedded): the texts of `Resources\fonts\OFL-DMSans.txt` and `OFL-Unbounded.txt`.
  - **uv** (bundled; MIT or Apache-2.0): the MIT text from https://github.com/astral-sh/uv/blob/0.12.21/LICENSE-MIT
  - **Installed at setup from PyPI** (full licences also sit in the engine's site-packages): Demucs (MIT, Meta Platforms), PyTorch and torchaudio (BSD-3-Clause), soundfile (BSD-3-Clause), CPython 3.11 (PSF License, python-build-standalone via uv).
  - **Microsoft Visual C++ Redistributable:** redistributed under the Microsoft Visual Studio licence terms.
- [ ] **Step 5: Build it.** `cmake --build build --config Release` then `& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 installer\lisn-vst.iss`. Expected: `installer\Output\LISN-StemSplitter-Setup.exe`, about 50 MB.
- [ ] **Step 6: Install on this PC.**
  1. Move the dev copy out of the DAWs' way: `Rename-Item "$env:USERPROFILE\VST3\LISN StemSplitter.vst3" "LISN StemSplitter.vst3.dev"`
  2. Run the installer: UAC, licence page, install, "Setting up the AI engine..." with a moving bar, Finish with no error box.
  3. `Test-Path "C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3\Contents\x86_64-win\LISN StemSplitter.vst3"` is True; `Get-Content C:\ProgramData\LISN\engine\ready.txt` prints a hash.
  4. `C:\Tools\pluginval\pluginval.exe --strictness-level 5 --validate "C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3"` ends in SUCCESS.
  5. FL Studio and Ableton: rescan, add the plugin in a new project, split a song. While it splits, Task Manager > Details > python.exe > Open file location shows `C:\ProgramData\LISN\engine\...`.
  6. Run the installer again: done in seconds (engine unchanged).
  7. Uninstall (Settings > Apps): the VST3 folder and `C:\ProgramData\LISN` are gone.
  8. Put the dev copy back: `Rename-Item "$env:USERPROFILE\VST3\LISN StemSplitter.vst3.dev" "LISN StemSplitter.vst3"`
- [ ] **Step 7:** `git add installer/lisn-vst.iss installer/THIRD-PARTY-NOTICES.txt` then `git commit -m "feat: Inno Setup installer with the VC++ runtime and the AI engine"`

### Task 6: GitHub Actions: test every push, draft a release on a tag

**Files:** Create `.github/workflows/build.yml`.

**Interfaces:** On any push or PR: build, ctest, pluginval. On a `v*` tag, additionally: check the tag against CMake's version, build the installer, create a **draft** release with `LISN-StemSplitter-Setup.exe` and its SHA-256.

- [ ] **Step 1:** `.github/workflows/build.yml`:
```yaml
name: build

on:
  push:
    branches: [master]
    tags: ['v*']
  pull_request:
  workflow_dispatch:

permissions:
  contents: write   # gh release create

jobs:
  windows:
    runs-on: windows-latest
    env:
      RELEASE: ${{ startsWith(github.ref, 'refs/tags/v') }}
    defaults:
      run:
        shell: pwsh
    steps:
      - uses: actions/checkout@v4

      - name: Build
        run: |
          cmake -S . -B build -A x64
          cmake --build build --config Release --parallel
          if ($LASTEXITCODE) { exit $LASTEXITCODE }

      - name: Unit tests
        run: ctest --test-dir build -C Release --output-on-failure

      - name: pluginval
        run: |
          Invoke-WebRequest https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip -OutFile pluginval.zip
          Expand-Archive pluginval.zip -DestinationPath pluginval
          # GUI tests run locally (Task 2 and 5) and in the DAWs; runners have no real display.
          .\pluginval\pluginval.exe --strictness-level 5 --skip-gui-tests --validate "build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3"

      - name: Tag matches the CMake version
        if: env.RELEASE == 'true'
        run: |
          $v = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(StemSplitter VERSION ([0-9.]+)\)').Groups[1].Value
          if ("v$v" -ne '${{ github.ref_name }}') { throw "tag ${{ github.ref_name }} but CMakeLists.txt says $v" }

      - name: Installer
        if: env.RELEASE == 'true'
        run: |
          Invoke-WebRequest https://github.com/astral-sh/uv/releases/download/0.12.21/uv-x86_64-pc-windows-msvc.zip -OutFile uv.zip
          if ((Get-FileHash uv.zip).Hash -ne '5d223efa0bf00208c3853246af09420419dfbd352536aa6bb8163d6170e23890') { throw 'uv checksum mismatch' }
          Expand-Archive uv.zip -DestinationPath uv
          Copy-Item uv\uv.exe installer\
          Invoke-WebRequest https://aka.ms/vs/17/release/vc_redist.x64.exe -OutFile installer\vc_redist.x64.exe
          choco install innosetup --no-progress -y
          & "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" "/DAppVersion=$('${{ github.ref_name }}'.TrimStart('v'))" installer\lisn-vst.iss
          if ($LASTEXITCODE) { exit $LASTEXITCODE }

      - name: Draft release
        if: env.RELEASE == 'true'
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          $exe = 'installer\Output\LISN-StemSplitter-Setup.exe'
          $sha = (Get-FileHash $exe).Hash.ToLower()
          gh release create '${{ github.ref_name }}' $exe --draft --title 'LISN StemSplitter ${{ github.ref_name }}' --notes "Installer SHA-256: $sha"
```
- [ ] **Step 2:** `git add .github/workflows/build.yml` then `git commit -m "ci: build, test and pluginval on push; draft a release with the installer on v* tags"`
- [ ] **Step 3 (owner OK):** `git push -u origin feature/installer`. Expected: the "build" run in lisn-vst's Actions tab goes green with the four release steps skipped (`gh run watch` follows it).

### Task 7: First release, v0.1.0

- [ ] **Step 1 (owner):** merge to master (`git switch master && git merge --ff-only feature/installer && git push`); CI is green on master.
- [ ] **Step 2 (owner OK):** `git tag v0.1.0 && git push origin v0.1.0`. After the run: `gh release view v0.1.0 --json isDraft,assets --jq '.isDraft, .assets[].name'` prints `true` and `LISN-StemSplitter-Setup.exe`. Drafts are invisible to the public. If the run fails, fix it, then (with owner OK) delete the tag and draft (`gh release delete v0.1.0 --yes`, `git push --delete origin v0.1.0`, `git tag -d v0.1.0`) and tag again.
- [ ] **Step 3: Clean-machine test.** This PC is Windows 11 Home (no Windows Sandbox), so use VirtualBox with a Windows 10 or 11 ISO from microsoft.com (unactivated is fine; give it 8 GB RAM, 4 cores, 60 GB disk), or a second PC. Install REAPER (free evaluation) as the host. Then:
  1. Get the draft's installer in (`gh release download v0.1.0` on this PC and copy it over; drafts need a GitHub login).
  2. Run it: SmartScreen "Windows protected your PC" > More info > Run anyway; UAC Yes; install; no error box.
  3. REAPER: Options > Preferences > Plug-ins > VST > Re-scan; add LISN StemSplitter to a track; split a song into 4 stems; play; mute one; drag a stem onto a track.
  4. Uninstall from Settings > Apps; the VST3 folder and `C:\ProgramData\LISN` are gone.
- [ ] **Step 4 (owner):** publish: lisn-vst > Releases > v0.1.0 > Edit > write notes > Publish release (or `gh release edit v0.1.0 --draft=false`). Only published, non-prerelease releases count as "latest".
- [ ] **Step 5:** `curl -sIL https://github.com/EdiKrasniqi11/lisn-vst/releases/latest/download/LISN-StemSplitter-Setup.exe | grep -iE '^(HTTP|content-length)'` ends in `200` with the installer's size.

**Every later release:** bump `project(StemSplitter VERSION 0.1.1)`, merge, `git tag v0.1.1 && git push origin v0.1.1`, test the draft, publish. The site never changes. Download counts: `gh api repos/EdiKrasniqi11/lisn-vst/releases --jq '.[] | "\(.tag_name) \(.assets[0].download_count)"'`

### Task 8: The real site, from your Claude Design template

**Prereq:** you designed it with the brief below and exported it (Export > standalone HTML, or Claude Design's handoff to Claude Code). Run this task from a session opened on `C:\Projects\lisn-site` (the preview pane reads that folder's `.claude/launch.json`) and tell it where the export is.

**Files (lisn-site, branch `feature/landing-page`):** Modify `index.html`. Create `styles.css`, `assets/fonts/` (4 TTFs + 2 OFL texts), `assets/img/`, `assets/og.png`, `assets/favicon.svg`.

- [ ] **Step 1:** `git -C C:/Projects/lisn-site switch -c feature/landing-page`. Rebuild the export as one `index.html` and one `styles.css`: drop any design-canvas runtime (`support.js`, `<x-dc>`, `{{...}}` bindings, `<dc-import>`), move inline styles into the stylesheet, use landmarks (`header`, `main`, one `section` with a heading per block, `footer`).
- [ ] **Step 2: Fonts, self-hosted.** Copy `C:\Projects\lisn-vst\Resources\fonts\` `DMSans-Regular.ttf`, `DMSans-Medium.ttf`, `DMSans-Bold.ttf`, `Unbounded-Bold.ttf`, `OFL-DMSans.txt`, `OFL-Unbounded.txt` into `assets/fonts/`; four `@font-face` rules with `font-display: swap`; remove any Google Fonts link.
- [ ] **Step 3: Images.** Copy the screenshots the design uses from `C:\Projects\lisn-vst\snapshots\` into `assets/img/`. Every `<img>` has `alt`, `width` and `height`; all but the hero get `loading="lazy"`. `og.png` is 1200 x 630, the hero screenshot on the wave background.
- [ ] **Step 4: Links.** Every Download button: `https://github.com/EdiKrasniqi11/lisn-vst/releases/latest/download/LISN-StemSplitter-Setup.exe`. Release notes: `https://github.com/EdiKrasniqi11/lisn-vst/releases`. Report a problem: `https://github.com/EdiKrasniqi11/lisn-vst/issues`. Asset paths relative (`assets/...`, never `/assets/...`).
- [ ] **Step 5: Head.** `<title>`, meta description, `og:title`, `og:description`, `og:url` = `https://edikrasniqi11.github.io/lisn-site/`, `og:image` = `https://edikrasniqi11.github.io/lisn-site/assets/og.png`, `twitter:card` = `summary_large_image`, `<link rel="icon" href="assets/favicon.svg">`.
- [ ] **Step 6: Phone and motion.** One `@media (max-width: 640px)` block for phone layout; no horizontal scroll at 375 px; any wave animation stops under `@media (prefers-reduced-motion: reduce)`.
- [ ] **Step 7: Verify locally.** A `.claude/launch.json` entry in lisn-site running `python -m http.server 8000`, opened in the preview pane at 1280 px and 375 px: matches the design; no console errors; no requests to other domains (network list); every link reachable by Tab with a visible focus ring.
- [ ] **Step 8 (owner OK):** commit the explicit files, merge to `main`, push. About a minute later `curl -s https://edikrasniqi11.github.io/lisn-site/ | grep -c LISN-StemSplitter-Setup.exe` is at least 1.
- [ ] **Step 9 (after Task 7):** on the live site, in a browser where you're not logged in to GitHub, click Download: the installer downloads.

---

## Claude Design brief (paste this; attach the screenshots)

Attach from `C:\Projects\lisn-vst\snapshots\`: `stems4-dusk@2x.png`, `drop-dusk@2x.png`, `stems4-dusk-zoom@2x.png`, `help-export-dusk@2x.png`, `stems6-midnight-loop.png`. Optional, for the wave style: `C:\Projects\lisn-vst\docs\design\Waves.dc.html` and `Main.dc.html`.

```text
Design a one-page landing site for LISN StemSplitter.

What it is: a free VST3 plugin for Windows that splits any song into stems with AI (vocals, drums, bass and other, plus guitar and piano in 6-stem mode), right inside FL Studio or Ableton Live. Splitting runs on the user's own computer; nothing is uploaded.

Audience: producers, DJs and beatmakers on Windows.
Main action: a "Download for Windows" button, with small print under it: Free · VST3 · Windows 10/11 64-bit · Beta.

Look: match the plugin in the attached screenshots.
- Fonts: Unbounded Bold for the LISN wordmark and big headings (wordmark letter-spacing 0.08em, followed by "StemSplitter" in DM Sans Medium). DM Sans 400/500/700 for everything else.
- Dusk theme (default): background #6F7679 under layered flowing waves in #D9775F, #F4A76F, #7B5467, #D95F6B, #F08A7C (back to front). Text is always cream #FFF4EA. Frosted glass panels rgba(32,18,27,0.66) with a 1px cream border at about 14% opacity and 22px corners. Pill-shaped controls. Primary buttons are cream with dark ink text #2B1822.
- Midnight theme, for at most one contrasting section: background #152238, waves #2C4A78, #4F86B8, #101A2E, #35679C, #86B6DD, panels rgba(8,14,28,0.66).
- Stem colours for chips and dots: vocals #F4A76F, drums #EF7A7F, bass #C9A0BD, guitar #F3CD7F, piano #EFE3D6, other #A9B8BC.
- Motifs from the plugin: dashed "Drag" chips, stem badges with colour dots, waveform rows.

Sections:
1. Header: the wordmark; links to Features, Install and FAQ; a small Download button.
2. Hero: a short headline (for example "Pull any song apart."), one line on what it does, the Download button, and a large plugin screenshot floating on the waves.
3. How it works, in 3 steps: Drop a song (wav, mp3, flac, aiff, ogg). Split it (4 stems, or 6 with guitar and piano). Drag the stems onto your tracks.
4. Features: all stems play in sync, with mute and solo. Loop on the beat, then drag the mix or just the loop into your DAW. Zoom and scrub the waveforms. Songs you've split reopen instantly. Dusk and Midnight themes. Everything runs locally and privately.
5. Install, as numbered steps:
   1) Close your DAW and run the installer.
   2) If Windows says "Windows protected your PC", click More info, then Run anyway. The beta isn't code-signed yet.
   3) Allow the admin prompt. The last step downloads the AI engine: about 450 MB, needs internet, takes a few minutes.
   4) Rescan plugins. FL Studio: Options > Manage plugins > Find installed plugins. Ableton Live: Settings > Plug-Ins > Rescan, with "Use VST3 Plug-In System Folders" on.
   5) Add LISN StemSplitter to a track.
6. Requirements: Windows 10 or 11 (64-bit). A VST3 host (tested in FL Studio and Ableton Live). 3 GB of free disk space. Internet during install. Runs on the CPU.
7. FAQ: Is it free? (Yes.) Does it upload my music? (No, everything runs on your PC.) Is there a Mac version? (Not yet.) Where are my stems saved? (%APPDATA%\StemSplitter\cache, and you can drag them anywhere.) How do I uninstall? (Windows Settings > Apps > LISN StemSplitter. This also removes the AI engine.) Why does Windows warn me? (The beta isn't code-signed yet.)
8. Footer: © 2026 LISN · Release notes · Report a problem · "Stem separation by Demucs (Meta AI, MIT licence). VST is a registered trademark of Steinberg Media Technologies GmbH."

Constraints: a single page, dark only. It must work on a phone (375px wide) as well as desktop. Plain HTML and CSS; JavaScript only for the wave animation, which stops when the visitor prefers reduced motion. No cookies, trackers or embedded videos. No em dashes anywhere in the copy.
```

## Optional later: a custom domain

1. Buy a domain (Cloudflare Registrar and Porkbun sell at cost).
2. Verify it for your account first, so nobody else can claim it: GitHub > Settings > Pages > "Add a domain", then add the TXT record it shows at your registrar.
3. lisn-site > Settings > Pages > Custom domain > enter it > Save (GitHub commits a `CNAME` file).
4. DNS at the registrar: apex `A` records `185.199.108.153`, `185.199.109.153`, `185.199.110.153`, `185.199.111.153` (optionally `AAAA` `2606:50c0:8000::153` through `2606:50c0:8003::153`); `www` as a `CNAME` to `edikrasniqi11.github.io`.
5. Once the certificate is issued (minutes to a day), tick "Enforce HTTPS".
6. Update the absolute URLs: `og:url` and `og:image` in lisn-site, `AppPublisherURL` in `installer/lisn-vst.iss`, lisn-vst's README and repo homepage.

## Not in this plan (add when needed)

- **Code signing** (removes the SmartScreen click): Microsoft's Trusted Signing service (monthly fee, eligibility limits for individuals) or an OV certificate (yearly, hardware token). One `signtool` step before "Draft release".
- A "run the installer again" hint on the plugin's Python-missing screen (a UI change, so mockup first).
- An update notice inside the plugin (GitHub Releases API); a GPU engine (CUDA torch, about +2.5 GB); a Mac build; a paid tier (store and licence keys; mind the $20k Starter cap).
- A Python-free engine (demucs.cpp or ONNX) if install-time downloads turn into support tickets.

## Verification summary

| Task | Done when |
|---|---|
| 1 | `https://edikrasniqi11.github.io/lisn-site/` returns 200 |
| 2 | on JUCE 8.0.15: ctest passes, pluginval SUCCESS, snapshots unchanged by eye, FL and Ableton smoke pass |
| 3 | PythonFinder tests pass with the engine as candidate 2 |
| 4 | the script builds an engine in TEMP that splits a song; a second run exits at once |
| 5 | installs, splits in FL and Ableton, reinstalls in seconds, uninstalls clean |
| 6 | CI green on push (build, tests, pluginval) |
| 7 | clean-VM install and split pass; release published; the `latest/download` URL returns 200 |
| 8 | the live site matches the design at 1280 and 375 px; Download fetches the installer |

## Execution

Inline with superpowers:executing-plans fits better than subagents here: the tasks are sequential and several need you at the keyboard (JUCE signup, pushes, the VM test, publishing, Claude Design). Task 1 and the Claude Design brief can start today, before anything else.
