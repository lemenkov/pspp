#! /usr/bin/python3

import sys

outputs = []
for format in ["f", "comma", "dot", "dollar", "pct", "e"]:
    outputs += [open(format + '.txt', 'w')]

for line in sys.stdin:
    line = line[1:]
    for i in range(len(outputs)):
        outputs[i].write(line[i * 10:(i + 1) * 10] + '\n')
