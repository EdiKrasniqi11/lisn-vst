# LISN Desktop App Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** LISN, a desktop app built from this repo's Standalone target, ships in its own per-user installer (`LISN-Setup.exe`) with its own AI engine, next to the VST's installer in the same GitHub Release.

**Architecture:**
- **The app:** `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` makes JUCE's Standalone wrapper call our `juce_CreateApplication()` in `Source/StandaloneApp.cpp`. That app class reuses JUCE's `StandalonePluginHolder` (audio device and saved state) with an output-only channel layout. It shows the normal editor in a fixed native window and forwards "Open with" files to the editor's own drop handler.
- **The installer:** `installer/lisn-desktop.iss` installs it per-user, reusing `setup-engine.ps1` for an engine in `%LOCALAPPDATA%\LISN\engine`, and `PythonFinder` learns that path.
- **CI:** the release workflow attaches both installers to one release.

**Tech Stack:** JUCE 8.0.15 (CMake, MSVC 2022), Inno Setup 6 (per-user at `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`), Windows PowerShell 5.1, uv 0.12.21, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-01-desktop-app-design.md`

## Global Constraints

- **Names:**
  - The app is called **LISN**: window title, installed exe `LISN.exe`, Start menu, Settings > Apps, Open with and the installer.
  - The VST stays **LISN StemSplitter**. `PRODUCT_NAME "LISN StemSplitter"` in `CMakeLists.txt` does not change.
- **Fixed values:**
  - Installer asset: `LISN-Setup.exe`, always.
  - AppId: `{{93E72666-5647-4F9A-8152-9DA75AC91D0F}`, never changed.
  - Install dir: `%LOCALAPPDATA%\Programs\LISN` (Inno `{autopf}\LISN` with `PrivilegesRequired=lowest`).
  - Engine: `%LOCALAPPDATA%\LISN\engine` (Inno `{localappdata}\LISN\engine`).
  - Settings: `%APPDATA%\LISN\LISN.settings`.
- **Files that must not change:** `installer/sitecustomize.py`, `installer/requirements.txt` and `installer/setup-engine.ps1` stay byte-identical. `ready.txt` hashes the first two, so any edit rebuilds every user's engine.
- **The VST:** its behaviour doesn't change. pluginval `--strictness-level 5` must still print SUCCESS.
- **No UI changes:** no new controls, and the redesign spec's "Must hold" list (no ComboBox, TextEditor, PopupMenu or ProgressBar) stays true.
- **Version:** it lives only in `project(StemSplitter VERSION x.y.z)`.
- **Writing:** no em dashes anywhere: code comments, installer text, docs, commit messages.
- **Git:**
  - Stage explicit paths in every commit, never `git add .`.
  - Every commit message ends with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - All work happens in the worktree `C:\Projects\lisn-vst-desktop` on branch `feature/desktop-app`.
  - Pushes, merges, tags and releases each need the owner's go-ahead.
- **Build and test commands** (PowerShell, from the worktree root):
  ```powershell
  cmake --build build --config Release --parallel
  ctest --test-dir build -C Release --output-on-failure
  C:\Tools\pluginval\pluginval.exe --strictness-level 5 --validate "build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3"
  ```

## Files

- **Create:**
  - `Source/OpenWith.h`: `fileToOpen()`, the command-line parser.
  - `Source/StandaloneApp.cpp`: the LISN app class.
  - `tests/OpenWithTests.cpp`.
  - `Resources/icon.png`: 1024 px, rendered from lisn-site's favicon.
  - `installer/lisn-desktop.iss`.
- **Modify:**
  - `CMakeLists.txt`: the icon, the define, the new source and the new test.
  - `Source/PythonFinder.h` and `Source/PythonFinder.cpp`: candidate 3.
  - `tests/PythonFinderTests.cpp`.
  - `LICENSE`: one sentence names the app.
  - `.github/workflows/build.yml`: the desktop installer and both assets.
  - `README.md`.

---

### Task 0: Worktree and baseline

- [ ] **Step 1: Create the worktree.**
  ```powershell
  git -C C:/Projects/lisn-vst worktree add ../lisn-vst-desktop -b feature/desktop-app
  ```
  This keeps `C:\Projects\lisn-vst` on `master` for other sessions. Every later command runs in `C:\Projects\lisn-vst-desktop`.

- [ ] **Step 2: First build and baseline tests.** The configure fetches JUCE, which takes a few minutes the first time.
  ```powershell
  cmake -S . -B build -A x64
  cmake --build build --config Release --parallel
  ctest --test-dir build -C Release --output-on-failure
  ```
  Expected: `100% tests passed`, and `build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe` exists. If anything fails here, stop and report: the baseline must be green before any change.

### Task 1: PythonFinder tries the per-user engine (TDD)

**Files:** Modify `Source/PythonFinder.h`, `Source/PythonFinder.cpp:35-36`, `tests/PythonFinderTests.cpp`.

**Interfaces:** Produces candidate 3, `localAppData\LISN\engine\venv\Scripts\python.exe`. Task 3's installer creates that file.

- [ ] **Step 1: Write the failing test.** In `tests/PythonFinderTests.cpp`, "candidate order":
  - after `const auto engine = fakePython ("pd/LISN/engine/venv/Scripts");` add
    ```cpp
            const auto user   = fakePython ("lad/LISN/engine/venv/Scripts");
    ```
  - change the expectation line to
    ```cpp
            const juce::StringArray expected { pf311, engine, user, path1, path2, lad312, lad310, sd39, mini };
    ```
  "bare hint is dropped" stays as it is (`[0]` is still `engine`).

- [ ] **Step 2: Run it and watch it fail.**
  ```powershell
  cmake --build build --config Release --target lisn_tests
  build\lisn_tests_artefacts\Release\lisn_tests.exe
  ```
  Expected: a `!!! Test ... failed` line under "PythonFinder / candidate order". The actual list lacks the `lad\LISN\engine\venv\Scripts\python.exe` line, and the exit code is 1.

- [ ] **Step 3: Implement.**
  - In `Source/PythonFinder.h`, replace the candidate list comment with:
    ```cpp
    // python.exe paths to try, first to last, without duplicates (case-insensitive):
    // 1. hint, if it is an absolute path to an existing file (a bare name such as "python" is dropped:
    //    CreateProcess could resolve it to the Microsoft Store stub)
    // 2. programData\LISN\engine\venv\Scripts\python.exe, the private engine the LISN installer sets up
    // 3. localAppData\LISN\engine\venv\Scripts\python.exe, the per-user engine the LISN desktop installer sets up
    // 4. <dir>\python.exe for each absolute PATH entry (quotes removed) that exists, skipping any containing "WindowsApps"
    // 5. localAppData\Programs\Python\Python3*, programFiles\Python3*, systemDrive\Python3* (each newest first by the number after "Python")
    // 6. userProfile\anaconda3\python.exe, userProfile\miniconda3\python.exe
    ```
  - In `Source/PythonFinder.cpp`, right after the ProgramData engine block (the `if (dirs.programData != juce::File())` line and its `addIfFile`), add:
    ```cpp

        if (dirs.localAppData != juce::File())   // the per-user engine the LISN desktop installer sets up
            addIfFile (dirs.localAppData.getChildFile ("LISN\\engine\\venv\\Scripts\\python.exe"));
    ```

- [ ] **Step 4: Run it and watch it pass.**
  ```powershell
  cmake --build build --config Release
  ctest --test-dir build -C Release --output-on-failure
  ```
  Expected: `100% tests passed`.

- [ ] **Step 5: Commit.**
  ```powershell
  git add Source/PythonFinder.h Source/PythonFinder.cpp tests/PythonFinderTests.cpp
  git commit -m "feat: PythonFinder tries the desktop app's per-user engine right after the ProgramData one" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 2: The LISN app

**Files:**
- Create `Source/OpenWith.h`, `tests/OpenWithTests.cpp`, `Source/StandaloneApp.cpp`, `Resources/icon.png`.
- Modify `CMakeLists.txt`.

**Interfaces:**
- **Produces:**
  - `juce::File fileToOpen (const juce::String& commandLine)` in `Source/OpenWith.h`;
  - the exe `build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe`, which Task 3 installs as `LISN.exe` and which accepts one audio file path as its argument;
  - the settings file `%APPDATA%\LISN\LISN.settings`, which Task 3's uninstaller deletes.
- **Consumes:**
  - `StemSplitterEditor`'s public `juce::FileDragAndDropTarget` overrides `isInterestedInFileDrag` and `filesDropped` (`Source/PluginEditor.h:26-29`);
  - JUCE's `juce::StandalonePluginHolder`: its constructor `(PropertySet*, bool takeOwnership, const String& preferredDefaultDeviceName, const AudioDeviceManager::AudioDeviceSetup*, const Array<PluginInOuts>&, bool autoOpenMidi)`, its public `processor` member and `savePluginState()`.

- [ ] **Step 1: Write the failing parser test.** `tests/OpenWithTests.cpp`:
  ```cpp
  #include "../Source/OpenWith.h"

  struct OpenWithTests : juce::UnitTest
  {
      OpenWithTests() : juce::UnitTest ("OpenWith") {}

      void runTest() override
      {
          beginTest ("one absolute path, quoted or not");
          expectEquals (fileToOpen ("\"C:\\My Music\\a b.mp3\"").getFullPathName(), juce::String ("C:\\My Music\\a b.mp3"));
          expectEquals (fileToOpen ("  C:\\x.wav ").getFullPathName(), juce::String ("C:\\x.wav"));

          beginTest ("anything else gives no file");
          expect (fileToOpen ({}) == juce::File());
          expect (fileToOpen ("song.mp3") == juce::File());            // relative
          expect (fileToOpen ("--version") == juce::File());
          expect (fileToOpen ("C:\\a.wav C:\\b.wav") == juce::File()); // two files
      }
  };

  static OpenWithTests openWithTests;
  ```
  In `CMakeLists.txt`, add `tests/OpenWithTests.cpp` to `target_sources(lisn_tests PRIVATE ...)`, after `tests/UiStateTests.cpp`.

- [ ] **Step 2: Run it and watch it fail.**
  ```powershell
  cmake --build build --config Release --target lisn_tests
  ```
  Expected: compile error C1083, cannot open include file `../Source/OpenWith.h`.

- [ ] **Step 3: Implement the parser.** `Source/OpenWith.h`:
  ```cpp
  #pragma once
  #include <juce_core/juce_core.h>

  // The one absolute path a command line names: Explorer's "Open with" passes "C:\My Music\song.mp3" (quoted).
  // No argument, several, or a relative path gives File(). The caller still checks the file exists and is audio.
  inline juce::File fileToOpen (const juce::String& commandLine)
  {
      juce::StringArray args;
      args.addTokens (commandLine, true);   // whitespace-separated, quoted paths kept whole
      args.removeEmptyStrings();
      const auto path = args[0].unquoted();
      return args.size() == 1 && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
  }
  ```

- [ ] **Step 4: Run it and watch it pass.**
  ```powershell
  cmake --build build --config Release --target lisn_tests
  ctest --test-dir build -C Release --output-on-failure
  ```
  Expected: `100% tests passed`.

- [ ] **Step 5: Render the app icon.** It's lisn-site's favicon at 1024 px with transparent corners:
  ```powershell
  $edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
  $html = Join-Path $env:TEMP 'lisn-icon.html'
  Set-Content $html -Encoding Ascii -Value '<!doctype html><body style="margin:0;background:transparent"><img src="file:///C:/Projects/lisn-site/assets/favicon.svg" width="1024" height="1024" style="display:block"></body>'
  Start-Process $edge -Wait -ArgumentList @('--headless', '--disable-gpu', '--hide-scrollbars', '--default-background-color=00000000',
      '--window-size=1024,1024', "--user-data-dir=$env:TEMP\lisn-edge", "--screenshot=$PWD\Resources\icon.png", "file:///$($html -replace '\\', '/')")
  Add-Type -AssemblyName System.Drawing
  $b = [System.Drawing.Bitmap]::FromFile("$PWD\Resources\icon.png"); "$($b.Width)x$($b.Height) corner=$($b.GetPixel(2,2).A) middle=$($b.GetPixel(512,512).Name)"; $b.Dispose()
  ```
  - **Expected:** `1024x1024 corner=0 middle=ff2b1822`, meaning a transparent corner and the dark square between the bars.
  - **If the corner is 255** (white, not transparent), run the same `Start-Process` with `'--headless=old'` in place of `'--headless'`, and check again.
  - Then open `Resources\icon.png` and look at it: four rounded bars (orange, red, mauve, grey-blue) on a dark rounded square.
  - Finally: `Remove-Item -Recurse -Force $html, "$env:TEMP\lisn-edge"`.

- [ ] **Step 6: The app class.** `Source/StandaloneApp.cpp`:
  ```cpp
  // LISN, the desktop app: the plugin's editor in its own window, playing through the default output.
  // CMakeLists.txt sets JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP, so JUCE's Standalone wrapper calls juce_CreateApplication()
  // below instead of using its own app class (which has an Options menu, an "input muted" banner and no single instance).
  #if JucePlugin_Build_Standalone

  #include <juce_audio_utils/juce_audio_utils.h>
  #include <juce_audio_plugin_client/juce_audio_plugin_client.h>
  #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
  #include "OpenWith.h"

  namespace
  {
  class MainWindow : public juce::DocumentWindow
  {
  public:
      MainWindow (const juce::String& name, juce::AudioProcessorEditor* editor)
          : DocumentWindow (name, juce::Colours::black, minimiseButton | closeButton)
      {
          setUsingNativeTitleBar (true);
          setResizable (false, false);
          setContentOwned (editor, true);   // the window deletes the editor, before the holder deletes the processor
          centreWithSize (getWidth(), getHeight());
      }

      void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
  };

  class LisnApp : public juce::JUCEApplication
  {
  public:
      const juce::String getApplicationName() override    { return "LISN"; }
      const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
      bool moreThanOneInstanceAllowed() override          { return false; }

      void initialise (const juce::String& commandLine) override
      {
          juce::PropertiesFile::Options options;
          options.applicationName = "LISN";        // %APPDATA%\LISN\LISN.settings
          options.filenameSuffix  = ".settings";
          settings.setStorageParameters (options);

          // Output only: no mic, no "input muted" banner. The processor's input bus just gets silence.
          const juce::StandalonePluginHolder::PluginInOuts outputOnly[] { { 0, 2 } };
          holder = std::make_unique<juce::StandalonePluginHolder> (settings.getUserSettings(), false, juce::String(), nullptr,
                                                                   juce::Array<juce::StandalonePluginHolder::PluginInOuts> (outputOnly, 1));

          window = std::make_unique<MainWindow> (getApplicationName(), holder->processor->createEditorIfNeeded());
          window->setVisible (true);
          window->getContentComponent()->grabKeyboardFocus();   // Space and the other keys work before any click
          open (commandLine);
      }

      void anotherInstanceStarted (const juce::String& commandLine) override
      {
          window->toFront (true);
          open (commandLine);
      }

      void systemRequestedQuit() override
      {
          holder->savePluginState();   // theme, 4 or 6 stems, the last song and its stems
          quit();
      }

      void shutdown() override
      {
          window = nullptr;   // the editor goes before the processor
          holder = nullptr;
          settings.saveIfNeeded();
      }

  private:
      void open (const juce::String& commandLine)   // a file from "Open with" goes the same way as a drop on the window
      {
          const auto file = fileToOpen (commandLine);
          const juce::StringArray files { file.getFullPathName() };
          auto* target = dynamic_cast<juce::FileDragAndDropTarget*> (window->getContentComponent());
          if (file.existsAsFile() && target != nullptr && target->isInterestedInFileDrag (files))
              target->filesDropped (files, 0, 0);
      }

      juce::ApplicationProperties settings;
      std::unique_ptr<juce::StandalonePluginHolder> holder;
      std::unique_ptr<MainWindow> window;
  };
  } // namespace

  juce::JUCEApplicationBase* juce_CreateApplication() { return new LisnApp(); }

  #endif
  ```

- [ ] **Step 7: Wire it into the build.** In `CMakeLists.txt`:
  - in `juce_add_plugin(StemSplitter ...)`, after `PRODUCT_NAME "LISN StemSplitter"`, add
    ```cmake
        ICON_BIG Resources/icon.png
    ```
  - replace `target_sources(StemSplitter PRIVATE ${LISN_SOURCES})` with
    ```cmake
    # Source/StandaloneApp.cpp is LISN, the desktop app: JUCE's Standalone wrapper runs it instead of its own app class.
    target_sources(StemSplitter PRIVATE ${LISN_SOURCES} Source/StandaloneApp.cpp)
    ```
  - in `target_compile_definitions(StemSplitter PUBLIC ...)`, add `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` after `JUCE_VST3_CAN_REPLACE_VST2=0`.

  `ICON_BIG` also gives the VST3 bundle folder this icon in Explorer (JUCE writes `Plugin.ico` and `desktop.ini` into it). That's expected, and harmless.

- [ ] **Step 8: Build and run the regression checks.**
  ```powershell
  cmake --build build --config Release --parallel
  ctest --test-dir build -C Release --output-on-failure
  C:\Tools\pluginval\pluginval.exe --strictness-level 5 --validate "build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3"
  ```
  - **Expected:** no errors (warnings from JUCE headers are fine), `100% tests passed`, and pluginval ends in `SUCCESS`.
  - **If `StandaloneApp.cpp` doesn't compile:** compare its includes with the top of `build\_deps\juce-src\modules\juce_audio_plugin_client\juce_audio_plugin_client_Standalone.cpp`, which includes the same window header, and fix in the smallest way.
  - **Snapshots aren't re-run:** `lisn_tests` compiles none of the files this task touches except the new test.

- [ ] **Step 9: Scripted checks of the exe.**
  ```powershell
  $exe  = "$PWD\build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe"
  $song = (Get-ChildItem "$env:USERPROFILE\Music", "$env:USERPROFILE\Downloads" -Recurse -Include *.mp3, *.wav, *.flac -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
  $song
  Start-Process $exe; Start-Sleep 3
  Get-Process 'LISN StemSplitter' | Select-Object Id, MainWindowTitle
  (Get-Process 'LISN StemSplitter').CloseMainWindow() | Out-Null; Start-Sleep 2
  Get-Process 'LISN StemSplitter' -ErrorAction SilentlyContinue
  Select-String -Path "$env:APPDATA\LISN\LISN.settings" -Pattern filterState -Quiet
  Start-Process $exe; Start-Sleep 3
  Start-Process $exe -ArgumentList "`"$song`""; Start-Sleep 3
  (Get-Process 'LISN StemSplitter').Count
  ```
  Expected, in order:
  - a song path is printed (if it's empty, ask the owner for any song and set `$song` to it);
  - one process titled `LISN`;
  - after the close, no process, and `True` for the settings file;
  - after the second launch with the song, the count is `1`, and the open window shows the Separating screen, or the stems if that song was split before.

  Leave it open for Step 10.

- [ ] **Step 10: Owner checks** (hand the app to the owner, wait for their answers):
  1. The title bar is the native Windows one with only minimise and close. There's no Options button and no "input muted" banner. The taskbar shows the four-bar icon.
  2. Once the split finishes: play, mute a row, right-click a light to solo, drag across the waves to loop. Drag a stem chip and the Drag mix chip into an Explorer folder; WAV files appear.
  3. Switch the theme, close the app, reopen it from the exe: the theme and the stems come back. Without clicking anything, press Space: it plays.

  Any "no" is a bug to fix in this task before committing.

- [ ] **Step 11: Commit.**
  ```powershell
  git add Source/OpenWith.h Source/StandaloneApp.cpp tests/OpenWithTests.cpp Resources/icon.png CMakeLists.txt
  git commit -m "feat: LISN desktop app: output-only standalone window, single instance, Open with, saved state and the four-bar icon" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 3: The desktop installer

**Files:** Create `installer/lisn-desktop.iss`. Modify `LICENSE`.

**Interfaces:**
- **Consumes:**
  - `build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe` (Task 2);
  - `installer\setup-engine.ps1 -EngineDir <dir>` (exit 0 = ready, log in `<dir>\setup.log`);
  - `installer\uv.exe`, `installer\vc_redist.x64.exe`, `installer\requirements.txt`, `installer\sitecustomize.py`, `installer\THIRD-PARTY-NOTICES.txt` and `LICENSE`.
- **Produces:** `installer\Output\LISN-Setup.exe`. `ISCC /DAppVersion=x.y.z` sets the version, and Task 4's CI calls exactly this.

- [ ] **Step 1: Fetch the installer's binaries** (gitignored, not committed):
  ```powershell
  Invoke-WebRequest https://github.com/astral-sh/uv/releases/download/0.12.21/uv-x86_64-pc-windows-msvc.zip -OutFile $env:TEMP\uv.zip
  if ((Get-FileHash $env:TEMP\uv.zip).Hash -ne '5d223efa0bf00208c3853246af09420419dfbd352536aa6bb8163d6170e23890') { throw 'uv checksum mismatch' }
  Expand-Archive $env:TEMP\uv.zip -DestinationPath $env:TEMP\uv -Force
  Copy-Item $env:TEMP\uv\uv.exe installer\
  Invoke-WebRequest https://aka.ms/vs/17/release/vc_redist.x64.exe -OutFile installer\vc_redist.x64.exe
  git status --short installer
  ```
  Expected: `git status` prints nothing, because both files are ignored.

- [ ] **Step 2: Name the app in the licence.** In `LICENSE`, replace the paragraph
  ```text
  The LISN StemSplitter plugin and installer are free to download and use,
  including for commercial music production. You may not sell, rent, or
  redistribute them; point people to the official download page instead.
  ```
  with
  ```text
  The LISN StemSplitter plugin, the LISN desktop app and their installers are
  free to download and use, including for commercial music production. You
  may not sell, rent, or redistribute them; point people to the official
  download page instead.
  ```

- [ ] **Step 3: Write `installer/lisn-desktop.iss`:**
  ```iss
  ; LISN, the desktop app. Per-user, no admin (one UAC prompt only if the VC++ runtime is missing or old).
  ; From the repo root, after a Release build, with uv.exe and vc_redist.x64.exe in installer\ (see build.yml):
  ;   ISCC /DAppVersion=0.1.0 installer\lisn-desktop.iss
  #ifndef AppVersion
    #define AppVersion "0.0.0"
  #endif
  #define Engine "{localappdata}\LISN\engine"

  [Setup]
  ; Never change the AppId: it's how an upgrade finds the old install.
  AppId={{93E72666-5647-4F9A-8152-9DA75AC91D0F}
  AppName=LISN
  AppVersion={#AppVersion}
  AppPublisher=LISN
  AppPublisherURL=https://edikrasniqi11.github.io/lisn-site/
  AppSupportURL=https://github.com/EdiKrasniqi11/lisn-vst/issues
  DefaultDirName={autopf}\LISN
  DisableDirPage=yes
  DisableProgramGroupPage=yes
  PrivilegesRequired=lowest
  ArchitecturesAllowed=x64compatible
  ArchitecturesInstallIn64BitMode=x64compatible
  MinVersion=10.0
  LicenseFile=..\LICENSE
  OutputDir=Output
  OutputBaseFilename=LISN-Setup
  Compression=lzma2
  SolidCompression=yes
  WizardStyle=modern
  UninstallDisplayName=LISN
  UninstallDisplayIcon={app}\LISN.exe

  [Tasks]
  Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
  Name: "openwith"; Description: "Add LISN to Explorer's Open with menu for audio files"

  [Files]
  Source: "..\build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe"; DestDir: "{app}"; DestName: "LISN.exe"; Flags: ignoreversion
  Source: "uv.exe"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
  Source: "setup-engine.ps1"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
  Source: "requirements.txt"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
  Source: "sitecustomize.py"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
  Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
  Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
  Source: "vc_redist.x64.exe"; DestDir: "{tmp}"

  [Icons]
  Name: "{autoprograms}\LISN"; Filename: "{app}\LISN.exe"
  Name: "{autodesktop}\LISN"; Filename: "{app}\LISN.exe"; Tasks: desktopicon

  [Registry]
  ; Open with for the audio types the app accepts, never the default app. FriendlyAppName makes the menu say "LISN"
  ; (the exe's version resource says "LISN StemSplitter", shared with the VST).
  Root: HKCU; Subkey: "Software\Classes\LISN.Audio"; ValueType: string; ValueName: ""; ValueData: "Audio file"; Flags: uninsdeletekey; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\LISN.Audio\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\LISN.exe"",0"; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\LISN.Audio\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "LISN"; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\LISN.Audio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\LISN.exe"" ""%1"""; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\Applications\LISN.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "LISN"; Flags: uninsdeletekey; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\.wav\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\.flac\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\.aif\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\.aiff\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
  Root: HKCU; Subkey: "Software\Classes\.ogg\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith

  [Run]
  Filename: "{app}\LISN.exe"; Description: "Launch LISN"; Flags: postinstall nowait skipifsilent

  [UninstallDelete]
  Type: filesandordirs; Name: "{#Engine}"
  Type: dirifempty; Name: "{localappdata}\LISN"
  ; Only the app's own settings file: another LISN product may share the folder one day.
  Type: files; Name: "{userappdata}\LISN\LISN.settings"
  Type: dirifempty; Name: "{userappdata}\LISN"

  [Code]
  // True when the x64 VC++ runtime is at least as new as the bundled redist (64-bit registry view, as Microsoft documents).
  function VCRuntimeIsCurrent(): Boolean;
  var
    Key: String;
    Installed, Major, Minor, Bld, MS, LS: Cardinal;
  begin
    Result := False;
    Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
    if not (RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) and (Installed = 1)
            and RegQueryDWordValue(HKLM64, Key, 'Major', Major)
            and RegQueryDWordValue(HKLM64, Key, 'Minor', Minor)
            and RegQueryDWordValue(HKLM64, Key, 'Bld', Bld)) then
      Exit;
    if not GetVersionNumbers(ExpandConstant('{tmp}\vc_redist.x64.exe'), MS, LS) then
      Exit;
    Result := (Major > (MS shr 16)) or ((Major = (MS shr 16)) and
              ((Minor > (MS and $FFFF)) or ((Minor = (MS and $FFFF)) and (Bld >= (LS shr 16)))));
  end;

  procedure CurStepChanged(CurStep: TSetupStep);
  var
    ResultCode: Integer;
  begin
    if CurStep <> ssPostInstall then
      Exit;

    if VCRuntimeIsCurrent() then
      Log('VC++ runtime is current, no admin prompt')
    else
    begin
      WizardForm.StatusLabel.Caption := 'Installing the Microsoft Visual C++ runtime...';
      // The one admin prompt. Declined or failed shows up below: the engine check loads torch's DLLs.
      ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    end;

    WizardForm.StatusLabel.Caption := 'Setting up the AI engine (about 320 MB download, a few minutes)...';
    WizardForm.ProgressGauge.Style := npbstMarquee;
    if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
                ExpandConstant('-NoProfile -ExecutionPolicy Bypass -File "{app}\engine-setup\setup-engine.ps1" -EngineDir "{#Engine}"'),
                '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
      MsgBox('LISN is installed, but its AI engine could not be set up.' + #13#10#13#10 +
             'Check your internet connection and run this installer again.' + #13#10 +
             'Details: ' + ExpandConstant('{#Engine}\setup.log'), mbError, MB_OK);
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;
  ```

- [ ] **Step 4: Build it.**
  ```powershell
  & "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 installer\lisn-desktop.iss
  ```
  Expected: `Successful compile`, and `installer\Output\LISN-Setup.exe` (about 50 MB, mostly `vc_redist` and `uv`). Fix any compile error it reports at the line it names.

- [ ] **Step 5: Silent install and checks** (scripted; the engine build takes about 5-7 min):
  ```powershell
  Start-Process installer\Output\LISN-Setup.exe -Wait -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$env:TEMP\lisn-setup.log"
  Test-Path "$env:LOCALAPPDATA\Programs\LISN\LISN.exe"
  Get-Content "$env:LOCALAPPDATA\LISN\engine\ready.txt"
  Test-Path "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\LISN.lnk"
  reg query HKCU\Software\Classes\.mp3\OpenWithProgids /v LISN.Audio
  Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{93E72666-5647-4F9A-8152-9DA75AC91D0F}_is1' | Select-Object DisplayName, DisplayVersion
  Select-String -Path $env:TEMP\lisn-setup.log -Pattern 'VC\+\+ runtime is current' -Quiet
  & "$env:LOCALAPPDATA\LISN\engine\venv\Scripts\python.exe" -m demucs -n htdemucs -o $env:TEMP\lisn-split-test $song
  Get-ChildItem $env:TEMP\lisn-split-test -Recurse -Filter *.wav | Measure-Object | Select-Object -ExpandProperty Count
  Remove-Item -Recurse -Force $env:TEMP\lisn-split-test
  ```
  Expected, in order:
  - `True`;
  - a hash line;
  - `True`;
  - the `LISN.Audio` value;
  - `DisplayName LISN, DisplayVersion 0.1.0`;
  - `True` (this PC's runtime is current, so the installer skipped it and asked for no admin);
  - the split runs offline on the per-user engine and writes `4` WAVs.

  (`$song` is from Task 2 Step 9; set it again if this is a new shell.)

- [ ] **Step 6: Owner checks** (hand over, wait for answers):
  1. Open LISN from the Start menu, then run `installer\Output\LISN-Setup.exe` normally while it's open. The wizard shows the licence (now naming the desktop app) and the two tasks. It offers to close LISN, and it finishes in seconds because the engine is unchanged. Launch LISN from the finish page.
  2. Right-click a .mp3 in Explorer > Open with: the entry reads "LISN" with the four-bar icon, and choosing it splits in the open window.
  3. Admin PowerShell: `Rename-Item C:\ProgramData\LISN\engine engine.off`. In LISN, drop a song that hasn't been split before. While it splits, Task Manager > Details > python.exe > Open file location shows `%LOCALAPPDATA%\LISN\engine\...`. Then `Rename-Item C:\ProgramData\LISN\engine.off engine`.

- [ ] **Step 7: Uninstall and checks** (scripted):
  ```powershell
  Start-Process "$env:LOCALAPPDATA\Programs\LISN\unins000.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES'
  while (Test-Path "$env:LOCALAPPDATA\Programs\LISN\unins000.exe") { Start-Sleep 1 }
  Test-Path "$env:LOCALAPPDATA\Programs\LISN", "$env:LOCALAPPDATA\LISN\engine", "$env:APPDATA\LISN\LISN.settings", 'HKCU:\Software\Classes\LISN.Audio', 'HKCU:\Software\Classes\Applications\LISN.exe'
  (Get-ItemProperty HKCU:\Software\Classes\.mp3\OpenWithProgids -ErrorAction SilentlyContinue).'LISN.Audio' -eq $null
  Test-Path "$env:APPDATA\StemSplitter\cache", 'C:\ProgramData\LISN\engine\ready.txt'
  ```
  Expected: five `False`, then `True`, then `True` `True`. The app, engine, settings and Open with entries are gone; the stem cache and the VST's engine stay.

- [ ] **Step 8: Commit.**
  ```powershell
  git add installer/lisn-desktop.iss LICENSE
  git commit -m "feat: LISN-Setup.exe, a per-user installer with its own engine, Open with and the VC++ runtime only when needed" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

### Task 4: CI attaches both installers; README

**Files:** Modify `.github/workflows/build.yml` (the Installer and Draft release steps) and `README.md`.

**Interfaces:**
- **Consumes:** `installer\lisn-desktop.iss`, which takes `/DAppVersion=x.y.z` and outputs `installer\Output\LISN-Setup.exe`.
- **Produces:** every `v*` tag's draft release has `LISN-StemSplitter-Setup.exe` and `LISN-Setup.exe`.

- [ ] **Step 1: The Installer step.** In `.github/workflows/build.yml`, after the existing two lines
  ```yaml
            & "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" "/DAppVersion=$('${{ github.ref_name }}'.TrimStart('v'))" installer\lisn-vst.iss
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
  ```
  add
  ```yaml
            & "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" "/DAppVersion=$('${{ github.ref_name }}'.TrimStart('v'))" installer\lisn-desktop.iss
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
  ```

- [ ] **Step 2: The Draft release step.** Replace its `run:` block with:
  ```yaml
          run: |
            $vst = 'installer\Output\LISN-StemSplitter-Setup.exe'
            $app = 'installer\Output\LISN-Setup.exe'
            $notes = "LISN-StemSplitter-Setup.exe is the VST3 for FL Studio and Ableton Live. SHA-256: $((Get-FileHash $vst).Hash.ToLower())`n`n" +
                     "LISN-Setup.exe is LISN, the desktop app. SHA-256: $((Get-FileHash $app).Hash.ToLower())"
            gh release create '${{ github.ref_name }}' $vst $app --draft --title 'LISN StemSplitter ${{ github.ref_name }}' --notes $notes
  ```

- [ ] **Step 3: README.**
  - After the first paragraph ("A VST3 for Windows that splits a song..."), add:
    ```markdown
    The same code builds **LISN**, a desktop app that does the same without a DAW: `Source/StandaloneApp.cpp`, installed by `installer/lisn-desktop.iss`.
    ```
  - Change the last line's "drafts the GitHub Release with the installer." to "drafts the GitHub Release with both installers: `LISN-StemSplitter-Setup.exe` (the VST3) and `LISN-Setup.exe` (the desktop app)."

- [ ] **Step 4: Commit.**
  ```powershell
  git add .github/workflows/build.yml README.md
  git commit -m "ci: a tagged release carries both installers; README names the desktop app" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  ```

- [ ] **Step 5 (owner OK): push and watch CI.**
  ```powershell
  git push -u origin feature/desktop-app
  gh run watch
  ```
  Expected: the "build" run goes green: build, unit tests and pluginval pass, and the four release steps are skipped (no tag). The two ISCC lines are exactly what Task 3 Step 4 ran locally. The release steps themselves run at the next tag, which waits for sub-project 2.

- [ ] **Step 6:** finish with superpowers:finishing-a-development-branch. Merging needs the owner's OK.

## Verification summary

| Task | Done when |
|---|---|
| 0 | the worktree builds and ctest passes before any change |
| 1 | PythonFinder test fails, then passes with the per-user engine as candidate 3 |
| 2 | OpenWith tests pass; pluginval SUCCESS; the exe is titled LISN, saves state, forwards a second launch's file; the owner's three checks pass |
| 3 | `LISN-Setup.exe` builds; silent install and checks pass; the owner's Open with, upgrade-while-open and engine-fallback checks pass; uninstall leaves only the shared cache and the VST's engine |
| 4 | CI green on the pushed branch |

## After this plan

- **Sub-project 2:** per-stem volume, speed and export, plus the app's "LISN" header. It starts with a mockup and gets its own spec. Delete `docs/superpowers/plans/2026-09-29-desktop-app.md` once that spec exists.
- **Release:** bump the version and tag. Run the clean-PC test of both installers (no VC++ runtime, no Python). Publish, then add a Download button to lisn-site linking `https://github.com/EdiKrasniqi11/lisn-vst/releases/latest/download/LISN-Setup.exe`.
