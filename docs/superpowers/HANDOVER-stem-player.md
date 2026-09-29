# Handover: stem player + loop handles (paused 2026-09-29, before the final FL check)

To resume, paste this into the new session as the first message:

> Resume the stem player work in C:\Projects\lisn-vst. Read `docs/superpowers/HANDOVER-stem-player.md` first, then continue from "Resume here". Keep ponytail mode on.

## What's on the branch

Branch `feat/stem-player` (from master `546e085`). It is **not pushed and not merged**. Don't push or merge without asking the user.

Two plans were built on it, both with subagent-driven development (a fresh implementer and a review per task, then a final review):

1. **Stem player** (done; its final review is clean). One shared playhead, mute lights with right-click solo, a beat-snapped loop, and Drag mix / Drag loop renders.
   - Plan: `docs/superpowers/plans/2026-09-29-stem-player.md`
   - Spec: `docs/superpowers/specs/2026-09-29-stem-player-design.md`
   - Mockup: `docs/design/StemPlayer.mockup.html`
   - The user's FL click-through passed.
2. **Loop handles, grid, zoom and keys** (built; its final review is done and the fix wave is re-reviewed clean).
   - Plan: `docs/superpowers/plans/2026-09-29-loop-handles.md`
   - Spec: `docs/superpowers/specs/2026-09-29-loop-handles-design.md`
   - Mockup: `docs/design/LoopHandles.mockup.html`. It wins over the text and over the older mockup.
   - What it adds:
     - Keyboard: clicking the plugin gives it focus. Space plays and pauses, L toggles the loop (it makes a 4-bar loop when there's none), 1-6 mute, Shift+1-6 solo, Home jumps back. `keyStateChanged` claims the key-downs so FL doesn't also get them.
     - Colour bars replace the stem icons, and the footer is gone.
     - A 100 Hz peak envelope per stem, Ctrl+wheel zoom, wheel scroll and a position strip.
     - Loop edges are handles with grab tabs. They snap to an adaptive grid (¼ beat, beat or bar), Alt drags free, beat and bar lines show, and the subtitle shows the loop length in beats.

| Loop-handles task | Commits |
|---|---|
| 1. Keyboard (the FL check passed) | `6e6edb6` |
| 2. Colour bars, no footer | `d787f17` |
| 3. Envelope, view, zoom, strip (+1 fix round) | `b421545`, `bd27457` |
| 4. Handles, grid, beats, Loop button (+1 fix round) | `a8c8ce0`, `f729bdc` |
| 5. Snapshots and bench | `932a1f6` |
| Final-review fix wave | `99840cf` |

The doc commits along the way are `1d92e4d`, `960d5c3`, `17e8e11` and `0ba6263`.

## Resume here: exact next steps

1. **One open residual. Ask the user whether to fold it in first.** It's a one-line fix, and the final review left it unfixed because there's no second fix wave.
   - **The problem:** a grab tab whose edge sits within 5-9 px of the view edge hangs outside the waveform's x range. There the row takes the mouse, so the drag-hand cursor shows and a 4 px drag starts a stem-file drag. This happens, for example, with a loop over the whole song at whole-song zoom.
   - **The fix:** in `StemRow::hitTest` (`Source/StemRow.cpp` ~200), open the x range to `[wave.getX() - 9, wave.getRight() + 9)`. That's safe: the light ends at x 180 and the chip starts at 584.
   - **If the user says yes:** dispatch a cheap implementer with a test, then a scoped re-review. Otherwise park it.
2. **Build Release for FL.** Ask the user to close FL, check with `Get-Process | Where-Object { $_.ProcessName -match 'FL' }`, then run `cmake --build build --config Release`. FL loads that build through the junction, so no copy and no rescan are needed.
3. **The user's FL click-through** (loop-handles plan, Task 5, Step 7). Ask them to check:
   - the look: colour bars, and no footer;
   - Ctrl+wheel zooms around the mouse, the wheel scrolls while zoomed, and the strip shows where the view is;
   - a drag makes a loop, and dragging either tab trims it on the grid (quarter beats zoomed in, bars zoomed out). Try the **top** of a tab too;
   - Alt+drag goes off the grid; an edge can't cross the other; a plain click on a handle changes nothing;
   - with no loop yet, Loop and L make a 4-bar loop at the playhead; after that they toggle it;
   - the keys still work (Space, L, 1-6, Shift+1-6, Home), FL's shortcuts still work, and Space goes back to FL after clicking FL;
   - Drag loop, and a row drag with a loop on, still deliver the trimmed loop.

   **One design call to ask about at the same time.** In the mockup, the 4-stem loop band, grid and playhead run about 31 px past the last row, and the playhead sticks out 6 px above and below the band. The code stops them flush at the last waveform. The reviewers judged the mockup's run-out a flex-layout accident and recommend keeping the code. Ask the user.
4. **Then run superpowers:finishing-a-development-branch.** Ask the user whether to merge to master or open a PR, and don't push unasked. After that, delete the SDD workspaces (`.superpowers/sdd/2026-09-29-stem-player/` and `.superpowers/sdd/2026-09-29-loop-handles/`).

## Local files (git-ignored, same machine)

- **SDD workspaces:**
  - `.superpowers/sdd/2026-09-29-loop-handles/`: `progress.md` is the ledger (every review result, the fix rounds, the deferred minors with the final review's triage, the pending steps), plus the briefs, reports and review packages.
  - `.superpowers/sdd/2026-09-29-stem-player/`: the first plan's ledger.
  - Trust the ledgers and `git log` over memory.
- **Snapshots:** `snapshots/` (render with `lisn_tests.exe --snapshots snapshots`).
- **Brainstorm scratch:** `.superpowers/brainstorm/` (for example `stem-badges.html`).

## Environment and gotchas

- **Tools:** Windows 11, JUCE 8.0.4, MSVC 2022. CMake is at `C:\Program Files\CMake\bin`; in Git Bash run `export PATH="/c/Program Files/CMake/bin:$PATH"`.
- **Build and test** (`build/` is already configured):
  - build: `cmake --build build --config Debug`;
  - tests: `build/lisn_tests_artefacts/Debug/lisn_tests.exe` (exit 0 means pass), then `ctest --test-dir build -C Debug --output-on-failure`.
- **Bench:** `lisn_tests.exe --bench` only means anything in **Release** (budget: motion frame ≤ 4 ms). This machine sits at about 3.9 ms median **even on the pre-plan build**, so single runs over 4 ms are noise with zero headroom. Compare A/B against a baseline worktree before calling it a regression.
- **FL loads the Release build directly.** `C:\Program Files\Common Files\VST3\LISN StemSplitter.vst3` is a directory junction to `build\StemSplitter_artefacts\Release\VST3\…`.
  - Build **Debug only** during tasks.
  - Build Release only with **FL closed**, and never kill FL.
  - Remove the junction only with `rmdir` without `/s`.
  - The Release install step overwrites `%USERPROFILE%\VST3\LISN StemSplitter.vst3`; the next Debug build restores it.
- **Debug VST3 flake:** if the post-build step fails with `juce_vst3_helper … DLL initialization routine failed`, delete `build/StemSplitter_artefacts/Debug/VST3/LISN StemSplitter.vst3/Contents/x86_64-win/LISN StemSplitter.vst3` and rebuild.
- **pluginval:** `C:\tools\pluginval\pluginval.exe --strictness-level 5 --validate "%USERPROFILE%\VST3\LISN StemSplitter.vst3"`. For Release, validate the Common Files path.
- **Commit trailer:** end every commit message with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Subagents sometimes use their own model's name; tell them to use this one, and fix a wrong one with an amend of the message only, before review.
- **Line endings:** the C++ files are CRLF. `sed -i` in Git Bash strips the CRs, so edit with the Edit tool or a byte-preserving script.

## Decisions made with the user (so nobody redoes them)

- **Earlier:** zoom is Ctrl+wheel (not auto-zoom). The plugin takes Space when focused, and FL gets Space back when the user clicks FL. Stem marker: colour bar (option A). No footer, not even a Space keycap.
- **From the keyboard FL check:** the Loop button was disabled with no loop (it was spec'd that way). The user chose: **Loop and L make a 4-bar loop at the bar under the playhead (8 s with no BPM, the song's last 4 bars near the end), and the button is never disabled.**
- **Slips in the plan's code:** the controller fixed these without asking, since they don't change the design:
  - Task 3: a stale played colour after a zoomed seek (a new `hasHead` flag), and `setView` float rounding that left the strip showing at whole-song zoom.
  - Task 4: `defaultLoop` fell off the grid when the playhead was before the first beat.
- **The final review's fix wave** (`99840cf`):
  - Tabs are whole handles: `StemRow::hitTest` opens the waveform column, and `StemsScreen` forwards those mouse events to `waveMouse`.
  - Handle drags have a 4 px threshold, and an unchanged loop isn't re-sent.
  - The strip is 2 px lower, to match the mockup.
- **Keyboard corner-cuts, deferred with `ponytail:` notes:** no auto-repeat guard, lone key-ups reach FL, `GetAsyncKeyState` timing, and Tab eaten once the plugin is focused. The full list is in the ledger.

## User and working preferences

- **The user:** learning VST development, design-focused, uses FL Studio 20 (plus Ableton) on Windows.
- **Ponytail mode** (lazy, minimal code) stays on.
- **Mockups come before any UI code** (an HTML canvas), approved first.
- **Review findings the plan mandates go to the user** as one short question with a recommendation. Pure slips in the plan's code are fixed and noted.
- **Don't push or merge without asking.** The user does the FL click-throughs.
