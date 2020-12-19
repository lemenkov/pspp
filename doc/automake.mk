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
	doc/dev/tlo-file-format.texi \
	doc/dev/encrypted-file-wrappers.texi \
	doc/dev/q2c.texi

dist_man_MANS += doc/pspp.1 \
                 doc/psppire.1

EXTRA_DIST += doc/get-commands.pl \
              doc/help-pages-list \
              doc/prepdoc.sh

$(srcdir)/doc/ni.texi: $(top_srcdir)/src/language/command.def doc/get-commands.pl
	$(AM_V_GEN)$(PERL) $(top_srcdir)/doc/get-commands.pl $(top_srcdir)/src/language/command.def > $@

$(srcdir)/doc/tut.texi:
	$(AM_V_GEN)echo "@set example-dir $(examplesdir)" > $@


doc/pspp.xml: doc/pspp.texi $(doc_pspp_TEXINFOS) doc/help-pages-list
if BROKEN_DOCBOOK_XML
	touch $@
else
	$(AM_V_GEN)$(MAKEINFO) --docbook $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) \
		-I doc -I $(srcdir)/doc $< -o $@
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

am__TEXINFO_TEX_DIR=:$(top_srcdir)/doc:$(top_builddir)/doc

################# Example programs ##############################

FIGURE_SYNTAX = \
 doc/pspp-figures/aggregate.sps \
 doc/pspp-figures/autorecode.sps \
 doc/pspp-figures/chisquare.sps \
 doc/pspp-figures/compute.sps \
 doc/pspp-figures/count.sps \
 doc/pspp-figures/descriptives.sps \
 doc/pspp-figures/flip.sps \
 doc/pspp-figures/frequencies.sps \
 doc/pspp-figures/means.sps \
 doc/pspp-figures/one-sample-t.sps \
 doc/pspp-figures/independent-samples-t.sps \
 doc/pspp-figures/reliability.sps \
 doc/pspp-figures/select-if.sps \
 doc/pspp-figures/sort-cases.sps \
 doc/pspp-figures/split.sps \
 doc/pspp-figures/temporary.sps \
 doc/pspp-figures/tutorial1.sps \
 doc/pspp-figures/tutorial2.sps \
 doc/pspp-figures/tutorial3.sps \
 doc/pspp-figures/tutorial4.sps \
 doc/pspp-figures/tutorial5.sps \
 doc/pspp-figures/tutorial6.sps \
 doc/pspp-figures/tutorial7.sps \
 doc/pspp-figures/weight.sps


EXTRA_DIST += $(FIGURE_SYNTAX)

FIGURE_SPVS = $(FIGURE_SYNTAX:.sps=.spv) \
	doc/pspp-figures/tutorial2a.spv \
	doc/pspp-figures/tutorial2b.spv \
	doc/pspp-figures/tutorial5a.spv \
	doc/pspp-figures/tutorial5b.spv \
	doc/pspp-figures/tutorial7a.spv \
	doc/pspp-figures/tutorial7b.spv
FIGURE_TXTS = $(FIGURE_SPVS:.spv=.txt)
FIGURE_TEXIS = $(FIGURE_TXTS:.txt=.texi)
FIGURE_HTMLS = $(FIGURE_SPVS:.spv=.html)
FIGURE_PDFS = $(FIGURE_SPVS:.spv=.pdf)

figure-spvs: $(FIGURE_SPVS)
figure-txts: $(FIGURE_TXTS)
figure-texis: $(FIGURE_TEXIS)
figure-htmls: $(FIGURE_HTMLS)
figure-pdfs: $(FIGURE_PDFS)
PHONY += figure-spv figure-txts figure-texis figure-htmls figure-pdfs

$(top_builddir)/doc/pspp.info:  $(FIGURE_TEXIS)
$(top_builddir)/doc/pspp.ps:    $(FIGURE_TEXIS)
$(top_builddir)/doc/pspp.dvi:   $(FIGURE_TEXIS)
$(top_builddir)/doc/pspp.html:  $(FIGURE_HTMLS)
$(top_builddir)/doc/pspp.pdf:   $(FIGURE_TEXIS)
$(top_builddir)/doc/pspp.xml:   $(FIGURE_TEXIS)

