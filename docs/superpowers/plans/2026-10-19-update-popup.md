# In-app Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clicking the Update pill shows a "What's new" panel (layout C) with notes and screenshots from the GitHub release. Update now downloads the installer inside LISN and installs it. On Windows, FL stays open.

**Architecture:**
- **`ReleaseInfo`** is pure parsing of GitHub's `releases/latest` JSON: notes, screenshots, and this platform's installer.
- **`UpdateCheck`**, the existing daily check, now keeps the whole release, not just the tag.
- **`Updater`** is a per-process singleton state machine: download, then install, then installed or failed. It has injectable download and launch functions for tests.
- **`UpdateScreen`** draws layout C in the panel, in place of the screen, the way `HelpSheet` does.
- **The Inno scripts** learn the rename trick (VST) and relaunch after a silent install (desktop).

**Tech Stack:** JUCE 8 C++ (juce::URL, juce::JSON, juce::Thread), Win32 `ShellExecuteExW`, Inno Setup 6 Pascal script, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-19-update-popup-design.md`

## Global Constraints

- Branch `feature/mac-release`. Never push to master.
- Builds slow the owner's PC: build only the `StemSplitter_VST3`, `StemSplitter_Standalone` and `StemSplitterTests` targets, with `/nodeReuse:false`.
- **No network in tests.** Every download and launch goes through `Updater`'s injectable functions.
- **Threads:** network and disk work happens off the message thread, and the UI only reads `Updater` state. `UpdateCheck` already follows this.
- **Asset names, verbatim:**
  - Installers: `LISN-Setup.exe` (desktop, Windows), `LISN-StemSplitter-Setup.exe` (VST, Windows), `LISN-Mac.pkg` (Mac, both).
  - Screenshots: `whats-new-1.png`, `whats-new-2.png`, `whats-new-3.png`. PNG or JPEG, sorted by name, at most 3.
- **Notes:** only the `- ` bullets under the heading `## What's new`, at most 6.
  - With more than 6, append `…and more`, which links to the release page (`html_url`).
  - With none, the notes are the one line `Fixes and improvements.`
- **Copy, verbatim:**
  - Downloading: `<n>% · keep making music, it downloads in the background`
  - Dimmed Update now: `Finish the current split first`
  - Errors: `The download didn't finish.` / `The update didn't install.`, each with buttons `Try again` and `Download from the website`
  - After a DAW install: `Installed. LISN <v> starts the next time you open <host>.`
  - Mac, Installer opened: `Finish in the Installer. LISN <v> starts the next time you open <host>.`
  - Pill after a DAW install: `Restart <host> to finish`
  - Rename refused: `Close <host>, then click Install`
  - Jobs blocked after an engine change: `Restart <host> to finish the update`
- **`<host>`:** `FL Studio` when `juce::PluginHostType().isFruityLoops()`, `Ableton Live` when `isAbletonLive()`, otherwise `your DAW`.
- **Download location:** `juce::File::getSpecialLocation (tempDirectory)/LISN-update/<asset name>`. Deleted after a successful install or a Cancel. Reused on the next launch if its SHA-256 still matches.
- **Hash check:** if the asset's `digest` is `sha256:<hex>`, the download must match it. If there's no digest, it isn't checked (the download is HTTPS from GitHub).
- **Inno exit codes LISN reads:**
  - `0` installed
  - `7` the plugin file couldn't be moved aside (PrepareToInstall failed)
  - `101` installed, but the AI engine setup failed
  - Windows' UAC decline: `ShellExecuteExW` fails with `ERROR_CANCELLED` (1223)
  - anything else: didn't install

## Review Focus

1. **Two LISN windows open in one FL** (two instances): both pills show one shared download. Pressing Update in one doesn't start a second download. Test in Task 3.
2. **The pop-up opened while a split runs:** Update now is dimmed. When the job ends it becomes clickable without reopening the pop-up. Test in Task 6.
3. **Cancel mid-download, then Update again:** it starts cleanly from 0% with no `.part` left behind. Test in Task 3.
4. **A release JSON with odd notes** (CRLF line endings, `*` bullets, no heading, a heading in another case, emoji): no crash. The bullets come out as expected, or the fallback line. Test in Task 1.
5. **The editor closes (FL closes the plugin window) during a download:** the download carries on. Reopening the editor shows the current percentage, and nothing dangles. Test in Task 3 (state outlives listeners).

