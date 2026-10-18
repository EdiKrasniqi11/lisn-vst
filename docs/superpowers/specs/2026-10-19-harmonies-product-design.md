# Harmonies as its own product inside LISN: design

Date: 2026-10-19. Owner: Edi Krasniqi. Status: approved in chat, to be planned. Builds on `feature/mac-release` (which contains Harmony Split, the Harmonies screen and the name reel); the Mac tester build waits for this.

## Why

Harmony Split today is a feature of the stem splitter: a "Harmony Split" text button on the stems screen opens a Quick / Best menu, and only after a stem split. The owner thinks of it as a separate function, "the other product" next to StemSplitter, and wants:
- a less assuming entry on the stems screen: a **choir icon** instead of the text button;
- to **start from a song directly in Harmonies**, without going through the stem splitter's screens first.

The name reel next to LISN (the "slot machine" that spins between "StemSplitter" and "Harmonies") is the switch between the two products.

## Decisions (the owner's)

1. **The choir icon** (mockup icon A, "three singers": one head and shoulders in the middle, one smaller on each side) replaces the "Harmony Split" text button on the stems screen. It shows only when the song on screen has been stem-split (a 4-stem split, as today). Icon only; "Harmonies" appears as a tooltip on hover.
2. **Without a split song, the way in is the name reel:** clicking the name spins it to "Harmonies" (or back) and switches LISN to that product.
3. **Harmonies has its own drop screen** (mockup "2"): the choir icon in a circle, "Drop a song here", "LISN finds the vocals, then splits the lead from the harmonies.", Browse…, "You'll get" with Lead (vocals colour) and Harmonies (gold) chips.
4. **In Harmonies, the header's 4 stems / 6 stems switch becomes a Quick | Best quality switch** (Best says it needs a 900 MB download first until that model is downloaded, as the current menu does).
5. **While it works, two steps** (mockup "3"): "Finding the vocals" (the normal stem split), then "Splitting the harmonies", each with its own progress bar and percentage, and Cancel. When both finish, the existing Harmonies screen opens.
6. **The Mac tester build waits for this** (see `docs/superpowers/specs/2026-10-18-mac-release-design.md`).
7. **Harmonies' drop screen offers the song on screen:** when the reel is spun to Harmonies while a stem-split song without a harmony result is on screen, the drop screen shows a primary button "Split the harmonies of <song name>" above "Drop a song here" (the drop zone and Browse still work for another song).

## Behaviour (proposed in chat, accepted by the owner)

