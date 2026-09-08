# Pass hotkeys and Neural Rendering supersampling

This local candidate adds two saved, rebindable controls to the existing
Controls section:

- `=/+`: increase RenoDX Neural Rendering pass count.
- `-/_`: decrease RenoDX Neural Rendering pass count.

Passes are clamped to RenoDX's native 1–10 range, written to the active preset,
and applied through the existing stream-transition guard. Each change displays
`NR PASSES: N` in the upper-right overlay.

Neural Rendering Resolution now spans 25–150%. An applied 100% is the exact
native RenoDX path. Other values scale only the internal Neural Rendering input
and output; the game output resolution and DLSS Super Resolution configuration
remain unchanged. Above 100%, native inputs are enlarged for NR evaluation and
the result is area-resolved back to the native output extent.

The visible ReShade tab is `RenoDX DLSS S_A`. Existing configuration and preset
section names are deliberately unchanged so saved settings continue to load.

Validation completed on the production shader and integrated binary: 432 GPU
resolve cases across 25–150%, 1,260 UI interaction cases, hardware and WARP
DX11/DX12 transport suites, and a real DX12 NR fixture cycling
50/75/99/100/101/125/150/75%. The fixture completed 757 NR evaluations with
scaled FrameGen work and no late automatic FrameGen duplicates. Its FrameGen
inputs are synthetic, so final game acceptance is still required.