---

### Task 1: ReleaseInfo, parsing a GitHub release

**Files:**
- Create: `Source/ReleaseInfo.h`, `Source/ReleaseInfo.cpp`
- Create: `tests/ReleaseInfoTests.cpp`
- Modify: `CMakeLists.txt`: add `Source/ReleaseInfo.cpp` to the plugin sources and the test target, and `tests/ReleaseInfoTests.cpp` to the test list.

**Interfaces:**
- Produces:
```cpp
struct ReleaseInfo
{
    juce::String version;          // "0.3.1" (tag without the v)
    juce::String pageUrl;          // html_url
    juce::StringArray notes;       // bullets, inline **bold** kept as-is for the screen to draw; never empty
    bool moreNotes = false;        // more than 6 bullets were cut
    juce::StringArray screenshots; // browser_download_url of whats-new-N.(png|jpg|jpeg), name order, at most 3
    juce::String installerUrl;     // empty when the release has no asset with that name
    juce::int64 installerSize = 0;
    juce::String installerSha256;  // lower-case hex from digest "sha256:…", or empty
    bool isValid() const { return version.isNotEmpty(); }
};
ReleaseInfo parseRelease (const juce::var& json, const juce::String& installerName);
juce::StringArray whatsNewBullets (const juce::String& body, bool& more);   // the notes rule in Global Constraints
juce::String installerAssetName (bool standalone);   // Mac: "LISN-Mac.pkg"; Windows: per the constraints
juce::String hostName();                              // "FL Studio" / "Ableton Live" / "your DAW"
```

- [ ] **Step 1: Write the failing tests** in `tests/ReleaseInfoTests.cpp` (a `juce::UnitTest` named "ReleaseInfo", like `UpdateCheckTests`):
  - `whatsNewBullets ("intro\r\n## What's new\r\n- **Harmonies**: split\r\n* Faster\r\n\r\n## SHA\r\n- not me", more)` returns `{"**Harmonies**: split", "Faster"}`, with `more == false`.
  - A heading `## WHAT'S NEW` matches, because the comparison ignores case.
  - With 8 bullets, it returns 6 and sets `more == true`.
  - A body with no heading returns `{"Fixes and improvements."}`. A heading followed by no bullets returns the same.
  - `parseRelease` on a JSON literal with `tag_name "v0.3.1"`, `html_url`, `body`, and assets `whats-new-2.png`, `whats-new-1.jpg`, `whats-new-4.png`, `whats-new-3.png`, `other.png`, `LISN-Setup.exe` (`size 31000000`, `digest "sha256:ABCD"`):
    - `version == "0.3.1"`
    - screenshots are the URLs of 1.jpg, 2.png and 3.png, in that order
    - `installerSize == 31000000`, `installerSha256 == "abcd"`
  - The same JSON asked for `"LISN-Mac.pkg"` gives `installerUrl.isEmpty()`.
  - An asset with no `digest` gives `installerSha256.isEmpty()`.
  - `parseRelease (juce::var(), "x")` is not valid.
- [ ] **Step 2: Run them and confirm they fail.** Build `StemSplitterTests`, run `build\tests\...\StemSplitterTests.exe` (as for the existing tests). Expected: a compile failure (no `ReleaseInfo.h`).
- [ ] **Step 3: Implement** `ReleaseInfo.h/.cpp`. `hostName()` uses `juce::PluginHostType`.
- [ ] **Step 4: Run the tests.** Expected: "ReleaseInfo" passes, and every other test still passes.
- [ ] **Step 5: Commit** with `feat: ReleaseInfo parses a GitHub release: notes, screenshots, installer`.

### Task 2: UpdateCheck keeps the release, plus a local test override

**Files:**
- Modify: `Source/UpdateCheck.h`, `Source/UpdateCheck.cpp`, `tests/UpdateCheckTests.cpp`

