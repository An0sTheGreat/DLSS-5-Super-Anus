# Capture Test 2 — interim input/UI and PNG-only update

Addon: `renodx-dlss5-super-anus.addon64`

SHA-256: `3ce0df6c748aa817ec596566f34fc51a2086bd74a6276a95c9ce7e9136a2cbd6`

## What changed

- Screenshot controls are above the NR toggle: Capture Screenshot / HDR mode,
  then the F5 screenshot binding, F6 NR-toggle binding and F7 preset-cycle binding.
- F7 is the preset-cycle default. Explicit saved bindings are preserved. If you
  previously saved numpad *, click that binding to change it to F7.
- Rebinding and hotkeys read ReShade runtime input, fixing the Windows keyboard
  suppression that left “Press a key” stuck while the overlay was open.
- Links and About start collapsed and can be expanded normally.
- Both SDR and HDR mode write **only two PNGs**. No EXR files are created.
- Screenshot failures now show a specific reason and log request/codec details.

## Install and test

1. Exit the game and back up the existing addon outside active search paths.
   Replace only the unified addon; keep existing INIs and vendor/runtime DLLs.
   Do not also load another RenoDX DLSS consumer or standalone DX11 bridge.
2. Check ReShade.log for `NR CAPTURE TEST 2`.
3. Open Controls, click a key binding and press an unused keyboard key. F7 is
   now assigned to cycling by default, so assigning it to another action is a
   conflict until the cycle binding is changed. Escape/click cancels. Restart
   to verify your customized binding persists.
4. Check Screenshot precedes Toggle, and Links/About initially collapse.
5. With NR active and Hook Method Auto/Upscaled, press F5 or Capture Screenshot.
   Wait for saved status. `DLSS5 Screenshots` under ReShade's base directory gets
   `<name>.png` (NR ON) and `<name>_NR_OFF.png` (input before NR). HDR mode still
   writes just these two PNGs, using the existing HDR-to-SDR preview conversion.

## Still unresolved — please do not treat this as a full capture fix

**FrameGen:** Auto capture is confirmed working in Dawnwalker; FrameGen is not.
Try one screenshot with FrameGen selected, keep the game running for at least
10 seconds, then preserve ReShade.log. New entries identify request receipt,
native/API codec counts, encoding and rejection reason. This build does not
switch your selected hook or change frame-generation rendering to obtain a shot.

**HDR brightness:** the supplied comparison images were not present at their
paths. The tone curve has not been adjusted without that evidence. PNG is an SDR
preview of the NR scene, not a capture of the final monitor image; later game
tone mapping, HUD, ReShade effects and Windows composition are excluded.
Please reattach the overbright PNG and the matching in-game comparison.

**Waiting:** successful-evaluation stall/resume diagnostics remain; no confirmed
automatic recovery fix is included. Vulkan compatibility is unchanged/paused.

## Verification / rollback

Real ImGui rebinding tests passed with Windows key state suppressed, together
with focus/conflict/persistence cases, screenshot-first order, collapsed sections
and 306 sharpness interaction cases. Existing lifecycle/retirement tests passed.
An RTX/DX11 HDR fixture completed 400/400 native SR evaluations and saved exactly
two valid 1920x1080 PNGs; native output matched Capture Test 1 byte-for-byte.
This does not certify every game or the unresolved FrameGen route.

Previous packaged builds and installed games were untouched. To roll back,
exit the game and restore your saved addon. Use only where injection is allowed.
