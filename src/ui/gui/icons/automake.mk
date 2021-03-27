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
EXTRA_DIST += $(icons) $(icon_srcs) src/ui/gui/icons/COPYING_CCBYSA3

include $(top_srcdir)/src/ui/gui/icons/manifest

themedir = $(DESTDIR)$(datadir)/icons/hicolor

sizes=16x16 22x22 24x24 32x32 48x48 96x96 256x256 scalable

install-ext-icons:
	for context in apps mimetypes; do \
		for size in $(sizes); do \
		$(MKDIR_P) $(themedir)/$$size/$$context ; \
			if (cd $(top_srcdir)/src/ui/gui/icons/$$size/$$context && \
				(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
				$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/icons/$$size/$$context/* $(themedir)/$$size/$$context ; \
			fi ; \
		done ; \
	done


uninstall-ext-icons:
	for context in apps mimetypes; do \
		for size in $(sizes); do \
			if (cd $(top_srcdir)/src/ui/gui/icons/$$size/$$context && \
				(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
				  rm -rf $(themedir)/$$size/$$context/application-x-spss-* ; \
				  rm -rf $(themedir)/$$size/$$context/pspp* ; \
			fi ; \
		done ; \
	done

if host_is_w32
w32_icons=src/ui/gui/icons/pspp.ico \
src/ui/gui/icons/pspp-sav.ico \
src/ui/gui/icons/pspp-zsav.ico \
src/ui/gui/icons/pspp-por.ico \
src/ui/gui/icons/pspp-sps.ico
endif

install-icons:
	for wicon in $(w32_icons); do \
	  $(MKDIR_P) $(DESTDIR)$(pkgdatadir)/icons/ ; \
	  $(INSTALL_DATA) $$wicon $(DESTDIR)$(pkgdatadir)/icons/ ; \
	done
	for context in actions categories ; do \
	  $(MKDIR_P) $(DESTDIR)$(pkgdatadir)/$$context; \
	  for size in $(sizes); do \
		if (cd $(top_srcdir)/src/ui/gui/icons/$$context/$$size && \
			(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
			$(MKDIR_P) $(DESTDIR)$(pkgdatadir)/$$context/$$size ; \
			$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/icons/$$context/$$size/* $(DESTDIR)$(pkgdatadir)/$$context/$$size ; \
		fi ; \
	  done ; \
	done



uninstall-icons:
	for context in actions categories ; do \
	  for size in $(sizes); do \
		if (cd $(top_srcdir)/src/ui/gui/icons/$$context/$$size && \
			(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
			rm -rf $(DESTDIR)$(pkgdatadir)/$$context/$$size ; \
		fi ; \
	  done ; \
	done
	rm -f $(DESTDIR)$(pkgdatadir)/icons/pspp*.ico



INSTALL_DATA_HOOKS += install-icons install-ext-icons
UNINSTALL_DATA_HOOKS += uninstall-icons uninstall-ext-icons

if building_gui
nodist_src_ui_gui_psppire_DATA = src/ui/gui/icons/splash.png

src/ui/gui/icons/splash.png: $(top_srcdir)/src/ui/gui/icons/splash-t.png $(top_srcdir)/src/ui/gui/icons/splash-r.png Makefile
	@$(MKDIR_P) src/ui/gui/icons
	@case `echo $(VERSION) | $(SED) -e 's/[0-9][0-9]*\.[0-9]*\([0-9]\)\.[0-9][0-9]*/\1/'` in \
	  [13579]) cp $(top_srcdir)/src/ui/gui/icons/splash-t.png $@ ; \
	;;\
	  *) cp $(top_srcdir)/src/ui/gui/icons/splash-r.png $@ ; \
	;;\
esac

EXTRA_DIST += $(top_srcdir)/src/ui/gui/artwork/splash.svg $(icons) $(icon_srcs)

src/ui/gui/icons/pspp.ico: src/ui/gui/icons/16x16/apps/pspp.png src/ui/gui/icons/32x32/apps/pspp.png src/ui/gui/icons/48x48/apps/pspp.png src/ui/gui/icons/256x256/apps/pspp.png
	$(MKDIR_P) ${@D}
	convert $^ $@

src/ui/gui/icons/pspp-sav.ico: src/ui/gui/icons/16x16/mimetypes/application-x-spss-sav.png src/ui/gui/icons/32x32/mimetypes/application-x-spss-sav.png src/ui/gui/icons/48x48/mimetypes/application-x-spss-sav.png src/ui/gui/icons/256x256/mimetypes/application-x-spss-sav.png
	$(MKDIR_P) ${@D}
	convert $^ $@

src/ui/gui/icons/pspp-zsav.ico: src/ui/gui/icons/16x16/mimetypes/application-x-spss-zsav.png src/ui/gui/icons/32x32/mimetypes/application-x-spss-zsav.png src/ui/gui/icons/48x48/mimetypes/application-x-spss-zsav.png src/ui/gui/icons/256x256/mimetypes/application-x-spss-zsav.png
	$(MKDIR_P) ${@D}
	convert $^ $@

src/ui/gui/icons/pspp-por.ico: src/ui/gui/icons/16x16/mimetypes/application-x-spss-por.png src/ui/gui/icons/32x32/mimetypes/application-x-spss-por.png src/ui/gui/icons/48x48/mimetypes/application-x-spss-por.png src/ui/gui/icons/256x256/mimetypes/application-x-spss-por.png
	$(MKDIR_P) ${@D}
	convert $^ $@

src/ui/gui/icons/pspp-sps.ico: src/ui/gui/icons/16x16/mimetypes/application-x-spss-sps.png src/ui/gui/icons/32x32/mimetypes/application-x-spss-sps.png src/ui/gui/icons/48x48/mimetypes/application-x-spss-sps.png src/ui/gui/icons/256x256/mimetypes/application-x-spss-sps.png
	$(MKDIR_P) ${@D}
	convert $^ $@


endif