**Interfaces:**
- Consumes: `parseRelease`, `installerAssetName` (Task 1).
- Produces:
  - `juce::var fetchLatestReleaseJson()` replaces `fetchLatestRelease()`. It returns the parsed JSON, or a void var.
  - `UpdateCheck::getRelease() const -> const ReleaseInfo&`, on the message thread. It's valid only when newer.
  - `getNewerVersion()` stays, returning `getRelease().version` when newer.
  - `static juce::File UpdateCheck::testOverrideFile()` is `update-test.json` beside `update.settings`.
  - `static juce::var UpdateCheck::releaseFor (juce::PropertiesFile& seen, juce::int64 nowMs, std::function<juce::var()> fetch)` is the cache rule, pulled out so it can be tested.

- [ ] **Step 1: Write the failing tests** in `tests/UpdateCheckTests.cpp`, using a `PropertiesFile` in a temp dir:
  - **Fresh cache:** with `checked` 1 h ago and a stored `release` JSON, `releaseFor` doesn't call `fetch` and returns the stored JSON. Assert `fetch` wasn't called.
  - **Stale cache:** with `checked` 25 h ago, it calls `fetch` once. It stores the result under `release` (the JSON string) and `checked`, and returns it.
  - **Stale and offline:** `fetch` returns void, so it returns the stored JSON and doesn't overwrite it.
  - **A 0.2.5-era cache** (only `latest`, no `release`) counts as stale, so `fetch` is called.
- [ ] **Step 2: Run them and confirm they fail** (no `releaseFor`).
- [ ] **Step 3: Implement `releaseFor`,** and make `run()` use it.
  - If `testOverrideFile()` exists, `run()` uses its JSON instead, with no cache and no network. This is how the owner tries the flow on their PC before a release, and it's documented in the header comment.
  - It parses with `installerAssetName (wrapperType == Standalone)`, which needs a `bool standalone` constructor argument or a setter, called before the thread starts. The editor already knows the wrapper type.
  - It posts the `ReleaseInfo` to the message thread as `newer` is today. Keep `latest` written too, for 0.2.5 processes that share the file.
- [ ] **Step 4: Run the tests.** Expected: everything passes.
- [ ] **Step 5: Commit** with `feat: the update check keeps the whole release; update-test.json overrides it`.

### Task 3: Updater, the download and install state machine

**Files:**
- Create: `Source/Updater.h`, `Source/Updater.cpp`, `tests/UpdaterTests.cpp`
- Modify: `CMakeLists.txt` (sources and tests)

**Interfaces:**
- Consumes: `ReleaseInfo` (Task 1), `HarmonyJob::download (url, sha256, dest, progress)`. That function needs its sha256 check skipped when `sha256` is empty, which is a one-line change in `HarmonyJob.cpp`. Keep its test green.
- Produces:
```cpp
class Updater : public juce::ChangeBroadcaster, public juce::DeletedAtShutdown, private juce::Thread
{
public:
    enum class Step { idle, downloading, ready, installing, installed, waitingForInstaller, failed };
    enum class Failure { none, download, install, inUse, engine };
    Step getStep() const;  Failure getFailure() const;  float getProgress() const;   // message thread
    bool engineChanged() const;          // after an install in this process changed the engine's ready.txt
    void startDownload (const ReleaseInfo&);   // idle/failed -> downloading; reuses a matching file -> ready at once
    void cancel();                             // downloading -> idle, file and .part deleted
    void install (const juce::File& engineStamp);   // ready -> installing -> installed / waitingForInstaller / failed
    void reset();                              // failed -> idle
    // Test hooks (defaults: HarmonyJob::download and the platform launcher of Task 4)
    std::function<bool (const juce::URL&, const juce::String&, const juce::File&, const std::function<bool (float)>&)> downloadFn;
    std::function<int (const juce::File& installer)> launchFn;   // returns the exit code; see Global Constraints
    juce::File downloadDir;   // default tempDirectory/LISN-update
    JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL (Updater)
};
```
- **The rules `install` follows:**
  - `launchFn` returns `0` → `installed`. `101` → `failed/engine`. `7` → `failed/inUse`. `1223` or anything else → `failed/install`.
  - A launch that can't report back (Mac `open`, the desktop app about to quit) returns `-2` → `waitingForInstaller`.
  - Before launching, it reads `engineStamp`'s text. After `installed`, it sets `engineChanged()` if the text differs.
