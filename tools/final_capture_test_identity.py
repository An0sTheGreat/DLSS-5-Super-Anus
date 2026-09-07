"""Test-only symbol locations in the current, verified embedded candidate."""
import json
from pathlib import Path
from capture_test_identity import root, data, addon, embedded, symbols, section
names = {'NR_FINAL_SUCCESS_RVA': 'g_successful_evaluations',
         'NR_FINAL_OFF_RVA': 'g_capture_off_until',
         'NR_FINAL_EVAL_RVA': 'scaled_evaluate_impl',
         'NR_TEST_SCALE_RVA': 'set_scale'}
result = {}
for key, name in names.items():
    matches = [v for k, v in symbols.items() if k.startswith('?' + name + '@')]
    assert len(matches) == 1, (name, matches)
    rva = matches[0] + section[1] - 0x1000
    assert section[1] <= rva < section[1] + section[0] - 32
    result[key] = f'{rva:x}'
result['NR_FG_CALLBACK_RVA'] = f"{symbols['observed_framegen_callback'] + section[1] - 0x1000:x}"
print(json.dumps(result))
