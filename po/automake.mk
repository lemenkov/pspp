# PSPP - a program for statistical analysis.
# Copyright (C) 2017, 2020, 2021 Free Software Foundation, Inc.
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
include $(top_srcdir)/po/Makevars

XGETTEXT=xgettext
MSGMERGE=msgmerge
MSGFMT=msgfmt

POFILES = \
	po/ar.po \
	po/ca.po \
	po/de.po \
	po/el.po \
	po/es.po \
	po/fr.po \
	po/gl.po \
	po/hu.po \
	po/ja.po \
	po/lt.po \
	po/nl.po \
	po/pl.po \
	po/pt_BR.po \
	po/ru.po \
	po/sl.po \
	po/ta.po \
	po/tr.po \
	po/uk.po \
	po/zh_CN.po

LOCALPOFILES = po/cs.po

POTFILE=po/$(DOMAIN).pot

TRANSLATABLE_FILES = $(DIST_SOURCES)

XGETTEXT_OPTIONS = \
	--copyright-holder="$(COPYRIGHT_HOLDER)" \
	--package-name=$(PACKAGE) \
	--package-version=$(VERSION) \
	--msgid-bugs-address=$(MSGID_BUGS_ADDRESS) \
        --from-code=UTF-8 \
	--add-comments='TRANSLATORS:' \
	--directory=$(top_srcdir)

ALL_TRANSLATABLE_FILES = \
	$(TRANSLATABLE_FILES) \
	$(UI_FILES) \
	doc/org.gnu.pspp.metainfo.xml.in \
	doc/org.gnu.pspp.desktop.in

$(POTFILE): $(ALL_TRANSLATABLE_FILES) Makefile
	@$(MKDIR_P) po
	$(AM_V_GEN)$(XGETTEXT) $(XGETTEXT_OPTIONS) $(TRANSLATABLE_FILES) --language=C --keyword=_ --keyword=N_ -o $@,tmp
	$(AM_V_at)test -z "$(UI_FILES)" || $(XGETTEXT) $(XGETTEXT_OPTIONS) -j $(UI_FILES) --language=Glade -o $@,tmp
	$(AM_V_at)$(XGETTEXT) $(XGETTEXT_OPTIONS) -j doc/org.gnu.pspp.metainfo.xml.in -o $@,tmp
	$(AM_V_at)$(XGETTEXT) $(XGETTEXT_OPTIONS) -j doc/org.gnu.pspp.desktop.in -o $@,tmp
	$(AM_V_at)mv $@,tmp $@

$(LOCALPOFILES) $(POFILES): $(POTFILE)
	$(AM_V_GEN)$(MSGMERGE) --previous --quiet $(top_srcdir)/$@ $? -o $@,tmp
	$(AM_V_at)if test -e $(top_srcdir)/$@,aux ; then \
		touch $@,tmp ; \
		msgcat --use-first $(top_srcdir)/$@,aux $@,tmp -o $@,tmp; \
	fi ;
	$(MSGFMT) -c $@,tmp -o - > /dev/null
	mv $@,tmp $@

SUFFIXES += .po .gmo
.po.gmo:
	@$(MKDIR_P) `dirname $@`
	$(AM_V_GEN)$(MSGFMT) $< -o $@


GMOFILES = $(LOCALPOFILES:.po=.gmo) $(POFILES:.po=.gmo)

ALL_LOCAL += $(GMOFILES)

install-data-hook: $(GMOFILES)
	for f in $(GMOFILES); do \
	  lang=`echo $$f | $(SED) -e 's%po/\(.*\)\.gmo%\1%' ` ; \
	  $(MKDIR_P) $(DESTDIR)$(prefix)/share/locale/$$lang/LC_MESSAGES; \
	  $(INSTALL_DATA) $$f $(DESTDIR)$(prefix)/share/locale/$$lang/LC_MESSAGES/$(DOMAIN).mo ; \
	done

uninstall-hook: uninstall-gmofiles
uninstall-gmofiles:
	for f in $(GMOFILES); do \
	  lang=`echo $$f | $(SED) -e 's%po/\(.*\)\.gmo%\1%' ` ; \
	  rm -f $(DESTDIR)$(prefix)/share/locale/$$lang/LC_MESSAGES/$(DOMAIN).mo ; \
	done
.PHONY: uninstall-gmofiles

EXTRA_DIST += \
	$(LOCALPOFILES) \
	$(POFILES) \
	$(POTFILE) \
	po/LINGUAS \
	po/ChangeLog \
	po/cs.po,aux

CLEANFILES += $(GMOFILES) $(POTFILE)

# Clean $(POFILES) from build directory but not if that's the same as
# the source directory.
po_CLEAN:
	@if test "$(srcdir)" != .; then \
		echo rm -f $(LOCALPOFILES) $(POFILES); \
		rm -f $(LOCALPOFILES) $(POFILES); \
	fi
CLEAN_LOCAL += po_CLEAN

# Download the po files from http://translationproject.org
# The final action to this rule is to remove the .pot file.  This
# is because the po files must be re-merged against an updated version of it.
#
# You can update just one .po file with, e.g.:
# make po-update POFILES=po/uk.po
PHONY += po-update
po-update:
	for p in $(POFILES); do \
		wget --no-use-server-timestamps -O $$p,tmp \
		https://translationproject.org/latest/pspp/`basename $$p` ; \
		mv $$p,tmp ${top_srcdir}/$$p; \
	done
	$(RM) $(POTFILE)
