# Unified addon V6.6 — integrated DX11 game test 1

Experimental testing build, not a stable release. Vulkan integration comes next;
this build does not add native Vulkan, DX9 or OpenGL NR support.

## Included

- Existing DX12 path and unified controls: presets/persistence, Numpad `/` and
  `*`, three-second notifications, staged 50–100% resolution with Apply, and
  reconstruction sharpness below 100%.
- Experimental DX11 **native DLSS SR** capture, private DX12 NR evaluation and
  GPU-synchronized output delivery. DLAA through the same SR feature may work
  but has not been separately accepted. Native ray reconstruction is not captured.
- Guarded private cleanup and recreation at native DX11 NGX shutdown boundaries.
  Failed initialization, incomplete GPU work or ambiguous cleanup disables
  private processing and retains unsafe resources rather than forcing release.
- Runtime API stays its own section below Debug, above Neural Rendering
  Performance. OptiScaler remains optional. No new XeFG flicker fix is included.

## Before installation

Use a 64-bit offline/single-player test game where mod injection is permitted.
Do not use this experimental addon with anti-cheat-protected multiplayer.
Use your existing working ReShade addon-enabled installation and required NR
runtime files; this package does not install ReShade, NVIDIA runtimes or drivers.
The controlled GPU tests used RTX 5080, driver 616.56 and ReShade 6.8.0. Other
games/drivers are not certified. Do not change your driver just for this test.

1. Fully exit the game.
2. Back up your current RenoDX addon(s), `ReShade.ini`, relevant ReShade preset
   INIs and any existing RenoDX settings files **outside all addon search paths**.
   Keep your runtime DLLs and unrelated mods unchanged.
3. Remove the old RenoDX DLSS addon and standalone neural-resolution addon from
   active addon search paths by moving them into that backup. Do not leave two
   RenoDX DLSS builds or the separate neural-resolution addon active together.
4. Copy only `renodx-dlss5-super-anus.addon64` from this package into the same
   addon directory used by your old build. Keep your settings; no new INI is
   supplied and no game files are automatically changed by this package.

## First test

1. Start with a DX11 game that has native DLSS SR. Enable native DLSS in its
   graphics settings. Prefer no optional frame generation for the first baseline;
   keep a separate log if later testing OptiScaler/FG. These are not requirements.
2. Open RenoDX DLSS. The lower build label must say
   `Unified addon V6.6 - integrated DX11 game test 1`.
3. Runtime API initially reports an awaiting-input state. Once private NR
   evaluation is observed, its DX12 consumer is reported and scaling controls
   become available. **A DX12 consumer under DX11 presentation is expected.**
   If input capture started too late, toggle the game's native DLSS off/on once
   or restart. Do not keep changing controls if NR never becomes active.
4. Establish a baseline at 100% resolution, one pass and sharpness 0%. Then try
   75% and Apply, 50% and Apply, and return to 100%. Test sharpness below 100%.
   Test repeated Numpad `*` cycles, `/` off/on, and preset restoration on restart.
5. Open/close Debug and check alignment. If stable, try one loading transition
   and then more passes/settings changes. Record approximate FPS, settings and
   the action that caused a fault. A static screenshot/FPS alone does not prove
   correct frame delivery or bounded memory usage.

Existing DX12 games can be used for regression testing. For games without native
DLSS, the chosen route remains external DLSS 5 Feeder plus a compatible motion
estimator, but unverified API/Feeder combinations are not made supported here.
Unavailable input paths leave native output unchanged; they do not silently
fall back to a different API or generate missing depth/motion.

## Logs to send

- Copy `ReShade.log` **immediately after the test/crash and before relaunching**;
  a new session may overwrite it. Include `ReShade.ini` if settings matter.
- Include game name, actual API, driver version, native DLSS mode, NR preset,
  scale, sharpness, passes, optional FG/OptiScaler configuration, and reproduction
  steps. State whether the fault persists after turning NR off.
- The log should contain `NR INTEGRATED GAME TEST 1`, native SR feature capture,
  `private NGX init`, `DX11 output copy queued`, and session counters. When a
  game calls native NGX shutdown, successful teardown uses `DX11 game test:`
  markers. Missing shutdown markers alone can mean the game never called it.
- If relevant, use the menu's **Capture 10-second frame trace**, reproduce the
  issue during that interval, and send the resulting log. A crash before the
  buffered trace flush may leave it incomplete; still send the log.
- If a crash dump or Windows crash-event entry exists, include it. Neither is
  mandatory to begin. Remove unrelated personal paths before sharing if desired.

## Rollback

Exit the game, move this testing addon out of active addon paths, and restore your
backed-up addon set and INIs. Restore the old two-addon arrangement only as a
complete set; do not combine it with this unified build. No driver rollback is
needed because this package makes no driver changes.

## Scope of verification

Packaged addon SHA-256:
`8e621c98f9d884925700b00b7af6bf3993179a63659d95b76e883e932c435780`.

The exact game-test binary passed 216 native/private frames and 431 two-pass NR
evaluations at 50% scale, across eight distinct source-device replacements and
nine complete native/private sessions. Frame-180 RGB exactly matches the same
feature-reset schedule without device replacement (all 6,220,800 components).
No transport skips; the process exited cleanly without forced termination.
The game-mode UI test passed 108 ImGui frames, with another 54 layout/preset
regression frames. WARP guards cover incomplete/removed-device fences and
failed initialization; they are not physical-GPU device-loss certification.

The packaged binary is checked for original DX12 sections/hooks, required UI
markers, managed DX11 lifecycle markers and absence of synthetic probe behavior
or Vulkan initialization. Tests cover guard failures and the actual ImGui Runtime
API section. A controlled native-SR GPU fixture verifies output across repeated
source-device recreation. This does not establish long-session game stability or
fix every settings-related slowdown. Report failures rather than treating this
as a general compatibility release.

Source build: `build-dx11-experimental.cmd game-test`.
UI regression: `test-integrated-ui.cmd`.
MinHook's license is included separately; no vendor runtime DLL is redistributed.
