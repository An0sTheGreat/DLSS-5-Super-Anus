# FrameGen Probe 2 — September 6, 2026

Diagnostic continuation of the approved multipass/Waiting plan. **This is not
a confirmed fix for Dawnwalker's Auto/focus stall.**

## Evidence and scope

- User reports that Upscaled works with multiple passes, while Auto becomes
  active when unfocused and returns to Waiting when focused. AC Black Flag
  Resynced continues evaluating at two passes on its native SR path.
- Dawnwalker's direct native FrameGen callback can evaluate NR without traversing
  the five observed native/Streamline frame-gate sites. Frozen gate counters alone
  therefore do not prove that NR has stopped or explain why it stopped.
- The pinned upstream callback checks route eligibility, its per-thread recursion
  guard, feature classification, MultiFrameIndex, color/motion/depth resources,
  subrects and command state before evaluation. The inspected normal cleanup
  clears its guard. No guard leak or faulty frame-index rule is established yet.
- The read-only attempt to inspect the running game's process was denied. No
  privileges were enabled, process memory changed, or game files installed.

## Implemented

1. Wrap the two verified native NGX callback registrations. Call the original
   callback exactly once, retaining its return and parameter/resource behavior.
2. For calls carrying FrameGen color resources, sample diagnostics every two
   seconds: focus, thread, route flags, hook, pass count, recursion guard before
   and after, frame index/query result, resource pointers and descriptors, and
   the global NR evaluation counter before and after the call.
3. Keep the original frame-index checks, feature lifetime, pass selection,
   resource barriers, copies and source routing unchanged. No replay, automatic
   hook switch, forced release or unconditional guard reset. UI is unchanged.
4. Add an isolated native DX12 callback test host. Real NR runs with synthetic
   FrameGen inputs; this does not generate interpolated frames or emulate the
   game's focus-dependent scheduling.

Diagnostics are observations, not definitive rejection reasons. The evaluation
counter is global and other threads may advance it. Calls with neither FG color
resource are not logged by this probe; absent probe entries alone cannot identify
the rejection. Focus means the foreground window belongs to the game process.

## Validation

- Integrated DX11/DX12 and DX12-only builds pass; PE validation checks 26 hook
  sites, including the two address registrations. Original callback and gate
  bodies remain intact.
- Final candidate's native DX12 callback fixture completes 400 calls and 679
  actual NR evaluations across two 1→2→3→1→off→2 sequences. The original fixture
  completed 650; the final fixture restores preset 2 during startup, accounting
  for the additional 29 evaluations. Both use synthetic FG parameters, index 1,
  and an unfocused host. This does not reproduce the game failure.
- DX11 native SR/private DX12 NR at 50%: 400/400 SR successes, 370 enabled-frame
  submissions, 679 scaled NR evaluations, repeated preset/pass changes, and a
  two-pass PNG screenshot pair. Normal host cleanup completes.
- Final-screen SDR and scRGB tests each save three pairs, 31 ms recording gaps,
  decoded colors within one byte of expected values. These use simulated NR and
  real D3D12 copies. Three deliberate timeouts write no files and restore bypass.
- Registry/fence/lifetime tests, UI layout, disabled sharpness, screenshot-first
  controls, bindings, capture-chain and deadline policies pass.

Evidence: `build/framegen-fixture-probe2-final`,
`build/api-native-capture-fgprobe2-50`,
`build/final-screen-fixture-fgprobe2-{sdr-final,hdr-final,timeout}`.

## In-game comparison needed

1. Close Dawnwalker, back up the installed addon, and install the addon from
   `build/integrated-fg-probe-2`. Keep existing INIs and only one unified addon.
2. Verify `NR FG PROBE 2:` near startup in ReShade.log.
3. Keep FrameGen enabled, Auto selected and two passes. Stay focused for 10
   seconds, switch to another app for 10 seconds, then refocus for 10 seconds.
   Note whether NR changes Active/Waiting. Repeat once if convenient.
4. Switch to Upscaled for 10 seconds as a control. Save ReShade.log before a
   relaunch overwrites it. No trace button or additional setting is required.

Use the reported-working Upscaled mode as the temporary workaround. The exact
Auto rejection and an evidence-backed recovery fix remain pending this game log.

Artifact SHA-256:
`b62836a4e3799c950f9744963cdebc054c3bf546ec0bc7d0172b433ed37ccb81`
