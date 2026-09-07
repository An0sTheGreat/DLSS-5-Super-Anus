# Screenshot Capture Test 1

Addon: `renodx-dlss5-super-anus.addon64`

SHA-256: `e27cb5ec971534affd24343456a9d3bbad8cc39b63944c5c385953b6c82ccd46`

## Install and try

1. Exit the game and back up your current addon outside active addon search
   paths. Keep your INIs and existing vendor/runtime DLLs.
2. Replace only the unified addon with this package. Do not also load another
   RenoDX DLSS consumer, standalone neural-resolution addon or DX11 bridge.
   This package is not an installer and has not modified any game files.
3. Check ReShade.log for `NR CAPTURE TEST 1`. With native DLSS and active NR,
   use Hook Method Auto or Upscaled and Encoding Auto for the first test.
4. In the existing Controls section, the default bindings are:
   - F6: toggle Neural Rendering (replaces numpad /).
   - Numpad *: cycle preset 1 -> 2 -> 3 -> 1.
   - F5: capture the NR ON/OFF pair.
   Click a key button, then press your replacement key. Escape/click cancels.
   Bindings are single keyboard keys; Escape is reserved for cancel. Duplicate
   action bindings are rejected. Avoid keys used by the game or other addons.
5. Enable **HDR mode** when using HDR. Press F5 once with NR active; wait for
   the saved status. Files appear in `DLSS5 Screenshots` under ReShade's base
   directory (normally alongside ReShade.ini). Do not hold/repeat F5 while
   a capture is pending. The Capture Screenshot button does the same thing.

## Output

- `NR_CAPTURE_<time>_<evaluation>.png`: NR ON.
- `NR_CAPTURE_<time>_<evaluation>_NR_OFF.png`: native scene input before NR.
- HDR mode adds matching `.exr` masters for both images.

The pair comes from the same codec transaction around all NR passes. It does
not temporarily switch your NR state or selected preset. The OFF image is the
pre-NR scene input, not a second independently rendered frame with NR disabled.
The three-second toggle/preset notifications and existing preset persistence
are retained. Key/HDR preferences persist in the existing INI namespace.

EXR preserves floating-point HDR values rather than clipping them into PNG.
It requires an HDR-aware viewer/editor. PNG is an SDR tone-mapped preview;
it does not reproduce Windows/game display tone mapping exactly. Native SR
scene-linear values remain relative; display-referred PQ/scRGB uses 80-nit
scRGB units. No automatic exposure calibration is claimed.

## Tested and limitations

Controlled RTX/DX11 native-DLSS tests passed at 100%/one pass and 50%/two passes.
The saved ON master matched native renderer RGB exactly, including an HDR scene
with values up to 9.25. Capture-on versus no-capture renderer output also matched
exactly. Real ImGui interaction and policy/retirement tests passed. These are
controlled tests, not a guarantee against every game-specific regression.

This reproduces the screenshot workflow, not ShortFuse's unavailable private
implementation or guaranteed identical pixels. Capture currently targets the
native SR/AA/RR codec route. Full-sized output and supported encoding are needed;
scRGB-nl, nonzero source/destination rectangles and codec-bypass routes are not
supported. Present/FrameGen/Feeder capture combinations are not certified.
Native DX12 codec interception is built and statically verified; new capture
behavior still needs DX12 in-game testing. Vulkan is not added in this update.

Images are captured before later game HUD/postprocessing, ReShade effects and
Windows composition. They are not whole-desktop screenshots. Unsupported routes
time out with a log message instead of guessing colors or saving stale frames.

Only one request can own GPU/readback storage at a time, within the existing
512 MiB budget. At very high resolution, or with other live working textures,
capture can be refused for budget reasons. Encoding runs on a worker after
GPU fences complete; the capture frame can still incur allocation/copy cost.
Wait for saving to finish before exiting the game. Normal errors remove only
the partial files created by that request; a forced process exit can interrupt
file writing. Existing screenshot files are never overwritten.

## Waiting investigation

This build adds `NR activity stalled` / `NR activity resumed` diagnostics using
successful NR evaluations. It does **not** contain a confirmed Waiting recovery
fix. No frame replay, gate bypass or speculative feature reset was added.
If Waiting recurs, preserve ReShade.log before restarting and report the game,
hook method, NR scale/pass count and the action preceding the stall.

## Game acceptance and rollback

Test several F6 toggles and at least two full numpad-* preset cycles. Rebind a
key, restart and verify persistence. Capture at 100%, then 50% with two passes;
confirm both images save at full output size. Try HDR mode on and off. Repeat
after a loading screen, watching for stalls or sustained memory growth.

For errors, provide ReShade.log and the affected PNG/EXR pair, if present.
To roll back, close the game and restore your saved addon. UI Revision 4 and the
existing release remain unchanged. Use only where addon injection is permitted.
