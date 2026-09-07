# DLSS 5 Super Anus

An experimental 64-bit ReShade add-on that extends RenoDX DLSS with practical
DLSS 5 Neural Rendering controls, cost scaling, presets, an integrated DX11
bridge, and PNG screenshot pairs.

The current public release is **v1.0.0**. Download the packaged add-on from the
[Releases](https://github.com/An0sTheGreat/DLSS-5-Super-Anus/releases) page.

> This is an unofficial community project. It is not affiliated with or
> endorsed by NVIDIA, RenoDX, ReShade, or any game developer.

> [!IMPORTANT]
> **You must supply your own `nvngx_dlss.dll` and `nvngx_dlssnr.dll`.** These
> NVIDIA runtime files are required but are not included or redistributed by
> this project. Obtain them from a legitimate game, driver, or software
> installation for which you have permission to use the files.

## Features

- One add-on containing the RenoDX DLSS interface and Neural Rendering controls.
- Neural Rendering resolution from 25% to 100%, staged behind an Apply button.
- Matched Residual and Direct Reconstruction modes.
- Adjustable neural transfer, color strength, and reconstruction sharpness.
- Saved presets and rebindable controls.
- Automatic recovery when a game temporarily stops submitting a usable native
  DLSS input.
- Integrated experimental DX11-to-DX12 Neural Rendering bridge.
- F5 NR ON/OFF PNG pairs with SDR and HDR-aware capture modes.
- Bounded resource caching, GPU-fence retirement, and guarded recreation.

## Compatibility

| Runtime | Status | Notes |
| --- | --- | --- |
| DirectX 12 | Supported | Primary path; requires a compatible native DLSS SR/NR setup. |
| DirectX 11 | Experimental | Integrated bridge; the game must expose usable native DLSS SR inputs. |
| Vulkan | Not implemented | Source contains exploratory guards and probes only. |
| DirectX 9 / OpenGL | Not supported | No Neural Rendering backend is present. |

OptiScaler is optional, not required. Games without native DLSS inputs may need
a separate DLSS feeder and motion-estimation solution; those tools are not
bundled here.

## Installation

1. Close the game.
2. Install a 64-bit ReShade build with add-on support.
3. Supply compatible copies of `nvngx_dlss.dll` and `nvngx_dlssnr.dll`; they are
   required and are not provided by this project.
4. Back up and remove any older or standalone version of this add-on.
5. Extract `renodx-dlss5-super-anus.addon64` beside the game's ReShade DLL, or
   into the add-on search directory configured by ReShade.
6. Do not stack the standalone DLSSNR Cost Scaler proxy or companion with this
   build. If one replaced NVIDIA's DLL, restore the genuine DLL first.
7. Launch the game and open the **RenoDX DLSS** tab.

See [Installation](docs/INSTALLATION.md) for upgrade and troubleshooting notes.

## Quick usage

- Start with **Matched Residual**, **75%**, transfer/color at **100%**, and
  sharpness at **0%**, then press **Apply**.
- An applied value of **100%** bypasses the replacement and uses the original
  Neural Rendering path. Resolve controls are disabled at 100%.
- Lower values change the internal Neural Rendering workload only; they do not
  change the game's output resolution or its DLSS Super Resolution setting.

Default controls:

| Key | Action |
| --- | --- |
| F5 | Capture an NR ON/OFF PNG pair |
| F6 | Toggle Neural Rendering |
| F7 | Cycle Preset 1 → 2 → 3 → 1 |

All three keys can be rebound in the existing Controls section. See
[Usage and configuration](docs/USAGE.md) for every setting and capture behavior.

## Known limitations

- Compatibility varies by game, DLSS integration, driver, and ReShade build.
- Lower Neural Rendering resolution necessarily reduces neural detail.
- Performance gains must be measured in-game; reconstruction and snapshot work
  have their own cost.
- HDR screenshots are SDR-rendered PNGs intended to resemble the displayed
  image, not lossless HDR masters.
- Frame-generation observations in the tests do not certify generated frames.
- KCD2/XeFG flicker investigation is outside this release's scope.

## Source and development

The repository includes the add-on source, shaders, focused tests, fixtures,
patch/build tools, and development scripts. Large SDK/runtime payloads, local
reverse-engineering databases, generated objects, test logs, and release
binaries are intentionally excluded from Git.

The current implementation notes and validation record are in
[Cost Scaler v1](docs/NR_COST_SCALER_1.md). Build scripts are Windows developer
harnesses and expect Visual Studio Build Tools, the Windows SDK, ReShade headers,
Dear ImGui headers, the NVIDIA NGX/DLSS SDK, and MinHook. See
[Building](docs/BUILDING.md).

## Credits and licensing

- [RenoDX](https://github.com/clshortfuse/renodx) by Carlos Lopez Jr.
- [DLSSNR Cost Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) by xen.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu and contributors.
- [ReShade](https://github.com/crosire/reshade) by crosire and contributors.

No project-wide license has been declared for original modifications. Included
third-party material remains subject to its respective license; see
[Third-party notices](THIRD_PARTY_NOTICES.md) and the `licenses` directory.
