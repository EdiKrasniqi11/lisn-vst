<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/design/logo/kit/lisn-horizontal-reversed.svg">
  <img src="docs/design/logo/kit/lisn-horizontal.svg" alt="LISN" width="240">
</picture>

# LISN StemSplitter

A VST3 for Windows that splits a song into stems with Demucs, plays them in sync and lets you drag them into your DAW.

The same code builds **LISN**, a desktop app that does the same without a DAW: `Source/StandaloneApp.cpp`, installed by `installer/lisn-desktop.iss`.

**Download:** https://edikrasniqi11.github.io/lisn-site/

LISN is free software under the GNU Affero General Public License v3 (see [LICENSE](LICENSE)). Built with JUCE 8, used under the same licence.

## Build (Windows, Visual Studio 2022, CMake 3.22+)

    cmake -S . -B build -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release

Releases: bump `project(StemSplitter VERSION ...)`, then push a matching `v*` tag. `.github/workflows/build.yml` drafts the GitHub Release with both installers: `LISN-StemSplitter-Setup.exe` (the VST3) and `LISN-Setup.exe` (the desktop app).

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

Team roles: committers, reviewers and approvers are all [Edi Krasniqi](https://github.com/EdiKrasniqi11).

Only the installers built by `.github/workflows/build.yml` from this repository are signed, and every release is approved by hand.

Privacy: LISN sends no information about you or your audio to any other computer. Songs are split on your own PC. The installers download the AI engine once (Python from GitHub, Python packages from PyPI, the Demucs models from Meta) and nothing else goes online.
