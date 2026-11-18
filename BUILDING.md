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

The same build creates `build/windows/crossover-dinput8/dinput8.dll`. It uses guarded game-side output slots, a token-authenticated loopback relay to the native Mac companion, and GameInput/XInput fallback. It omits VR and Linux HID compatibility hooks. Owned fixtures cover separate channels, readiness and stop handoffs, source/peer expiry, literal native-launch paths, input preservation and unchanged system XInput code. They do not establish physical Mac output.

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

## Native macOS companion

On macOS with Xcode command-line tools:

```sh
cmake -S mac -B build/mac -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.3
cmake --build build/mac --parallel 2
ctest --test-dir build/mac --output-on-failure
codesign --force --sign - --timestamp=none build/mac/ed-macos-haptics.app
codesign --verify --deep --strict build/mac/ed-macos-haptics.app
```

CI builds both architectures with Apple's SDK, runs the no-hardware self-test and uploads `mac-native-app` as a tar archive preserving its app layout and signature. The Linux packager can consume that unpacked build directory. Never substitute a Linux executable or claim ad hoc signing is Developer ID notarization. The native helper's physical actuator and background playback behavior need testing on the target Mac.

## Assemble manual-copy packages

After the builds above:

```sh
python3 tools/package_preview.py \
  --windows-build build/vr-windows --linux-build build/vr-linux \
  --gamepad-build build/windows --mac-build build/mac
python3 tools/verify_release.py --directory dist
```

This always creates Windows, Linux/Proton and Mac/CrossOver ZIPs under `dist`, each with `INSTALL.txt`, file hashes, dependency notices and a companion SHA-256 file. All three platforms are validated before any ZIP is written. Windows/Linux include standalone and chain proxies; Mac contains a single gamepad proxy and the universal native haptics app. It reads build artifacts and project resources only and does not locate, launch or install into a game directory. Package assembly is distinct from hardware validation; test custom builds before distributing them.

Run `python3 verification/package_preview.py` for hermetic packaging checks. Before publishing, complete the local Kilo review and required checks, then use `python3 tools/verify_release.py --directory dist --publish --notes-file RELEASE-NOTES.txt`. Prepare that notes file first. The command requires all three matching-version packages and checksums, rejects changed members and publishes a GitHub prerelease with all six assets. It requires the release commit and lightweight tag to exist on public `main` with successful native, Windows and macOS CI; push the reviewed commit first. Never publish private analysis files or logs.
