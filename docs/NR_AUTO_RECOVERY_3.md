# Auto Recovery 3 — September 6, 2026

Implements the approved Auto handoff/fallback plan. Local tests pass; Dawnwalker
focus/FrameGen acceptance remains pending. This is a recovery path, not a claim
that the underlying disappearance of FrameGen inputs has been repaired.

## Finding

The first log read showed 1,416 successful evaluations followed by a stall at
23:14:03 with Auto/FG active, then native-source recovery at 23:14:11 when the FG
flag cleared. The game replaced that log during investigation. The newer 23:15
session also stalled at one pass and is preserved in
`build/auto-recovery3-game-evidence/Dawnwalker-current.log`.

The pinned native SR callback selects source 3 in Auto whenever FG is marked
active, withholding source-1 NR even with valid native inputs. Native FG probes
cease supplying usable observations during the stall. The sampled successful
calls have index 1 and a cleared recursion guard; no guard leak was established.

## Change

1. At the validated native SR descriptor's source-selection point, allow Auto to
   recover through native SR after 750 ms without successful observed FG/other
   source NR work. The initial native-input observation also starts a 750 ms
   grace period. This is a CPU eligibility interval, not a guaranteed recovery
   latency, GPU timeout or permission to free GPU resources.
2. Require a nonzero application frame newer than the last observed other-source
   frame, with no other-source NR callback in flight. Atomic ownership plus an
   epoch closes the callback enter/leave race; no blocking wait is added.
3. Once selected, retain native ownership for Auto for the rest of this process.
   Late native-FG NR callbacks and other-source NR parents are skipped, and
   other-source gates cannot consume Auto's native frame claim. The original
   native frame gate remains in force; no rejected native evaluation is replayed.
   This skips our NR work, not the vendor's frame-generation evaluation.
4. Explicit manual hook choices retain their original behavior. Returning to
   Auto uses its retained native fallback until the next game launch. No setting
   is saved for this fallback. UI, presets, hotkeys, API backends, capture,
   scaling/sharpness and resource-retirement rules remain unchanged.

The callback work counter used for health is global: unrelated concurrent NR can
delay fallback conservatively. No source buffers are retained for this decision.
Fallback requires the native SR callback and its validated inputs; it cannot
invent missing inputs or repair a stalled vendor/Feeder path. Manual FrameGen is
not silently converted to Upscaled and may still wait if its inputs stop.

## Verification

- Both integrated DX11/DX12 and DX12-only builds pass. Static validation checks
  27 byte-verified hook sites and the intact original source CMP/JE and native
  gate. The new assembly bridge preserves the displaced descriptor store and
  registers except the intentionally selected source; it has unwind metadata.
- Production ownership policy: initial grace, successful-work grace, nested and
  in-flight callbacks, same/backwards frame rejection, stable fallback and 2,000
  concurrent FG/native claim races pass.
- Real native DX12 SR/NR test with synthetic FG parameters: healthy FG work,
  missing producer, Auto takeover, late producer return, simulated FG-active
  flag changes, manual FrameGen/Upscaled, repeated 1/2/3/off preset changes.
  Final packaged candidate at 50% completes 625 NR evaluations: 516 native,
  69 initially healthy FG, 40 explicit-manual FG, zero late automatic FG.
  Per-frame assertions check no native duplication during healthy initial FG
  and exact native pass counts after recovery. A two-pass PNG pair is saved.
  Evidence: `build/framegen-fixture-recovery3-final`.
- Before the final startup-message-only rebuild, the same rendering code passes
  100% native-DX12 recovery (619 NR), DX11 50% (400/400 SR, 679 NR and PNG pair),
  and SDR/scRGB final-screen capture. Final-screen tests use simulated NR and
  real D3D12 copies; three pairs per mode record 15–32 ms apart with decoded
  colors within one byte of expected values. Three timeouts save no files.
  Evidence: `build/framegen-fixture-recovery3-verified`,
  `build/api-native-capture-recovery3-50`,
  `build/final-screen-fixture-recovery3-{sdr-final,hdr-final,timeout}`.
- WARP lifetime/fence/registry, UI layout/default collapse, 306 sharpness cases,
  bindings, capture chain and deadline policy regressions pass.

The host does not generate interpolated frames, reproduce Dawnwalker's actual
focus handling, or certify long-session stability. Game validation is necessary.

## Install and test

1. Close the game, back up the installed addon, then install only
   `build/integrated-auto-recovery-3/renodx-dlss5-super-anus.addon64`. Keep INIs.
2. Verify `NR AUTO RECOVERY 3:` near startup in ReShade.log.
3. Test Auto with FG enabled at two passes: focused 10 seconds, unfocused 10
   seconds, refocused 10 seconds. Then test one and three passes and NR off/on.
4. If needed, the log reports `selected native SR input` when recovery takes over.
   Save the log before relaunch. Also check an F5 pair and the manual hook choices.

No installed game addon, game INI or release artifact was replaced. Earlier
Pass Test 1 and FG Probe 2 package hashes remain unchanged.

SHA-256:
`843153473da3d754898a0f40e4ef1150ebf6efd2806763f3326948c84f3741e0`
