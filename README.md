# EDUP - NHOE

**Native Haptics Odyssey Expanded** restores authored controller vibration in Elite Dangerous and adds feedback for mapped Odyssey on-foot actions, Scorpion weapons, ship weapons, and Supercruise Overcharge. It supports gamepads and haptic output through SteamVR, Meta/Oculus PC, and compatible OpenXR frontends.

This is a **test preview**. The current release is **0.12.1-preview.5**.

## Download and install

- [Windows x64 preview](https://github.com/Fenris159/EDUP-NHOE/releases/download/v0.12.1-preview.5/EDUP-NHOE-0.12.1-preview.5-windows-x64.zip)
- [Linux / Proton x64 preview](https://github.com/Fenris159/EDUP-NHOE/releases/download/v0.12.1-preview.5/EDUP-NHOE-0.12.1-preview.5-linux-proton-x64.zip)
- [Mac / CrossOver gamepad preview](https://github.com/Fenris159/EDUP-NHOE/releases/download/v0.12.1-preview.5/EDUP-NHOE-0.12.1-preview.5-macos-crossover-x64.zip)
- [All releases and checksums](https://github.com/Fenris159/EDUP-NHOE/releases)

Close the game and launcher, back up matching mod files, and copy the **contents** of `Copy-to-game` beside `EliteDangerous64.exe`. Keep the support folder intact and retain your existing settings. Read the included `INSTALL.txt` before copying over an existing DirectInput proxy.

[Windows instructions](docs/INSTALL-WINDOWS.txt) · [Linux / Proton instructions](docs/INSTALL-LINUX.txt) · [Mac / CrossOver instructions](docs/INSTALL-MAC.txt) · [Optional OpenXR registration](docs/OPENXR.txt)

SteamVR/Meta detection is automatic once the adapter loads. OpenXR requires the documented one-time layer registration. Linux/Proton also needs the documented launch wrapper and DLL override. The package does not change those settings automatically.

The Mac package contains five loose files, a universal native `ed-macos-haptics.app`, and `INSTALL.txt`. Set `dinput8` to **Native then Builtin** once in the game's CrossOver bottle. Approve the native app through macOS security if prompted; opening it manually shows a capability check without sending vibration. Then launch Elite normally. With one native Xbox controller, one XInput gamepad and four usable actuators, the companion automatically routes each channel through Apple's GameController/Core Haptics APIs. Otherwise the existing GameInput/XInput fallback remains available. The app handles haptics only. macOS 11.3+ is required for the native companion; it is ad hoc signed and not notarized. The previous Mac XInput route is user-confirmed; the new native four-motor route needs physical testing. ARM64EC/FEX remains experimental.

## Feedback and routing

| Feature | Behavior |
| --- | --- |
| Original ship and vehicle Motion | Restores the game's authored haptic paths; original envelopes and conditional mutes remain. |
| Odyssey additions | New patterns for mapped weapon fire, running contacts, jump/landing, jetpack, melee, reload, shield toggles and selected completion cues. These additions are separate from authored Motion. |
| Scorpion weapons | Light right-trigger turret pulses and a longer left-trigger/body missile-ejection push. |
| Ship weapons | Main-motor feedback for mapped hardpoints, including held beams and individual multicannon shots. |
| Supercruise Overcharge | A 2.5-second uneven shake on engagement and a smooth 2.5-second slowdown on disengagement. |
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

[SCO]
Enabled=1
Strength=60

[ShipCombat]
Enabled=1
Strength=60

[Controller]
Output=auto
```

`Strength` ranges from 0 to 100. Set `Enabled=0` in `[OnFoot]`, `[SCO]`, and `[ShipCombat]` to play the original authored effects only. Keep your configuration when upgrading. To enable ship weapon feedback in an older configuration, add the `[ShipCombat]` section shown above.

`Output=auto` enables native four-motor discovery on Mac and uses GameInput/XInput fallback when unavailable. Windows/Linux retain their existing GameInput selection. For compatibility troubleshooting, set `Output=xinput` and restart Elite to use two main motors through XInput. This setting changes haptic output only. Native GameInput trigger support depends on the platform transport; the redistributable alone cannot add it on Mac.

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
