# Building

The repository contains the maintained source and the developer harnesses used
for v1.0 validation. It is not a dependency-complete SDK checkout.

## Toolchain

- Visual Studio Build Tools with the x64 MSVC compiler and assembler.
- Windows 10/11 SDK with `dxc.exe`.
- Python 3.
- ReShade add-on headers and Dear ImGui headers.
- NVIDIA NGX/DLSS SDK headers and import libraries.
- MinHook for the experimental DX11 bridge.
- Vulkan headers for the exploratory Vulkan probe only.

The current `.cmd` files reflect the maintainer's Windows toolchain layout.
Review their `VCVARS`, SDK, ReShade, ImGui, NGX, and MinHook paths before running
them on another machine. Downloaded SDKs and proprietary runtime binaries are
intentionally ignored and must not be committed.

## Important entry points

- `src/neural_resolution_addon.cpp` — integrated add-on and UI entry point.
- `src/backends/dx12_backend.inl` — primary Neural Rendering backend.
- `src/backends/dx11_native_bridge.inl` — experimental DX11 bridge.
- `src/neural_resample.hlsl` — Cost Scaler reconstruction shader.
- `build-v66.cmd` — current integrated build line used by v1.0.
- `build-dx11-experimental.cmd` — DX11 bridge build/test harness.
- `test-cost-scaler.cmd` — focused Cost Scaler GPU validation.
- `test-api-transports.cmd` — API transport validation.

Generated shader headers under `src` are committed so the source snapshot is
self-contained. Regenerated objects, DLLs, executables, maps, SDK trees, test
fixtures, logs, and add-on binaries belong under ignored output directories.

## Validation used for v1.0

The v1.0 implementation passed the focused Cost Scaler GPU suite on NVIDIA and
WARP, PE validation, DX11 bridge fixtures, resource-lifetime tests, UI/layout
tests, key-binding tests, capture-policy tests, and SDR/scRGB capture fixtures.
See `docs/NR_COST_SCALER_1.md` for the detailed record and limitations.
