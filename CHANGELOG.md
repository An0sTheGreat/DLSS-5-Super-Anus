# Changelog

All notable public changes are documented here.

## [Unreleased]

## [1.0.3] - 2026-09-08

### Added

- Expanded internal Neural Rendering resolution from 25–100% to 25–150%, with
  100% retained as the exact native bypass and supersampled NR reconstructed to
  the game's unchanged output resolution.
- Added saved and rebindable pass-count controls, defaulting to =/+ to increase
  and -/_ to decrease, with 1–10 bounds and upper-right notifications.
- Added adaptive GPU-memory admission that preserves headroom for the game,
  Frame Generation, and NGX features.
- Added observed source-frame and MFG-index context plus synthetic 2x/3x/4x
  callback-cadence coverage.

### Fixes

- Separated stream configuration epochs from resource-allocation generations,
  preventing preset, pass-count, and hook changes from retiring live native NR
  features or leaving FrameGen stuck on `Waiting`.
- Removed periodic render-thread diagnostics and repeated DXGI factory creation;
  VRAM queries now reuse a cached adapter.
- Bounded background maintenance to one destructive retirement per interval,
  avoiding multi-resource cleanup spikes while NR and render locks are held.
- Reused compatible multipass working sets instead of continuously prewarming
  and retiring replacement resources.
- Prewarmed complete working-texture groups before scaled multipass begins,
  preventing a group from switching between scaled and native output midway.
- Latched unsafe scale/pass configurations to the native path until settings
  change, preventing repeated allocation and fallback flicker.
- Expanded the in-flight resource limit according to pass count while retaining
  GPU-memory headroom and a hard cache limit.
- Grouped FrameGen transitions using the observed source frame and MFG index.
- Restored a transparent upstream FrameGen call for manual hooks at native 100%.
- Rebuilt affected idle DX11 transport slots after safe pre-submission failures
  instead of permanently poisoning the session.

### Miscellaneous

- Renamed the visible ReShade tab to `RenoDX DLSS S_A` while retaining existing
  preset and configuration identifiers.
- Restricted detailed FrameGen diagnostics to explicit frame traces.
- Added a build ID, canonical filename warning, configuration schema,
  attachment-order report, and more precise stability diagnostics.
- Added runtime-lifetime, multipass, scaling, controls, capture, and FrameGen
  cadence regression coverage.

## [1.0.2] - 2026-09-07

- Fixed reduced Neural Rendering scale remaining on the native path after a
  scale or hook transition in manual FrameGen mode.
- Added a pass-aware transition fallback for Present and other final-color
  routes that do not expose an advancing native-DLSS frame identifier.
- Added FrameGen-scaled, transition-native, and scale-fallback diagnostics
  without changing the selected hook method's color encoding.

## [1.0.1] - 2026-09-07

- Reused fence-drained DX12 working textures when games rotate FrameGen input
  resources, preventing continuous large texture allocation and retirement.
- Limited each reduced-resolution evaluation stream to four working sets and
  retained the native path when every safe slot is busy.
- Moved Neural Rendering reset history from individual texture sets to the
  device/pass stream, preventing periodic history resets and reduced-scale
  flicker as source resources rotate.
- Added a native transition frame for scale, preset, pass-count, and hook-method
  changes so stale reduced-resolution output is never presented.
- Added real-fence rotation, stream-history, and transition regressions while
  preserving DX11, UI, screenshot, preset, and keybinding behavior.

## [1.0.0] - 2026-09-07

- Unified RenoDX DLSS controls and Neural Rendering performance controls into a
  single ReShade add-on.
- Replaced the previous full-image scaling path with Neural Rendering-only cost
  scaling from 25% through 100%.
- Added staged resolution changes with an Apply button and a native 100% bypass.
- Added Matched Residual and Direct Reconstruction modes.
- Added neural transfer, color-strength, and reconstruction-sharpness controls.
- Added automatic native-input recovery and guarded multi-pass handling.
- Added an experimental integrated DX11 Neural Rendering bridge.
- Added saved presets and rebindable F5/F6/F7 controls.
- Added timed upper-right preset and NR ON/OFF notifications.
- Added NR ON/OFF PNG screenshot pairs and HDR-aware display capture.
- Reorganized the ReShade interface, collapsed diagnostic sections by default,
  and disabled unavailable controls contextually.
- Added bounded resource caches and GPU-fence-based retirement for safer repeated
  scaling, pass, preset, and capture changes.
- Validated the Cost Scaler on NVIDIA hardware and WARP, plus focused DX11,
  lifetime, UI, hotkey, capture, and recovery fixtures.

[Unreleased]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.3...HEAD
[1.0.3]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/releases/tag/v1.0.0
