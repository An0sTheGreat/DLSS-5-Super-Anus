# Put your NVIDIA DLSS files here

This folder is intentionally included but contains no NVIDIA binaries.

Place any DLLs you are legally permitted to use here:

- `nvngx_dlss.dll` — DLSS Super Resolution
- `nvngx_dlssg.dll` — DLSS Frame Generation
- `nvngx_dlssnr.dll` — DLSS Ray Reconstruction / Neural Rendering

The tool validates each file and copies every valid DLL available during installation.
Existing game files are backed up first. Never commit these DLLs to GitHub.
