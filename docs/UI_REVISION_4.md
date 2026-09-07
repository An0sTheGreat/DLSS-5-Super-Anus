# UI Revision 4 — sharpness availability

Addon: `renodx-dlss5-super-anus.addon64`

SHA-256: `b31f73644a3d95beb7ce2b7dd8a752a105cd93326a8f543fb7e5a31c8fa08aae`

Reconstruction Sharpness is now grayed out and cannot be edited at an applied
Neural Rendering Resolution of 100%. At 50–99% it is editable when the backend's
NR controls are available. Staging a resolution value does not change this state;
press Apply to activate it. The saved sharpness value is retained while disabled.

All UI Revision 3 layout changes and the working Game Test 2 rendering backends
are retained. No rendering, preset, hotkey, OptiScaler or API behavior is changed.
Identify this update by `NR UI REVISION 4` in ReShade.log.

## Install / rollback

1. Close the game and back up your current unified addon outside active addon
   search paths. Keep existing INIs, ReShade and runtime/vendor DLLs.
2. Replace the unified addon with this package's addon. Do not also load a
   separate DX11 bridge or standalone neural-resolution addon.
3. At applied 100%, confirm sharpness is gray and uneditable. Stage 99%: it
   should remain disabled until Apply. Apply 99%: it should become editable.
   Apply 100% again: it should gray out without losing its saved value.
4. To roll back, exit the game and restore the backed-up addon. The previous
   `build/integrated-ui-3` package remains unchanged.

No game files were automatically installed or modified. Use only where mod
injection is permitted. This update adds no new API compatibility or flicker fix.

## Verified

- 306 actual ImGui click cases covering every applied scale from 50 through
  100, staged values 50/99/100 and backend availability. Disabled sliders rejected
  input and retained their value; enabled sliders accepted input. No disabled
  state or opacity escaped the section.
- Existing 432 compact-layout frames, 54 shared-layout frames and regression
  tests passed. Build lifecycle guard tests and static binary checks passed;
  official sections remain preserved except nine verified hook sites.
- No new in-game or NR GPU evaluation test was run for this UI-only update.
