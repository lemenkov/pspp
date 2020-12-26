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

dates = (#yyyy  mm  dd  jjj  HH  MM  SS
         (1648,  6, 10, 162,  0,  0,  0),
         (1680,  6, 30, 182,  4, 50, 38),
         (1716,  7, 24, 206, 12, 31, 35),
         (1768,  6, 19, 171, 12, 47, 53),
         (1819,  8,  2, 214,  1, 26,  0),
         (1839,  3, 27,  86, 20, 58, 11),
         (1903,  4, 19, 109,  7, 36,  5),
         (1929,  8, 25, 237, 15, 43, 49),
         (1941,  9, 29, 272,  4, 25,  9),
         (1943,  4, 19, 109,  6, 49, 27),
         (1943, 10,  7, 280,  2, 57, 52),
         (1992,  3, 17,  77, 16, 45, 44),
         (1996,  2, 25,  56, 21, 30, 57),
         (1941,  9, 29, 272,  4, 25,  9),
         (1943,  4, 19, 109,  6, 49, 27),
         (1943, 10,  7, 280,  2, 57, 52),
         (1992,  3, 17,  77, 16, 45, 44),
         (1996,  2, 25,  56, 21, 30, 57),
         (2038, 11, 10, 314, 22, 30,  4),
         (2094,  7, 18, 199,  1, 56, 51))

syntax_file = open('%s.sps' % fmt_name, 'w')
syntax_file.write('''\
SET EPOCH 1930.
DATA LIST NOTABLE FILE='%(fmt_name)s.input'/%(fmt_name)s 1-40 (%(fmt_name)s).
PRINT OUTFILE='%(fmt_name)s.output'/%(fmt_name)s (F16.2).
EXECUTE.
''' % {'fmt_name': fmt_name})
syntax_file.close()

expout_file = open('expout', 'w')
input_file = open('%s.input' % fmt_name, 'w')

def is_leap_year(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)

n = 0
def print_all_formats(date, template, formatted, exp_y, exp_m, exp_d,
                      exp_time, exp_sign):
    if template != '':
        global n
        n += 1
        year, month, day, julian, hour, minute, second = date
        quarter = (month - 1) // 3 + 1
        week = (julian - 1) // 7 + 1
        if year >= 1930 and year < 2030:
            years = ('%d' % year, '%d' % (year % 100))
        else:
            years = ('%d' % year,)

        c = template[0]
        template = template[1:]
        if c == 'd':
            for f in ('%d', '%02d'):
                print_all_formats(date, template, formatted + (f % day),
                                  exp_y, exp_m, day, exp_time, exp_sign)
        elif c == 'm':
            for f in ('%d' % month,
                      '%02d' % month,
                      ('i', 'ii', 'iii', 'iv', 'v', 'vi',
                       'vii', 'viii', 'ix', 'x', 'xi', 'xii')[month - 1],
                      ('I', 'II', 'III', 'IV', 'V', 'VI',
                       'VII', 'VIII', 'IX', 'X', 'XI', 'XII')[month - 1],
                      ('jan', 'feb', 'mar', 'apr', 'may', 'jun',
                       'jul', 'aug', 'sep', 'oct', 'nov', 'dec')[month - 1],
                      ('JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN',
                       'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC')[month - 1],
                      ('january', 'february', 'march',
		       'april', 'may', 'june',
		       'july', 'august', 'september',
		       'october', 'november', 'december')[month - 1],
                      ('JANUARY', 'FEBRUARY', 'MARCH',
		       'APRIL', 'MAY', 'JUNE',
		       'JULY', 'AUGUST', 'SEPTEMBER',
		       'OCTOBER', 'NOVEMBER', 'DECEMBER')[month - 1]):
                print_all_formats(date, template, formatted + f,
                                  exp_y, month, exp_d, exp_time, exp_sign)
        elif c == 'y':
            for f in years:
                print_all_formats(date, template, formatted + f,
                                  year, exp_m, exp_d, exp_time, exp_sign)
        elif c == 'j':
            for f in years:
                print_all_formats(date, template,
                                  formatted + f + ('%03d' % julian),
                                  year, month, day, exp_time, exp_sign)
        elif c == 'q':
            print_all_formats(date, template, formatted + ('%d' % quarter),
                              exp_y, (quarter - 1) * 3 + 1, 1,
                              exp_time, exp_sign)
        elif c == 'w':
            exp_m = month
            exp_d = day - (julian - 1) % 7
            if exp_d < 1:
                exp_m -= 1
                exp_d += (31, 29 if is_leap_year(year) else 28, 31,
                          30, 31, 30, 31, 31, 30, 31, 30, 31)[exp_m]
            print_all_formats(date, template, formatted + ('%d' % week),
                              exp_y, exp_m, exp_d, exp_time, exp_sign)

        elif c == 'H':
            for f in ('%d', '%02d'):
                print_all_formats(date, template, formatted + (f % hour),
                                  exp_y, exp_m, exp_d, exp_time + hour * 3600,
                                  exp_sign)
        elif c == 'M':
            for f in ('%d', '%02d'):
                print_all_formats(date, template, formatted + (f % minute),
                                  exp_y, exp_m, exp_d, exp_time + minute * 60,
                                  exp_sign)
        elif c == 'S':
            for f in ('%d', '%02d'):
                print_all_formats(date, template, formatted + (f % second),
                                  exp_y, exp_m, exp_d, exp_time + second,
                                  exp_sign)
        elif c == '-':
            for f in ' -.,/':
                print_all_formats(date, template, formatted + f,
                                  exp_y, exp_m, exp_d, exp_time,
                                  exp_sign)
        elif c == ':':
            for f in ' :':
                print_all_formats(date, template, formatted + f,
                                  exp_y, exp_m, exp_d, exp_time,
                                  exp_sign)
        elif c == ' ':
            print_all_formats(date, template, formatted + ' ',
                              exp_y, exp_m, exp_d, exp_time,
                              exp_sign)
        elif c == 'Q' or c == 'W':
            infix = 'q' if c == 'Q' else 'wk'
            for before in (' ', ''):
                for middle in (infix, infix.upper()):
                    for after in (' ', ''):
                        print_all_formats(date, template,
                                          formatted + before + middle + after,
                                          exp_y, exp_m, exp_d, exp_time,
                                          exp_sign)
        elif c == '+':
            for f in ('', '+', '-'):
                print_all_formats(date, template, formatted + f,
                                  exp_y, exp_m, exp_d, exp_time,
                                  f)
        else:
            assert False
    else:
        # Write the formatted value to fmt_name.input.
        input_file.write('%s\n' % formatted)

        # Write the expected value to 'expout'.
        assert exp_y >= 1582 and exp_y <= 2100
        assert exp_m >= 1 and exp_m <= 12
        assert exp_d >= 1 and exp_d <= 31
        EPOCH = -577734         # 14 Oct 1582
        expected = (EPOCH - 1
                    + 365 * (exp_y - 1)
                    + (exp_y - 1) // 4
                    - (exp_y - 1) // 100
                    + (exp_y - 1) // 400
                    + (367 * exp_m - 362) // 12
                    + (0 if exp_m <= 2
                       else -1 if exp_m >= 2 and is_leap_year(exp_y)
                       else -2)
                    + exp_d) * 86400
        if exp_sign == '-' and expected > 0:
            expected -= exp_time
        else:
            expected += exp_time
        expected_s = '%17.2f\n' % expected
        expected_s = expected_s.replace(' 0.', '  .')
        expout_file.write(expected_s)


for template in templates:
    for date in dates:
        print_all_formats(date, template, '', 0, 0, 1, 0, '')

