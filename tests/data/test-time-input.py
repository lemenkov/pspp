#! /usr/bin/env python
# Copyright (C) 2020  Free Software Foundation

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import re
import sys

fmt_name = sys.argv[1]
templates = sys.argv[2:]

times = (#  D  HH  MM     SS
         (  0,  0,  0,  0.00),
         (  1,  4, 50, 38.68),
         (  5, 12, 31, 35.82),
         (  0, 12, 47, 53.41),
         (  3,  1, 26,  0.69),
         (  1, 20, 58, 11.19),
         ( 12,  7, 36,  5.98),
         ( 52, 15, 43, 49.27),
         (  7,  4, 25,  9.24),
         (  0,  6, 49, 27.89),
         ( 20,  2, 57, 52.56),
         (555, 16, 45, 44.12),
         (120, 21, 30, 57.27),
         (  0,  4, 25,  9.98),
         (  3,  6, 49, 27.24),
         (  5,  2, 57, 52.13),
         (  0, 16, 45, 44.35),
         (  1, 21, 30, 57.32),
         ( 10, 22, 30,  4.27),
         ( 22,  1, 56, 51.18))

syntax_file = open('%s.sps' % fmt_name, 'w')
syntax_file.write('''\
DATA LIST NOTABLE FILE='%(fmt_name)s.input'/%(fmt_name)s 1-40 (%(fmt_name)s).
PRINT OUTFILE='%(fmt_name)s.output'/%(fmt_name)s (F16.2).
EXECUTE.
''' % {'fmt_name': fmt_name})
syntax_file.close()

expout_file = open('expout', 'w')
input_file = open('%s.input' % fmt_name, 'w')

def print_all_formats(d, h, m, s, template, formatted, expected, sign):
    if template != '':
        c = template[0]
        template = template[1:]
        if c == '+':
            assert sign == ''
            for new_sign in ('', '-', '+'):
                print_all_formats(d, h, m, s, template,
                                  formatted + new_sign, expected, new_sign)
        elif c == 'D':
            for f in ('%d', '%02d'):
                print_all_formats(0, h, m, s, template,
                                  formatted + (f % d), expected + d * 86400,
                                  sign)
        elif c == 'H':
            for f in ('%d', '%02d'):
                print_all_formats(0, 0, m, s, template,
                                  formatted + (f % (h + d * 24)),
                                  expected + h * 3600 + d * 86400,
                                  sign)
        elif c == 'M':
            for f in ('%d', '%02d'):
                print_all_formats(0, 0, 0, s, template,
                                  formatted + (f % (m + h * 60 + d * 1440)),
                                  expected + m * 60 + h * 3600 + d * 86400,
                                  sign)
        elif c == 'S':
            for f in ('%.0f', '%02.0f', '%.1f', '%.2f'):
                ns = s + m * 60 + h * 3600 + d * 86400
                formatted_s = f % ns
                print_all_formats(0, 0, 0, 0, template,
                                  formatted + formatted_s,
                                  expected + float(formatted_s),
                                  sign)
        elif c == ':':
            for f in (' ', ':'):
                print_all_formats(d, h, m, s, template, formatted + f,
                                  expected, sign)
        elif c == ' ':
            print_all_formats(d, h, m, s, template, formatted + ' ',
                              expected, sign)
        else:
            assert False
    else:
        # Write the formatted value to fmt_name.input.
        input_file.write('%s\n' % formatted)

        # Write the expected value to 'expout'.
        if sign == '-' and expected > 0:
            expected = -expected
        expected_s = '%17.2f\n' % expected
        expected_s = expected_s.replace(' 0.', '  .')
        expout_file.write(expected_s)


for template in templates:
    for time in times:
        d, h, m, s = time
        print_all_formats(d, h, m, s, template, '', 0, '')

