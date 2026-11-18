# Changelog

## 0.12.0-preview.1

- Add a universal arm64/x86_64 macOS GameController/Core Haptics companion with automatic launch and independent handle/trigger output.
- Select native output conservatively for one native Xbox controller, one XInput gamepad and four usable actuator engines; preserve fallback for unavailable or ambiguous devices.
- Add authenticated loopback transport, bounded pulses, source/peer expiry, controller-generation fencing and quiet provider handoffs.
- Add a no-vibration capability check, native status reporting and a playback-failure backoff. Keep controller input and graphics configuration unchanged.
- Require macOS CI, a signed universal native app and all three platform bundles before publication. New native four-motor delivery still needs physical testing.

## 0.11.0-preview.1

- Add a minimal Mac/CrossOver gamepad package with five copyable files and one installation guide. Physical Mac output remains untested; ARM64EC/FEX is experimental.
- Route the Mac profile through verified game-side XInput bindings, preserving system XInput code and controller input APIs. Wait for audio initialization and stop restoration on foreign slot ownership.
- Preserve native GameInput capabilities on Mac, with XInput fallback and an optional startup-only `Output=xinput` setting for compatibility troubleshooting on all platforms.
- Retain the same authored Motion correction and Odyssey/Scorpion patterns, including disabled walking contacts and enabled running/sprinting.
- Always assemble Windows, Linux/Proton and Mac/CrossOver bundles together and validate their checksums before publication.

## 0.10.1-preview.1

- Disable walking contacts while retaining running/sprinting through a native speed gate; gameplay confirmation remains pending.
- Keep four-channel GameInput output, XInput fallback and capability-aware VR output routing.
- Fix fallback focus/shutdown stops, stale-frame submission ordering and bounded queue draining.
- Reject queued inactive effects across readiness changes and preserve authored Motion when additions are disabled.
- Preserve native API behavior for unrelated controller indices and the application's OpenXR instance negotiation.
- Provide separate Windows and Linux/Proton manual-copy previews with checksums and installation instructions.
