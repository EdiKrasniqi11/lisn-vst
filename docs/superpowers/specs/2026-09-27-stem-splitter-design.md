# Stem Splitter VST3: Design + Plan

## Context
You want a plugin for FL Studio and Ableton on Windows: drop in an audio file, get back separate stems. The goals are to learn VST development and to have a tool for your own use. The project folder `C:\Projects\lisn-vst` is empty.

Decisions made so far:
- **Format:** VST3 only, on Windows. That covers FL Studio and Ableton, so AU and AAX are skipped.
- **Engine:** the plugin runs the official Python `demucs` command-line tool in the background.
- **Output:** the plugin writes WAV files and shows one draggable tile per stem, which you drag onto DAW tracks.

**About the stems:** the default Demucs model (`htdemucs`) produces **vocals, drums, bass, other**. No model outputs a separate "FX" stem. `htdemucs_6s` adds guitar and piano, and you can pick it from a dropdown.

## Architecture
```
DAW (FL / Ableton)
 └─ StemSplitter.vst3 (JUCE, C++)
     ├─ Editor UI: drop zone → model dropdown → progress bar → 4 stem tiles (drag out)
     ├─ SeparationJob (juce::ChildProcess on a background thread)
     │     runs: python -m demucs -n htdemucs -o <cache>\<hash> "<file>"
     │     parses the percentage demucs prints to stdout → drives the progress bar
     └─ Audio processBlock: pass audio through unchanged (the plugin does no live DSP)
```
Stems are saved to `%APPDATA%\StemSplitter\cache\<file-hash>\`. If you run the same file again, the plugin loads the cached stems instead of separating it again.

## Toolchain (what VST developers use)
- **JUCE 8**: the standard C++ framework for audio plugins. It handles VST3 wrapping, the UI, and audio file I/O.
- **CMake + Visual Studio 2022** (Community edition, with the "Desktop C++" workload).
- **AudioPluginHost**, which ships with JUCE: a lightweight host that loads your plugin quickly without opening a full DAW.
- **pluginval** (Tracktion): the standard automated stability checker for plugins, run from the command line.
- **Python 3.11 + `pip install demucs`**. A CUDA GPU makes separation about 10× faster, but it's optional.

## Build steps
1. `git init` and save this design to `docs/superpowers/specs/2026-09-27-stem-splitter-design.md`.
2. Install the tools. Check that `python -m demucs --help` works and separate one test song from the command line.
3. Build a minimal plugin from JUCE's CMake example (`examples/CMake/AudioPlugin`), with the VST3 format and `COPY_PLUGIN_AFTER_BUILD`. Confirm it loads in AudioPluginHost, FL Studio, and Ableton.
4. Build the drop zone: accept a dropped wav, mp3, or flac file and show its name.
5. Write `SeparationJob`: launch demucs in the background, parse its progress output, and add a Cancel button. Handle these errors: Python not found, demucs failed (show the last lines of its stderr), and the user closing the plugin mid-job (kill the process).
6. Build the stem tiles: one tile per output WAV, dragged out with `DragAndDropContainer::performExternalDragDropOfFiles`.
7. Save state: store the last file and stem paths in `getStateInformation`, so a reopened project still shows its stems.
8. Settings: add a model dropdown and a field for the Python path (auto-detected by default).

## Testing
- **Unit:** one small test that checks progress-line parsing against captured demucs output.
- **Host:** load the plugin in AudioPluginHost after every build.
- **Stability:** `pluginval --strictness-level 5 --validate StemSplitter.vst3` must pass.
- **Real DAWs:** in both FL Studio and Ableton, drop in a song, wait for 4 tiles, drag each stem onto a track, and check they play in sync. Then save the project, reopen it, and confirm the stems are still shown.
- **Quality check:** sum the 4 stems, which should come out roughly equal to the original.

## Skipped for now (add when needed)
- In-plugin playback and multi-out routing: add it if dragging stems out feels clunky.
- A self-contained engine with no Python (demucs.cpp or ONNX): add it if you ever distribute the plugin.
- An installer, code signing, and a Mac build: only needed to ship it.
