# In-app update: "What's new" and Update now

2026-10-19. Ships in 0.3.0, with Harmonies and the Mac release.

## Why

Today the Update pill (0.2.5) opens the browser on the new installer and leaves the rest to the user. The owner wants the update to feel like part of LISN. Clicking the pill shows what's new, with notes and screenshots. One button then downloads and installs the update without the browser, and in FL Studio without closing FL.

## What the user sees

1. **The pill.** It's unchanged ("Update 0.3.1", right of the title, appears after the daily check). There is no automatic pop-up and no nagging: the pill is the only signal.
2. **What's new.** Clicking the pill shows layout C from the mockup (`lisn-site/.superpowers/brainstorm/14413-1792427946/content/update-popup.html`, option "panel"):
   - It takes the main panel's place like a screen. There is no dimming and the header stays.
   - Left: a big screenshot, with dots or arrows to flip when there are several. With no screenshots, it shows the LISN logo on the panel background.
   - Right: "LISN 0.3.1", "<size> · you have 0.3.0", the notes, and at the bottom **Later** / **Update now**.
   - Later (or the pill again) returns to the screen that was there before.
3. **Downloading.** The same panel shows a progress bar with "<n>% · keep making music, it downloads in the background", and Cancel. Playback and everything else keep working. Leaving the pop-up (Later) does not stop the download, and the pill shows the percentage.
4. **Ready, desktop app (Windows).** "Install and restart":
   - LISN starts the installer, which shows only its own progress window with no wizard pages, and LISN quits.
   - When the installer finishes, LISN opens again on the new version.
5. **Ready, in a DAW (Windows).** "Install":
   - Windows asks its usual admin question, because the plugin lives in Common Files (the installer is admin today).
   - The installer's progress window runs while FL stays open.
   - Then the panel says "Installed. LISN 0.3.1 starts the next time you open FL Studio." It names the host (FL Studio, Ableton Live), or says "your DAW".
   - For the rest of that FL session the pill reads "Restart FL Studio to finish"; clicking it shows the same message.
6. **Mac (app and DAW).** "Install":
   - LISN opens the downloaded `LISN-Mac.pkg` in Apple's Installer. The pkg holds both the app and the plugin, and asks for the Mac password as it does today.
   - The desktop app quits once the Installer opens. In a DAW, LISN stays open and shows the same "starts the next time you open…" message.
   - The Mac's "Open Anyway" step shouldn't appear. Only files a browser downloads are quarantined, and LISN downloading the pkg itself doesn't mark it.

**Update now is unavailable while a split or Harmonies job runs.** The button is dimmed with "Finish the current split first". It wakes up when the job ends. This matters because the installer may rebuild the AI engine that the job is using.

**Missing installer:** if the release has no installer for this platform (for example, published before every file was uploaded), Update now falls back to opening the download page in the browser.

**A download left from before:** if LISN quits between "Ready" and the install, the file stays in the temp folder. Next time it's re-checked against the hash and reused if it matches; otherwise it's deleted and downloaded again.

**Errors:**
- **Download fails or the hash doesn't match:** "The download didn't finish." with Try again and "Download from the website" (today's browser link as a fallback).
- **Windows admin prompt declined, or the installer fails:** "The update didn't install." with the same two buttons.

## Where the notes and screenshots come from

They come from the GitHub release itself, so there's no new server and no new file to host. The daily check already fetches `releases/latest`, and that response carries everything:

- **Notes:** the bullets under a `## What's new` heading in the release text.
  - `**bold**` shows bold, and other markdown shows as plain text.
  - At most about 6 bullets fit. The panel cuts off the rest with "…and more" linking to the release page.
  - With no such heading, the notes read "Fixes and improvements."
  - The rest of the release text, like the SHA-256 lines for the website, is never shown.
- **Screenshots:** release assets named `whats-new-1.png`, `whats-new-2.png`, … (PNG or JPEG), in name order, at most 3.
  - They're drawn to fit the left side, so the owner takes them from LISN at 2× (760×500 window).
  - They're downloaded when the pop-up first opens, not by the daily check.
