# Building and installing

Ember requires CMake 3.22 or newer and a C++20 compiler. CMake fetches JUCE 8.0.10 automatically; use `-DJUCE_SOURCE_DIR=/path/to/JUCE` to supply a local checkout.

## macOS

Install Xcode command-line tools and CMake, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The build creates an ad-hoc signed universal plugin. Copy `build/Ember_artefacts/Release/VST3/Ember.vst3` into `~/Library/Audio/Plug-Ins/VST3/`.

## Windows

Install Visual Studio 2022 with the Desktop development with C++ workload and CMake, then run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Copy the complete `build/Ember_artefacts/Release/VST3/Ember.vst3` directory into your host's VST3 search path, usually `C:\Program Files\Common Files\VST3\`.

Rescan plugins in your audio host after installation.

## Development

DSP regression tests are enabled by default. Set `-DEMBER_BUILD_TESTS=OFF` to omit them, or `-DEMBER_BUILD_UI_PREVIEW=ON` to build the editor preview utility. Floating-point contraction and fast math are disabled to keep DSP evaluation consistent.

JUCE is a separately licensed dependency; its license is available in the JUCE source distribution.
