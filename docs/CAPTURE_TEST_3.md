# Capture Test 3 — HDR and FrameGen test build

File: `build/integrated-capture-3/renodx-dlss5-super-anus.addon64`

SHA-256: `540e5334c48e1b85ab1e585cfc40a7c58f6185c075da4e34fcf8bc4384631dd2`

## Changes

- DX12 HDR mode captures final game frames, including post-processing and UI.
- ON/OFF capture briefly bypasses NR without changing your preset or saved settings.
- Targets under 100 ms; rejects pairs at 500 ms. NR bypass expires automatically.
- Uses the swapchain color space and Windows SDR white level for HDR-to-PNG conversion.
- Adds capture for the previously missed no-codec rendering path, including multiple passes/scaling.
- Still PNG only. Controls, custom bindings and menu positions are preserved.

## Test in Dawnwalker

1. Close the game. Back up the current unified addon outside active addon paths.
   Replace only that addon with this file. Keep your INIs and other dependencies.
   Do not load two unified RenoDX DLSS consumers at once.
2. Confirm `NR CAPTURE TEST 3` appears in the new ReShade.log.
3. Enable **HDR mode** in Controls. With NR active, use **Capture Screenshot**
   (or your saved screenshot key) first in Auto, then in FrameGen.
4. The pair appears under ReShade's base folder / `DLSS5 Screenshots`.
   `_NR_OFF.png` is the OFF image; the other PNG is ON. Files may finish writing
   after the short capture interval. UI and small movement differences are expected.
5. Compare the ON PNG with a Snipping Tool image of the same stationary scene.
   Verify NR immediately resumes, then repeat capture a few times.
6. Send the new log and comparison if brightness, FrameGen capture or NR recovery
   is still wrong. Look for `NR final ON recorded`, `NR final OFF recorded: gap=...`,
   and `NR screenshot pair written`. Do not infer success from the button alone.

## Limits and evidence

This is a test build, not a confirmed Dawnwalker fix. Local real-GPU capture tests
saved repeated SDR/scRGB pairs in 15–31 ms with verified PNG pixels. Those tests
simulate NR; actual FrameGen presentation and game-specific post-processing still
need your test. Timeout cases produced no partial files. Native NR capture/scaling
and prior HDR output passed separate GPU regression tests.

PNG output is an SDR rendition of HDR, not an HDR master or a guarantee of exact
monitor/Snipping Tool tone mapping. DX11 keeps its previous same-frame native pair;
the new final-screen HDR path is DX12-only. HDR mode off keeps native same-frame
capture. No driver update, game files, INIs or old packages were changed.
