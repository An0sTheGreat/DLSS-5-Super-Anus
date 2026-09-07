# FrameGen and Present scaling — v1.0.2

Release notes for Neural Rendering resolution changes that previously remained
on the native path in manual FrameGen or Present modes.

## Changes

- Manual FrameGen callbacks now provide a callback-scoped transition token to
  every nested Neural Rendering pass.
- Present and other final-color routes use a pass-aware transition fallback
  when no native DLSS frame identifier advances.
- The first complete pass group after a scale, preset, pass-count, or hook
  change remains native. Reduced scaling starts with the following group.
- Scaling continues to process the resources and encoding supplied by the
  selected hook. It does not attempt to make Upscaled, FrameGen, and Present
  color grading identical because those modes observe different render stages.
- Diagnostics now report total scaled calls, FrameGen-scaled calls, native
  transition calls, and scale fallbacks.

## Game validation

1. In a FrameGen game, select **FrameGen**, apply 72% or another clearly reduced
   scale, and verify performance or image detail changes relative to 100%.
2. In Resident Evil 4 Remake, select **Present**, turn **Require DLSS** off, and
   repeat the 100% versus reduced-scale comparison.
3. Switch between 100% and a reduced value several times, then test F6 and a
   preset cycle.
4. Confirm that color at a reduced scale matches that hook method's own 100%
   appearance apart from the expected loss of neural detail.
5. If scaling still has no effect, attach the complete `ReShade.log`. The final
   activity line should show whether `scaled`, `fg-scaled`, or
   `scale-fallbacks` advanced.

Present mode still requires RenoDX to submit actual Neural Rendering work. The
addon cannot scale a route that performs no Neural Rendering evaluation.

## Verification

- Full V6.6 build and 27-site static PE validation passed.
- Manual FrameGen scale churn: 559 NR evaluations, 457 FrameGen-scaled calls,
  27 deliberate native transition calls, and zero scale fallbacks.
- Auto recovery plus manual FrameGen: 527 NR evaluations, 423 scaled calls,
  84 FrameGen-scaled calls, 29 deliberate native transition calls, zero late
  duplicate FrameGen work, and zero scale fallbacks.
- Present/no-native-DLSS one-pass and multi-pass transition sequences passed
  against the production transition implementation.
- DX11 bridge lifecycle, Cost Scaler HDR/signed-color identity, Auto ownership,
  UI, capture, screenshot, keybinding, resource-pool, and fence tests passed.

Integrated DX11/DX12 addon SHA-256:
`63447b8a5607cf32d549ae01fb37899b1c387dbc18aac4480e7fa2251ac6fab4`
