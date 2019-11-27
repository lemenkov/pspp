#!/bin/sh

# PSPP - a program for statistical analysis.
# Copyright (C) 2017 Free Software Foundation, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# This little script can be used in conjunction with gcov to see how well
# the regression test suite is covering the PSPP code.
#
# To use it:
#
# make distclean
# CFLAGS="-O0 -g -fprofile-arcs -ftest-coverage"
# export CFLAGS
# ./configure
# make check
# tests/coverage.sh

TEMPDIR=/tmp/pspp-cov-$$
export TEMPDIR

mkdir -p $TEMPDIR

files=`find src -name '*.c'`

summary_file="$TEMPDIR/coverage.txt"
export summary_file

rm -f $summary_file

for f in $files ; do
	dir=`dirname $f`
	gcov -p -l -n -o $dir $f  | grep -v '^Creat' >> $summary_file
done

cat  > "$TEMPDIR/cov.sps" << EOF
DATA LIST  FREE  FILE='$summary_file' /COVERAGE (PCT8) d1 (a2) lines *  d2 (a10) d3 (a10) d4 (a10) d5 (a10) file (a25).


AGGREGATE OUTFILE=*
	/BREAK=file
	/COVERAGE=MIN(COVERAGE).

SORT CASES BY COVERAGE.

LIST /COVERAGE file.

FREQUENCIES /COVERAGE
  /HISTOGRAM
  /PERCENTILES=25,50,75,90.

EOF

src/pspp -o pspp.html $TEMPDIR/cov.sps

rm -rf $TEMPDIR
