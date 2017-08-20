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
include $(top_srcdir)/po/Makevars

XGETTEXT=xgettext
MSGMERGE=msgmerge
MSGFMT=msgfmt

POFILES = \
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
	po/tr.po \
	po/uk.po \
	po/zh_CN.po

LOCALPOFILES = po/cs.po po/en_GB.po

POTFILE=po/$(DOMAIN).pot

TRANSLATABLE_FILES = $(DIST_SOURCES) $(all_q_sources)

XGETTEXT_OPTIONS = \
	--copyright-holder="$(COPYRIGHT_HOLDER)" \
	--package-name=$(PACKAGE) \
	--package-version=$(VERSION) \
	--msgid-bugs-address=$(MSGID_BUGS_ADDRESS) \
        --from-code=UTF-8 \
	--add-comments='TRANSLATORS:'

$(POTFILE): $(TRANSLATABLE_FILES) $(UI_FILES) src/ui/gui/gen-dot-desktop.sh
	@$(MKDIR_P) po
	$(AM_V_GEN)$(XGETTEXT) --directory=$(top_srcdir) $(XGETTEXT_OPTIONS)    $(TRANSLATABLE_FILES) --language=C --keyword=_ --keyword=N_ -o $@,tmp
	$(AM_V_at)$(XGETTEXT) --directory=$(top_srcdir) $(XGETTEXT_OPTIONS) -j $(UI_FILES) --language=glade -o $@,tmp
	$(AM_V_at)$(XGETTEXT) --directory=$(top_srcdir) $(XGETTEXT_OPTIONS) -j src/ui/gui/gen-dot-desktop.sh --language=shell --keyword=TRANSLATE -o $@,tmp
	$(AM_V_at)$(SED) -e '/^"POT-Creation-Date: .*/d' $@,tmp > $@

$(LOCALPOFILED) $(POFILES): $(POTFILE)
	$(AM_V_GEN)$(MSGMERGE) --quiet $(top_srcdir)/$@ $? -o $@,tmp
	$(AM_V_at)if test -e $(top_srcdir)/$@,aux ; then \
	         touch $@,tmp ; \
		 msgcat --use-first $(top_srcdir)/$@,aux $@,tmp -o $@,tmp; \
	fi ;
	$(AM_V_at)$(SED) -e '/^"POT-Creation-Date: /d' $@,tmp > $@,tmp2
	$(RM) $@,tmp
	mv $@,tmp2 $@

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

uninstall-hook:
	for f in $(GMOFILES); do \
	  lang=`echo $$f | $(SED) -e 's%po/\(.*\)\.gmo%\1%' ` ; \
	  rm -f $(DESTDIR)$(prefix)/share/locale/$$lang/LC_MESSAGES/$(DOMAIN).mo ; \
	done


EXTRA_DIST += \
	$(LOCALPOFILES) \
	$(POFILES) \
	$(POTFILE) \
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

RSYNC = rsync
po-update:
	$(RSYNC) -Lrtvz translationproject.org::tp/latest/pspp/ $(srcdir)/po
