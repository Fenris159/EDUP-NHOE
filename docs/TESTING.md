# Preview feedback

Report the package version, operating system, Proton version if applicable, controller model, USB/Bluetooth/wireless connection, Steam Input state, and VR frontend/runtime if used.

Test one action at a time and report what you felt:

1. Original ship feedback during a familiar authored event, such as docking or undocking.
2. Walking, running and sprinting separately on the same surface. Walking should be quiet; running/sprinting should retain mapped contacts.
3. A short weapon burst and release, then a reload. Include weapon type and whether the feedback stopped.
4. A short jetpack boost, native cutoff and landing separately.
5. Scorpion turret fire and missile launch separately, noting main motors versus triggers or hands.
6. Focus loss/regain and disabling added effects. Background output should stop, and disabling additions should preserve any active original authored effect.

For VR, identify whether the output was per hand or separately localized at a trigger. Stock SteamVR and legacy Meta output blend trigger contributions into the corresponding hand. OpenXR trigger separation requires the application's negotiated profile and bound endpoints.

Known preview gaps include surface coverage, the inferred walk/run threshold, broader tool/combat mapping, and physical validation across VR frontends. A missing effect should be reported with its exact action and context.
