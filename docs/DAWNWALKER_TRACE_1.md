# Dawnwalker FrameGen callback trace

This diagnostic candidate identifies itself in `ReShade.log` as
`NR BUILD ID: 1.0.3-dawnwalker-trace.1`.

It retains the multipass motion-vector correction from
`1.0.3-motion-zero.2`. It does not add a speculative FrameGen workaround.

## Capture procedure

1. Install only this package's `renodx-dlss5-super-anus.addon64`.
2. In Dawnwalker, select the FrameGen hook, 100% NR scale, and one NR pass.
3. Open the addon's ReShade panel and click **Capture Dawnwalker trace**.
4. During the next ten seconds, increase the NR pass count to two and reproduce
   the FrameGen failure.
5. Leave the game running until `NR V6.6 trace END` appears in `ReShade.log`,
   then send the complete log.

Each `NR FG trace entry` should have a matching `NR FG trace exit`. The records
include the gap since the previous callback, source frame, MFG index, selected
hook and pass count, command list, whether the vendor callback was called, its
return value, and the number of NR evaluations injected during that callback.

The trace is bounded to 4096 records and performs no extra GPU work. Additional
parameter inspection on the native 100% bypass occurs only during the explicit
ten-second capture.
