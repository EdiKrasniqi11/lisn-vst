# LISN StemSplitter Desktop: the app shell and its installer

## Context

The user wants a public desktop app of LISN StemSplitter, with everything in `docs/superpowers/plans/2026-09-29-desktop-app.md`:
- splitting, the stem player, mute/solo, loop and drag-out (all built in the VST today);
- per-stem volume and speed;
- export: one stem, all stems as a .zip, or the mix as a .wav.

That plan predates the stem player, the installer, CI and the v0.1.0 release. Its separate `lisn-desktop` repo, `lisn_core` target, first-run engine screen and second CI are no longer needed. This repo already builds a desktop exe: `CMakeLists.txt` has `FORMATS VST3 Standalone`, which produces `build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe`.

The work is split into two sub-projects, each with its own spec, plan and implementation:
1. **This spec:** the installable app running today's UI, in its own per-user installer.
2. **Next spec:** per-stem volume, speed and export. It starts with one mockup covering all three, since they share the 760x500 screen. The new controls go in the VST too (one UI for both).

The public release (the next version tag, plus a Download button for the app on lisn-site) waits until both are done.

## Decisions (2026-10-01)

- **Same repo, JUCE's Standalone format plus a small custom app class.** It doesn't use a separate repo or a hand-written `juce_add_gui_app`.
- **A separate desktop installer, per-user, no admin.** It has its own AI engine in `%LOCALAPPDATA%\LISN\engine`, so someone with both apps installed has a second ~900 MB copy. The two uninstallers never depend on each other.
- **One release for both.** The VST and the app share `project(StemSplitter VERSION ...)` and one GitHub Release per tag.
- **Default output only.** There's no audio device picker; that stays in "later".
- **Fixed 760x500 window**, the same editor as the VST.

## The app

### Build (`CMakeLists.txt`)
- Add `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` to the plugin target's `PUBLIC` compile definitions, so the Standalone wrapper picks it up.
- Add `Source/StandaloneApp.cpp` to the plugin target only (`target_sources(StemSplitter PRIVATE ...)`), not to `LISN_SOURCES`, so `lisn_tests` doesn't compile it.
- Add `ICON_BIG Resources/icon.png` to `juce_add_plugin`. The icon is lisn-site's `assets/favicon.svg` (four stem-coloured bars on a dark rounded square) rendered once at 1024x1024 with transparent corners and committed. The exe, taskbar and Start menu then show the same mark as the site.

### `Source/StandaloneApp.cpp`
One file, about 80 lines, guarded by `#if JucePlugin_Build_Standalone`. It includes `juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h` and defines `juce::JUCEApplicationBase* juce_CreateApplication()`.

- **Settings file:** `PropertiesFile::Options` with `applicationName = JucePlugin_Name` and `filenameSuffix = ".settings"`, the same as JUCE's default wrapper. The file lands at `%APPDATA%\LISN StemSplitter\LISN StemSplitter.settings`.
- **Audio:** a `StandalonePluginHolder` with the channel layout `{ { 0, 2 } }`.
  - Only the default output device opens: no mic, no "input muted" banner, no audio settings dialog.
  - The processor keeps its stereo input bus. The player gets silence there and adds the stems on top, as it does in a DAW.
- **Window:** a `DocumentWindow` titled `LISN StemSplitter`.
  - It has the native title bar with minimise and close buttons, and isn't resizable.
  - Its content is `processor->createEditor()` at 760x500, centred on screen.
  - When the window first shows, the editor gets keyboard focus, so Space, L, 1-6, Shift+1-6 and Home work before any click.
- **Close:** `holder->savePluginState()`, then `quit()`.
  - Shutdown deletes the window (and so the editor) before the holder, which stops audio and deletes the processor.
  - The saved state is whatever `getStateInformation` writes today: the theme, 4 or 6 stems, the Python hint, the last song and its stem folder.
- **Start:** the holder's constructor restores the saved state (`reloadPluginState`) and starts audio. The window is created after that.
- **Single instance and "Open with":**
  - `moreThanOneInstanceAllowed()` returns false.
  - The startup command line and the string passed to `anotherInstanceStarted` go through one function. It splits the string into tokens with quotes respected (`StringArray::addTokens (cmd, true)`) and unquotes each token.
  - Exactly one token, an existing file the editor's `isInterestedInFileDrag` accepts, is handed to the editor's `filesDropped`: the same path as a drop on the window.
  - `anotherInstanceStarted` then brings the window to the front (`toFront (true)`), with or without a file.
  - Anything else on the command line is ignored.

