# Contributing

EDUP - NHOE restores authored Motion and adds explicitly separate haptic patterns. Keep those origins clear in code, documentation and feedback.

Preserve the haptic-only boundary: no button, axis, pose or trigger-pressure reads; no controller grabs, input focus requests, runtime switching or binding changes that alter the game's controls. Maintain per-hand fallback when an output endpoint is unavailable.

Keep version/signature guards, finite pulse bounds, focus/disconnect cancellation, authored stop ordering and safe queue handling. Disabling added effects must preserve valid authored output. Unknown devices, emitters and unsupported game builds must remain conservative.

Run the applicable builds and synthetic checks in [BUILDING.md](BUILDING.md). Tests should assert observable outputs with independent expected values and meaningful boundaries. Any new regression check should detect a deliberately broken copy of the behavior it protects.

Use issues for concise feedback or proposed changes, with platform/controller details and exact reproduction steps. Hardware testing should distinguish accepted API submissions from vibration that was physically felt.
