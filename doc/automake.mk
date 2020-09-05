## PSPP - a program for statistical analysis.
## Copyright (C) 2019 Free Software Foundation, Inc.
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.

## Process this file with automake to produce Makefile.in  -*- makefile -*-

info_TEXINFOS = doc/pspp.texi doc/pspp-dev.texi

doc_pspp_TEXINFOS = doc/version.texi \
	doc/bugs.texi \
	doc/command-index.texi \
	doc/concept-index.texi \
	doc/data-io.texi \
	doc/data-selection.texi \
	doc/expressions.texi \
	doc/files.texi \
	doc/combining.texi \
	doc/flow-control.texi \
	doc/function-index.texi \
	doc/installing.texi \
	doc/introduction.texi \
	doc/invoking.texi \
	doc/language.texi \
	doc/license.texi \
	doc/pspp-convert.texi \
	doc/pspp-output.texi \
	doc/pspp-dump-sav.texi \
	doc/ni.texi \
	doc/not-implemented.texi \
	doc/statistics.texi \
	doc/transformation.texi \
	doc/tutorial.texi \
	doc/tut.texi \
	doc/regression.texi \
	doc/utilities.texi \
	doc/variables.texi \
	doc/fdl.texi

doc_pspp_dev_TEXINFOS = doc/version-dev.texi \
	doc/dev/intro.texi \
	doc/dev/concepts.texi \
	doc/dev/syntax.texi \
	doc/dev/data.texi \
	doc/dev/i18n.texi \
	doc/dev/output.texi \
	doc/dev/system-file-format.texi \
	doc/dev/pc+-file-format.texi \
	doc/dev/portable-file-format.texi \
	doc/dev/spv-file-format.texi \
	doc/dev/encrypted-file-wrappers.texi \
	doc/dev/q2c.texi

dist_man_MANS += doc/pspp.1 \
                 doc/psppire.1

EXTRA_DIST += doc/get-commands.pl \
              doc/help-pages-list \
              doc/prepdoc.sh

$(srcdir)/doc/ni.texi: $(top_srcdir)/src/language/command.def doc/get-commands.pl
	@$(MKDIR_P)  doc
	$(AM_V_GEN)$(PERL) $(top_srcdir)/doc/get-commands.pl $(top_srcdir)/src/language/command.def > $@

$(srcdir)/doc/tut.texi:
	@$(MKDIR_P) doc
	$(AM_V_GEN)echo "@set example-dir $(examplesdir)" > $@


$(srcdir)/doc/pspp.xml: doc/pspp.texi $(doc_pspp_TEXINFOS) $(top_srcdir)/doc/help-pages-list
if BROKEN_DOCBOOK_XML
	touch $@
else
	@$(MKDIR_P)  doc
	$(AM_V_GEN)$(MAKEINFO) $(AM_MAKEINFOFLAGS) --docbook -I $(top_srcdir) \
		$< -o $@
endif

docbookdir = $(docdir)
dist_docbook_DATA = doc/pspp.xml


CLEANFILES += pspp-dev.dvi $(docbook_DATA)

doc: $(INFO_DEPS) $(DVIS) $(PDFS) $(PSS) $(HTMLS) $(dist_docbook_DATA)
PHONY += doc