- **Change messages:** `sendChangeMessage` on each step change, and on progress at most every 1 %.

- [ ] **Step 1: Write the failing tests** in `tests/UpdaterTests.cpp`. Use a fresh `Updater` per test (construct directly, not via the singleton), a temp `downloadDir`, and a fake `downloadFn` that writes a file and reports progress 0.5 then 1.0.
  - **Download:** `startDownload` then waiting for the step gives `ready`, with progress 1 and the file present.
  - **A second `startDownload`** while downloading is ignored: the fake is called once.
  - **Cancel:** a blocking fake, then `cancel()` gives `idle`. No file and no `.part` in `downloadDir`. A new `startDownload` then calls the fake again.
  - **A failing fake** gives `failed/download`. `reset()` then gives `idle`.
  - **A file already there:** its SHA-256 matches `installerSha256`, so `startDownload` goes straight to `ready` without calling the fake. With a mismatching hash, the file is deleted and the fake is called.
  - **Exit codes:** `launchFn` returning 0, 101, 7, 1223 and -2 maps to `installed`, `failed/engine`, `failed/inUse`, `failed/install` and `waitingForInstaller`.
  - **Engine change:** a stamp file `"a"`, then a `launchFn` that rewrites it to `"b"` and returns 0, gives `engineChanged() == true`. Unchanged gives false.
  - **After `installed`,** the installer file is deleted.
  - **Listeners:** a `ChangeListener` added, then removed mid-download, doesn't stop the download (the editor closing).
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement `Updater`.** The download and the launch both run on its thread, and the results are posted to the message thread. The tests pump the message loop the way `HarmonyFlowTests` does.
- [ ] **Step 4: Run the tests.** Expected: all pass.
- [ ] **Step 5: Commit** with `feat: Updater downloads and installs a release, with injectable download and launch`.

### Task 4: Platform launch: Windows elevation and wait, Mac open, desktop quit

**Files:**
- Modify: `Source/Updater.cpp` (the default `launchFn`). Windows code under `#if JUCE_WINDOWS`.

**Interfaces:**
- Consumes: `Updater::launchFn` (Task 3).
- Produces: `static int Updater::launchInstaller (const juce::File& installer, bool standalone)`, the default `launchFn`.
  - **Windows, plugin:** `ShellExecuteExW` with verb `open`, parameters `/SILENT /SUPPRESSMSGBOXES /NORESTART`, and `SEE_MASK_NOCLOSEPROCESS`. The exe's manifest asks for admin, so Windows shows UAC.
    - If it fails with `ERROR_CANCELLED`, return 1223. Any other failure returns -1.
    - Otherwise wait on the process handle and return `GetExitCodeProcess`.
  - **Windows, desktop:** start it with `/SILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1` without waiting, and return -2. The editor then quits the app (Task 6).
  - **Mac:** `installer.startAsProcess()` opens Apple's Installer. Return -2.

- [ ] **Step 1: No unit test.** It's OS process launching. The fakes in Task 3 cover the mapping. Instead, write a manual check in the Task 7 checklist.
- [ ] **Step 2: Implement it,** and build all three targets. Expected: they build. CI's macos job builds the Mac branch.
- [ ] **Step 3: Commit** with `feat: launch the installer: elevated and awaited for the plugin, relaunching for the app, Installer on Mac`.

### Task 5: Installers, the rename trick, relaunch and the release notes template

