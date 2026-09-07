# Build and test scripts

Command files are grouped here to keep the repository root focused on the
project documentation. Run commands from any working directory; each script
resolves the repository root from its own location.

## Build

Build commands are under `scripts/build/`.

| Command | Purpose |
| --- | --- |
| `build-v66.cmd` | Current v1-era integrated DX12 build and regression line. |
| `build-dx11-experimental.cmd` | Integrated experimental DX11 bridge; accepts modes such as `game-test`. |
| `build-framegen-host.cmd` | Builds the FrameGen/NR fixture host. |
| `build-final-screen-host.cmd` | Builds the final-screen screenshot fixture host. |
| `build-ffxiv-fixture.cmd` | Builds the FFXIV-shaped DX11 fixture. |
| `build-vulkan-probe.cmd` | Builds exploratory Vulkan probes; not a Vulkan product backend. |

The remaining `build-v6*.cmd`, `build-unified.cmd`, and `build.cmd` files are
retained development/history harnesses.

Examples:

```bat
scripts\build\build-v66.cmd
scripts\build\build-dx11-experimental.cmd game-test
```

## Test

Test commands are under `scripts/test/`.

| Command | Purpose |
| --- | --- |
| `test-cost-scaler.cmd` | Runs the focused Cost Scaler GPU suite; pass `warp` for WARP. |
| `test-api-transports.cmd` | Runs DX11/DX12 transport coverage; pass `warp` for WARP. |
| `test-integrated-ui.cmd` | Runs integrated UI regressions. |
| `test-capture-policies.cmd` | Runs capture, key-binding, activity, and timeout policies. |
| `test-capture-ui.cmd` | Runs screenshot/key-binding UI coverage. |
| `test-key-bindings.cmd` | Runs focused key-binding policy coverage. |
| `test-auto-source.cmd` | Runs Auto-source selection/recovery coverage. |
| `test-screenshot-files.cmd` | Validates screenshot files at a caller-supplied path. |

Examples:

```bat
scripts\test\test-cost-scaler.cmd
scripts\test\test-api-transports.cmd warp
```

These are maintainer-oriented Windows harnesses. Review
[`docs/BUILDING.md`](../docs/BUILDING.md) for required SDKs and local
toolchain paths before running them.
