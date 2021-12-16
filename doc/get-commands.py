#! /usr/bin/python3
# Creates Texinfo documentation from the source

import re
import sys

print("""\
@c Generated from %s by get-commands.py
@c Do not modify!

@table @asis""" % sys.argv[1])
for line in open(sys.argv[1], 'r'):
    m = re.match(r'^\s*UNIMPL_CMD\s*\(\s*"([^"]*)"\s*,\s*"([^"]*)"\)\s*$', line)
    if m:
        command, description = m.groups()
        print("@item @cmd{%s}\n%s\n" % (command, description))
print("""\
@end table
@c Local Variables:
@c buffer-read-only: t
@c End:""")

