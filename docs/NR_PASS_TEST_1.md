# NR Pass Test 1 — September 6, 2026

This is a tracking fix and diagnostic test build, **not a confirmed fix for
Dawnwalker's persistent Waiting state**. No automatic NR reset or frame replay
has been added.

## Evidence

The supplied Dawnwalker log is preserved at
`build/nr-pass-recovery-1/Dawnwalker-ReShade.log`.

- At 21:41:37 the old 256-entry command-list registry filled.
- After selecting two passes, evaluations initially continued successfully.
- At 21:43:03 activity stalled at 13,359 entries / 13,359 successes. Returning
  to one pass did not resume it. There was no logged NGX evaluation failure.
- The old native-gate counter observed only one call site. Its frozen value
  cannot establish whether FrameGen/Present routes stopped or rejected work.
- The earlier HDR screenshot pair completed with a 31 ms recording gap, followed
  by minutes of NR activity. This log does not establish it as the cause.

## Implemented

1. Keep the command-list registry bounded at 256 entries, but recycle unpinned
   entries when full. Never recycle records referencing NR resources, including
   recordings with a pending PRE-Reset notification.
2. Re-register live commands at the verified parent callback and, on an NR
   evaluation lookup miss, recover the upstream native-to-ReShade private-data
   mapping. Upstream command destruction clears that mapping. A fully pinned
   registry still takes the original safe fallback; no budget increase, forced
   release, stale-pointer retry, or time-based GPU-completion assumption.
3. Observe all five verified direct calls to the original frame gate, forwarding
   each exactly once and preserving its decision. Log per-source calls,
   rejections, last frame/tick, parent calls/failures at pass changes and
   stall/resume events. Preserve the old native-only counter semantics.
4. Preserve all UI, bindings, presets, scaling/sharpness, screenshot/HDR behavior,
   API backends and optional OptiScaler behavior. No game files were installed.

## Validation

- DX11 integrated and DX12-only builds compile and pass PE validation: official
  image preserved outside 24 byte-verified hook sites; original gate body intact.
- Production registry test: 4,096 identities, bounded recycling, fully pinned
  rejection, PRE-Reset preservation, and reuse only after recording replacement.
- Actual WARP collector/fence tests and existing DX11 lifecycle mocks pass.
- Actual ImGui layout, disabled sharpness, bindings and capture-policy tests pass.
- Two real-NR DX11/private-DX12 fixtures each run two 1→2→3→1→off→2 sequences
  through the production preset transaction. One uses 50% scale / no-codec
  capture; one uses 100% / HDR. Each completes 400/400 SR evaluations, 650 NR
  evaluations and one two-pass screenshot pair. The 50% fixture reports all
  650 NR evaluations scaled. These are not Dawnwalker's native DX12 route.
- Final-screen SDR/scRGB fixtures each save three pairs, with 15–32 ms recording
  gaps and decoded pixels within one byte of expected colors. The actual D3D12
  copies are tested with simulated NR. Three deliberate timeout cases write no
  files and leave bypass inactive.
- These tests do not certify long-session stability, live scale transitions,
  native DX12 FrameGen multipass behavior, or recovery from Dawnwalker's stall.

## Dawnwalker test

1. Close the game. Back up its current addon, then install only the staged addon
   from `build/integrated-pass-test-1`. Keep existing INIs and avoid loading a
   second unified/standalone neural-resolution addon alongside it.
2. Confirm `NR PASS TEST 1:` appears near the beginning of the new ReShade.log.
3. Start at one pass with the same hook/FrameGen settings that showed the issue.
   Test 1→2→3→1, leaving each running for at least 10 seconds. Stop the sequence
   if NR fails; return to one pass and leave it running another 10 seconds.
4. Save the new ReShade.log before another launch overwrites it. Diagnostics are
   automatic; no special trace button is required. Note the failing pass count,
   hook mode and whether FrameGen was enabled.

Keep one pass as the temporary workaround. If the Waiting stall persists,
restart the game; this build intentionally does not force a potentially unsafe
in-place recovery. The next log is needed before selecting a source/gate fix.

Artifact SHA-256:
`f87228d40fca409e0c2b46277c9ddc687a59d64da3e40fd7385e6c7717e4a7bf`