### The VST
Behaviour is unchanged. `StandaloneApp.cpp` is compiled into the shared code, but only the Standalone exe calls `juce_CreateApplication`. pluginval `--strictness-level 5` must still pass, and the existing snapshots must come out identical.

## The engine

- **`installer/setup-engine.ps1` is reused unchanged**, run with `-EngineDir "%LOCALAPPDATA%\LISN\engine"`. It needs the same ~320 MB download, and the same `ready.txt` check makes a re-install skip a finished engine.
- **`installer/sitecustomize.py`:** only the comment changes. It says every Windows user shares the models, which holds for the ProgramData engine but not the per-user one. Reword it to cover both: the models live inside whichever engine runs, so uninstall removes them.
- **`PythonFinder`:** a new candidate 3 is inserted after the ProgramData engine (the later candidates move down by one):
  ```
  3. localAppData\LISN\engine\venv\Scripts\python.exe, the per-user engine the desktop installer sets up
  ```
  - With both apps installed, the app uses the VST's engine first.
  - If the VST is uninstalled, the saved hint stops existing and the search falls through to the app's own engine.
- **Stem cache:** both apps already use `%APPDATA%\StemSplitter\cache` (`SeparationJob::stemDirFor`), so a song split in one reopens instantly in the other. Nothing changes here.

## The desktop installer (`installer/lisn-desktop.iss`)

- **Output:** `installer\Output\LISN-StemSplitter-Desktop-Setup.exe`. That asset name never changes; the site will link `releases/latest/download/LISN-StemSplitter-Desktop-Setup.exe`.
- **Setup:**
  - `AppId={{93E72666-5647-4F9A-8152-9DA75AC91D0F}`. Never change it: it's how upgrades find the old install.
  - `AppName=LISN StemSplitter`.
  - `UninstallDisplayName=LISN StemSplitter (desktop app)`, so Settings > Apps tells it apart from the VST.
  - `AppVersion` comes from `/DAppVersion`, as in `lisn-vst.iss`.
  - `PrivilegesRequired=lowest`, with `DefaultDirName={autopf}\LISN StemSplitter`, which for a per-user install is `%LOCALAPPDATA%\Programs\LISN StemSplitter`.
  - The licence page, architecture and Windows 10+ settings, compression and wizard style are the same as `lisn-vst.iss`.
- **Files:**
  - the Release `LISN StemSplitter.exe` into `{app}`;
  - `uv.exe`, `setup-engine.ps1`, `requirements.txt` and `sitecustomize.py` into `{app}\engine-setup`;
  - `THIRD-PARTY-NOTICES.txt`, and `LICENSE` as `LICENSE.txt`;
  - `vc_redist.x64.exe` into `{tmp}`.
- **Tasks:**
  - `desktopicon`: "Create a desktop shortcut", unchecked.
  - `openwith`: "Add to Explorer's Open with menu for audio files", checked.
