# LISN for Mac: design

Date: 2026-10-18. Owner: Edi Krasniqi. Status: approved in chat, to be planned.

## Why

People on r/FL_Studio and the free-VST subreddit have asked for a Mac version. LISN is Windows-only today: a VST3 plugin (`LISN-StemSplitter-Setup.exe`) and a standalone desktop app (`LISN-Setup.exe`), both from the same JUCE code, both running a private Python AI engine (Demucs, plus Harmony Split on `feature/harmony-split`). The owner wants Mac users to get the same thing, looking the same, and to download it easily from lisn.digital.

## What success looks like

A Mac user on an Apple Silicon Mac lands on lisn.digital, clicks **Download for Mac**, gets past macOS's one-time block by following the steps the site shows them right then, and ends up with:
- the LISN StemSplitter VST3 loading in FL Studio (and Ableton), and
- the LISN desktop app in Applications,

both looking identical to Windows (same panel, waves, fonts, Dusk and Midnight, 4 and 6 stems, Harmony Split and its Harmonies screen), splitting songs with no Terminal and no extra downloads beyond what the installer and the app do themselves.

## Decisions (the owner's)

1. **Formats:** VST3 and the standalone desktop app, as on Windows. Not AU (Logic) for now.
2. **Macs:** Apple Silicon (M1 and newer) only for now. The owner hopes to cover Intel Macs later; don't design anything that rules that out.
3. **Built on GitHub**, the owner has no Mac: GitHub Actions macOS runners build, test and package. The first real-world test is done by a Mac user (a Reddit tester or a friend).
4. **Unsigned** for now (no Apple Developer Program, no notarization), with clear instructions. Signing may come later.
5. **One Mac download** (website mockup B): a single `LISN-Mac.pkg` installs both the plugin and the desktop app, with one shared AI engine.
6. **The "Open Anyway" steps show in both places**: in a pop-up right after the download click (the existing newsletter pop-up, steps first, newsletter question under them), and in the site's install section, with small drawings of the two macOS windows.
7. **The desktop app's title bar on Mac uses macOS's own red, yellow and green window buttons on the left** (mockup B), then "LISN" and the name reel, then the Dusk/Midnight and 4/6-stem pills. Everything else in the window is identical to Windows. (Inside FL the plugin window has no title bar of its own: unchanged.)
8. **Mac launches together with Harmony Split.** Harmony Split merges first; then Windows and Mac release as the same new version. The Harmony Split model licences (the UVR karaoke models' permission to redistribute, see `installer/THIRD-PARTY-NOTICES.txt`) still have to be settled before that public release.
9. **All together, always:** Windows does not ship Harmony Split ahead of Mac; the public release waits until the Mac version is confirmed. From then on a failing macOS job blocks a release just like a failing Windows job.
10. **A private Mac page on the site for testing first:** the owner may have a Mac tester today. Before anything public changes, the Mac download and its steps go on a private, unlisted page of lisn.digital that the owner sends to the tester. The public page gets the Mac download only once the tester has confirmed it works.

## Goals

- The same LISN on Mac: the plugin and the desktop app from the same source, one version number for both systems, released together from one tag.
- One `LISN-Mac.pkg` that a non-technical Mac user can install with exactly one macOS hurdle (Open Anyway on the pkg itself).
- The website serves Mac visitors a Mac download automatically, and Windows visitors exactly what they get today.
- Everything Windows users have keeps working: no regressions in the Windows build, installers, website or update check.

## Non-goals (this release)

- Intel Macs (later).
- Apple signing and notarization (later).
- AU / Logic, AAX, Linux.
- A Mac App Store version.

## Anti-goals (a failure even if it technically works)

- A user needs Terminal (`xattr`, `chmod`, running scripts) to install or to make the plugin load.
- The plugin installs but FL Studio on Mac doesn't see it or refuses it with no explanation.
- The Mac app looks "almost" like LISN: different fonts, blurry, wrong sizes, a different layout.
- Windows visitors or Windows users notice anything changed for the worse.

## The pieces

### 1. The app and plugin on macOS (lisn-vst)

The JUCE project already builds both formats; the work is making the Windows-only parts cross-platform:
- **Python engine discovery** (`Source/PythonFinder.*`): today it looks for `python.exe` in Windows places (ProgramData and LocalAppData engines, PATH, Program Files). On Mac it looks for the shared engine's `venv/bin/python3` first, then the hint, then sensible fallbacks. The "Find python.exe" button and file filter on the error screen (`Source/Screens.cpp`, `Source/PluginEditor.cpp`) say and accept the Mac equivalent.
- **Harmony Split on Mac**: `Source/HarmonyJob.cpp` uses `windowsLocalAppData` for the Best quality download; on Mac use the user's Application Support folder (`~/Library/Application Support/LISN/models/harmony`). `Resources/harmony_split.py` lowers its priority with `SetPriorityClass` (Windows only): on Mac use `os.nice`. Its model search paths follow the same Mac folders. Its `python -I` usage and venv layout differences (`bin/` vs `Scripts\`) are handled where the engine path is derived (`findModel`).
- **The desktop app's title bar** (`Source/StandaloneApp.cpp`, `Header::makeTitleBar` in `Source/Screens.cpp`): on Mac, the window keeps macOS's native red, yellow and green buttons on the left, the header starts after them, and LISN's own Minimise/Close buttons and their separator are not shown. Dragging the header still moves the window.
- **The update pill** (`Source/PluginEditor.cpp`, `header.onUpdate`): on Mac it opens the Mac download (`releases/latest/download/LISN-Mac.pkg`) for both the plugin and the desktop app. The update check itself (`Source/UpdateCheck.*`) is already cross-platform (GitHub API, settings in the user's app-data folder): verify it on Mac.
- **Look:** fonts are embedded (BinaryData), so the UI should match. Retina: JUCE renders at the display scale; check the background waves, frost and the symbol image (`Header::paint` makes it at the pixel scale) on a 2x display.
- **Splits on the Mac's GPU:** Demucs can run on Apple's GPU (`-d mps` with torch's MPS backend). Use it when available, fall back to CPU. This is a guess that it is faster and stable for htdemucs on Apple Silicon; the builder verifies it on the runner and the test Mac, and keeps CPU if MPS is slower or wrong.
- Anything else Windows-only (`#if JUCE_WINDOWS` blocks in `Source/PluginEditor.cpp` and `Source/StandaloneApp.cpp`, the "Open with" registration in `Source/OpenWith.h`): give it a Mac equivalent where one makes sense, else leave it Windows-only.

### 2. The AI engine on Mac

Same idea as Windows (`installer/setup-engine.ps1`, `installer/requirements.txt`, `installer/sitecustomize.py`): uv-managed CPython 3.11, the locked Demucs + Harmony Split stack, Demucs's htdemucs and htdemucs_6s and Harmony Split's Quick model prefetched, a ready stamp so an unchanged engine isn't rebuilt.
- **One shared engine** for the plugin and the app, installed by the pkg in a system-wide location (the Mac counterpart of ProgramData), so FL Studio's plugin and the app both find it.
- **Mac requirements:** the hash-locked requirements file has to resolve for macOS arm64 (torch, onnxruntime and the rest have arm64 wheels). Keep the Windows file's pins where possible; a separate Mac lock file is fine if the platforms need different hashes.
- **A shell-script counterpart of `setup-engine.ps1`** that the pkg's postinstall runs, with the same free-space check, log file and failure message.
- **Best quality's model** downloads in the app on first use, into the user's folder, as on Windows.

### 3. The installer: `LISN-Mac.pkg`

- Installs `LISN.app` into `/Applications` and `LISN StemSplitter.vst3` into `/Library/Audio/Plug-Ins/VST3` (the folder FL Studio and Ableton scan on Mac), then runs the engine setup (about 390 MB downloaded, a few minutes; the pkg should make that wait clear).
- **Why a pkg:** after the one-time "Open Anyway" on the pkg, files placed by macOS's Installer don't carry the "downloaded from the internet" quarantine mark, so the app opens and the plugin loads without further blocks. This is the key assumption of the unsigned plan: **verify it first** on a real Mac (or a runner, as far as one can show it), including FL Studio loading the plugin.
- Upgrades: installing a newer pkg replaces the app and plugin and rebuilds the engine only if its requirements changed (the ready stamp).
- Uninstall: no standard uninstaller on Mac. The site documents what to delete (the app, the plugin, the engine folder, the models folder).

### 4. Building and releasing (GitHub Actions)

`.github/workflows/build.yml` gains a macOS job (Apple Silicon runner) next to the Windows one:
- build both formats, run `lisn_tests` (including `--snapshots`, and keep the snapshot images as a build artifact so the look can be checked from Windows), and pluginval on the VST3;
- on a `v*` tag, build `LISN-Mac.pkg` and attach it to the same draft GitHub Release as the two Windows installers.
- Ad-hoc code signature only (what the Apple Silicon toolchain needs to run code at all), no Developer ID.

### 5. The website (lisn-site)

From the brainstorm mockups (`lisn-site/.superpowers/brainstorm/`, local only):
- **Mac visitors** (detected from the browser) see one **Download for Mac** button where the two Windows buttons are, with fine print: "One installer: the VST3 for FL Studio and Ableton, and the desktop app · macOS, Apple Silicon (M1 and newer) · Beta". A small **Windows | Mac** switch under the buttons shows the other system's downloads. **Windows visitors** see exactly today's page.
- Any Mac visitor on an Intel Mac, or one the site can't tell apart, can still find the download through the switch; the fine print says Apple Silicon only. (How well a browser can tell Apple Silicon from Intel is the builder's to check; never block the download on it.)
- **After the download click:** the existing pop-up (`<dialog class="news">`, shown once per browser today) shows, for the Mac download, "Your download has started" and the three steps:
  1. Open **LISN-Mac.pkg** from Downloads. macOS says it can't be opened: click **Done**.
  2. Open **System Settings › Privacy & Security**, scroll down, click **Open Anyway**, then type your Mac password.
  3. Click through the installer. The last step downloads the AI engine (about 390 MB, a few minutes).
  Then the newsletter question as today. The Mac steps show on every Mac download click, even when the newsletter question has already been answered.
- **The install section** gets the same Mac steps, with small drawings of the two macOS windows ("Not Opened" with Done, and Privacy & Security's "Open Anyway"), plus how to make FL Studio on Mac find the plugin if it doesn't right away.
- **FAQ:** "Why does my Mac block it?" (the beta isn't signed with Apple yet) and Mac uninstall steps. System requirements list macOS on Apple Silicon.
- **Search and sharing:** the page title, description and structured data mention Mac.
- **Stats:** the Mac download click gets its own GoatCounter name, so the local stats dashboard (`tools/stats`, local only, never committed) can show Mac downloads; the dashboard's release table counts `LISN-Mac.pkg` downloads too.
- **Every download link follows the Windows/Mac choice**, including the "In your DAW, or on its own" section's buttons and the live demo's "Get LISN, free" links: a Mac visitor is never handed a Windows `.exe`. Apart from those links, the live browser demo and the rest of the site are unchanged.

### 6. The private test page (first, before the public site changes)

- An unlisted page on lisn.digital (for example `/mac/`): not linked from anywhere, `noindex`, left out of the sitemap. It shows what a Mac visitor will get: the **Download for Mac** button, the three steps with their drawings, and a short note asking the tester to report back (does the installer run, does the app open, does FL Studio find and load the plugin, do a split and a Harmony Split work).
- **The pkg it offers is downloaded through the browser**, the way real users will get it, so macOS marks it as from the internet and the test is real.
- **Where that pkg lives (builder's call):** not as a GitHub "latest" release, so the update pill and the public download links don't change. Either a GitHub pre-release or a file served by the site. Keep in mind that the lisn-vst and lisn-site repos are public, so "private" means unlisted, not secret. The owner accepted that for the test, even though the build includes Harmony Split before its licences are settled.
- The build for it comes from the macOS job without needing a release tag (a manual run or a branch build), so a fix can be re-sent to the tester quickly.
- Once the tester confirms, the public page changes (section 5), and the private page is either removed or redirected to the main page.

## Order

The tester may be available today, so the first testable pkg comes first; polish follows.

1. Harmony Split merges (it is the base; Mac ships with it).
2. The Mac build on GitHub: both formats build and the tests and snapshots pass on the macOS runner.
3. The engine and the pkg, built without a release tag, and the private test page (section 6). The tester installs it and reports back; that checks the unsigned-install assumption in FL Studio.
4. Fixes from the test; then the public website changes (section 5).
5. Release: one version and tag for Windows and Mac, both jobs green, after the model licences are settled.

## Calls the builder makes (not decided by the owner)

- The exact engine and model folders on Mac, the pkg's internals (pkgbuild/productbuild, scripts), and whether Mac gets its own lock file.
- How the website detects a Mac, and the switch's exact look within LISN's existing style.
- Whether MPS is used for Demucs (only if it is faster and correct).
- The Mac minimum macOS version (whatever torch and JUCE need on Apple Silicon), stated on the site.

## Open risks

- **The unsigned path is the whole plan's weak spot.** If FL Studio on Mac refuses an unsigned or ad-hoc-signed plugin even when installed by the pkg, the fallback is documented Terminal steps, which is an anti-goal; at that point signing (Apple Developer Program, availability from Kosovo unknown) comes back on the table.
- **No Mac in the loop until a tester tries it.** Snapshots from the runner cover the look; behaviour inside FL needs a human with a Mac. Because Windows waits for Mac (decision 9), this also holds back Harmony Split on Windows.
- **The Harmony Split model licences** gate the public release of both systems.
