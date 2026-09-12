# Multipass motion-vector correction

This test candidate keeps the original motion vectors for the first Neural
Rendering pass and supplies zero motion to every subsequent pass in the same
configured pass group.

Motion vectors describe movement into the first evaluation. The image produced
by that pass becomes the input to the next pass without another game-frame
movement interval. Reusing the original vectors for pass two and later therefore
creates a temporal mismatch that can appear as Frame Generation flicker.

The correction reuses the existing working-texture path for later evaluations:

- At scales other than 100%, pass index 0 retains the existing bilinear
  motion-vector resampling.
- At 100%, pass index 0 retains the untouched original evaluation.
- Pass indices 1 through 9 use an explicit compute-shader zero-fill mode at
  every scale, including 100%.
- Color, depth, UI, exposure, reset history, and native-transition frames are
  unchanged. Native-resolution later passes use same-size working textures.
- The private DX11 consumer uses the same DX12 scaled evaluator, so it receives
  the correction without changing its transport or synchronization model.

The candidate identifies itself in `ReShade.log` as
`NR BUILD ID: 1.0.3-motion-zero.2`.

## Local validation

- Production DXIL completed 433 GPU resolves on both the RTX 5080 and WARP,
  including a nonzero source texture verified as all-zero after the new filter
  mode.
- V6.4/V6.5/V6.6 policy, lifetime, scale, UI, and static PE validation passed,
  including 10,000 multipass policy cycles and WARP multi-queue fences.
- Hardware RTX 5080 and WARP DX11/DX12 transport suites each completed 216
  resize/format cycles and 382 record calls.
- The native-100 recovery/FrameGen fixture completed 611 real NR evaluations,
  255 working-path calls, 28 FrameGen working-path calls, preset/pass churn, zero
  late duplicate FrameGen evaluations, and returned exit code 0.
- Package addon SHA-256: `C831823FAA3FF983B69C777F77177E57C8B90551054662C94E0C23C839EDC592`.

Synthetic fixtures verify routing and zero-fill behavior but do not render
generated frames. Final visual acceptance still requires a multi-pass Frame
Generation game test.
