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

use strict;
use warnings 'all';

my (@prev) = ();
our ($n) = 0;
our ($suffix) = '';
our ($prefix) = '';
while (<>) {
    s/^ //;
    if (scalar (my (@line) = /^([A-Z]+)(\d+)([^"]+")( *)([^%"]*)(%?")$/) == 6) {
	if (defined ($prev[0])
	    && $line[0] eq $prev[0]
	    && $line[1] == $prev[1] + 1
	    && $line[2] eq $prev[2]
	    && $line[5] eq $prev[5]) {
	    if ($line[3] eq " $prev[3]"
		&& $line[4] eq $prev[4]) {
		flush_prefix ();
		flush_suffix ();
		$n++;
	    } elsif ($line[3] eq $prev[3]
		     && length ($line[4]) == length ($prev[4]) + 1
		     && $prev[4] eq substr ($line[4], 0, length ($line[4]) - 1)) {
		flush_n ();
		flush_prefix ();
		$suffix .= substr ($line[4], -1);
	    } elsif ($line[3] eq $prev[3]
		     && $prev[4] eq substr ($line[4], 1)) {
		flush_n ();
		flush_suffix ();
		$prefix .= substr ($line[4], 0, 1);
	    } else {
		flush ();
		print $_;
	    }
	} else {
	    flush ();
	    print $_;
	}
	@prev = @line;
    } else {
	flush ();
	print $_;
	@prev = ();
    }
}
flush ();

sub flush_suffix {
    if ($suffix ne '') {
	print "\$$suffix\n";
	$suffix = '';
    }
}

sub flush_prefix {
    if ($prefix ne '') {
	print "^$prefix\n";
	$prefix = '';
    }
}

sub flush_n {
    if ($n) {
	print "*$n\n";
	$n = 0;
    }
}

sub flush {
    flush_prefix ();
    flush_suffix ();
    flush_n ();
}
