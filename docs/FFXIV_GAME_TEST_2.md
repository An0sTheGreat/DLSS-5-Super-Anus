# Integrated DX11 Game Test 2 — FFXIV compatibility update

Addon: `renodx-dlss5-super-anus.addon64`

SHA-256: `0be17453e119870eae77f465856c3d418ddc7e3d2b8519c519d2994f746c6624`

## Changes

- Prefer a complete set of native SDK exports in the game EXE when available.
  FFXIV exposes these, including `EvaluateFeature_C`. Otherwise retain the
  driver interception route. Hook only one boundary, and guard nested evaluation
  wrappers so one call cannot accidentally apply NR twice.
- Preserve the different shutdown ABIs: SDK frontend takes one argument;
  the tested driver entry takes two. Do not interchange them.
- Convert shader-readable, single-sample `R24G8_TYPELESS` depth into normalized
  `R32_FLOAT` depth on the GPU. Ignore stencil bits. Save/restore the full DX11
  context state around conversion, including the game's bound depth target.
- Start Debug collapsed. You can expand it normally; it is not forced closed
  each frame. Other sections keep their existing defaults.
- Retain existing DX12 behavior, presets/persistence, hotkeys/notifications,
  50–100% scaling with Apply, sharpness, and optional OptiScaler behavior.
  Vulkan integration is not included. No new XeFG flicker fix is claimed.

## Install for the next FFXIV test

1. Exit the game completely. Back up your current addon set, `ReShade.ini` and
   preset/settings INIs outside the game's active addon search paths.
2. Move the old RenoDX DLSS addon, **`dlss5-dx11-bridge.addon64`**, and any
   standalone neural-resolution addon out of active addon paths. This test must
   load just one unified DLSS addon; do not run the external bridge alongside it.
3. Copy this package's `renodx-dlss5-super-anus.addon64` into your existing addon
   folder. Keep existing ReShade, vendor NR/runtime DLLs and settings. No replacement
   INI or runtime DLL is supplied. No driver update is requested.
4. Launch FFXIV with its DLSS option enabled. Start with Hook Method Auto,
   resolution 100%, one pass and sharpness 0%. The lower build label must read
   **Unified addon V6.6 - integrated DX11 game test 2**. Debug should start closed.
5. Check for active NR. In Runtime API, DX11 presentation with a private DX12
   consumer is expected. If the native feature was created before discovery,
   toggle the game's DLSS setting off/on once or restart. If it remains waiting,
   save the log instead of cycling every Hook Method.
6. If active and stable, try 75% then Apply; test repeated `/` and `*`, then one
   loading transition. Save a separate log for each optional FG/OptiScaler test.

Use only where mod injection is permitted; do not bypass anti-cheat protections.
Testing is manual: this package has not modified or launched your game.

## Send back

Copy `ReShade.log` immediately after the test or crash, **before another launch**.
Attach the copied file itself, preferably named `FFXIV-game-test-2.log`, along
with API, driver, DLSS mode, NR scale/passes, optional FG/OptiScaler settings,
whether Debug opens/closes correctly, and the action that failed. Include
`ReShade.ini` if settings are relevant. A dump is useful if available, not required.

Expected diagnostics include `NR INTEGRATED GAME TEST 2`,
`interception boundary: executable SDK`, `native SR feature captured`,
`private NGX init` and `DX11 output copy queued`. If those are absent or a
`transport skipped` message appears, the log now reports its stage/error too.
The menu's 10-second frame trace can capture an intermittent rendering issue;
reproduce it during that window, then preserve the resulting log.

## Rollback

Exit the game, move this addon out of active addon paths, and restore your saved
addon set and INIs. Restore your working base-addon + external-bridge pair as a
pair; never combine it with this unified testing build.

## What was verified

- A synthetic EXE-SDK / `_C` / D24 / DLAA host reproduced Game Test 1's rejection:
  zero private frames/evaluations. Game Test 2 delivered 216 frames and 431 NR
  evaluations at 50% / two passes, with changed, finite GPU output and clean
  shutdown. This is **not an actual FFXIV game test**.
- The existing eight-device-replacement test still passed with all 6,220,800
  reference RGB components equal, no transport skips and clean teardown.
- RTX 5080 and WARP tests verified D24 normalization, exclusion of stencil bits,
  restoration of depth/compute bindings and 12 conversion resize/source cycles.
- Debug-default interaction, 108 Runtime API layout frames, 54 shared UI frames,
  registry/shutdown/fence failure guards and static addon checks passed.
- Driver tested: 616.56; native-runtime test fixtures use the existing local
  NVIDIA DLLs. Other drivers and long-session game behavior are not certified.

Source commands: `build-dx11-experimental.cmd game-test`,
`build-ffxiv-fixture.cmd`, `test-integrated-ui.cmd`, `test-api-transports.cmd`.
Only the addon and documentation/license are packaged; vendor DLLs are not.
