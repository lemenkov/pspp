# PSPP - a program for statistical analysis.
# Copyright (C) 2019 Free Software Foundation, Inc.
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

use 5.008008;
use ExtUtils::MakeMaker;
# See lib/ExtUtils/MakeMaker.pm for details of how to influence
# the contents of the Makefile that is written.


do './pspp-module-config' || do {
    my $build = prompt ("Enter the location of the build directory of the configured pspp source:", "" );
    my $src = $top_srcdir;

    %Locations = (SourceDir => "$src", BuildDir => "$build");
};

WriteMakefile(
    MAKEFILE          => 'MakefileX',
    FULLPERL          => "PSPP_TEST_CMD=\"$Locations{BuildDir}/src/ui/terminal/pspp\" \$(PERL)",
    NAME              => 'PSPP',
    DISTNAME          => 'PSPP-Perl',
    VERSION_FROM      => "$Locations{BuildDir}/src/libpspp/version.c",
    PREREQ_PM         => {POSIX=>0},
    PM                => {"lib/PSPP.pm", "\$(INST_LIBDIR)/PSPP.pm"},
    ($] >= 5.005 ?     ## Add these new keywords supported since 5.005
      (ABSTRACT       => 'Perl extension to PSPP',
       AUTHOR         => 'John Darrington <john@darrington.wattle.id.au>') : ()),
    INC       => "-I $Locations{SourceDir}  -I $Locations{SourceDir}/src -I $Locations{SourceDir}/gl -I $Locations{BuildDir}/gl -I $Locations{BuildDir}",
    MYEXTLIB  => "$Locations{BuildDir}/src/.libs/libpspp-core.\$(SO)",
    MAN3PODS  => {"lib/PSPP.pm", "\$(INST_MAN3DIR)/PSPP.3pm",
	    "Examples.pod", "\$(INST_MAN3DIR)/PSPP::Examples.3pm"}
);

if  (eval {require ExtUtils::Constant; 1}) {
  # If you edit these definitions to change the constants used by this module,
  # you will need to use the generated const-c.inc and const-xs.inc
  # files to replace their "fallback" counterparts before distributing your
  # changes.
  my @names = (qw());
  ExtUtils::Constant::WriteConstants(
                                     NAME         => 'PSPP',
                                     NAMES        => \@names,
                                     DEFAULT_TYPE => 'IV',
                                     C_FILE       => 'const-c.inc',
                                     XS_FILE      => 'const-xs.inc',
                                  );

}
else {
  use File::Copy;
  use File::Spec;
  foreach my $file ('const-c.inc', 'const-xs.inc') {
    my $fallback = File::Spec->catfile('fallback', $file);
    copy ($fallback, $file) or die "Can't copy $fallback to $file: $!";
  }
}
