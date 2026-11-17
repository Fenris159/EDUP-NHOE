# Changelog

## 0.10.1-preview.1

- Disable walking contacts while retaining running/sprinting through a native speed gate; gameplay confirmation remains pending.
- Keep four-channel GameInput output, XInput fallback and capability-aware VR output routing.
- Fix fallback focus/shutdown stops, stale-frame submission ordering and bounded queue draining.
- Reject queued inactive effects across readiness changes and preserve authored Motion when additions are disabled.
- Preserve native API behavior for unrelated controller indices and the application's OpenXR instance negotiation.
- Provide separate Windows and Linux/Proton manual-copy previews with checksums and installation instructions.