CLEANFILES += $(FIGURE_TXTS) $(FIGURE_SPVS) $(FIGURE_TEXIS) $(FIGURE_HTMLS)
SUFFIXES += .sps .spv .txt .html .texi .pdf

# Use pspp to process a syntax file into an output file.
if cross_compiling
pspp = native/src/ui/terminal/pspp
pspp_output = native/utilities/pspp-output

native/Makefile:
	$(MKDIR_P) native
	(cd native && $(top_srcdir)/configure --without-gui)

native/gl/libgl.la: native/Makefile
	(cd native && flock --verbose $(top_builddir)/native-lock $(MAKE) gl/libgl.la)

$(pspp): native/gl/libgl.la
	(cd native && flock --verbose $(top_builddir)/native-lock $(MAKE) src/ui/terminal/pspp)

$(pspp_output): native/gl/libgl.la
	(cd native && flock --verbose $(top_builddir)/native-lock $(MAKE) utilities/pspp-output)
else
pspp = src/ui/terminal/pspp$(EXEEXT)
pspp_output = utilities/pspp-output$(EXEEXT)
endif

$(FIGURE_SPVS): $(pspp)
.sps.spv:
	$(AM_V_GEN)(cd $(top_srcdir)/examples \
         && $(abs_top_builddir)/$(pspp) ../doc/pspp-figures/$(<F) -o - -O format=spv) > $@.tmp
	$(AM_V_at)mv $@.tmp $@

# In some cases, the tutorial only wants some parts of the output.
convert = $(AM_V_GEN)$(pspp_output) convert $< $@
doc/pspp-figures/tutorial2a.spv: doc/pspp-figures/tutorial2.spv $(pspp_output)
	$(convert) --command='Descriptives'
doc/pspp-figures/tutorial2b.spv: doc/pspp-figures/tutorial2.spv $(pspp_output)
	$(convert) --label='Extreme Values'
doc/pspp-figures/tutorial5a.spv: doc/pspp-figures/tutorial5.spv $(pspp_output)
	$(convert) --commands=examine --nth-command=1 --labels=descriptives
doc/pspp-figures/tutorial5b.spv: doc/pspp-figures/tutorial5.spv $(pspp_output)
	$(convert) --commands=examine --nth-command=2 --labels=descriptives
doc/pspp-figures/tutorial7a.spv: doc/pspp-figures/tutorial7.spv $(pspp_output)
	$(convert) --commands=regression --nth-command=1 --subtypes=coefficients
doc/pspp-figures/tutorial7b.spv: doc/pspp-figures/tutorial7.spv $(pspp_output)
	$(convert) --commands=regression --nth-command=2 --subtypes=coefficients

# Convert an output file into a text file or HTML file.
$(FIGURE_TXTS) $(FIGURE_HTMLS): $(pspp_output)
.spv.txt:
	$(convert)
.spv.pdf:
	$(convert) -O left-margin=0pt -O right-margin=0pt -O top-margin=0pt -O bottom-margin=0pt -O paper-size=6x20in -O font-size=12000 --table-look=$(HOME)/pspp/spss15/Looks/report.tlo
.spv.html:
	$(convert) -O format=html -O bare=true

# Convert a text file into a Texinfo file.
.txt.texi:
	$(AM_V_GEN)$(SED) -e 's/@/@@/g' $< > $@

