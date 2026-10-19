# Trying the in-app update before a release

LISN reads `update-test.json` instead of GitHub when the file exists. There's no cache and no network check, so the flow
can be tried on any PC without publishing a release.

## 1. The override

Put the file beside `update.settings`:

- Windows: `%APPDATA%\StemSplitter\update-test.json`
- Mac: `~/Library/Application Support/StemSplitter/update-test.json`

Delete it when you're done, or LISN keeps offering the test version.

## 2. The test release

The file is a GitHub release's JSON. The quickest way to get one is to copy a real release:

```bash
gh api repos/EdiKrasniqi11/lisn-vst/releases/tags/mac-test-1 > update-test.json
```

Then edit it:

- `tag_name` → `"v9.9.9"`, so it's newer than any build.
- `body` needs a `## What's new` heading with `- ` bullets under it.
- `assets` needs the installer for what you're testing: `LISN-Setup.exe`, `LISN-StemSplitter-Setup.exe` or
  `LISN-Mac.pkg`. Each asset needs `name`, `size` and `browser_download_url`. A `digest` of `sha256:<hex>` is
  checked when present.
- For screenshots, add a `whats-new-1.png` asset (up to 3, sorted by name).

The installers can come from a `mac-test-N` pre-release (`gh workflow run build.yml --ref <branch> -f test_tag=mac-test-N`).
Pre-releases never reach users, because LISN only reads `releases/latest`.

## 3. The checks

Run these from Explorer, not from an agent's shell (agent shells are sandboxed).

**Windows, desktop app**
- The pill shows `Update 9.9.9`. Clicking it opens What's new with the notes and screenshots.
- Update now shows the percentage, then Install. Install closes LISN, runs the installer silently, and LISN opens again.

**Windows, FL Studio**
- Install shows the UAC prompt. FL stays open, and the pill says `Restart to finish`.
- After restarting FL, the new version loads. After restarting Windows, `LISN StemSplitter.vst3.old` is gone from
  `C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3\Contents\x86_64-win\`.
- Declining UAC shows `The update didn't install.` with Try again.
- During a split, Update now is dimmed with `Finish the current split first`. It comes back when the split ends.

**Mac (tester)**
- Install with FL open: the Installer opens, and FL keeps working. If FL crashes, report it. The Mac DAW path then
  becomes "quit FL first".
