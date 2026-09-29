# Handover: stem player (paused 2026-09-29 mid-plan)

Paste this into the new session as the first message, or point it at this file:

> Resume the stem player work in C:\Projects\lisn-vst. Read `docs/superpowers/HANDOVER-stem-player.md` first, then continue with superpowers:subagent-driven-development from where the handover says. Keep ponytail mode on.

## What is being built

The Stems screen of the LISN StemSplitter VST3 becomes an FL-style stem player: one shared playhead, a mute light per stem (right-click solos), a beat-snapped loop, and "Drag mix / Drag loop" renders.
- **Spec:** `docs/superpowers/specs/2026-09-29-stem-player-design.md`
- **Mockup** (approved; wins over the text): `docs/design/StemPlayer.mockup.html`
- **Plan:** `docs/superpowers/plans/2026-09-29-stem-player.md` (7 tasks)

## Where it stands

**Branch:** `feat/stem-player`, based on master `546e085`. It is **not pushed**; don't push without asking the user.

| Task | State | Commits |
|---|---|---|
| 1. No-rescan FL setup | ✅ done by the controller (no repo change) | none |
| 2. `StemPlayer` engine | ✅ reviewed, 1 fix round | `2f80b29`, `dbe20dd`, `c2eeddd` |
| 3. Offline render | ✅ reviewed | `04bc629`, `0268ce0` |
| 4. `detectTempo` | ✅ reviewed, 1 fix round (user-approved method change) | `aecef90`, `caf87b2` |
| 5. Wire player into the UI | ⏳ **implemented, NOT yet reviewed** | `61246da`, `ca20537` |
| 6. Loop, BPM snapping, Drag mix / Drag loop | ⬜ not started | none |
| 7. Snapshots, bench, pluginval, FL check | ⬜ not started | none |

Doc commits along the way are plan and spec corrections: `3a6033f`, `6289d9f`, `0af9396`.

## Resume here: exact next steps

1. **Task 5 review.** Run the task reviewer (superpowers:subagent-driven-development `task-reviewer-prompt.md`, sonnet) with:
   - the brief `.superpowers/sdd/2026-09-29-stem-player/task-5-brief.md`;
   - the report `.superpowers/sdd/2026-09-29-stem-player/task-5-report.md`;
   - the **diff package (already built)** `.superpowers/sdd/2026-09-29-stem-player/review-0af9396..ca20537.diff` (base `0af9396`, head `ca20537`).

   Tell the reviewer about two deliberate deviations and have it judge them on their merits:
   - The StemRow alpha test uses `expectWithinAbsoluteError (…, 0.38f, 0.005f)`, because `Component::setAlpha` stores alpha in 1/255 steps.
   - `ca20537` moved the player load from `show()` into `tick()`, so the player always holds the stems on screen. A cached re-split of the same song keeps the same stem dir, so `show()` never ran and the player stayed unloaded. A failed load is remembered in `unloadableDir` so it isn't retried every tick.

   Also hand the reviewer the plan's Global Constraints: no ComboBox/TextEditor/PopupMenu/ProgressBar; no keyboard focus; idle `processBlock` does no work; the mockup layout (light 38 at x 142, waveform 376 at x 194, muted rows at alpha 0.38, one shared playhead); click toggles mute and right-click solos.
2. **Tasks 6 and 7** go through the normal loop: `scripts/task-brief` → implementer → review → fixes → ledger.
   - Use sonnet implementers for 6 (multi-file UI); 7 is mostly verification.
   - Task 6's brief quotes code as the plan expected it. Task 5's implementer changed some details (see its report), so implementers must match against the current files.
3. **Final whole-branch review** on the most capable model. Base: `git merge-base master HEAD` = `546e085`. Point it at the "minor (deferred)" lines in the ledger (copied below).
4. Then run **superpowers:finishing-a-development-branch**: merge to master or open a PR. **Ask the user**, and don't push unasked.
5. Then ask the user for the **FL click-through**, the last step of Task 7. Checking that FL's spacebar still works after every click is the key check.

## Local files (git-ignored, same machine)

- **SDD workspace:** `.superpowers/sdd/2026-09-29-stem-player/`. It holds `progress.md` (the ledger), the task briefs and reports, the fix files and the review packages. The ledger's first line names the plan. Trust it plus `git log` over memory.
- **Brainstorm mockups:** `.superpowers/brainstorm/`.

## Environment and gotchas

