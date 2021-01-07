/*
PSPP - a program for statistical analysis.
Copyright (C) 2017 Free Software Foundation, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CALENDAR_H
#define CALENDAR_H 1

struct fmt_settings;

double calendar_gregorian_to_offset (int y, int m, int d,
                                     const struct fmt_settings *,
                                     char **errorp);
void calendar_offset_to_gregorian (int ofs, int *y, int *m, int *d, int *yd);
int calendar_offset_to_year (int ofs);
int calendar_offset_to_month (int ofs);
int calendar_offset_to_mday (int ofs);
int calendar_offset_to_yday (int ofs);
int calendar_offset_to_wday (int ofs);

int calendar_days_in_month (int y, int m);

#endif /* calendar.h */
