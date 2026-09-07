# Cost Scaler replacement 1 — September 7, 2026

Replaces the old NR resampler inside the existing single addon. Not a stacked
proxy, a pre-SR implementation, or a new graphics API backend.

## Implementation

- Area-weighted color downsampling replaces the old bilinear color reduction.
  Motion/depth retain the established bilinear path and existing coordinate,
  jitter and motion-vector scaling. Caller descriptors remain unmodified.
- Each evaluation snapshots its full-resolution color input before NR. The
  default Matched Residual mode computes low-resolution output minus matching
  input, reconstructs that edit, and adds it to the native reference. Direct
  Reconstruction instead blends toward the enlarged neural output.
- Snapshots are per resource set, refreshed each pass (not prior-frame images),
  included in budget accounting, pinned with command recordings and retired by
  the existing real queue fences. In-place input/output snapshots are guarded
  by state transitions. Output origins are respected; out-of-bounds or
  non-1:1 native input extents use logged native-NR fallback.
- Native alpha is retained. Signed float values are not forcibly clipped to
  zero. The existing RenoDX codec remains responsible for transfer/encoding;
  no extra PQ/scRGB conversion or screenshot tonemapper is introduced here.
- At 100% the entire replacement is bypassed in favor of original NR. At 0%
  transfer the resolve returns the native reference, including alpha, even if
  sharpening is saved above zero. This does not bypass the NR inference cost.
- Preserves Auto Recovery 3, native frame gating and manual hook behavior.

## Controls

Neural Rendering Performance stays below Advanced and above Debug:

1. Neural Rendering Resolution: 25–100%, staged until Apply.
2. Apply / applied percentage.
3. Reconstruction Mode: Direct Reconstruction / Matched Residual (default).
4. Neural Transfer Strength: 0–200%, default 100%.
5. Neural Color Strength: 0–100%, default 100%.
6. Reconstruction Sharpness: existing saved value retained.

Resolve controls are disabled at applied 100% or when the backend is unavailable.
The staged slider does not enable them until Apply. Resolve/strength/sharpness
edits take effect without resource recreation. Configuration remains in the
existing ReShade section; added keys are CostResolveMode, CostTransferPercent
and CostColorPercent. No companion INI watcher or additional hotkeys.
Existing F5 screenshot, F6 toggle, F7 presets and rebinding are unchanged.

## Source references and deliberate differences

- https://github.com/xenmods/DLSSNR-Cost-Scaler/releases (1.0.3 reviewed)
- https://github.com/xenmods/DLSSNR-Cost-Scaler/blob/main/shaders.hlsl
- https://github.com/xenmods/DLSSNR-Cost-Scaler/blob/main/nvngx_dlssnr.ini
- MIT license retained in LICENSE-DLSSNR-Cost-Scaler.txt.

Shader algorithms adapted to our resource/descriptor ABI; upstream's DLL proxy,
hotkeys and fixed-frame retirement strategy are not adopted. We keep actual
GPU completion tracking. Sharpening is described as adaptive sharpening, not
claimed to be a verified AMD RCAS implementation. Color strength also works in
Direct mode. Signed values and explicit zero-transfer identity are retained.

https://github.com/matiasLombo/neural-upstream and its src/codec.hlsl.h were
reviewed. That project develops a scene-linear pre-SR input into a bounded NR
image, restores its luminance range and optionally reuses effects across frames.
Those are not drop-in changes to our existing codec stage. No code was copied
from it, and no pre-SR routing or cadence skipping was added. Its frame-pacing
warnings reinforce keeping evaluation cadence unchanged in this replacement.

## Local verification

- Both builds pass 27-site PE validation with official sections/exports retained.
- Production DXIL: 270 resolves on NVIDIA GPU and 270 on WARP. Area averages
  match CPU reference at 25/33/50/75/99%; matched identity retains high-frequency
  native pixels; transfer/color controls, signed HDR, alpha, offsets/sentinels
  and sharpening checks pass. Shader arithmetic tests use RGBA32F.
- Real DX12 NR: initial 25% fixture completed 607 NR evaluations. Final candidate
  Direct-mode churn fixture completed 605, with eight scale changes through
  25/50/75/99/100, one/two/three-pass and off/on preset transactions. Auto
  recovered native input; late automatic FG supplied zero duplicate work.
  Logged input formats include R10G10B10A2 and R11G11B10. No native fallback or
  texture-budget exhaustion was logged. Final sampled cache: 108/512 MiB,
  zero safety-held resources, 25 retired sets.
- Final DX11 bridge fixture: 400/400 native SR calls succeeded, with real private
  DX12 NR, repeated pass/off/on changes, PNG capture and clean private shutdown.
  Sampled cache 62/512 MiB, zero budget fallbacks. This includes the PQ codec
  path and RGBA16F private output; not a visual game HDR calibration test.
- ImGui: 456 sharpness click cases across 25–100%, plus 144 layout cases with
  all new controls present; section order, left alignment, default collapse,
  disabled state and surrounding UI isolation pass.
- Existing recording/fence collector, 10,000 lifetime-policy cycles, native
  feature/bridge guards, 2,000 Auto ownership races, hotkey and capture policies
  pass. Actual multi-queue WARP fence tests retain their existing coverage.
- Final-screen SDR and scRGB fixtures: three PNG pairs each, expected decoded
  pixels within 1 LSB; ON/OFF gaps 15–16 ms. Three 500 ms expiry cases restore
  bypass and write no files. Initial SDR attempt had ScreenshotHDR=0, selecting
  the codec path rather than the fixture's final-screen path; corrected rerun
  is preserved separately. No product capture change was needed.

Evidence directories: build/framegen-fixture-cost-scaler-1 (first candidate),
build/framegen-fixture-cost-scaler-churn (final),
build/api-native-capture-cost-scaler-final,
build/final-screen-fixture-cost-scaler-{sdr-verified,hdr-final,timeout}.
Commands: scripts/test/test-cost-scaler.cmd [warp], scripts/build/build-v66.cmd,
scripts/build/build-dx11-experimental.cmd game-test, scripts/test/test-integrated-ui.cmd,
scripts/test/test-auto-source.cmd, scripts/test/test-capture-policies.cmd, scripts/test/test-capture-ui.cmd.

## Limits and game acceptance

Controlled NR tests use synthetic scenes and FG observations, not generated
frames. They do not certify every game, visual equivalence to native NR,
long-session behavior or an FPS gain. Lower NR resolution still loses neural
detail; Matched Residual preserves the base, not the full-resolution network's
answer. Snapshot/resolve overhead means savings must be measured in-game.
The texture budget does not include the NVIDIA model's private allocations.
Actual aliased game inputs and unusual dynamic subrects still need game testing.
No Vulkan, DX9, OpenGL or pre-SR implementation is added. OptiScaler is optional.
KCD2/XeFG flicker investigation is excluded by the user's latest instruction.

## Package

build/integrated-cost-scaler-1/renodx-dlss5-super-anus.addon64

SHA-256: 54efefb405491ecaa6580a10dead86ed94c230dd9c29f6bfff09c0429a625f77

Old known-good package retained:
build/integrated-auto-recovery-3/renodx-dlss5-super-anus.addon64
SHA-256: 843153473da3d754898a0f40e4ef1150ebf6efd2806763f3326948c84f3741e0

No installed game files, game INIs or release-directory artifacts were changed.