- **Spinning the name to Harmonies:** if the song on screen already has a Harmony Split result, its Harmonies screen opens (as today). Otherwise Harmonies' drop screen shows. Spinning back to StemSplitter shows StemSplitter's screen for the same song (its stems), or StemSplitter's drop screen if there is no song.
- **The choir icon** spins to Harmonies and starts that song's harmony pass right away (or just opens it if a result exists), with the quality the Quick | Best switch is set to (Quick unless changed). The Quick / Best pop-up menu is removed; the switch replaces it.
- **A song dropped (or browsed) in Harmonies** runs the 4-stem split first ("Finding the vocals"; skipped instantly when that song's stems are already cached), then the harmony pass ("Splitting the harmonies"), then opens the Harmonies screen. Dropping a file onto the window while in Harmonies goes this way too.
- **Harmonies always uses the normal 4-stem split** under the hood, whatever the StemSplitter's 4/6 switch says. The choir icon stays hidden in 6-stem mode, as today.
- **The Harmonies screen gets a "+ New song"** button next to "All stems" (it opens the file chooser, Harmonies flow). "All stems" keeps spinning back to StemSplitter with that song's stems.
- **LISN remembers the product** (StemSplitter or Harmonies) and the Quick | Best choice per plugin instance / app, like the theme and 4/6 stems today, so it reopens where you left off.
- **Errors** (no engine, a failed split, a failed harmony pass) show the existing error screens and copy (on Mac: run LISN-Mac.pkg again); Retry repeats the whole Harmonies flow for that song.

## Calls made after review (the controller's, accepted by default — the owner can override)

- **4/6 independence:** Harmonies keeps its own song and its own 4-stem stem folder; StemSplitter's 4/6 setting, saved model and song are never changed by Harmonies. "All stems" spins back to StemSplitter, which shows the song at StemSplitter's own 4/6 setting (splitting it then if needed, as switching 4/6 does today).
- **Existing results:** an existing harmony result just opens, at whatever quality it was made. Changing Quick | Best while a Harmonies result is on screen re-runs that song at the new quality (the way the 4/6 switch re-splits); that is how a Quick result is upgraded. Dropping a song that already has a result opens it (both steps skipped).
- **Best's download:** no extra confirmation; the switch's Best side carries "900 MB download first" until the model is downloaded, and while it downloads, step 2 reads "Downloading the Best quality model" with its own percentage, then "Splitting the harmonies".
- **The flow survives the plugin window closing:** the Harmonies flow (both steps, the harmony job and its state) lives in the processor like the stem split does, so closing the DAW's plugin window doesn't stop it, and reopening shows its progress (or the result).
- **While a step runs:** the name reel, the Quick | Best switch, the theme switch and file drops are paused, as the switches are during a split today. Cancel returns to Harmonies' drop screen; stems already made stay cached.

## Goals

- Harmonies feels like its own product: own drop screen, own switch, own progress, reached by the reel; StemSplitter's screens are untouched apart from the choir icon.
- One flow from a dropped song to Lead + Harmonies, with clear two-step progress and Cancel.
- Everything already built keeps working: the reel animation and slide, the Harmonies screen, the player switch between stems and Lead/Harmonies, Best quality's download, the Mac build.

## Non-goals

- Changing the Harmonies screen itself (lanes, faders, drag) or the models.
- Harmonies from a 6-stem split.
- A website change (the public site work for Mac comes later).

## Anti-goals

- StemSplitter users notice anything different besides the choir icon.
- A song dropped in Harmonies silently does nothing, or the user can't tell which of the two steps it's on.
- Switching products loses the user's place: the song on screen must follow the switch.

## What exists (for the builder)

- Screens: `enum class Screen { Drop, Splitting, Stems, Error }` and `UiState` (`Source/UiState.h`) are built from the processor's job every tick; `StemSplitterEditor::show/showScreen/tick` (`Source/PluginEditor.cpp`) map them to `DropScreen`, `SplittingScreen`, `StemsScreen`, `ErrorScreen`, plus the editor-level Harmonies view (`HarmonyScreen`, `harmonyView`, `slide`, `openHarmonies`, `backToStems`).
- The reel: `Header::spinTo`, `onLanded`, `onName`, `setNameClickable` (`Source/Screens.*`). Today the name is clickable only when the song has a Harmony Split result; it must become always clickable.
- The harmony pass: `HarmonyJob` (`Source/HarmonyJob.*`, qualities "quick"/"best", progress, download phase, `qualityOf`, `hasResult`); the stem split: `proc.startSplit`, `SeparationJob`.
- The current entry: `StemsScreen` `harmonyButton` + `harmonyMenu`/`showHarmonyMenu`/`HarmonyChoice` and the editor's `harmonyClicked`/`harmonyChosen`/`updateHarmony` — replaced by the choir icon and the switch.
- Saved state: `StemSplitterProcessor::getStateInformation`/`setStateInformation` (theme, model 4/6, Python path, last input).
- Icons are SVG path strings in `Source/Icons.h`, drawn with `strokeIcon`.
- Mockups: `lisn-site/.superpowers/brainstorm/4365-1792342644/content/harmonies-product.html` (local only).

## Left to the builder

- How the product state lives (processor field + `UiState`, or editor-level) as long as it is remembered and the reel, slide and player behave as above.
- The exact layout and copy sizes of the Harmonies drop and progress screens, matched to the existing Drop and Splitting screens' style.
- How "Finding the vocals" reuses the stem split (the existing job, forced to 4 stems) and how its progress and the harmony pass's progress are shown.
- Tests and snapshots for the new screens (Dusk and Midnight), the switch, the icon, and the flow; Windows and Mac CI stay green.
