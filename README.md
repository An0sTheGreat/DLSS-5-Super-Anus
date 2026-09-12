# DLAssAss 5 Tool

**Current release: v.1.0.0**

DLAssAss 5 Tool is a local Windows game-library manager for installing the
DLSS 5 Super Anus ReShade add-on, supplying your own NVIDIA DLSS runtime files,
installing ReShade with add-on support, and safely restoring replaced files.

The original standalone DLSS 5 Super Anus project is preserved on the
[`DLSS-5-Super-Anus-Legacy`](../../tree/DLSS-5-Super-Anus-Legacy) branch.

## Features

- Steam discovery and recursive scanning of user-selected drives or folders
- Steam-style cover library and sortable Explorer-style table views
- Graphics API detection using ReShade logs, executable imports, runtime files,
  executable names, and bounded binary evidence
- Explicit API selection for games supporting multiple graphics APIs
- Installation of the latest official ReShade build with add-on support and no
  shader packages
- One-click installation of the bundled add-on and validated user-supplied DLSS
  files
- Per-game backups and one-click restoration of the latest installation
- Game launching, folder access, renaming, hiding, and persistent view settings
- Steam and GOG cover lookup with executable-icon fallback and local caching

## Requirements

- 64-bit Windows 10 or Windows 11
- A game supported by the included ReShade add-on
- NVIDIA DLSS DLLs supplied by the user
- Internet access for ReShade version checks and game-cover discovery

The tool does not download or redistribute NVIDIA DLLs.

## Installation

1. Download `DLAssAss-5-Tool-win-x64.zip` from
   [GitHub Releases](../../releases/latest).
2. Extract the complete archive to a writable folder.
3. Open the included `DLSS Files` folder.
4. Add the NVIDIA DLLs you are legally permitted to use:

   | File | Purpose |
   | --- | --- |
   | `nvngx_dlss.dll` | DLSS Super Resolution |
   | `nvngx_dlssg.dll` | DLSS Frame Generation |
   | `nvngx_dlssnr.dll` | DLSS Ray Reconstruction / Neural Rendering |

5. Start `DLAssAss 5 Tool.exe`. The status bar confirms each valid DLL and
   identifies missing or mismatched files.

## Using the tool

1. Select **Scan Steam** to find Steam games automatically, or select
   **Add Search Directory** to scan another drive or folder.
2. Select a game in Library View or Folder View.
3. Review the detected executable, graphics API, ReShade state, add-on state,
   and available DLSS features.
4. If ReShade is missing, select **Install ReShade**. For a multi-API game,
   choose the API you intend to launch so the correct proxy DLL is installed.
5. Select **Install** to install the included add-on and every validated DLSS
   DLL currently available in `DLSS Files`.
6. Select **Play** to launch the detected game executable.

ReShade installation uses the latest official full add-on build available from
`reshade.me`, configures the selected API, and does not install shaders.

## Backups and restoration

Before replacing a managed file, the tool creates a per-game backup under its
local application-data directory. Select **Restore Latest** to restore the most
recent manager backup for the selected game.

Settings, logs, backups, and cached covers are stored under
`%LOCALAPPDATA%\DLSS5ManAger`. The legacy directory name is intentionally kept
so upgrades preserve existing game libraries and backups.

## Important notes

- Use ReShade's full add-on build only where appropriate; avoid multiplayer or
  anti-cheat-protected games unless the game explicitly permits it.
- Graphics API detection is evidence-based. Confirm the selected API when a game
  offers multiple renderers.
- Cover lookup may require a few moments after initial game discovery.
- NVIDIA DLLs in `DLSS Files` and add-on payload binaries are ignored by Git.

## Building from source

Install the .NET 8 SDK on Windows, then run:

```powershell
dotnet build .\DLAssAss5Tool.csproj -c Release
dotnet run --project .\tests\DLAssAss5Tool.Tests.csproj -c Release
```

To create a self-contained release, place the verified
`renodx-dlss5-super-anus.addon64` in `Payload`, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\publish.ps1
```

The publisher refuses to include NVIDIA DLLs and validates the add-on payload
against its pinned SHA-256 before creating the archive.

Third-party attribution is available in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
