"""Validate decoded final-screen test pixels and timeout output (no image edits)."""
from pathlib import Path
import math
import re
import sys
from PIL import Image

root = Path(__file__).resolve().parent.parent
prefix = sys.argv[1] if len(sys.argv) > 1 else ''
for suffix, hdr in [('sdr-final', False), ('hdr-final', True)]:
    if suffix == 'sdr-final' and len(sys.argv) > 2:
        suffix = sys.argv[2] # Optional preserved rerun, e.g. sdr-verified.
    directory = root / ('build/final-screen-fixture-' + prefix + suffix)
    log = (directory / 'ReShade.log').read_text(errors='replace')
    gaps = [int(x) for x in re.findall(r'NR final OFF recorded: gap=(\d+) ms', log)]
    assert len(gaps) == 3 and max(gaps) < 500, gaps
    white = float(re.search(r'SDR-white=([\d.]+) nits', log)[1])
    files = sorted((directory / 'DLSS5 Screenshots').glob('*'))
    assert len(files) == 6 and all(p.suffix == '.png' for p in files)
    for path in files:
        color = [1.5, .5, .25] if hdr else [.4, .6, .8]
        if '_NR_OFF' in path.stem:
            color = [c / 2 for c in color]
        if hdr:
            color = [c * 80 / white for c in color]
            luminance = sum(c*w for c,w in zip(color,[.2126,.7152,.0722]))
            if luminance > .95:
                scale = (.95 + .05*(1-math.exp(-20*(luminance-.95))))/luminance
                color = [c*scale for c in color]
            color = [12.92*c if c <= .0031308 else 1.055*c**(1/2.4)-.055 for c in color]
        expected = [round(max(0,min(1,c))*255) for c in color]
        with Image.open(path) as image:
            assert image.size == (320,180)
            actual = image.getpixel((300,160))
            assert max(abs(a-b) for a,b in zip(actual,expected)) <= 1, (path,actual,expected)
    print(suffix, 'PASS: three pairs, exact expected pixels within 1 LSB, gaps', gaps, 'ms; SDR white', white)
timeout = root / ('build/final-screen-fixture-' + prefix + 'timeout')
log = (timeout/'ReShade.log').read_text(errors='replace')
assert log.count('NR final-screen capture expired at 500 ms') == 3
assert log.count('NR screenshot pair aborted before completion') == 3
assert not list((timeout/'DLSS5 Screenshots').glob('*'))
print('Timeout PASS: three expired pairs, restored bypass, zero output files.')