- **Icons:** a Start menu shortcut `{autoprograms}\LISN StemSplitter`, plus `{autodesktop}\LISN StemSplitter` when the desktop task is chosen. With `PrivilegesRequired=lowest`, these resolve to the user's own Start menu and desktop.
- **"Open with"** (`openwith` task, all under HKCU, removed on uninstall):
  - a ProgID `LISN.StemSplitter.Audio` whose `shell\open\command` is `"{app}\LISN StemSplitter.exe" "%1"` and whose `DefaultIcon` is the exe;
  - the ProgID listed under `Software\Classes\.<ext>\OpenWithProgids` for wav, mp3, flac, aif, aiff and ogg (the editor's `isInterestedInFileDrag` list).
  - It never sets a default app.
- **VC++ runtime:**
  - `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64` is compared with the bundled `vc_redist.x64.exe`'s file version.
  - If the key is missing, `Installed` isn't 1, or Major.Minor.Bld is older, the installer runs `vc_redist.x64.exe /install /quiet /norestart` with `ShellExec ('runas', ...)`: the one possible UAC prompt.
  - A declined prompt or a failure isn't fatal on its own. It shows up in the engine step, which loads torch's DLLs.
- **Engine step:** the same `CurStepChanged (ssPostInstall)` code as `lisn-vst.iss`:
  - a marquee bar, status text "Setting up the AI engine (about 320 MB download, a few minutes)...";
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{app}\engine-setup\setup-engine.ps1" -EngineDir "{localappdata}\LISN\engine"`.
  - On failure, the same message box, pointing at `{localappdata}\LISN\engine\setup.log`.
- **Finish page:** `[Run]` "Launch LISN StemSplitter" (`postinstall nowait skipifsilent`, checked).
- **Running app:** Inno's default `CloseApplications=yes` closes a running copy before files are replaced.
- **Uninstall** (`[UninstallDelete]`):
  - removes `{localappdata}\LISN\engine`, and `{localappdata}\LISN` if it's left empty;
  - removes `{userappdata}\LISN StemSplitter`, the app's settings folder.
  - The stem cache `%APPDATA%\StemSplitter\cache` stays, because the VST uses it too.

## CI and release (`.github/workflows/build.yml`)

- **Build:** the existing Build step already builds the Standalone exe; nothing changes there.
- **Installer step:** after `lisn-vst.iss`, also runs `ISCC /DAppVersion=... installer\lisn-desktop.iss`. It uses the same `uv.exe` and `vc_redist.x64.exe` the step already downloads.
- **Draft release:** attaches both installers, with both SHA-256s in the notes.
- **A VST-only tag before sub-project 2 ships** will also carry the desktop installer on its GitHub release page. That's accepted: the site doesn't link to it until the app is complete.

## Failure cases

| Case | What the user sees |
|---|---|
| Engine not ready (setup failed, UAC declined, offline) | The existing "Python isn't set up yet" screen, as in the VST. Running the installer again fixes it. |
| No output device | The app opens and splits; playback is silent. |
| A command-line argument that isn't one supported, existing audio file | Ignored. The app opens normally (or the open window comes to the front). |
| Second launch, no file | The existing window comes to the front; no second window. |
| App running during an upgrade or uninstall | Inno closes it first. |

## Testing

- **Unit (TDD):** `tests/PythonFinderTests.cpp` "candidate order" expects the per-user engine right after the ProgramData engine.
- **Regression:**
  - `ctest` passes;
  - pluginval `--strictness-level 5` on the VST3 prints SUCCESS;
  - `lisn_tests --snapshots snapshots` output is unchanged (no UI change in this spec).
- **Manual, dev PC:**
  1. The app opens on the Drop screen, with no banner and no Options button. Space plays before any click, once stems are loaded.
  2. Split a song, then play, mute, solo and loop. Drag a stem and the mix into an Explorer folder.
  3. Close and reopen the app: the theme and the stems are restored.
  4. Right-click a .mp3 in Explorer > Open with > LISN StemSplitter: it splits. With the app open, opening another file goes to the same window, and no second window appears.
  5. Run the desktop installer: no UAC prompt (the runtime is present on this PC), the engine builds into `%LOCALAPPDATA%\LISN\engine`, and the finish page launches the app.
  6. With `C:\ProgramData\LISN\engine` temporarily renamed (admin), a split runs on the per-user engine. Task Manager > python.exe > Open file location shows `%LOCALAPPDATA%\LISN\engine\...`. Rename it back afterwards.
  7. Uninstall from Settings > Apps > "LISN StemSplitter (desktop app)":
     - the app folder, the per-user engine, the settings folder and the Open with entries are gone;
     - the VST and its engine still work;
     - the stem cache is still there.
- **Release checklist (after sub-project 2):** a clean Windows PC or VM with no VC++ runtime and no Python. Install the app (one UAC prompt for the runtime), split, play, uninstall. Do this together with the VST's outstanding clean-machine test.

## Docs

- `docs/superpowers/plans/2026-09-29-desktop-app.md` gets a note under its title. It says the plan is superseded by this spec for the app shell, and that its volume, speed and export sections feed sub-project 2. Delete it once sub-project 2's spec exists.
- `README.md`: the Releases line says the workflow drafts the release "with both installers".

## Not in this spec

- **Sub-project 2:** per-stem volume, speed and export (mockup first).
- **Later:** an audio device picker, a resizable or scaled window, a Mac build, code signing and an in-app update check.