**Files:**
- Modify: `installer/lisn-vst.iss`, `installer/lisn-desktop.iss`, `.github/workflows/build.yml` (the `release` job's notes)

- [ ] **Step 1: VST installer** (`lisn-vst.iss`):
  - **`[Setup]`:** add `CloseApplications=no`.
  - **`[InstallDelete]`:** add `Type: files; Name: "{commoncf64}\VST3\LISN StemSplitter.vst3\Contents\x86_64-win\*.old"`.
  - **`[UninstallDelete]`:** already removes the whole bundle folder, so nothing to add.
  - **`[Code]` `PrepareToInstall`:**
    - If `…\Contents\x86_64-win\LISN StemSplitter.vst3` exists, delete any old `LISN StemSplitter.vst3.old`, then `RenameFile` the DLL to `LISN StemSplitter.vst3.old`.
    - If the rename fails and the file still exists, return the message `LISN is in use. Close your DAW, then run this installer again.`. Inno then exits with code 7.
    - After a successful rename, call `RestartReplace (<the .old path>, '')` so Windows deletes it at the next restart.
  - **The engine failure:** change its `MsgBox` to `SuppressibleMsgBox`, set a global flag, and add `function GetCustomSetupExitCode(): Integer;` that returns `101` when the flag is set.
- [ ] **Step 2: Desktop installer** (`lisn-desktop.iss`):
  - **`[Run]`:** add `Filename: "{app}\LISN.exe"; Flags: nowait runasoriginaluser; Check: IsRelaunch and VCRuntimeIsCurrent`.
  - **`IsRelaunch`:** `ExpandConstant('{param:RELAUNCH|0}') = '1'`.
  - **`InitializeSetup`:** when `IsRelaunch`, wait up to 15 s (polling every 250 ms) for `CheckForMutexes('Global\juceAppLock_LISN,Local\juceAppLock_LISN')` to clear before continuing, so the quitting LISN.exe can be replaced.
- [ ] **Step 3: Release notes:** in `build.yml`'s `release` job, put `## What's new\n\n- \n\n` at the top of `notes`, before the SHA lines, so the draft has the section to fill in.
- [ ] **Step 4: Verify the installers.**
  - Run `ISCC /DAppVersion=0.0.1 installer\lisn-vst.iss` and the same for `lisn-desktop.iss`. Expected: both compile.
  - Manual check, by the owner, from Explorer (agent shells are sandboxed). Load LISN in FL. In an elevated prompt, run `Output\LISN-StemSplitter-Setup.exe /SILENT /SUPPRESSMSGBOXES`, then `echo %errorlevel%`. Expected: `0`, FL still running, and the `.old` file beside the new DLL.
- [ ] **Step 5: Commit** with `feat: installers: the VST moves a loaded plugin aside, the app relaunches after a silent update`.

### Task 6: UpdateScreen and the editor wiring

**Files:**
- Modify: `Source/Screens.h`, `Source/Screens.cpp` (the new `UpdateScreen`)
- Modify: `Source/Controls.*`: the pill's text is free text now; `preferredWidth` already follows the text.
- Modify: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
- Test: `tests/Snapshots.cpp`, `tests/UpdateCheckTests.cpp` (the pill and panel behaviour)

**Interfaces:**
- Consumes: `ReleaseInfo` (Task 1), `UpdateCheck::getRelease` (Task 2), `Updater` (Tasks 3–4).
- Produces:
```cpp
class UpdateScreen : public juce::Component   // layout C: screenshot left (flip dots when >1), text right, buttons bottom-right
{
public:
    void setTheme (const Theme&);
    void setRelease (const ReleaseInfo&, const juce::String& currentVersion);
    void setScreenshots (juce::Array<juce::Image>);   // empty: the LISN symbol on the panel instead
    // What the right side shows. busyJob dims Update now with "Finish the current split first".
    enum class View { whatsNew, downloading, ready, installed, waiting, failedDownload, failedInstall, inUse, engine };
    void setView (View, float progress, bool busyJob, bool standalone, const juce::String& host);
    std::function<void()> onLater, onUpdate, onCancel, onInstall, onRetry, onWebsite;
    int shownScreenshot() const;                      // test hook
};
```
- **The editor:**
  - **`setUpdateOpen (bool)`** works like `setHelpOpen`: it shows the update screen in the panel in place of the current screen, and opening one closes the other. The pill toggles it.
  - **Screenshots** load on a background thread the first time the panel opens (`juce::URL::readEntireBinaryStream`, then `ImageFileFormat::loadFrom`). They're kept in the editor.
  - **`tick()` maps `Updater` state to `UpdateScreen::setView`, and to the pill text:**
    - `Update <v>`, or `Downloading <n>%` while downloading
    - after `installed` in a plugin: `Restart <host> to finish`
  - **The buttons:**
    - Update now: `Updater::startDownload`.
    - Install: `Updater::install (<engine dir>/ready.txt)`, where the engine dir is three parents up from `proc.getPythonPath()`.
    - In the desktop app, Install also calls `juce::JUCEApplicationBase::getInstance()->systemRequestedQuit()` once the step is `waitingForInstaller`. On Mac it does so after the Installer opens.
    - With an empty `installerUrl`, Update now opens `pageUrl` in the browser instead.
    - Download from the website opens the 0.2.5 URL (`releases/latest/download/<asset>`).
  - **`startSplit`/`startHarmonies`:** if `Updater::engineChanged()`, show the Error screen with `Restart <host> to finish the update` and don't start.

- [ ] **Step 1: Write the failing snapshot entries and tests.**
  - **Snapshots,** each at 1×, `dusk` unless named. Use a `ReleaseInfo` built in the test with 3 notes (one with `**Harmonies**`) and a generated 1520×1000 gradient image as the screenshot:
    - `update-whatsnew`, `update-whatsnew-noshots-midnight`, `update-busy` (with `busyJob` set)
    - `update-downloading` (58%), `update-ready-app`, `update-ready-daw`
    - `update-installed-daw` (host `FL Studio`), `update-waiting-mac`
    - `update-failed-download`, `update-inuse`
  - **Tests** in `UpdateCheckTests` ("the update panel"):
    - Clicking the pill opens the panel, and clicking it again closes it. Opening help closes the panel.
    - Later closes it and shows the same screen as before.
    - `busyJob` true: the Update now button is disabled. Set it false and the button is enabled, without reopening the panel.
    - With 3 screenshots, clicking the second dot makes `shownScreenshot() == 1`.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement `UpdateScreen` and the editor wiring.**
  - **Layout C, in panel coordinates (712×400) with a 24 px inset:**
    - Screenshot: left 55 %, rounded 10 px, drawn to fit. Dots under it when there's more than one.
    - Right column: title `LISN <v>` (Unbounded, as the screen titles are), subtitle `<size> MB · you have <current>`, then the notes. Bold for `**…**` runs, a gold bullet, and `…and more` as a link.
    - Buttons at the bottom right: Later, then the primary button, in the existing `LisnButton` styles.
  - Match the mockup at `lisn-site/.superpowers/brainstorm/14413-1792427946/content/update-popup.html`, option C.
- [ ] **Step 4: Run the tests and the snapshots, and look at every `update-*` PNG.** Expected: the tests pass, and each image matches its view's copy from the Global Constraints.
- [ ] **Step 5: Commit** with `feat: the What's new panel and Update now, wired to the pill`.

### Task 7: The end-to-end check on this PC, and the Mac tester's checklist

**Files:**
- Create: `docs/update-testing.md` (short)

- [ ] **Step 1: Write `docs/update-testing.md`.**
  - **The override** (Task 2): the steps to drop `update-test.json` in `%APPDATA%\StemSplitter\` (Mac: `~/Library/Application Support/StemSplitter/`).
  - **The test release:**
    - `tag_name "v9.9.9"`, `html_url`, and a `body` with `## What's new`.
    - Assets whose `browser_download_url` are the installers built in Task 5, uploaded to a `mac-test-N` pre-release, or served from the owner's PC.
    - The `whats-new-1.png` asset.
    - Pre-releases never reach users.
  - **The checks:**
    - Windows desktop: pill, What's new, Update now, Install and restart, LISN reopens.
    - Windows FL: Install, the UAC prompt, FL stays open, "Restart FL Studio to finish". FL restart loads the new version, and the `.old` file is gone after a Windows restart.
    - Declining UAC shows the error.
    - Update now is dimmed during a split.
    - Mac, by the tester: Install with FL open, FL keeps working (the spec's untested case). If FL crashes, report it, and the Mac DAW path becomes "quit FL first".
- [ ] **Step 2: Commit** with `docs: how to try the in-app update before a release`.
- [ ] **Step 3: Hand the owner the checklist.** They run the Windows checks from Explorer, since agent shells are sandboxed. This doesn't block the branch.
