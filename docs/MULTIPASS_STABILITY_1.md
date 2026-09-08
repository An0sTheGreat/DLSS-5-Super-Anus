# Multipass stability candidate 1

This local candidate is built on the newest `RenoDX DLSS S_A` source and keeps
the 25–150% Neural Rendering scale, pass hotkeys, presets, screenshots, Auto
recovery, and integrated DX11 backend.

The working-texture cache now follows the process-local DXGI video-memory
budget instead of reserving a fixed 1 GiB. It is capped at 512 MiB and leaves
additional headroom for the game, Frame Generation, and NGX feature memory,
which is not represented by the add-on's texture counter. If a complete
multipass group cannot be admitted safely, that group uses the native path and
briefly remains there while fence-retired resources drain.

Scale, preset, pass-count, hook, and on/off changes advance a configuration
epoch. Scaled work stays native until objects from the previous epoch are safe
to release. Unused pooled working textures are discarded after one second so
cutscenes, resource rotations, and swapchain changes do not leave obsolete
allocations resident indefinitely.

FrameGen callbacks now carry the callback token and observed
`DLSSG.MultiFrameIndex` through thread-local context. The add-on still preserves
RenoDX's selected hook and native callback decision; it does not synthesize or
replay generated-frame NR work.

Validation completed with the production binary and shader:

- 10,000 transition/lifetime cycles plus every 1–10 pass and 25–150% policy
  combination.
- 432 hardware GPU resolve cases across reduced, native, and supersampled
  scales.
- Hardware RTX 5080 and WARP DX11/DX12 transport suites.
- Integrated UI and all five saved keybindings.
- A 400-frame DX12 NR fixture with scale churn, preset/pass changes, Auto
  recovery, manual Upscaled/FrameGen routes, and synthetic FrameGen inputs:
  607 successful NR evaluations, 493 scaled calls, 102 FrameGen-scaled calls,
  no late automatic FrameGen duplicates, and successful manual FrameGen scale.

The fixture does not generate interpolated frames, so Dawnwalker, Black Flag,
Cyberpunk, and other game-specific acceptance still requires in-game testing.
