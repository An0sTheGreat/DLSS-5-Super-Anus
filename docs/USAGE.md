# Usage and configuration

## Presets and operating mode

Use the existing **RenoDX DLSS** preset buttons to select Off or Preset 1–3.
The most recently enabled preset is saved and restored on future launches.

Hook Method controls where the add-on looks for a usable DLSS input. **Auto** is
the normal starting point. **Upscaled** can help games whose focus state or frame
generation changes which source is visible. Manual choices are diagnostic tools;
their behavior depends on the game pipeline.

## Neural Rendering Performance

| Setting | Range | Behavior |
| --- | --- | --- |
| Neural Rendering Resolution | 25–100% | Stages the internal NR evaluation scale. Press Apply to activate it. |
| Reconstruction Mode | Direct / Matched Residual | Chooses how reduced NR output is combined with the native reference. |
| Neural Transfer Strength | 0–200% | Controls the strength of the neural edit. At 0%, output returns to the native reference, though NR still runs. |
| Neural Color Strength | 0–100% | Controls chromatic contribution relative to luminance/detail. |
| Reconstruction Sharpness | 0–100% | Applies after reduced-resolution reconstruction. Disabled at applied 100%. |

At applied 100%, the replacement is fully bypassed and original Neural Rendering
is used. The controls below the resolution setting become unavailable because
they do not affect that path.

Recommended baseline:

- Resolution: 75%
- Reconstruction Mode: Matched Residual
- Neural Transfer Strength: 100%
- Neural Color Strength: 100%
- Reconstruction Sharpness: 0%

Change one control at a time and compare stable scenes. Lower scale values reduce
the Neural Rendering workload but cannot preserve every detail from a full-scale
network evaluation.

## Controls

| Default | Action |
| --- | --- |
| F5 | Capture an NR ON/OFF pair |
| F6 | Toggle Neural Rendering |
| F7 | Cycle Preset 1 → 2 → 3 → 1 |

Click a binding in the Controls section, then press the replacement key. Preset
and toggle actions display an upper-right notification for three seconds: one
second at full opacity followed by a two-second fade.

## Screenshots

F5 or **Capture Screenshot** records a pair with Neural Rendering enabled and
disabled. PNG files are written under `DLSS5 Screenshots` in ReShade's base
directory.

- **HDR mode off:** captures the normal PNG path.
- **HDR mode on:** on DX12, captures successive displayed frames with a target
  gap below 100 ms and a hard 500 ms expiry. UI and small scene movement can
  differ between images.
- DX11 retains the native same-frame pair path.

HDR output is an SDR rendition stored as PNG. It is intended for convenient
visual comparison and is not a lossless HDR master.

Capture requires active Neural Rendering and registered lifetime tracking. If a
capture fails, check `ReShade.log` for the specific rejection reason.

## Configuration keys

The add-on stores values in `ReShade.ini`, principally under
`RenoDXNeuralResolution`. Relevant keys include:

- `CostResolveMode`
- `CostTransferPercent`
- `CostColorPercent`
- `LastEnabledPreset`
- `NRToggleKey`
- `PresetCycleKey`
- `NRScreenshotKey`
- `ScreenshotHDR`

Edit these through the ReShade interface where possible. Close the game before
manually changing the INI.

## Runtime API and Debug

Runtime API and Debug are collapsed by default. Runtime API reports the observed
presentation API and active backend. Debug counters are intended for issue
reports and may make the panel considerably taller when expanded.
