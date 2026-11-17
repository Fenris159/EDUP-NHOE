# EDUP - NHOE

**Native Haptics Odyssey Expanded** restores authored controller vibration in Elite Dangerous and adds feedback for mapped Odyssey on-foot actions and Scorpion weapons. It supports gamepads and haptic output through SteamVR, Meta/Oculus PC, and compatible OpenXR frontends.

This is a **test preview**. The current release is **0.11.0-preview.1**.

## Download and install

- [Windows x64 preview](https://github.com/Fenris159/EDUP-NHOE/releases/download/v0.11.0-preview.1/EDUP-NHOE-0.11.0-preview.1-windows-x64.zip)
- [Linux / Proton x64 preview](https://github.com/Fenris159/EDUP-NHOE/releases/download/v0.11.0-preview.1/EDUP-NHOE-0.11.0-preview.1-linux-proton-x64.zip)
- [Mac / CrossOver x64 compatibility preview](https://github.com/Fenris159/EDUP-NHOE/releases/download/v0.11.0-preview.1/EDUP-NHOE-0.11.0-preview.1-macos-crossover-x64.zip) — gamepad only; physical Mac testing pending.
- [All releases and checksums](https://github.com/Fenris159/EDUP-NHOE/releases)

Close the game and launcher, back up matching mod files, and copy the **contents** of `Copy-to-game` beside `EliteDangerous64.exe`. Keep the support folder intact and retain your existing settings. Read the included `INSTALL.txt` before copying over an existing DirectInput proxy.

[Windows instructions](docs/INSTALL-WINDOWS.txt) · [Linux / Proton instructions](docs/INSTALL-LINUX.txt) · [Mac / CrossOver instructions](docs/INSTALL-MAC.txt) · [Optional OpenXR registration](docs/OPENXR.txt)

SteamVR/Meta detection is automatic once the adapter loads. OpenXR requires the documented one-time layer registration. Linux/Proton also needs the documented launch wrapper and DLL override. The package does not change those settings automatically.

The Mac package contains five files to copy and a single `INSTALL.txt`. Set `dinput8` to **Native then Builtin** once in the game's CrossOver bottle, then launch normally. It omits VR helpers and Linux components, preserves your graphics setup, and routes through game-side XInput bindings rather than detouring CrossOver's system XInput code. ARM64EC/FEX bottles remain experimental; no physical Mac validation has been performed.

## Feedback and routing

| Feature | Behavior |
| --- | --- |
| Original ship and vehicle Motion | Restores the game's authored haptic paths; original envelopes and conditional mutes remain. |
| Odyssey additions | New patterns for mapped weapon fire, running contacts, jump/landing, jetpack, melee, reload, shield toggles and selected completion cues. These additions are separate from authored Motion. |
| Scorpion weapons | Light right-trigger turret pulses and a longer left-trigger/body missile-ejection push. |
| Walking | Walking-contact vibration is disabled. Running/sprinting retain contact feedback through a native speed gate. |
| GameInput | Separate main and trigger channels when the matched device and transport expose them. XInput provides main-motor fallback. |
| SteamVR / legacy Meta | Each trigger contribution blends into its corresponding hand. |
| OpenXR | Separate trigger output only with the application's existing negotiated Touch Pro profile and bound outputs; otherwise per-hand fallback. |

The bridge handles **haptic output only**. Your existing gamepad/VR controls and Steam Input configuration stay in use. It does not read buttons, axes, poses or trigger pressure, acquire controller input, or select a different VR runtime. Version-checked runtime hooks apply the haptic correction; installed game binaries are not edited.

## Settings

The supplied `ed-onfoot-haptics.ini` enables added effects at 60%:

```ini
[OnFoot]
Enabled=1
Strength=60

[Controller]
Output=auto
```

`Strength` ranges from 0 to 100. Set `Enabled=0` for original authored effects only. This removes the added contribution while preserving authored output. Existing configuration files should be kept when upgrading.

`Output=auto` uses usable GameInput output with XInput fallback. For compatibility troubleshooting, set `Output=xinput` and restart Elite to use two main motors through XInput. This setting changes haptic output only. Native GameInput trigger support depends on the platform transport; the redistributable alone cannot add it on Mac.

## Compatibility and preview limits

The supported executable version is `2026.10.23.334925`, SHA-256:

```text
ed5a8f8cd10f9a80d3bda5badd7ba1d4fcfc15996c8c7e018852cb170e264b63
```

Unsupported versions fail the restoration checks rather than applying incompatible hooks. Four-motor gamepad output depends on device capabilities, connection, permissions and the runtime transport; Steam Input can expose a reduced virtual device. The Linux preview does not install a controller permission rule.

The walk/run boundary of 2.0 native speed units and its emitter association still need controlled gameplay confirmation. Unknown gait stays quiet; surface coverage remains incomplete. Physical VR delivery and headset/frontend combinations also need testing. Passing synthetic tests does not establish a felt vibration.

Please [report feedback](https://github.com/Fenris159/EDUP-NHOE/issues/new/choose) with your controller, connection, platform, Steam Input state and the exact action tested. [Testing guide](docs/TESTING.md).

## Build and contribute

The runtime source, pinned third-party dependencies and synthetic tests are included. [BUILDING.md](BUILDING.md) describes native tests, Windows cross-compilation and VR builds. [CONTRIBUTING.md](CONTRIBUTING.md) describes the project boundaries and validation expectations.

Third-party components retain their included notices: [dependency inventory](THIRD-PARTY.md).
