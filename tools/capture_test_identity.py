"""Identify a retained internal request entry for a controlled test host only."""
import hashlib, json
from pathlib import Path
from patch_v6_addon import PeImage, read_map_symbols
root = Path(__file__).resolve().parent.parent
data = (root/'build/dx11-integrated-game-test.addon64').read_bytes()
addon = PeImage(bytearray(data))
embedded = PeImage(bytearray((root/'build/neural_resolution_dx11_embedded.dll').read_bytes()))
symbols = read_map_symbols(root/'build/neural_resolution_dx11.map', embedded.image_base)
section = addon.section(addon.section_count-1)
assert section[4] == b'.nr-dx11'
rva = symbols['embedded_request_screenshot'] + section[1] - 0x1000
assert section[1] <= rva < section[1]+section[0]-16
offset = addon.rva_to_offset(rva)
preset_symbols = [value for name,value in symbols.items() if name.startswith('?apply_preset_transaction@')]
assert len(preset_symbols) == 1
preset_rva = preset_symbols[0] + section[1] - 0x1000
assert section[1] <= preset_rva < section[1]+section[0]-16
preset_offset = addon.rva_to_offset(preset_rva)
print(json.dumps(dict(sha256=hashlib.sha256(data).hexdigest(), rva=f'{rva:x}', bytes=data[offset:offset+16].hex(),
    preset_rva=f'{preset_rva:x}', preset_bytes=data[preset_offset:preset_offset+16].hex())))
