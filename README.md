# LISN StemSplitter

A VST3 for Windows that splits a song into stems with Demucs, plays them in sync and lets you drag them into your DAW.

**Download:** https://edikrasniqi11.github.io/lisn-site/

This source is published for reference and is not open source (see [LICENSE](LICENSE)). Built with JUCE 8 under the JUCE Starter licence.

## Build (Windows, Visual Studio 2022, CMake 3.22+)

    cmake -S . -B build -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release

Releases: bump `project(StemSplitter VERSION ...)`, then push a matching `v*` tag. `.github/workflows/build.yml` drafts the GitHub Release with the installer.