- **The installer:** the asset for this platform and product: `LISN-Setup.exe` (desktop), `LISN-StemSplitter-Setup.exe` (VST, Windows), or `LISN-Mac.pkg`.
  - Its `size` feeds the "<size>" label. Its `digest` (`sha256:…`, which GitHub's API gives for release assets) is checked after the download.
  - If the release has no `digest`, the check is skipped, since the download is HTTPS from GitHub.

The daily check keeps working as now: once a day, shared by every LISN on the PC, pre-releases never offered. It now also saves the notes, screenshot URLs, installer URL, size and digest beside `latest`, so the pop-up opens instantly offline.

**Owner's release routine:** the release job still creates a draft. Before publishing it, the owner adds the `## What's new` bullets and uploads the screenshots. The release job's notes template gets an empty `## What's new` section to fill in.

## Installing over a running plugin (Windows)

Windows won't overwrite a DLL that FL has loaded, but it does allow renaming it. So the VST installer does this:

- **Before copying files:** if `…\LISN StemSplitter.vst3\Contents\x86_64-win\LISN StemSplitter.vst3` is in use, rename it to `LISN StemSplitter.vst3.old` in the same folder, then install the new file.
  - FL keeps running the old copy it already loaded. On its next start it loads the new one.
  - The `.old` name doesn't end in `.vst3`, so no host ever scans it as a second LISN.
  - The plugin ID is unchanged, so saved projects open the new version with their settings.
- **Clean-up:**
  - The installer asks Windows to delete the `.old` file at the next restart.
  - Every install also deletes any `.old` file left from before.
  - The uninstaller removes it too.
  - LISN itself can't delete it, because Common Files is admin-only.
- **The installer must not try to close FL.** Inno's "close applications" check is off for the VST installer.
- **If the rename fails** (rare: something holds the file in an unusual way), the installer stops before changing anything. LISN shows "Close FL Studio, then click Install", and Install runs the same installer again.

The desktop app doesn't need the trick, because LISN quits before its installer copies `LISN.exe`.

**More LISNs opened in the same FL session after the install still get the old code.** Windows hands back the module it already loaded from that path, so every LISN in that FL keeps sharing one version and one update state until FL restarts.

**Old plugin, new engine.** For the rest of that FL session, the old plugin is running next to whatever the installer put in the shared engine.
- If the engine's `ready.txt` changed since this process started, LISN refuses new splits and Harmonies jobs with "Restart FL Studio to finish the update". The old script may not match the new packages.
- If the engine didn't change, everything keeps working as before.
- The "no job running" gate is per process. A job running in another program on the PC (Ableton beside FL) during an engine rebuild can fail. That job shows its normal error and works again after Retry. Engine rebuilds are rare.

**Mac, in a DAW.** Apple's Installer writes new files rather than overwriting the old ones in place. A running FL should keep the old plugin it loaded, but this is untested.
- The Mac tester checks it: install from LISN in FL with FL open, then keep using LISN in that session.
- If FL crashes or LISN misbehaves, the Mac DAW path becomes "Quit FL Studio, then open the installer" instead.
- LISN can't see the Installer finish. So once the Installer opens, the panel says "Finish in the Installer. LISN 0.3.1 starts the next time you open FL Studio."

**Windows, reading the result.** LISN starts the admin installer through Windows' elevation call and waits for it, reading its exit code: success, declined (the "cancelled" error), or failed. It doesn't guess.

**The engine:** each installer rebuilds the AI engine only when its requirements changed, as now (`ready.txt`). Most updates are a minute. When the engine does change, the installer's own window says "Setting up the AI engine (about 390 MB download, a few minutes)", as today.

## Goals

- Pill → notes and screenshots → one button → updated, without the browser.
- FL stays open on Windows and on Mac.
- It never interrupts a split or Harmonies job, or playback.
- It behaves the same everywhere the pill appears today.

## Non-goals

- Automatic or silent updates, or a pop-up the user didn't ask for.
- Updating the desktop app from the plugin or the other way round. Each updates its own install, as each has its own pill today. The Mac pkg covers both anyway.
- "Skip this version", a changelog history, or a beta channel.
- Delta or partial downloads.
- Code signing. It's still unsigned, so Windows' admin prompt says "Unknown publisher" for the VST installer, as the website download already does.

## Anti-goals (a failure even if it works)

- FL closes, freezes, or loses the project because of an update.
- A host lists two LISNs, or a project opens with LISN missing or reset.
- A split is broken mid-way by an engine rebuild.
- The pop-up feels like an ad: it shows up by itself, can't be dismissed, or returns every launch.

## Constraints

- Every network and disk step runs off the message thread (`juce::Thread`/`URL`, as `UpdateCheck` and `HarmonyJob::download` already do). The UI only reads state.
- Download state is per process (one `UpdateCheck`-like singleton), so every LISN window in one FL shares a single download and its result.
- Downloads go to the OS temp folder (`%TEMP%\LISN-update\`, Mac `$TMPDIR`) and are deleted after a successful install or a Cancel.
- 0.2.5 users get 0.3.0 the old way (their pill opens the browser). The pop-up applies from 0.3.0 to every later version, so the owner first experiences it on 0.3.0 → 0.3.1.
- Snapshot tests for each new screen state (no network): What's new with and without screenshots, downloading, ready (desktop / DAW / Mac), installed, both errors, and Update dimmed during a job.

## Decided by the owner

- The pill opens a "What's new" pop-up with notes and screenshots, instead of a web page.
- Layout C.
- Update downloads and installs from inside LISN.
- In FL, don't make the user close FL if there's any other way. Closing is acceptable only as the fallback.

## My calls (the owner can override)

- The notes and screenshots live in the GitHub release (`## What's new` + `whats-new-N.png`) rather than a file on lisn.digital.
- Mac uses Apple's Installer window rather than a silent install, since a silent install would need the admin password a different way. The desktop app quits when the Installer opens and the user reopens it; it doesn't relaunch by itself.
- Leaving the pop-up during a download keeps the download going, and the pill shows the percentage.
- The pill reads "Restart FL Studio to finish" after a DAW install.
- After a DAW install, new jobs in that FL session are blocked only when the engine actually changed.
- The "no job running" gate is per process, not across programs.
- The Mac in-DAW install is on the tester's checklist, with "quit FL first" as the fallback if it misbehaves.
- With no installer in the release, Update now falls back to the browser. A downloaded installer is reused if its hash still matches.

## Left to the builder

- How the Inno scripts do the rename and the reboot delete (Pascal `[Code]` before `ssInstall`, `RestartReplace`, `[InstallDelete]`). Also how the desktop installer relaunches LISN in silent mode (a `[Run]` entry for an update flag without `skipifsilent`).
- How LISN starts the installer (`/SILENT /SUPPRESSMSGBOXES` or similar) and waits on or reads its result for the DAW case.
- The markdown subset and the bullet extraction.
- Exact copy beyond the strings above, and the screenshot flip control.
- A way to test the whole flow before a real release, for example a setting that points the check at a given release tag or a local JSON file, so the owner can try it on this PC.
