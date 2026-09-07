# Changelog

All notable public changes are documented here.

## [Unreleased]

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

[Unreleased]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.2...HEAD
[1.0.2]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/An0sTheGreat/DLSS-5-Super-Anus/releases/tag/v1.0.0
