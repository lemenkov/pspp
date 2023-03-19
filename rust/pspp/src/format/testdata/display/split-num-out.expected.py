#! /usr/bin/python3

import sys
import re

outputs = {}
for format in [
        "CCA", "CCB", "CCC", "CCD", "CCE",
        "COMMA", "DOLLAR", "DOT", "E",
        "F", "N", "PCT", "Z",
]:
    outputs[format] = open(format.lower() + '.txt', 'w')

for line in sys.stdin:
    line = line.strip()
    if line == '':
        continue
    m = re.match('([A-Z]+)', line)
    if m:
        outputs[m.group(1)].write(line + '\n')
    else:
        for f in outputs.values():
            f.write(line + '\n')
