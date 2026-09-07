# Installation

> [!IMPORTANT]
> **This add-on requires user-supplied `nvngx_dlss.dll` and
> `nvngx_dlssnr.dll`.** NVIDIA runtime DLLs are not included or redistributed in
> this repository or its releases. Obtain compatible files from a legitimate
> game, driver, or software installation for which you have permission to use
> them.

## Requirements

- Windows 10 or Windows 11.
- A 64-bit game and 64-bit ReShade build with add-on support.
- A compatible NVIDIA GPU, driver, and DLSS setup.
- User-supplied compatible `nvngx_dlss.dll` and `nvngx_dlssnr.dll` files.
- A game that supplies usable native DLSS inputs. DirectX 12 is the primary
  path; DirectX 11 support uses the integrated experimental bridge.

## Fresh installation

1. Close the game completely.
2. Install ReShade with add-on support for the game's rendering API.
3. Supply compatible `nvngx_dlss.dll` and `nvngx_dlssnr.dll` files from your own
   legitimate installation. This project does not provide them.
4. Download the latest `DLSS-5-Super-Anus-v*.zip` package from GitHub Releases.
5. Extract `renodx-dlss5-super-anus.addon64` beside the ReShade DLL in the game
   directory, or into the add-on search path configured in ReShade.
6. Start the game, open ReShade, and select the **RenoDX DLSS** tab.
7. Confirm that the add-on appears under ReShade's **Add-ons** tab and that the
   Runtime API section reports the expected presentation API.

ReShade's loader path varies by game. `dxgi.dll`, `d3d11.dll`, or another proxy
name may be used by ReShade; do not rename the `.addon64` file to one of those
proxy names.

## Upgrading

1. Close the game.
2. Back up the currently installed `.addon64` file and `ReShade.ini`.
3. Replace the old add-on with the new release file.
4. Keep only one copy of this add-on in ReShade's search paths.

Saved presets, key bindings, screenshot mode, and Cost Scaler values live in
ReShade configuration and should survive replacement.

## Avoid incompatible stacking

- Do not load an older unified build or the standalone neural-resolution add-on
  beside this build.
- Do not stack the standalone DLSSNR Cost Scaler proxy or companion.
- If another mod replaced NVIDIA's DLSS Neural Rendering DLL, restore the genuine
  DLL using that mod's backup instructions before testing this add-on.
- OptiScaler is optional. Test without it first when diagnosing a problem.

## Basic verification

After launch, search `ReShade.log` for `NR COST SCALER 1:`. Then:

1. Apply 75% with Matched Residual selected.
2. Toggle Neural Rendering with F6.
3. Cycle all three presets with F7.
4. Return to 100% and confirm the reconstruction controls become unavailable.

If the status stays at **Waiting**, confirm the game is actively producing a
supported native DLSS input. Try the **Upscaled** hook mode for diagnosis. Games
without native DLSS may require a separate feeder and motion-estimation solution.

When reporting an issue, attach `ReShade.log` and include the game, rendering
API, GPU, driver, ReShade version, DLSS/OptiScaler configuration, hook mode, and
the exact action that triggered the problem.
