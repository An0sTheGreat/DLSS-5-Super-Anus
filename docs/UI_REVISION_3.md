# UI Revision 3 — unified addon

Addon: `renodx-dlss5-super-anus.addon64`

SHA-256: `9aac8876db1e40377c684b4623fa977849480b18204d2efb8c168ae9082b55d6`

## Changes

- Section order: Advanced, Neural Rendering Performance, Debug, Runtime API,
  Controls, Links, About.
- Debug and Runtime API start collapsed. Both can be expanded normally.
- Resolution and sharpness sliders, Apply and the applied-scale indicator remain.
  The resolution/Apply explanation is removed.
- Removed the XeFG native-input compatibility checkbox and its following
  diagnostics, lower build label and frame-trace controls before Controls.
- Removed the DX11 experimental/discovery explanatory paragraphs.
- Rendering backends, presets/persistence, hotkeys/notifications, scaling,
  sharpness and optional OptiScaler behavior are unchanged from Game Test 2.

Identify this package by `NR UI REVISION 3` in ReShade.log. The retained
`NR INTEGRATED GAME TEST 2` log marker identifies its rendering backend.

## Install and check

1. Exit the game. Back up the current addon and settings INIs outside active
   addon search paths.
2. Replace only the unified addon with this package's addon. Do not also load
   the separate DX11 bridge or standalone neural-resolution addon. Keep existing
   ReShade, vendor/runtime DLLs and settings; no replacement INIs are supplied.
3. Check the section order above, then expand/collapse Debug and Runtime API.
   Confirm sliders, Apply, Controls, Links and About stay aligned at the left.
4. Check that existing presets, hotkeys and rendering still behave as before.
   If reporting an issue, copy ReShade.log before launching the game again and
   include a screenshot and the action that triggered it.

No game files were installed or changed by this build process. Use only where
mod injection is permitted. This package adds no Vulkan integration or XeFG
flicker fix. The working FFXIV Game Test 2 backend is retained; compatibility
with every DX11 game is not implied.

## Rollback

Exit the game, move this addon out of active addon paths, and restore your saved
addon/settings. The previous `build/integrated-game-test-2` package is unchanged.
If restoring the separate base-addon/bridge setup, restore that pair together;
do not combine it with this unified addon.

## Validation

- Actual shared UI functions passed 144 combinations / 432 ImGui frames covering
  section states, widths, font scales and backend availability. Default collapse,
  retained user expansion, section order, alignment and disabled-state isolation
  passed; another 54 shared layout frames and existing regression tests passed.
- Static validation passed: official sections preserved except nine verified
  hook sites; required UI marker present and removed UI text absent.
- Controlled FFXIV-shaped DX11 GPU host: 216 submitted frames, 431 NR evaluations
  at 50% scale / two passes, no transport skips and clean shutdown. All 6,220,800
  captured RGB components exactly matched Game Test 2; all RGBA components were
  finite. This is a controlled-host test, not an in-game UI acceptance test or
  a performance benchmark.
- Stable release and prior test packages were preserved. Vendor DLLs are not
  redistributed; the included license covers MinHook.
