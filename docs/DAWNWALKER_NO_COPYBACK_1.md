# Dawnwalker no-copyback A/B test

This diagnostic candidate identifies itself in `ReShade.log` as
`NR BUILD ID: 1.0.3-dawnwalker-no-copyback.2`.

It still evaluates later Neural Rendering passes, including zeroed motion
vectors, but does not copy a later FrameGen pass result back to the caller's
output. Native, Upscaled, one-pass, and non-FrameGen routes are unchanged.

Visual corruption is expected. This package is only intended to determine why
Dawnwalker stops scheduling FrameGen callbacks.

## Test procedure

1. Install only this package's `renodx-dlss5-super-anus.addon64`.
2. Select the FrameGen hook, 100% NR scale, and one NR pass.
3. Increase to two passes and keep playing for at least 30 seconds.
4. Record whether FrameGen continues or stops. Send the complete `ReShade.log`.

The log must contain both of these markers:

- `NR DAWNWALKER A/B 1: later FrameGen passes still evaluate`
- `NR DAWNWALKER A/B 1: pass=1 evaluated successfully`

The second marker confirms that the test branch actually ran. A result without
it is inconclusive.

If FrameGen continues, later-pass copyback triggers Dawnwalker's shutdown. If it
still stops, merely recording the extra NR work inside the DLSSG callback is the
remaining cause.
