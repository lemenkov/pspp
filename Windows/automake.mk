#   Copyright (C) 2021 John Darrington
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

EXTRA_DIST+=Windows/build-dependencies \
Windows/AdvUninstLog.nsh \
Windows/MUI_EXTRAPAGES.nsh \
Windows/README \
Windows/pspp.nsi

nsis_installer_deps=$(DESTDIR)$(prefix)/share/doc/pspp/pspp.html \
	$(DESTDIR)$(prefix)/share/doc/pspp/pspp.pdf \
	Windows/AdvUninstLog.nsh Windows/MUI_EXTRAPAGES.nsh

environment_dir = $$(echo $(LDFLAGS) | $(SED) -e 's/^-L//' -e 's|/lib$$||')
libgcc_dir = $$(dirname $$($(CC) -print-libgcc-file-name))

# Note that install is a PHONY target.  Therefore this rule is always executed.
Windows/nsis-bin: install
	@$(RM) -r $@
	@$(MKDIR_P) $@
	cp $(DESTDIR)$(prefix)/bin/* $@
	cp `$(CC) -print-file-name=libwinpthread-1.dll` $@
	cp $(environment_dir)/bin/*.dll $@
	cp $(environment_dir)/bin/*.exe $@
	cp $(libgcc_dir)/*.dll $@


# Create a hash from everything in Windows/nsis-bin, but don't
# update it unless it has changed
Windows/nsis-bin-hashes: Windows/nsis-bin
	md5sum Windows/nsis-bin/* > $@,tmp
	@diff -q $@,tmp $@ || mv $@,tmp $@

# Copy Windows/nsis-bin/* into Windows/nsis-bin-stripped and then strip everything
Windows/nsis-bin-stripped: Windows/nsis-bin-hashes
	@$(MKDIR_P) $@
	cp Windows/nsis-bin/* $@
	$(STRIP) $@/*

# Create a hash from everything in Windows/nsis-bin-stripped, but don't
# update it unless it has changed
Windows/nsis-bin-stripped-hashes: Windows/nsis-bin-stripped
	md5sum Windows/nsis-bin-stripped/* > $@,tmp
	@diff -q $@,tmp $@ || mv $@,tmp $@


Windows/pspp-$(binary_width)bit-debug-install.exe: Windows/pspp.nsi  Windows/nsis-bin-hashes $(nsis_installer_deps)
	@$(top_builddir)/config.status --config | grep -e --enable-relocatable > /dev/null || \
	(echo "PSPP must be configured with --enable-relocatable"; false)
	@$(MKDIR_P) ${@D}
	makensis -Dpspp_version=$(PACKAGE_VERSION) \
	 -DDEBUG=1 \
	 -DPtrSize=$(binary_width) \
	 -DPrefix=$(DESTDIR)$(prefix) \
	 -DBinDir=$(abs_top_builddir)/Windows/nsis-bin \
	 -DEnvDir=$(environment_dir) \
	 -DSourceDir=$(abs_top_srcdir) \
	 -DOutExe=$(abs_builddir)/$@ $<

Windows/pspp-$(binary_width)bit-install.exe: Windows/pspp.nsi Windows/nsis-bin-stripped-hashes $(nsis_installer_deps)
	@$(top_builddir)/config.status --config | grep -e --enable-relocatable > /dev/null || \
	(echo "PSPP must be configured with --enable-relocatable"; false)
	@$(MKDIR_P) ${@D}
	makensis -Dpspp_version=$(PACKAGE_VERSION) \
	 -DDEBUG=0 \
	 -DPtrSize=$(binary_width) \
	 -DPrefix=$(DESTDIR)$(prefix) \
	 -DBinDir=$(abs_top_builddir)/Windows/nsis-bin-stripped \
	 -DEnvDir=$(environment_dir) \
	 -DSourceDir=$(abs_top_srcdir) \
	 -DOutExe=$(abs_builddir)/$@ $<

Windows/installers: Windows/pspp-$(binary_width)bit-install.exe Windows/pspp-$(binary_width)bit-debug-install.exe
