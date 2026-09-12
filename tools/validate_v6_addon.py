"""Static regression checks for the unified V6.4 addon (not an in-game test)."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import struct
from pathlib import Path

from patch_v6_addon import (EXPECTED_SHA256, PATCHES, CAPTURE_PATCHES, PeImage,
    ADDON_NAME_POINTER_RVA, ORIGINAL_ADDON_NAME, DISPLAY_ADDON_NAME,
    OVERLAY_TITLE_PATCHES)


def imported_modules(image: PeImage) -> dict[str, set[str]]:
    modules: dict[str, set[str]] = {}
    rva, _ = image.directory(1)
    cursor = image.rva_to_offset(rva)
    while True:
        original_first, _, _, name_rva, first = struct.unpack_from("<IIIII", image.data, cursor)
        if (original_first, name_rva, first) == (0, 0, 0):
            break
        name_offset = image.rva_to_offset(name_rva)
        end = image.data.index(0, name_offset)
        module = bytes(image.data[name_offset:end]).decode("ascii").upper()
        functions = modules.setdefault(module, set())
        thunk_rva = original_first or first
        thunk = image.rva_to_offset(thunk_rva)
        while True:
            value = struct.unpack_from("<Q", image.data, thunk)[0]
            if value == 0:
                break
            if value & (1 << 63) == 0:
                hint_name = image.rva_to_offset(value) + 2
                name_end = image.data.index(0, hint_name)
                functions.add(bytes(image.data[hint_name:name_end]).decode("ascii"))
            thunk += 8
        cursor += 20
    return modules


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--addon", type=Path, required=True)
    parser.add_argument("--version", default="V6.4", choices=("V6.4", "V6.5", "V6.6"))
    parser.add_argument("--experimental-dx11", action="store_true")
    parser.add_argument("--experimental-vulkan", action="store_true")
    parser.add_argument("--dx11-game-test", action="store_true")
    parser.add_argument("--screenshot-capture", action="store_true")
    args = parser.parse_args()

    base_bytes = args.base.read_bytes()
    assert hashlib.sha256(base_bytes).hexdigest() == EXPECTED_SHA256
    base = PeImage(bytearray(base_bytes))
    addon = PeImage(bytearray(args.addon.read_bytes()))
    assert addon.section_count == base.section_count + 1
    new_section = addon.section(addon.section_count - 1)
    new_start, new_end = new_section[1], new_section[1] + new_section[0]
    entry = struct.unpack_from("<I", addon.data, addon.optional + 16)[0]
    assert new_start <= entry < new_end

    # Existing sections may differ only at the verified hook and title sites.
    allowed_offsets: set[int] = set()
    name_pointer_offset = base.rva_to_offset(ADDON_NAME_POINTER_RVA)
    allowed_offsets.update(range(name_pointer_offset, name_pointer_offset + 8))
    for rva, (expected, replacement) in OVERLAY_TITLE_PATCHES.items():
        offset = base.rva_to_offset(rva)
        assert bytes(base.data[offset:offset + len(expected)]) == expected
        assert bytes(addon.data[offset:offset + len(replacement)]) == replacement
        allowed_offsets.update(range(offset, offset + len(expected)))
    patches = PATCHES | CAPTURE_PATCHES if args.screenshot_capture else PATCHES
    for rva, (expected, _, kind) in patches.items():
        assert bytes(base.data[base.rva_to_offset(rva):base.rva_to_offset(rva) + len(expected)]) == expected
        allowed_offsets.update(range(base.rva_to_offset(rva), base.rva_to_offset(rva) + len(expected)))
        replacement = addon.data[addon.rva_to_offset(rva):addon.rva_to_offset(rva) + len(expected)]
        if kind == "lea7":
            assert replacement[:3] == expected[:3]
            target = rva + 7 + struct.unpack_from("<i", replacement, 3)[0]
        else:
            assert replacement[0] in (0xE8, 0xE9)
            displacement = struct.unpack_from("<i", replacement, 1)[0]
            target = rva + 5 + displacement
        assert new_start <= target < new_end
    actual_offsets: set[int] = set()
    for index in range(base.section_count):
        _, _, raw_size, raw, _, _ = base.section(index)
        for offset in range(raw, raw + raw_size):
            if base.data[offset] != addon.data[offset]:
                actual_offsets.add(offset)
    assert actual_offsets <= allowed_offsets
    assert len(PATCHES) == 9

    base_name_va = struct.unpack_from("<Q", base.data, name_pointer_offset)[0]
    base_name_rva = base_name_va - base.image_base
    assert bytes(base.data[base.rva_to_offset(base_name_rva):
        base.rva_to_offset(base_name_rva) + len(ORIGINAL_ADDON_NAME)]) == ORIGINAL_ADDON_NAME
    addon_name_va = struct.unpack_from("<Q", addon.data, addon.rva_to_offset(ADDON_NAME_POINTER_RVA))[0]
    addon_name_rva = addon_name_va - addon.image_base
    assert new_start <= addon_name_rva < new_end
    assert bytes(addon.data[addon.rva_to_offset(addon_name_rva):
        addon.rva_to_offset(addon_name_rva) + len(DISPLAY_ADDON_NAME)]) == DISPLAY_ADDON_NAME
    assert len(OVERLAY_TITLE_PATCHES) == 2
    # Internal config and preset namespaces intentionally remain compatible.
    assert b"RENODX-DLSS" in bytes(addon.data)

    # V6.2 must not patch the official presentation callback or preset slider.
    # Those two hooks caused the V6/V6.1 hotkey crashes and XeFG misrouting.
    for untouched_rva, size in ((0x0AEC60, 5), (0x0CC58B, 5), (0x0A82E3, 7),
                                (0x0B1F40, 5)):
        base_offset = base.rva_to_offset(untouched_rva)
        addon_offset = addon.rva_to_offset(untouched_rva)
        assert addon.data[addon_offset:addon_offset + size] == base.data[base_offset:base_offset + size]

    for directory in (1, 3, 5):
        rva, size = addon.directory(directory)
        assert new_start <= rva and rva + size <= new_end

    imports = imported_modules(addon)
    assert {"InitializeCriticalSection", "EnterCriticalSection", "LeaveCriticalSection",
            "DeleteCriticalSection", "GetTickCount64", "GetProcAddress",
            "GetModuleHandleW", "GetCurrentThreadId", "AcquireSRWLockExclusive",
            "ReleaseSRWLockExclusive"} <= imports["KERNEL32.DLL"]
    if args.screenshot_capture:
        assert b'NR PASS TEST 1:' in bytes(addon.data)
        assert b'NR AUTO RECOVERY 3:' in bytes(addon.data)
        assert 0x09C776 in CAPTURE_PATCHES
        # Keep the original source predicate/branch and native gate body intact.
        for start, size in ((0x09C77D,10),(0x0B1F40,32)):
            assert addon.data[addon.rva_to_offset(start):addon.rva_to_offset(start)+size] == base.data[base.rva_to_offset(start):base.rva_to_offset(start)+size]
        # Four extra observers plus the existing native observer cover every
        # direct B1F40 call in this pinned official image, without patching B1F40.
        assert {0x09E2A1, 0x09F8DD, 0x0B008D, 0x0C9D37} <= CAPTURE_PATCHES.keys()
        assert "GetAsyncKeyState" not in imports["USER32.DLL"]  # ReShade runtime input, not blocked Windows polling.
    else:
        assert "GetAsyncKeyState" in imports["USER32.DLL"]
    if args.version == "V6.4":
        assert "wsprintfA" in imports["USER32.DLL"]
    else:
        assert "__stdio_common_vsprintf" in imports["API-MS-WIN-CRT-STDIO-L1-1-0.DLL"]
        assert "wsprintfA" not in imports["USER32.DLL"]
        expected_section = b".nr-dx11" if args.experimental_dx11 else b".nr-" + args.version.lower().replace(".", "").encode("ascii")
        assert new_section[4] == expected_section
        base_imports = imported_modules(base)
        allowed_new = {"API-MS-WIN-CRT-STDIO-L1-1-0.DLL"}
        if args.screenshot_capture:
            allowed_new |= {"API-MS-WIN-CRT-MATH-L1-1-0.DLL", "OLE32.DLL"}
            assert imports["OLE32.DLL"] - base_imports.get("OLE32.DLL", set()) <= {
                "CoCreateInstance", "CoInitializeEx", "CoUninitialize", "CreateStreamOnHGlobal", "GetHGlobalFromStream"}
        if args.experimental_vulkan:
            allowed_new.add("BCRYPT.DLL")
            assert {"BCryptOpenAlgorithmProvider", "BCryptCreateHash", "BCryptHashData", "BCryptFinishHash"} <= imports["BCRYPT.DLL"]
        assert set(imports) - set(base_imports) <= allowed_new

    exception_rva, exception_size = addon.directory(3)
    exception_offset = addon.rva_to_offset(exception_rva)
    begins = [struct.unpack_from("<I", addon.data, exception_offset + offset)[0]
              for offset in range(0, exception_size, 12)]
    assert begins == sorted(begins)
    assert begins[-1] < new_end

    strings = bytes(addon.data[addon.rva_to_offset(new_start):addon.rva_to_offset(new_start) + new_section[2]])
    if args.experimental_vulkan:
        for marker in (b"NR VULKAN NATIVE 1", b"same-command-buffer post-DLSS NR", b"exact nvngx_dlssnr.dll not found"):
            assert marker in strings
    if args.dx11_game_test:
        assert args.experimental_dx11 and args.version == "V6.6"
        for marker in (b"NR INTEGRATED GAME TEST 2", b"DX11 game test: private NGX core shutdown result",
                       b"DX11 game test: private graphics/cache ownership released",
                       b"DX11 game test: fresh private core initialization permitted",
                       b"DX11 native SR bridge (awaiting NR evaluation)",
                       b"interception boundary: executable SDK", b"packed depth conversion setup"):
            assert marker in strings
        forbidden_markers = [b"TEST ONLY:", b"recycle probe", b"replacement probe"]
        if not args.experimental_vulkan:
            forbidden_markers.append(b"NVSDK_NGX_VULKAN_Init_Ext2")
        for forbidden in forbidden_markers:
            assert forbidden not in strings
    if args.experimental_dx11:
        assert args.version == "V6.6"
        for marker in (b"NR DX11 experimental", b"NVSDK_NGX_D3D11_CreateFeature",
                       b"NVSDK_NGX_D3D11_EvaluateFeature_C", b"NVSDK_NGX_D3D11_Shutdown1",
                       b"DX11 output copy queued"):
            assert marker in strings
    for marker in (b"Neural Rendering Resolution", b"Reconstruction Sharpness",
                   b"NR ON", b"NR OFF", b"PRESET 1", b"AppliedScalePercentV6",
                   b"LastEnabledPreset", b"OptiScaler XeFG", args.version.encode("ascii"),
                   b"XeFGNativeInputCompatibility", b"loaded settings from [%s]",
                   b"ReShadeRegisterEventForAddon"):
        assert marker in strings
    if args.screenshot_capture:
        assert b".exr" not in strings and ".exr".encode("utf-16-le") not in strings
        for marker in (b"NR CAPTURE TEST 3", b"NRToggleKey", b"PresetCycleKey", b"NRScreenshotKey",
                       b"PassCountIncreaseKey", b"PassCountDecreaseKey", b"NR PASSES: %u", b"ScreenshotHDR",
                       b"Capture NR ON/OFF pair", b"HDR mode", b"NR screenshot pair aborted", b"NR activity %s"):
            assert marker in strings
    else:
        assert b"Numpad /" in strings and b"Numpad *" in strings
    for forbidden in (b"RenoDXPresetCommitV61", b"uses RenoDX Present for real-frame-only",
                      b"official preset commit did not consume"):
        assert forbidden not in strings
    if args.version in ("V6.5", "V6.6"):
        for marker in (b"Runtime API", b"trace END", b"DX11-to-DX12 transport",
                       b"Vulkan native hook" if args.experimental_vulkan else b"Vulkan-to-DX12 transport"):
            assert marker in strings
    if args.version == "V6.6":
        assert b"NR UI REVISION 3" in strings
        for forbidden in (b"Capture 10-second frame trace", b"XeFG native-input compatibility",
                          b"Moving the slider only stages", b"Legacy replay bypass disabled:",
                          b"Working textures:", b"Recording-pinned:", b"Tracked native NR features:",
                          b"Last evaluation scale:", b"Unified addon V6.6",
                          b"Integrated DX11 game test: native", b"Experimental DX11 native-DLSS adapter:",
                          b"Early native-DLSS discovery enabled;"):
            assert forbidden not in strings
        for marker in (b"Presentation API (last observed)", b"DX12 evaluation observed",
                       b"external DLSS 5 Feeder", b"No motion estimator is bundled"):
            assert marker in strings

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint]
    kernel32.LoadLibraryExW.restype = ctypes.c_void_p
    handle = kernel32.LoadLibraryExW(str(args.addon.resolve()), None, 1)
    assert handle, f"LoadLibraryEx failed with {ctypes.get_last_error()}"
    kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
    kernel32.FreeLibrary(handle)

    print(f"{args.version} static validation passed (no GPU execution)")
    print(f"official sections preserved outside {len(patches)} verified hook sites and three display-name sites")
    print(f"new section 0x{new_start:X}-0x{new_end:X}; entry 0x{entry:X}")
    print(f"sha256={hashlib.sha256(addon.data).hexdigest()}")


if __name__ == "__main__":
    main()
