# DLSS 5 Super Anus

An experimental 64-bit ReShade add-on that extends RenoDX DLSS with practical
DLSS 5 Neural Rendering controls, cost scaling, presets, an integrated DX11
bridge, and PNG screenshot pairs.

The current public release is **v1.0.3**. Download the packaged add-on from the
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
- Neural Rendering resolution from 25% to 150%, staged behind an Apply button.
- Matched Residual and Direct Reconstruction modes.
- Adjustable neural transfer, color strength, and reconstruction sharpness.
- Saved presets and rebindable controls.
- Automatic recovery when a game temporarily stops submitting a usable native
  DLSS input.
- Integrated experimental DX11-to-DX12 Neural Rendering bridge.
- Experimental native Vulkan post-DLSS Neural Rendering at 100% and one pass.
- F5 NR ON/OFF PNG pairs with SDR and HDR-aware capture modes.
- Bounded resource caching, GPU-fence retirement, and guarded recreation.

## Compatibility

| Runtime | Status | Notes |
| --- | --- | --- |
| DirectX 12 | Supported | Primary path; requires a compatible native DLSS SR/NR setup. |
| DirectX 11 | Experimental | Integrated bridge; the game must expose usable native DLSS SR inputs. |
| Vulkan | Experimental | Native post-DLSS path; currently limited to 100% resolution and one NR pass. Unsupported settings preserve native output. |
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
7. Launch the game and open the **RenoDX DLSS_A** tab.

See [Installation](docs/INSTALLATION.md) for upgrade and troubleshooting notes.

## Quick usage

- Start with **Matched Residual**, **75%**, transfer/color at **100%**, and
  sharpness at **0%**, then press **Apply**.
- At **100%**, the first pass uses the original Neural Rendering path. Later
  multipass evaluations substitute zero motion vectors to avoid temporal
  mismatch. Resolve controls remain disabled at 100%.
- Lower values change the internal Neural Rendering workload only; they do not
  change the game's output resolution or its DLSS Super Resolution setting.
- Values above 100% supersample only the internal Neural Rendering evaluation,
  then reconstruct it to the game's unchanged output resolution.

Default controls:

| Key | Action |
| --- | --- |
| F5 | Capture an NR ON/OFF PNG pair |
| F6 | Toggle Neural Rendering |
| F7 | Cycle Preset 1 → 2 → 3 → 1 |
| = / + | Increase Neural Rendering pass count |
| - / _ | Decrease Neural Rendering pass count |

All five keys can be rebound in the existing Controls section. See
[Usage and configuration](docs/USAGE.md) for every setting and capture behavior.

## Known limitations

- Compatibility varies by game, DLSS integration, driver, and ReShade build.
- Lower Neural Rendering resolution necessarily reduces neural detail.
- Values above 100% increase GPU workload and working-texture memory use.
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
[Building](docs/BUILDING.md) and the [scripts index](scripts/README.md).

## Credits and licensing

- [RenoDX](https://github.com/clshortfuse/renodx) by Carlos Lopez Jr.
- [DLSSNR Cost Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) by xen.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu and contributors.
- [ReShade](https://github.com/crosire/reshade) by crosire and contributors.

No project-wide license has been declared for original modifications. Included
third-party material remains subject to its respective license; see
[Third-party notices](THIRD_PARTY_NOTICES.md) and the `licenses` directory.
