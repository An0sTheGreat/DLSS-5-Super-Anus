# Dawnwalker resource-pool fix — v1.0.1

Release notes for reduced-resolution freezes, gradual FPS loss, flicker and
black output during scale/preset/pass/hook transitions.

## Changes

- DX12 Neural Rendering history now belongs to the device/pass stream rather
  than a rotating game texture set. A new source identity no longer resets the
  same stream's temporal history.
- Fence-drained source bindings are released while their large private working
  textures are retained in a reusable pool.
- A stream owns at most four working sets. If every safe set is busy, that call
  retains the original native path instead of allocating another set or waiting.
- Applied scale, preset, pass-count and hook-method changes increment the stream
  generation and retain native output for the first observed transition frame.
- Old generations, 100% scale, NR Off and device teardown still release pooled
  resources through the existing nonblocking lifetime path.
- Cache diagnostics now include `pooled` and cumulative `rebound` counts.

## Install and test

Close the game, back up the current addon and replace it with
`renodx-dlss5-super-anus.addon64` from this folder. Keep the existing ReShade
configuration and load only one copy of the addon.

1. Start Dawnwalker at 100%, then apply 65% or 62%.
2. Play for at least ten minutes without changing settings and watch for gradual
   FPS loss or periodic flicker.
3. Toggle NR with F6, turn it back on and apply a different reduced scale.
4. Try Auto and Upscaled, then one manual hook transition while below 100%.
5. Confirm there is no freeze, black output or crash and that 100% still returns
   to the untouched native NR path.

In `ReShade.log`, `pooled` should remain bounded and `rebound` should increase
when Dawnwalker rotates source resources. A one-frame native handoff may occur
after a configuration transition. Attach the complete log if any issue remains.

## Local verification

- Full V6.6 build, 27-site PE validation and unchanged official sections.
- 64 repeated source identities using real WARP queue fences retained one
  93 MiB allocation and one stream-history reset.
- 40,000 device/pass history records reset once per generation.
- 270 production-DXIL Cost Scaler GPU resolves passed.
- DX11 bridge build, device replacement, teardown, UI, Auto recovery, capture,
  keybinding and screenshot policy regressions passed.

SHA-256:
`0845873e460bb87c16c6eac9db462ddc161bf2b895bb9bfe63b5152ae40f96e3`