AM_MAKEINFOHTMLFLAGS = $(AM_MAKEINFOFLAGS) --css-ref=pspp-manual.css
# Adjust the path for screenshot images.
# But make sure these operations are idempotent.
html-local: doc/pspp.html
	test -d doc/pspp.html
	for h in doc/pspp.html/*.html; do \
		if grep -Fq '<img src="screenshots/' $$h; then continue; fi ; \
		$(SED) -i -e 's|<img src="\([^"]*\)"|<img src="screenshots/\1"|' $$h; \
	done

install-html-local: html-local $(HTML_SCREENSHOTS)
	$(MKDIR_P) $(DESTDIR)$(prefix)/share/doc/pspp/pspp.html
	$(INSTALL) -d $(DESTDIR)$(prefix)/share/doc/pspp/pspp.html/screenshots
	for p in $(HTML_SCREENSHOTS); do \
		$(INSTALL_DATA) $$p $(DESTDIR)$(prefix)/share/doc/pspp/pspp.html/screenshots ;\
	done
	$(INSTALL_DATA) ${top_srcdir}/doc/pspp-manual.css $(DESTDIR)$(prefix)/share/doc/pspp/pspp.html



desktopdir = $(datadir)/applications

doc/org.fsf.pspp.metainfo.xml: doc/org.fsf.pspp.metainfo.xml.in $(POFILES)
	$(AM_V_GEN)$(MSGFMT) --xml --template $< -o $@ -d $(top_srcdir)/po || \
	  $(MSGFMT) -L appdata --xml --template $< -o $@ -d $(top_srcdir)/po

doc/org.fsf.pspp.desktop: doc/org.fsf.pspp.desktop.in $(POFILES)
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -o $@ -d $(top_srcdir)/po

CLEANFILES+=doc/org.fsf.pspp.desktop \
            doc/org.fsf.pspp.metainfo.xml

desktop_DATA = doc/org.fsf.pspp.desktop

appdatadir = $(datadir)/metainfo
dist_appdata_DATA = doc/org.fsf.pspp.metainfo.xml

EXTRA_DIST += doc/org.fsf.pspp.metainfo.xml.in \
	doc/org.fsf.pspp.desktop.in



SCREENSHOTS = \
$(top_srcdir)/doc/screenshots/autorecode.grab \
$(top_srcdir)/doc/screenshots/chisquare.grab \
$(top_srcdir)/doc/screenshots/count.grab \
$(top_srcdir)/doc/screenshots/count-define.grab \
$(top_srcdir)/doc/screenshots/compute.grab \
$(top_srcdir)/doc/screenshots/descriptives.grab \
$(top_srcdir)/doc/screenshots/one-sample-t.grab \
$(top_srcdir)/doc/screenshots/independent-samples-t.grab \
$(top_srcdir)/doc/screenshots/define-groups-t.grab \
$(top_srcdir)/doc/screenshots/frequencies.grab \
$(top_srcdir)/doc/screenshots/reliability.grab \
$(top_srcdir)/doc/screenshots/split-status-bar.grab \
$(top_srcdir)/doc/screenshots/sort-simple.grab \
$(top_srcdir)/doc/screenshots/sort.grab


PDF_SCREENSHOTS =  $(SCREENSHOTS:.grab=-hc.png)
EPS_SCREENSHOTS =  $(SCREENSHOTS:.grab=-hc.eps)
HTML_SCREENSHOTS = $(SCREENSHOTS:.grab=-ad.png)
INFO_SCREENSHOTS = $(SCREENSHOTS:.grab=-ad.png)

doc-make: doc/doc-make.in Makefile
	$(SED) -e 's|%top_srcdir%|@top_srcdir@|g' \
	-e 's|%abs_builddir%|@abs_builddir@|g' \
	-e 's|%MKDIR_P%|@MKDIR_P@|g' \
	-e 's|%src_ui_gui_psppiredir%|$(src_ui_gui_psppiredir)|g' \
	-e 's|%UI_FILES%|$(UI_FILES)|g' \
	-e 's|%IMAGES%|$(INFO_SCREENSHOTS) $(HTML_SCREENSHOTS) $(EPS_SCREENSHOTS) $(PDF_SCREENSHOTS)|g' \
	$< > $@


# Install all the PNG files so that info readers can recognise them
install-info-local:
	$(MKDIR_P) $(DESTDIR)$(prefix)/share/info/screenshots
	for p in $(INFO_SCREENSHOTS); do \
		$(INSTALL_DATA) $$p $(DESTDIR)$(prefix)/share/info/screenshots ;\
	done

uninstall-local:
	for p in $(INFO_SCREENSHOTS); do \
		f=`basename $$p ` ; \
		$(RM) $(DESTDIR)$(prefix)/share/info/$$f ; \
	done

EXTRA_DIST+= $(SCREENSHOTS) doc/doc-make.in doc/screengrab

EXTRA_DIST+= $(EPS_SCREENSHOTS) $(PDF_SCREENSHOTS) $(INFO_SCREENSHOTS)
