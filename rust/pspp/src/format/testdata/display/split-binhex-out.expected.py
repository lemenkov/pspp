#! /usr/bin/python3

import sys
import pathlib
import struct

outputs = {}
for format in ["P", "PK", "IB", "PIB", "PIBHEX", "RB", "RBHEX"]:
    outputs[format] = open(format.lower() + '.txt', 'w')

values = [
    ".",
    "2",
    "11",
    "123",
    "1234",
    "913",
    "3.14159",
    "777",
    "82",
    "690",
    "-2",
    "-11",
    "-123",
    "-1234",
    "-913",
    "-3.14159",
    "-777",
    "-82",
    "-690",
    "-.1",
    "-.5",
    "-.9",
    "9999.1",
    "9999.5",
    "9999.9",
    "10000",
    "18231237",
    "-9999.1",
    "-9999.5",
    "-9999.9",
    "-10000",
    "-8231237",
    "999.1",
    "999.5",
    "999.9",
    "1000",
    "8231237",
    "-999.1",
    "-999.5",
    "-999.9",
    "-1000",
    "-8231237",
    "99.1",
    "99.5",
    "99.9",
    "100",
    "821237",
    "-99.1",
    "-99.5",
    "-99.9",
    "-100",
    "-831237",
    "9.1",
    "9.5",
    "9.9",
    "10",
    "81237",
    "-9.1",
    "-9.5",
    "-9.9",
    "-10",
    "-81237",
    "1.1",
    "-1.1",
    "1.5",
    "-1.5",
    "1.9",
    "-1.9",
]

b = pathlib.Path('binhex-out.expected').read_bytes()
ofs = 0
for value in values:
    for f in outputs.values():
        f.write(f"{value}\n")
    x = ofs
    for d in range(4):
        for w in range(d + 1, 5):
            outputs["P"].write(f"P{w}.{d}: \"{b[x:x + w].hex()}\"\n")
            x += w
    for d in range(4):
        for w in range(d + 1, 5):
            outputs["PK"].write(f"PK{w}.{d}: \"{b[x:x + w].hex()}\"\n")
            x += w
    for d in range(11):
        for w in range([1, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4][d], 5):
            outputs["IB"].write(f"IB{w}.{d}: \"{b[x:x + w].hex()}\"\n")
            x += w
    for d in range(11):
        for w in range([1, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4][d], 5):
            outputs["PIB"].write(f"PIB{w}.{d}: \"{b[x:x + w].hex()}\"\n")
            x += w
    for w in [2,4,6,8]:
        outputs["PIBHEX"].write(f"PIBHEX{w}: \"{b[x:x + w].decode('UTF-8')}\"\n")
        x += w

    value = -sys.float_info.max if value == '.' else float(value)
    outputs["RB"].write(f"RB8: \"{struct.pack('>d', value).hex()}\"\n")
    outputs["RBHEX"].write(f"RBHEX16: \"{struct.pack('>d', value).hex().upper()}\"\n")

    ofs += 256