- **Tools:** Windows 11, JUCE 8.0.4, MSVC 2022, CMake at `C:\Program Files\CMake\bin`. Prepend it to PATH: in Git Bash, `export PATH="/c/Program Files/CMake/bin:$PATH"`.
- **Builds and tests** (the build dir `build/` is already configured):
  - build: `cmake --build build --config Debug`;
  - tests: `build/lisn_tests_artefacts/Debug/lisn_tests.exe`, then `ctest --test-dir build -C Debug --output-on-failure`;
  - snapshots: `lisn_tests.exe --snapshots snapshots` (the output folder is git-ignored; read the PNGs to check them).
  - **Bench:** `lisn_tests.exe --bench` only means anything in **Release** (budget: motion frame ≤ 4 ms). Debug always "FAILs" it.
- **FL loads the Release build directly.** `C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3` is a **directory junction** to `build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3`.
  - Build **Debug only** during tasks.
  - Build Release only for final checks, and **with FL closed** (FL locks the DLL).
  - No copy and no rescan are needed. Remove the junction only with `rmdir` **without** `/s`.
- **Debug VST3 flake:** if a Debug build fails at the post-build step with `juce_vst3_helper … DLL initialization routine failed`, delete `build/StemSplitter_artefacts/Debug/VST3/LISN StemSplitter.vst3/Contents/x86_64-win/LISN StemSplitter.vst3` and rebuild. It's a corrupt incremental link, not a code bug.
- **pluginval:** `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\LISN StemSplitter.vst3"`. For the Release check, validate the Common Files path.
- **Auto-mode outages:** on 2026-09-29 the auto-mode safety checker was down intermittently, so commands and SendMessage failed with "classifier gave no verdict". If that recurs, ask the user to switch the session to manual permission mode.
- **Commits:** end every message with a blank line plus `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Decisions and corrections made during execution (so nobody redoes them)

- **Task 2:**
  - The plan's loop-test expectation was an arithmetic slip: the first wrap is at 8820 samples. The plan is fixed.
  - **Design change:** JUCE's `BufferingAudioSource` keeps buffered audio when a seek lands inside its valid range, so loop changes played up to 0.74 s of stale audio. `StemPlayer::seekSource` now jumps to a **fresh linear range** (`base`), and `songPosition()` maps `origin + (linear - base)` (commit `c2eeddd`). Seeks made before `prepare()` are re-applied in `prepare()`.
- **Task 3:** the render test's empty-range file was renamed to `empty-range.wav`, because an earlier test writes `empty.wav`.
- **Task 4** (the user chose "fix both"): the tempo method is now **mean-removed autocorrelation plus a 0.01-frame lag sweep over the whole song** (commit `caf87b2`). The plan's Task 4 code is marked superseded, and the spec is updated.
- **Task 5:** the player load moved into `tick()` (see above).

## Deferred minor findings (the final review triages these)

- **Task 2:**
  - `seekSource` stores `origin` before `base`, leaving a tiny window where `hasStreamFinished` can fire spuriously. Store `base` first (StemPlayer.cpp ~198).
  - The 2×readAheadSamples fresh-range margin isn't tied to the real buffer size, `max(2*scaledBlock, 32768)`.
  - The tests are timing-sensitive (`sleep(1)` pacing; `load<100 ms` includes the prefill).
  - Coverage gaps: mono stem, solo audibility in the output, seek while playing.
- **Task 3:**
  - A stale `<name>.part` is left on late failure paths.
  - The tests don't cover multi-chunk ranges, the output format, mono stems, an unreadable stem, or the `.part` file being gone after success.
  - `endSec` isn't clamped (the Task 6 caller must pass clamped ranges).
  - `readStereo` can't report read errors.
- **Task 4:**
  - The 70.0–70.18 BPM edge band can't be reached (fix: `ceil` for `maxLag` and clamp the swept lag).
  - The 0.2 gate may reject very loose playing (≥15 ms jitter).
  - Half or double tempo is systematic for accent-alternating patterns (`ponytail:`-noted).
  - `detectTempo` has no abort callback.
  - No stereo, non-44.1 kHz or under-4 s tests.
- **Task 5** (from the implementer):
  - `timeText` is briefly stale until the peaks arrive.
  - `CircleButton`'s comment says 38 px but it's used at 36.

## User and working preferences

- **The user:** learning VST development, design-focused, uses FL Studio 20 (plus Ableton) on Windows.
- **Ponytail mode** (lazy, minimal code) is on and should stay on.
- **Mockups before any UI code** (HTML canvas), approved first. This round's mockup is already approved.
- **Don't push or merge without asking.** The user does the final FL click-through.
- **Plan-mandated review findings go to the user** as one short question with a recommendation. Pure arithmetic slips in the plan were fixed directly and noted.
- **Related plan:** `docs/superpowers/plans/2026-09-29-desktop-app.md` (a future desktop app). Task 7 adds a note to its Phase 4 saying the StemPlayer already exists.
