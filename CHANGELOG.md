# Changelog

## [v.1.0.1] - 2026-09-11

- Install and restore the add-on and supplied DLSS files beside the selected
  game executable, including games with deeply nested binary directories.
- Detect ReShade, the add-on, and DLSS files in the executable's actual folder
  and reject add-on installation when ReShade is not present there.
- Update the included add-on with upstream FrameGen multipass routing, stable
  scaled-resource admission, and the ordinal-safe export lookup fix.
- Preserve GPU fence protections and hold at native 100% after genuine resource
  admission failure instead of repeatedly alternating render dimensions.

## [v.1.0.0] - 2026-09-11

- Initial public release of DLAssAss 5 Tool.
- Added game discovery, cover-art library and sortable folder views.
- Added graphics API detection and explicit multi-API selection.
- Added official ReShade installation without shader packages.
- Added validated user-supplied DLSS file handling.
- Added add-on installation, backups, restoration, and game launching.
