# v1.0.3 Runtime Stability

This document describes the runtime-stability work included in public release v1.0.3.

## What changed

- Reserves a complete multi-pass working set before starting a scaled group.
- Keeps all passes in a group on the same route instead of mixing scaled and native output.
- If memory pressure or allocation failure makes a configuration unsafe, keeps that configuration on the native route until the user changes scale, pass count, hook method, or preset.
- Scales the working-set cap with pass count while retaining a hard upper bound.
- Restores a transparent upstream FrameGen callback at native 100% for manual hook modes.
- Allows a DX11 transport slot to rebuild after a safe pre-submit Reset or Close failure instead of permanently poisoning the session.
- Logs the exact loaded addon path, build identity, configuration schema, and late NGX attachment state.
- Keeps hook, pass, and preset changes in a stream epoch without invalidating
  the host's live native NR features.
- Reuses the DXGI adapter for budget queries and removes five-second render-path
  reports; detailed probes are emitted only for a user-requested frame trace.
- Retires at most one destructive resource per maintenance interval and counts
  compatible in-use or pooled sets before prewarming another multipass group.
- Associates FrameGen transitions with the observed source frame and MFG index.

## Install

1. Remove or rename every older copy of this addon beside the game executable.
2. Install only `renodx-dlss5-super-anus.addon64`.
3. Keep `ReShade.ini`; deleting it is not part of a normal update.
4. If configuration reset is needed for troubleshooting, back up the INI and reset only `[RenoDXNeuralResolution]`.

The user must supply compatible `nvngx_dlss.dll` and `nvngx_dlssnr.dll` files.

## Suggested game checks

Start each game at Auto, one pass, and 100% scale. Confirm the log contains:

- `NR BUILD ID: 1.0.3`
- `config-schema=7`
- `NR RUNTIME STABILITY 3`

Then test:

1. Two and three passes at 100%.
2. Two passes at 75% and 50%.
3. One pass at 101%, 125%, and 150%.
4. Toggle NR off and on, change presets, and refocus the game.
5. For FrameGen, compare manual FrameGen at 100% with a reduced scale.
6. For FFXIV/DX11, leave Auto active through several focus changes and loading transitions.

If a scaled configuration is rejected, it should remain visually stable on the native route instead of repeatedly retrying. Change one of the configuration fields above to start a new guarded attempt.

## Scope

This release does not attempt to fix KCD2 OptiScaler XeFG flicker. Vulkan and OpenGL neural-rendering backends remain future work.
