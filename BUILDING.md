# Building EDUP - NHOE

The Windows runtime currently requires a 64-bit MinGW-w64 C++17 compiler. Native Linux VR components use a C++17 compiler. CMake 3.20+, Ninja and Python 3.10+ are used below. Required SDK files are pinned under `third_party`; no game installation is needed to build or run the synthetic tests.

## Native policy and OpenXR tests

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

These tests use synthetic events, clocks, API interfaces and runtime data. They do not send vibration or require a headset, controller or game process.

## Windows gamepad runtime

On Linux, install the MinGW-w64 x64 GCC/G++ toolchain, then:

```sh
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64.cmake
cmake --build build/windows --parallel 2
python3 tools/verify_windows.py --build build/windows
```

The verification command uses Wine and an isolated prefix inside the build directory. Its controller and SDK boundaries are synthetic; it never loads the supplied GameInput runtime. It creates only its own test results and fixture directories.

The standalone DLL is `build/windows/restoration-dinput8/dinput8.dll`. The chain variant is `build/windows/restoration-dinput8-chain/dinput8.dll` and requires the explicitly preserved prior bootstrap described in the installation instructions. Do not treat it as an automatic merger for unrelated mods.

## Windows VR build

```sh
cmake -S vr -B build/vr-windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/mingw64.cmake"
cmake --build build/vr-windows --parallel 2 --target \
  ed_vr_haptics dinput_vr_proxy dinput_vr_chain_proxy ed_vr_openxr \
  vr_mirror_verification vr_publisher_verification vr_broker_verification
python3 tools/verify_windows.py --build build/windows --vr-build build/vr-windows
```

This produces the game-side VR proxies, SteamVR companion and Windows OpenXR layer. The adapter remains haptic-only; binding/profile handling preserves the application's control setup.

## Native Linux VR build

```sh
cmake -S vr -B build/vr-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/vr-linux --parallel 2
ctest --test-dir build/vr-linux --output-on-failure
```

For broad Proton/Steam Linux Runtime compatibility, build release binaries against an appropriate older runtime baseline. A build on a newer distribution can require a newer glibc. The published preview's native layer was checked with a glibc 2.31 loader; a locally built binary does not inherit that validation.

## Assemble manual-copy packages

After the builds above:

```sh
python3 tools/package_preview.py \
  --windows-build build/vr-windows --linux-build build/vr-linux
```

This creates Windows and Linux/Proton ZIPs under `dist`, each with `INSTALL.txt`, file hashes, dependency notices and separate standalone/chain proxies. It reads build artifacts and project resources only. It does not locate, launch or install into a game directory. Package assembly is distinct from hardware validation; test custom builds before distributing them.
