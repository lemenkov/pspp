## PSPP - a program for statistical analysis.
## Copyright (C) 2019, 2020 Free Software Foundation, Inc.
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


doc/pspp.xml: doc/pspp.texi $(doc_pspp_TEXINFOS) doc/help-pages-list
if BROKEN_DOCBOOK_XML
	touch $@
else
	@$(MKDIR_P)  doc
	$(AM_V_GEN)$(MAKEINFO) $(AM_MAKEINFOFLAGS) --docbook -I $(top_srcdir) \
		$< -o $@
endif

docbookdir = $(docdir)
dist_docbook_DATA = doc/pspp.xml


CLEANFILES += pspp-dev.dvi $(docbook_DATA) doc/pspp.info* doc/pspp.xml


doc: $(INFO_DEPS) $(DVIS) $(PDFS) $(PSS) $(HTMLS) $(dist_docbook_DATA)
PHONY += doc

doc/help-pages-list: $(UI_FILES)
	 $(AM_V_GEN)cat $^ | grep '"help[-_]page"' | \
   $(SED) -e 's% *<property name="help[-_]page">\([^<]*\)</property>%//*[@id='"'"'\1'"'"']%' \
	-e 's%#%'"'"']/*[@id='"'"'%g' > $@,tmp
	test -s $@,tmp
	mv $@,tmp $@

EXTRA_DIST += \
	doc/help-pages-list \
	doc/pspp-manual.css


AM_MAKEINFOFLAGS=-I $(top_srcdir)/doc/examples -I $(top_builddir)/doc/examples
am__TEXINFO_TEX_DIR=:$(top_srcdir)/doc/examples:$(top_builddir)/doc/examples

################# Example programs ##############################

EXAMPLE_SYNTAX = \
 doc/examples/autorecode.sps \
 doc/examples/chisquare.sps \
 doc/examples/compute.sps \
 doc/examples/count.sps \
 doc/examples/descriptives.sps \
 doc/examples/flip.sps \
 doc/examples/frequencies.sps \
 doc/examples/means.sps \
 doc/examples/one-sample-t.sps \
 doc/examples/independent-samples-t.sps \
 doc/examples/reliability.sps \
 doc/examples/split.sps \
 doc/examples/weight.sps


EXTRA_DIST += $(EXAMPLE_SYNTAX)

EXAMPLE_OUTPUTS = $(EXAMPLE_SYNTAX:.sps=.out)
EXAMPLE_HTML = $(EXAMPLE_SYNTAX:.sps=.html)

$(top_builddir)/doc/pspp.info:  $(EXAMPLE_OUTPUTS)
$(top_builddir)/doc/pspp.ps:    $(EXAMPLE_OUTPUTS)
$(top_builddir)/doc/pspp.dvi:   $(EXAMPLE_OUTPUTS)
$(top_builddir)/doc/pspp.html:  $(EXAMPLE_HTML)
$(top_builddir)/doc/pspp.pdf:   $(EXAMPLE_OUTPUTS)
$(top_builddir)/doc/pspp.xml:   $(EXAMPLE_OUTPUTS)

# The examples cannot be built until the binary has been built
$(EXAMPLE_OUTPUTS): $(top_builddir)/src/ui/terminal/pspp
$(EXAMPLE_HTML): $(top_builddir)/src/ui/terminal/pspp

CLEANFILES += $(EXAMPLE_OUTPUTS)

SUFFIXES: .sps

# use pspp to process a syntax file and reap the output into a text file
.sps.out:
	$(MKDIR_P) $(@D)
	where=$$PWD ; \
	(cd $(top_srcdir)/examples; ${abs_builddir}/src/ui/terminal/pspp $(abs_srcdir)/doc/examples/$(<F) -o $$where/$@)

# Use pspp to process a syntax file and reap the output into a html file
# Then, use sed to delete everything up to and including <body> and
# everything after and including </body>
.sps.html:
	$(MKDIR_P) $(@D)
	where=$$PWD ; \
	(cd $(top_srcdir)/examples; ${abs_builddir}/src/ui/terminal/pspp $(abs_srcdir)/doc/examples/$(<F) -o $$where/$@,x -O format=html)
	$(SED) -e '\%</body%,$$d' -e '0,/<body/d' $@,x > $@

# Insert the link tag for the cascading style sheet.
# But make sure these operations are idempotent.
html-local:
	for h in doc/pspp.html/*.html; do \
		if grep -Fq '<link rel="stylesheet"' $$h; then continue; fi ; \
		$(SED) -i -e '/^<\/head>/i \\\
<link rel="stylesheet" href="pspp-manual.css">' $$h; \
	done

install-html-local: html-local
	$(MKDIR_P) $(DESTDIR)$(prefix)/share/doc/pspp/pspp.html
	$(INSTALL_DATA) ${top_srcdir}/doc/pspp-manual.css $(DESTDIR)$(prefix)/share/doc/pspp/pspp.html
