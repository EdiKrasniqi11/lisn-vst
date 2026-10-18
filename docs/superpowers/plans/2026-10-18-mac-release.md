# LISN for Mac Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship LISN (the VST3 and the desktop app, with Harmony Split) on Apple Silicon Macs as one unsigned `LISN-Mac.pkg`, first to a tester through a private page on lisn.digital, then publicly next to Windows.

**Architecture:** The same JUCE code builds on a GitHub Actions macOS (arm64) runner; Windows-only paths get Mac branches (`#if JUCE_MAC`, `sys.platform`). A shell counterpart of `setup-engine.ps1` builds the shared AI engine in `/Library/Application Support/LISN/engine` from a Mac lock file; `pkgbuild`/`productbuild` wrap the app, the plugin and that script into one pkg whose postinstall runs it. The website detects Macs and swaps every download link.

**Tech Stack:** JUCE 8 / CMake, C++17, Objective-C++ (one small `.mm`), Python 3.11 via uv, bash, `pkgbuild`/`productbuild`, GitHub Actions (`macos-14`), plain HTML/CSS/JS on GitHub Pages (lisn-site).

**Spec:** `docs/superpowers/specs/2026-10-18-mac-release-design.md`

## Global Constraints

- Apple Silicon only (arm64). Nothing may assume arm64 in a way that blocks an Intel build later (no hard-coded `arm64` outside the pkg's `hostArchitectures` and the runner choice).
- Unsigned: ad-hoc code signature only (`codesign -s -`). No Developer ID, no notarization.
- One installer file, named exactly `LISN-Mac.pkg`. It installs `LISN.app` into `/Applications`, `LISN StemSplitter.vst3` into `/Library/Audio/Plug-Ins/VST3`, and the engine into `/Library/Application Support/LISN/engine`.
- Best quality's model on Mac: `~/Library/Application Support/LISN/models/harmony`.
- The desktop app's title bar on Mac: macOS's native red/yellow/green buttons on the left; no LISN Minimise/Close buttons; everything else identical.
- The update pill on Mac opens `https://github.com/EdiKrasniqi11/lisn-vst/releases/latest/download/LISN-Mac.pkg` (plugin and app alike).
- Windows must not change behaviour: its job, installers and tests stay green and identical in output.
- Releases: Windows and Mac go out together from one `v*` tag; a failing macOS job blocks the release.
- The tester build is never a "latest" release: it goes out as a GitHub pre-release (tag `mac-test-N`, not `v*`), which `/releases/latest` ignores.
- Website copy (verbatim from the spec):
  - Mac fine print: "One installer: the VST3 for FL Studio and Ableton, and the desktop app · macOS, Apple Silicon (M1 and newer) · Beta"
  - Steps: 1. "Open **LISN-Mac.pkg** from Downloads. macOS says it can't be opened: click **Done**." 2. "Open **System Settings › Privacy & Security**, scroll down, click **Open Anyway**, then type your Mac password." 3. "Click through the installer. The last step downloads the AI engine (about 390 MB, a few minutes)."
- Pushing `feature/mac-release` to GitHub (for CI) and creating the `mac-test-N` pre-release are outward-facing: confirm with the owner the first time.

## Review Focus

1. **Paths with spaces** (`/Library/Application Support/...`, `LISN StemSplitter.vst3`): every script and `ChildProcess` call must quote them. Pinned by Task 4's CI run installing into the real paths.
2. **An engine setup that fails** (no internet, no disk space) during the pkg's postinstall: the install should report failure, the app should then show its "engine isn't installed" error screen (not crash or hang), and re-running the pkg should fix it. Pinned in Task 4 (a run with a deliberately bad requirements file must exit non-zero and leave no ready stamp).
3. **Installing over an existing install** (upgrade, or a re-run): app and plugin replaced, the engine kept when its stamp matches. Pinned in Task 4 (install twice; the second run must skip the engine rebuild).
4. **A pkg downloaded by a browser** (quarantined): nothing it installs may carry `com.apple.quarantine`. Pinned in Task 4 (quarantine the pkg, install, assert no quarantine on the app and plugin).
5. **Intel-Mac and unknown-platform visitors** on the website: never blocked from the download; they see the Apple Silicon note and the switch. Pinned in Task 7 (`?os=mac` and `?os=win` overrides plus the default for an unknown UA).

---

### Task 1: Build and test on a macOS runner

Make the code compile and the tests pass on macOS; add the CI job. No packaging yet.

**Files:**
- Modify: `CMakeLists.txt` (`VST3_COPY_DIR` uses `$ENV{USERPROFILE}`: on Apple use `$ENV{HOME}/Library/Audio/Plug-Ins/VST3`)
- Modify: `.github/workflows/build.yml` (new job `macos`)
- Modify: `tests/HarmonyTests.cpp` (the fake scripts are `cmd.exe` batch files: on Mac write `/bin/sh` scripts instead)
- Modify: `tests/PythonFinderTests.cpp`, `tests/OpenWithTests.cpp` (Windows-only cases behind `#if JUCE_WINDOWS`; Task 2 adds the Mac cases)
- Modify: any source that fails to compile on clang/macOS (fix in place, keep Windows unchanged)

**Interfaces:**
- Produces: CI job `macos` on `macos-14` that builds `lisn_tests`, `StemSplitter_VST3`, `StemSplitter_Standalone` (Release, `-G Xcode` or Ninja, builder's call) and uploads artifacts `mac-snapshots` (the `lisn_tests --snapshots` PNGs) and `mac-build` (the `.app` and `.vst3`).

- [ ] **Step 1:** Add the `macos` job: checkout, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64`, build the three targets, `ctest --test-dir build -C Release --output-on-failure`, `build/.../lisn_tests --snapshots snapshots`, pluginval 1.0.4 macOS `--strictness-level 5 --skip-gui-tests` on the built VST3, upload the two artifacts. Triggers: same as the Windows job plus `workflow_dispatch`.
- [ ] **Step 2:** In `HarmonyTests.cpp`, `runFake` writes `fake.sh` (`#!/bin/sh`, `chmod +x`) on Mac with the same behaviour as the `.cmd` scripts (print `37%|`, copy vocals.wav to `"$1/lead.wav"` and `"$1/harmonies.wav"`, write `$2` to `"$1/quality.txt"`; the failing one prints `NEEDS SETUP: no model` and `exit 3`; the slow one `sleep 3`). `testCommand` becomes `{ "/bin/sh", script }` on Mac.
- [ ] **Step 3:** Push `feature/mac-release` (owner OK first time); fix compile errors and failing tests until the `macos` job is green and the `windows` job is still green.
- [ ] **Step 4:** Download `mac-snapshots`, compare `harmony-dusk.png`, `stems4-dusk-mix.png`, `titlebar-dusk.png` with the Windows snapshots side by side: same layout, fonts, colours (text antialiasing may differ). Note any real difference and fix it.
- [ ] **Step 5:** Commit: `ci: build and test LISN on macOS`.

### Task 2: Mac paths and labels in the app

**Files:**
- Modify: `Source/PythonFinder.h/.cpp`
- Modify: `Source/HarmonyJob.cpp` (`userModelDir`)
- Modify: `Resources/harmony_split.py` (priority, model dirs)
- Modify: `Source/PluginEditor.cpp` (update pill URL, "Find python" chooser), `Source/Screens.cpp` (button label)
- Test: `tests/PythonFinderTests.cpp`, `tests/HarmonyTests.cpp`

**Interfaces:**
- Produces: `PythonSearchDirs::programData` on Mac = `/Library/Application Support`; engine Python = `<programData>/LISN/engine/venv/bin/python3` on Mac (`venv\Scripts\python.exe` on Windows). PATH entries are searched for `python3` on Mac. `HarmonyJob::userModelDir()` on Mac = `~/Library/Application Support/LISN/models/harmony`.

- [ ] **Step 1: Write the failing tests** (Mac only, `#if JUCE_MAC`):
  - `PythonFinder / candidate order (Mac)`: with fake `python3` files in `pd/LISN/engine/venv/bin`, `path1`, `path2`, `pythonCandidates (hint, dirs)` equals `{ hint, engine, path1/python3, path2/python3 }`; a bare `"python3"` hint is dropped.
  - `Harmony Split / Mac model folder`: `HarmonyJob::userModelDir().getFullPathName().endsWith ("Library/Application Support/LISN/models/harmony")`; `findModel ("quick", "<venv>/bin/python3")` finds `<venv>/models/harmony/UVR_MDXNET_KARA_2.onnx`.
- [ ] **Step 2:** Push; the `macos` job fails on those tests.
- [ ] **Step 3:** Implement: Mac branches in `PythonSearchDirs::fromEnvironment` and `pythonCandidates` (Windows-only candidates — Store filter, `Python3*` folders, conda — stay Windows-only; Mac adds `/opt/homebrew/bin/python3` last); `userModelDir` uses `userApplicationDataDirectory` + `Application Support/LISN/models/harmony` on Mac. In `harmony_split.py`: `SetPriorityClass` only when `sys.platform == "win32"`, else `os.nice(10)`; the user model dir is `~/Library/Application Support/LISN/models/harmony` on darwin. Update pill: on Mac both wrappers open `.../latest/download/LISN-Mac.pkg`. The error screen's button reads "Already installed? Find python3…" and the chooser accepts any file on Mac.
- [ ] **Step 4:** Push; both jobs green.
- [ ] **Step 5:** Commit: `feat: Mac paths for the engine, Harmony Split models and updates`.

### Task 3: The desktop app's window on Mac

**Files:**
- Create: `Source/MacWindow.mm` — `void makeTitleBarTransparent (juce::Component& window);` (declared in `Source/MacWindow.h`): the window's `NSWindow` gets `NSWindowStyleMaskFullSizeContentView`, `titlebarAppearsTransparent = YES`, `titleVisibility = NSWindowTitleHidden`, so the traffic lights sit over the header.
- Modify: `CMakeLists.txt` (add `Source/MacWindow.mm` to the `StemSplitter` target on `APPLE`)
- Modify: `Source/StandaloneApp.cpp` (`MainWindow`: on Mac, native title bar + `makeTitleBarTransparent`, and `makeTitleBar ({}, {})`)
- Modify: `Source/Screens.cpp` (`Header::makeTitleBar`: empty callbacks = native buttons: don't add LISN's Minimise/Close or the separator), `Source/PluginEditor.cpp` (`resized`: with native buttons the header starts 64 px further right and is 64 px narrower, so the pills stay right-aligned and the symbol clears the lights)
- Test: `tests/Snapshots.cpp` (new shot `titlebar-mac-dusk` calling `makeTitleBar ({}, {})`)

**Interfaces:**
- Consumes: `Header::makeTitleBar (std::function<void()> minimise, std::function<void()> close)` (existing).
- Produces: `StemSplitterEditor::makeTitleBar ({}, {})` = the Mac title bar mode.

- [ ] **Step 1:** Add the snapshot `titlebar-mac-dusk` (Drop screen, `makeTitleBar ({}, {})`); its header must show no round buttons and the LISN symbol at window x ≥ 88.
- [ ] **Step 2:** Implement the header/editor change and `MacWindow.mm`; on Windows nothing changes (`titlebar-dusk.png` byte-identical to before on the Windows job).
- [ ] **Step 3:** In the `macos` job, launch the built app for 8 s and `screencapture -x` it into the `mac-snapshots` artifact (best effort: don't fail the job if the runner has no display).
- [ ] **Step 4:** Check the screenshot: lights top-left over the header, LISN after them, pills right, drag moves the window (the tester confirms the drag).
- [ ] **Step 5:** Commit: `feat: the desktop app on Mac uses macOS's window buttons`.

### Task 4: The Mac engine and `LISN-Mac.pkg`

**Files:**
- Create: `installer/requirements-macos.txt` (`uv pip compile installer/requirements.in -o installer/requirements-macos.txt --python-version 3.11 --python-platform aarch64-apple-darwin --generate-hashes`; same pins as Windows)
- Create: `installer/mac/setup-engine.sh` — `setup-engine.sh <EngineDir>`: the same steps and checks as `setup-engine.ps1` (3 GB free check, `uv venv --managed-python --python 3.11`, `uv pip install --require-hashes --compile-bytecode -r requirements-macos.txt`, `sitecustomize.py` into `lib/python3.11/site-packages`, the Demucs models, the Quick model with `shasum -a 256` = `bf32e15105a09c0f7dddd2b67346146334d6f3ecb399ed7638eba2ab07cbf5f4`, the import check `import demucs.separate, onnxruntime, rotary_embedding_torch`, ready stamp of the two input files' hashes, `setup.log`, exit 0 only when ready). `uv`, the requirements and `sitecustomize.py` sit next to it.
- Create: `installer/mac/build-pkg.sh <version> <app> <vst3>` — ad-hoc signs both bundles, stages `LISN.app` and the VST3, writes a `postinstall` that runs `setup-engine.sh "/Library/Application Support/LISN/engine"`, `pkgbuild` + `productbuild` with a distribution file (title "LISN", `hostArchitectures="arm64"`, minimum macOS = what torch and JUCE need), output `installer/Output/LISN-Mac.pkg`.
- Modify: `.github/workflows/build.yml` (`macos` job: fetch uv for `aarch64-apple-darwin` with a pinned SHA-256, build the pkg on every run, upload it as artifact `LISN-Mac.pkg`)

**Interfaces:**
- Consumes: Task 1's built `.app` and `.vst3`; Task 2's engine path.
- Produces: artifact `LISN-Mac.pkg` from every `macos` run.

- [ ] **Step 1:** CI test of the engine script alone: run `setup-engine.sh "$RUNNER_TEMP/LISN engine"` (note the space); assert exit 0, `ready.txt` exists, and a Quick Harmony Split on a 15 s test WAV (generated in the job) produces `lead.wav` and `harmonies.wav`; run it again and assert it exits 0 within 10 s (stamp matches); run it with a broken requirements file and assert non-zero exit and no `ready.txt`.
- [ ] **Step 2:** CI test of the pkg: quarantine it (`xattr -w com.apple.quarantine "0081;$(date +%s);Safari;" LISN-Mac.pkg`), `sudo installer -pkg LISN-Mac.pkg -target /`; assert `/Applications/LISN.app`, `/Library/Audio/Plug-Ins/VST3/LISN StemSplitter.vst3`, `/Library/Application Support/LISN/engine/ready.txt` exist, and `xattr -p com.apple.quarantine` fails on the app and the plugin; pluginval the installed plugin; install the same pkg again and assert success.
- [ ] **Step 3:** Implement the three files and the workflow steps until Steps 1–2 pass and the Windows job is unchanged.
- [ ] **Step 4:** Commit: `feat: LISN-Mac.pkg with the shared AI engine`.

### Task 5: Release wiring and the tester pre-release

**Files:**
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Produces: on `v*` tags, a `release` job that `needs: [windows, macos]` and drafts one release with `LISN-StemSplitter-Setup.exe`, `LISN-Setup.exe` and `LISN-Mac.pkg` (SHA-256 of each in the notes, `LISN-Mac.pkg is LISN for Apple Silicon Macs: the VST3 and the desktop app.`). On `workflow_dispatch` with input `test_tag` (e.g. `mac-test-1`): a public **pre-release** with only `LISN-Mac.pkg`.

- [ ] **Step 1:** Move the Windows job's "Draft release" into the new `release` job (the Windows job uploads its two installers as artifacts); add the `macos` job's tag-version check like Windows'.
- [ ] **Step 2:** Add the `test_tag` pre-release path (`gh release create "$TAG" LISN-Mac.pkg --prerelease --title "LISN for Mac, test build" --notes ...`).
- [ ] **Step 3:** With the owner's OK, run it once with `test_tag=mac-test-1`; check `https://github.com/EdiKrasniqi11/lisn-vst/releases/download/mac-test-1/LISN-Mac.pkg` downloads and that `releases/latest` still points at v0.2.5 (the update check sees nothing new).
- [ ] **Step 4:** Commit: `ci: one release for Windows and Mac; tester pre-releases`.

### Task 6: The private test page (lisn-site)

**Files:**
- Create: `lisn-site/mac/index.html` (uses `../styles.css`; `<meta name="robots" content="noindex">`; not linked from any page; not in `sitemap.xml`; not in `robots.txt`)

**Interfaces:**
- Consumes: the pre-release URL from Task 5 (`.../releases/download/mac-test-N/LISN-Mac.pkg`).

- [ ] **Step 1:** Build the page in LISN's look (the hero's dusk background and fonts): title "LISN for Mac (test build)", the **Download for Mac** button (GoatCounter click `download-mac-test`), the fine print, the three steps verbatim with the two macOS window drawings (as in the brainstorm mockup `mac-steps.html`), and "What to check" — the installer finishes; LISN opens from Applications; FL Studio finds LISN StemSplitter (Options › Manage plugins › Find plugins) and it loads; a split works; a Harmony Split (Quick) works — with "Send what you see to info@lisn.digital" (mailto).
- [ ] **Step 2:** Preview it locally (`.claude/launch.json` `lisn-site`), check phone width and both link targets.
- [ ] **Step 3:** Commit in lisn-site on a branch, and with the owner's OK merge to `master` and push (GitHub Pages publishes `lisn.digital/mac/`). Give the owner the link to send the tester.

### Task 7: The public website (after the tester confirms)

**Files:**
- Modify: `lisn-site/index.html`, `lisn-site/app.js` (or the inline scripts it already uses), `lisn-site/styles.css` (bump their `?v=`), `lisn-site/sitemap.xml` (only if a URL changes)

**Interfaces:**
- Produces: `data-win` / `data-mac` hrefs on every download link; `document.documentElement.dataset.os` = `"mac"` | `"win"`, from `?os=` if present, else a Mac user agent → `"mac"`, else `"win"`.

- [ ] **Step 1:** Implement detection and the switch (Windows | Mac pills under the hero buttons, LISN's pill style); all download links (hero, "In your DAW, or on its own", the demo's "Get LISN, free") swap with the OS; Mac shows the single **Download for Mac** button and the Mac fine print.
- [ ] **Step 2:** The pop-up: on a Mac download click it shows "Your download has started", the three steps, and (only if not asked before, as today) the newsletter question; Windows clicks behave exactly as today.
- [ ] **Step 3:** Install section, FAQ ("Why does my Mac block it?", Mac uninstall: delete `/Applications/LISN.app`, `/Library/Audio/Plug-Ins/VST3/LISN StemSplitter.vst3`, `/Library/Application Support/LISN`, `~/Library/Application Support/LISN`), requirements ("macOS on Apple Silicon (M1 and newer)"), title/description/structured data mention Mac; GoatCounter click `download-mac`.
- [ ] **Step 4:** Verify in the browser preview: `?os=win` is pixel-for-pixel today's page; `?os=mac` shows the Mac download everywhere, the pop-up steps on every click, newsletter only once; no `.exe` link anywhere in Mac mode; phone width works.
- [ ] **Step 5:** Commit on a lisn-site branch; merge and push only with the owner's go-ahead (it goes live). Remove `mac/` or make it redirect to `/#download`.
- [ ] **Step 6 (local only, never committed):** `tools/stats` counts `LISN-Mac.pkg` downloads in the release table.

### Task 8: Splits on the Mac's GPU (only after the tester's first run)

**Files:**
- Modify: `Source/SeparationJob.cpp` (on Mac add `-d mps` to the demucs command when the engine reports MPS available; fall back to CPU if that run fails with an MPS error)

- [ ] **Step 1:** With the tester (or their logs), time one split with and without `-d mps` and compare the stems (null test, like Harmony Split's). Only if MPS is faster and the stems match: implement, push, ship in the next test build; otherwise drop this task.

---

**Release (not a code task):** when the tester has confirmed and the model licences are settled: merge `feature/harmony-split` and `feature/mac-release` to `master`, bump `project(StemSplitter VERSION ...)`, tag `v0.3.0`, publish the draft (both jobs green), then merge Task 7's site branch.
