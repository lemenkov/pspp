#   pspp.nsi - a NSIS script for generating a PSPP MSWindows installer.
#   Copyright (C) 2012-2013 Harry Thijssen
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

SetCompressor lzma

;; Turning compression off saves a huge amount of time when working on the
;; installer.  Keeping it on makes the installer much smaller.
;SetCompress off

!searchparse /${pspp_version} "." vers_min "."

;Include Modern UI
!include "MUI2.nsh"

   !define MUI_ICON "${Prefix}/share/pspp/icons/pspp.ico"
   !define MUI_UNICON "${Prefix}/share/pspp/icons/pspp.ico"
   !if ${PtrSize} == 64
   !define ProgramFiles $Programfiles64
   !else if ${PtrSize} == 32
   !define ProgramFiles $Programfiles32
   !else
   !error "Unknown architecture"
   !endif
   !define MUI_EXTRAPAGES MUI_EXTRAPAGES.nsh
   !define AdvUninstLog AdvUninstLog.nsh


;--------------------------------
!define APP_NAME "PSPP"
!define INSTDIR_REG_ROOT "HKLM"
!define INSTDIR_REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

!include "${MUI_EXTRAPAGES}"
!include "${AdvUninstLog}"

;------------------------------- Settings -----------------------------------------------;

  !define SavExt    ".sav"
  !define ZSavExt   ".zsav"
  !define PorExt    ".por"
  !define SpsExt    ".sps"
  ;Name and file
  Name "${APP_NAME}"                     ; The name of the installer
  OutFile "${OutExe}"             ; The file to write

  ;Request application privileges for Windows 7
  RequestExecutionLevel highest

;--------------------------------

;Interface Settings

  !define MUI_ABORTWARNING

;--------------------------------
;Pages

;Add the install read me page
  !insertmacro MUI_PAGE_README "${SourceDir}/COPYING"

  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES

  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES

  !insertmacro UNATTENDED_UNINSTALL

;--------------------------------
;Languages

  !insertmacro MUI_LANGUAGE "English" ;first language is the default language
  !insertmacro MUI_LANGUAGE "French"
  !insertmacro MUI_LANGUAGE "German"
  !insertmacro MUI_LANGUAGE "Spanish"
;  !insertmacro MUI_LANGUAGE "SpanishInternational"
  !insertmacro MUI_LANGUAGE "SimpChinese"
;  !insertmacro MUI_LANGUAGE "TradChinese"
  !insertmacro MUI_LANGUAGE "Japanese"
;  !insertmacro MUI_LANGUAGE "Korean"
;  !insertmacro MUI_LANGUAGE "Italian"
  !insertmacro MUI_LANGUAGE "Dutch"
;  !insertmacro MUI_LANGUAGE "Danish"
;  !insertmacro MUI_LANGUAGE "Swedish"
;  !insertmacro MUI_LANGUAGE "Norwegian"
;  !insertmacro MUI_LANGUAGE "NorwegianNynorsk"
;  !insertmacro MUI_LANGUAGE "Finnish"
  !insertmacro MUI_LANGUAGE "Greek"
  !insertmacro MUI_LANGUAGE "Russian"
;  !insertmacro MUI_LANGUAGE "Portuguese"
  !insertmacro MUI_LANGUAGE "PortugueseBR"
  !insertmacro MUI_LANGUAGE "Polish"
  !insertmacro MUI_LANGUAGE "Ukrainian"
  !insertmacro MUI_LANGUAGE "Czech"
;  !insertmacro MUI_LANGUAGE "Slovak"
;  !insertmacro MUI_LANGUAGE "Croatian"
;  !insertmacro MUI_LANGUAGE "Bulgarian"
  !insertmacro MUI_LANGUAGE "Hungarian"
;  !insertmacro MUI_LANGUAGE "Thai"
;  !insertmacro MUI_LANGUAGE "Romanian"
;  !insertmacro MUI_LANGUAGE "Latvian"
;  !insertmacro MUI_LANGUAGE "Macedonian"
;  !insertmacro MUI_LANGUAGE "Estonian"
  !insertmacro MUI_LANGUAGE "Turkish"
  !insertmacro MUI_LANGUAGE "Lithuanian"
  !insertmacro MUI_LANGUAGE "Slovenian"
;  !insertmacro MUI_LANGUAGE "Serbian"
;  !insertmacro MUI_LANGUAGE "SerbianLatin"
;  !insertmacro MUI_LANGUAGE "Arabic"
;  !insertmacro MUI_LANGUAGE "Farsi"
;  !insertmacro MUI_LANGUAGE "Hebrew"
;  !insertmacro MUI_LANGUAGE "Indonesian"
;  !insertmacro MUI_LANGUAGE "Mongolian"
;  !insertmacro MUI_LANGUAGE "Luxembourgish"
;  !insertmacro MUI_LANGUAGE "Albanian"
;  !insertmacro MUI_LANGUAGE "Breton"
;  !insertmacro MUI_LANGUAGE "Belarusian"
;  !insertmacro MUI_LANGUAGE "Icelandic"
;  !insertmacro MUI_LANGUAGE "Malay"
;  !insertmacro MUI_LANGUAGE "Bosnian"
;  !insertmacro MUI_LANGUAGE "Kurdish"
;  !insertmacro MUI_LANGUAGE "Irish"
;  !insertmacro MUI_LANGUAGE "Uzbek"
  !insertmacro MUI_LANGUAGE "Galician"
;  !insertmacro MUI_LANGUAGE "Afrikaans"
  !insertmacro MUI_LANGUAGE "Catalan"
;  !insertmacro MUI_LANGUAGE "Esperanto"
;  !insertmacro MUI_LANGUAGE "Asturian"

;--------------------------------

;Installer Sections

!macro FilePresent name
!if /FileExists ${name}
 nop
!else
  !error "Missing item: ${name}"
!endif
!macroend

Section "PSPP" SecDummy

 ;;  If these are missing, then someone probably forgot to run "make install-html"
 ;; and/or  "make install-pdf"
 !insertmacro FilePresent "${Prefix}/share/doc/pspp/pspp.html"
 !insertmacro FilePresent "${Prefix}/share/doc/pspp/pspp.pdf"

  SetOutPath "$INSTDIR"
  !insertmacro UNINSTALL.LOG_OPEN_INSTALL

  SetOutPath "$INSTDIR\etc"
  File /r "${EnvDir}/etc/*"

  SetOutPath "$INSTDIR\bin"
  File ${BinDir}/*.dll
  File ${BinDir}/gdbus.exe
  File ${BinDir}/glib-compile-schemas.exe
  File ${BinDir}/pspp.exe
  File ${BinDir}/psppire.exe
  File ${BinDir}/pspp-*.exe
  File ${BinDir}/gtk-update-icon-cache.exe

  SetOutPath "$INSTDIR\share"
  File /r "${Prefix}/share/*"

  SetOutPath "$INSTDIR\share\icons\hicolor"
  File /r "${EnvDir}/share/icons/hicolor/*"

  SetOutPath "$INSTDIR\share\icons\Adwaita"
  File /r /x cursors /x legacy "${EnvDir}/share/icons/Adwaita/*" ;; legacy and cursors are HUGE!

  SetOutPath "$INSTDIR\share\glib-2.0\schemas"
  File /r "${EnvDir}/share/glib-2.0/schemas/*"
  ExecWait '"$INSTDIR\bin\glib-compile-schemas.exe" "$INSTDIR\share\glib-2.0\schemas"' $0

  SetOutPath "$INSTDIR\share\locale"
  File /r /x "gtk30-properties.mo" "${EnvDir}/share/locale/*"

  File "/oname=$INSTDIR\share\README.txt"  "${SourceDir}/README"
  File "/oname=$INSTDIR\share\COPYING.txt" "${SourceDir}/COPYING"
  File "/oname=$INSTDIR\share\NEWS.txt"    "${SourceDir}/NEWS"
  File "/oname=$INSTDIR\share\THANKS.txt"  "${SourceDir}/THANKS"
  File "/oname=$INSTDIR\share\AUTHORS.txt" "${SourceDir}/AUTHORS"

  !insertmacro UNINSTALL.LOG_CLOSE_INSTALL

  # call userInfo plugin to get user info.  The plugin puts the result in the stack
  userInfo::getAccountType
  pop $0
  strCmp $0 "Admin" Admin
  # not admin
    WriteRegStr HKCU "Software\Classes\${SavExt}" "" "PSPP System File"
    WriteRegStr HKCU "Software\Classes\${SavExt}" "Content Type" "application/x-spss-sav"
    WriteRegStr HKCU "Software\Classes\PSPP System File\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-sav.ico"
    WriteRegStr HKCU "Software\Classes\PSPP System File\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

    WriteRegStr HKCU "Software\Classes\${ZSavExt}" "" "PSPP System File Compressed"
#    WriteRegStr HKCU "Software\Classes\${ZSavExt}" "Content Type" "application/x-spss-sav"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\application/x-spss-sav" "Extension" ".sav"
    WriteRegStr HKCU "Software\Classes\PSPP System File Compressed\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-zsav.ico"
    WriteRegStr HKCU "Software\Classes\PSPP System File Compressed\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

    WriteRegStr HKCU "Software\Classes\${PorExt}" "" "PSPP Portable File"
    WriteRegStr HKCU "Software\Classes\${PorExt}" "Content Type" "application/x-spss-por"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\application/x-spss-por" "Extension" ".por"
    WriteRegStr HKCU "Software\Classes\PSPP Portable File\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-por.ico,0"
    WriteRegStr HKCU "Software\Classes\PSPP Portable File\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

    WriteRegStr HKCU "Software\Classes\${SpsExt}" "" "PSPP Syntax File"
    WriteRegStr HKCU "Software\Classes\${SpsExt}" "Content Type" "application/x-spss-sps"
    WriteRegStr HKCU "Software\Classes\PSPP Syntax File\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-sps.ico,0"
    WriteRegStr HKCU "Software\Classes\PSPP Syntax File\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

  # registering additional mime types
    WriteRegStr HKCU "Software\Classes\.txt"  "Content Type" "text/plain"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\text/plain" "Extension" ".txt"
    WriteRegStr HKCU "Software\Classes\.text" "Content Type" "text/plain"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\text/plain" "Extension" ".text"
    WriteRegStr HKCU "Software\Classes\.csv"  "Content Type" "text/csv"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\text/csv" "Extension" ".csv"
    WriteRegStr HKCU "Software\Classes\.tsv"  "Content Type" "text/tab-separated-values"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\text/tab-separated-values" "Extension" ".tsv"
    WriteRegStr HKCU "Software\Classes\.gnumeric" "Content Type" "application/x-gnumeric"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\text/plain" "Extension" ".gnumeric"
    WriteRegStr HKCU "Software\Classes\.ods"  "Content Type" "application/vnd.oasis.opendocument.spreadsheet"
    WriteRegStr HKCU "Software\Classes\MIME\Database\Content Type\application/vnd.oasis.opendocument.spreadsheet" "Extension" ".ods"

    WriteRegStr HKCU ${INSTDIR_REG_KEY} "DisplayName"     "${APP_NAME}"
    WriteRegStr HKCU ${INSTDIR_REG_KEY} "UninstallString" '"$INSTDIR\UNINSTALL.exe"'
    WriteRegStr HKCU ${INSTDIR_REG_KEY} "Publisher"       "Free Software Foundation, Inc."
    WriteRegStr HKCU ${INSTDIR_REG_KEY} "DisplayIcon"     '"$INSTDIR\bin\psppire.exe"'
    WriteRegStr HKCU ${INSTDIR_REG_KEY} "DisplayVersion"  "${pspp_version}"

    Goto EndStrCmp
  Admin:  ;MultiUser Install
    SetShellVarContext all
    WriteRegStr HKCR "${SavExt}" "" "PSPP System File"
    WriteRegStr HKCR "${SavExt}" "Content Type" "application/x-spss-sav"
    WriteRegStr HKCR "MIME\Database\Content Type\application/x-spss-sav" "Extension" ".sav"
    WriteRegStr HKCR "PSPP System File\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-sav.ico,0"
    WriteRegStr HKCR "PSPP System File\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

    WriteRegStr HKCR "${ZSavExt}" "" "PSPP System File Compressed"
    WriteRegStr HKCR "PSPP System File Compressed\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-zsav.ico,0"
    WriteRegStr HKCR "PSPP System File Compressed\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

    WriteRegStr HKCR "${PorExt}" "" "PSPP Portable File"
    WriteRegStr HKCR "${PorExt}" "Content Type" "application/x-spss-por"
    WriteRegStr HKCR "MIME\Database\Content Type\application/x-spss-por" "Extension" ".por"
    WriteRegStr HKCR "PSPP Portable File\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-por.ico,0"
    WriteRegStr HKCR "PSPP Portable File\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

    WriteRegStr HKCR "${SpsExt}" "" "PSPP Syntax File"
    WriteRegStr HKCR "${SpsExt}" "Content Type" "application/x-spss-sps"
    WriteRegStr HKCR "PSPP Syntax File\DefaultIcon" "" "$INSTDIR\share\pspp\icons\pspp-sps.ico,0"
    WriteRegStr HKCR "PSPP Syntax File\Shell\Open\Command" "" '$INSTDIR\bin\psppire.exe "%1"'

  # registering additional mime types
    WriteRegStr HKCR ".txt"  "Content Type" "text/plain"
    WriteRegStr HKCR "MIME\Database\Content Type\text/plain" "Extension" ".txt"
    WriteRegStr HKCR ".text" "Content Type" "text/plain"
    WriteRegStr HKCR "MIME\Database\Content Type\text/plain" "Extension" ".text"
    WriteRegStr HKCR ".csv"  "Content Type" "text/csv"
    WriteRegStr HKCR "MIME\Database\Content Type\text/csv" "Extension" ".csv"
    WriteRegStr HKCR ".tsv"  "Content Type" "text/tab-separated-values"
    WriteRegStr HKCR "MIME\Database\Content Type\text/tab-separated-values" "Extension" ".tsv"
    WriteRegStr HKCR ".gnumeric" "Content Type" "application/x-gnumeric"
    WriteRegStr HKCR "MIME\Database\Content Type\text/plain" "Extension" ".gnumeric"
    WriteRegStr HKCR ".ods"  "Content Type" "application/vnd.oasis.opendocument.spreadsheet"
    WriteRegStr HKCR "MIME\Database\Content Type\application/vnd.oasis.opendocument.spreadsheet" "Extension" ".ods"

    WriteRegStr HKLM ${INSTDIR_REG_KEY} "DisplayName"     "${APP_NAME}"
    WriteRegStr HKLM ${INSTDIR_REG_KEY} "UninstallString" '"$INSTDIR\UNINSTALL.exe"'
    WriteRegStr HKLM ${INSTDIR_REG_KEY} "Publisher"       "Free Software Foundation, Inc."
    WriteRegStr HKLM ${INSTDIR_REG_KEY} "DisplayIcon"     '"$INSTDIR\bin\psppire.exe"'
    WriteRegStr HKLM ${INSTDIR_REG_KEY} "DisplayVersion"  "${pspp_version}"

  EndStrCmp:


# flush the icon cache
  System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'

  ; Now create shortcuts
  SetOutPath "%HOMEDRIVE%HOMEPATH"
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\PSPP.lnk"           "$INSTDIR\bin\psppire.exe" "" "$INSTDIR\share\pspp\icons\pspp.ico"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\README.lnk"         "$INSTDIR\share\README.txt"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Manual.lnk"         "$INSTDIR\share\doc\pspp\pspp.pdf"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\License.lnk"        "$INSTDIR\share\COPYING.txt"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\NEWS.lnk"           "$INSTDIR\share\NEWS.txt"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\THANKS.lnk"         "$INSTDIR\share\THANKS.txt"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\AUTHORS.lnk"        "$INSTDIR\share\AUTHORS.txt"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Examples.lnk"       "$INSTDIR\share\pspp\examples"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall PSPP.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\${APP_NAME}.lnk"                   "$INSTDIR\bin\psppire.exe"  "" "$INSTDIR\share\pspp\icons\pspp.ico"
  IfFileExists "$INSTDIR\bin\gdb.exe" file_found file_not_found
  file_found:
    IfFileExists   "c:\windows\command\start.exe" on-wine on-windows
    on-wine:
      SetOutPath "$INSTDIR"
      CreateShortcut "$DESKTOP\DebugPSPP.lnk"          "$INSTDIR\bin\gdb" 'bin\psppire.exe' '"$INSTDIR\bin"'
      CreateShortcut "$INSTDIR\DebugPSPP.lnk"          "$INSTDIR\bin\gdb" 'bin\psppire.exe' '"$INSTDIR\bin"'
      SetOutPath "%HOMEDRIVE%HOMEPATH"
    goto end_of_test-win
    on-windows:
      SetOutPath "$INSTDIR"
      CreateShortCut "$DESKTOP\PSPPDebug.lnk"          "$INSTDIR\bin\DebugPSPP.bat" '"$INSTDIR\bin"'
      CreateShortcut "$SMPROGRAMS\PSPP\DebugPSPP.lnk"  "$INSTDIR\bin\DebugPSPP.BAT" '"$INSTDIR\bin"'
      SetOutPath "%HOMEDRIVE%HOMEPATH"
      CreateShortcut "$DESKTOP\GDB Manual.lnk"           "http://sourceware.org/gdb/current/onlinedocs/gdb/"
      CreateShortcut "$SMPROGRAMS\PSPP\GDB Manual.lnk"   "http://sourceware.org/gdb/current/onlinedocs/gdb/"
    end_of_test-win:
  file_not_found:
  CreateShortCut "$DESKTOP\PSPP Manual.lnk"            "$INSTDIR\share\doc\pspp\pspp.pdf"
  CreateShortcut "$SMPROGRAMS\PSPP\PSPP_Commandline.lnk"  "$INSTDIR\bin\pspp.exe" '"$INSTDIR\bin"'

  ;Create uninstaller
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

SectionEnd

;--------------------------------
;Installer Functions

Function .onInit
  ;; Test Versions get a default directory of "PSPP-TESTING"
  ;; Released Versions have "PSPP"
  ;; Debug Versions have "PSPP-DEBUG"
  Var /global DefaultInstallDir
  ${If} ${DEBUG} == 1
    StrCpy $DefaultInstallDir "${APP_NAME}-DEBUG"
  ${Else}
  Intop $0 ${vers_min} % 2
  ${If} $0 == 0
    StrCpy $DefaultInstallDir "${APP_NAME}"
  ${Else}
    StrCpy $DefaultInstallDir "${APP_NAME}-TESTING"
  ${Endif}
  ${Endif}

  MessageBox MB_OKCANCEL "This windoze installer is work in progress.  It is known to have deficiencies and is recommended for testing and evaluation purposes only." IDOK continue
  Abort ; Allow the use to decide not to continue
  continue:

; initial actions

  # call userInfo plugin to get user info.  The plugin puts the result in the stack
  userInfo::getAccountType
  pop $0
  strCmp $0 "Admin" Admin
   # not admin
    StrCpy $INSTDIR "$PROFILE\$DefaultInstallDir"
    Goto EndStrCmp
  Admin:
    StrCpy $INSTDIR "${ProgramFiles}\$DefaultInstallDir"
  EndStrCmp:

  !insertmacro UNINSTALL.LOG_PREPARE_INSTALL
FunctionEnd

Function .onInstSuccess
  !insertmacro UNINSTALL.LOG_UPDATE_INSTALL
FunctionEnd

;Uninstaller Section

Section "Uninstall"

  ;; Remove these directories in a single operation.
  ;; Doing so makes uninstallation much faster.  However don't
  ;; be tempted to do "Rmdir /r $INSTDIR" - this would wipe out
  ;; the user's entire system if she had decided that INSTDIR is
  ;; the C:\ directory.

  Rmdir /r "$INSTDIR\etc"
  Rmdir /r "$INSTDIR\bin"
  Rmdir /r "$INSTDIR\share"

  !insertmacro UNINSTALL.LOG_UNINSTALL "$INSTDIR"
  !insertmacro UNINSTALL.LOG_UNINSTALL "$APPDATA\${APP_NAME}"
  !insertmacro UNINSTALL.LOG_END_UNINSTALL

  userInfo::getAccountType
  pop $0
  strCmp $0 "Admin" Admin
  # not admin
    DeleteRegKey HKCU "Software\Classes\${PorExt}"
    DeleteRegKey HKCU "Software\Classes\${SavExt}"
    DeleteRegKey HKCU "Software\Classes\${SpsExt}"
    DeleteRegKey HKCU "Software\Classes\PSPP Portable File"
    DeleteRegKey HKCU "Software\Classes\PSPP System File"
    DeleteRegKey HKCU "Software\Classes\PSPP Syntax File"

    DeleteRegKey HKCU "${INSTDIR_REG_KEY}"

    Goto EndStrCmp
  Admin:
    SetShellVarContext all
    DeleteRegKey HKCR "${PorExt}"
    DeleteRegKey HKCR "${SavExt}"
    DeleteRegKey HKCR "${SpsExt}"
    DeleteRegKey HKCR "PSPP Portable File"
    DeleteRegKey HKCR "PSPP System File"
    DeleteRegKey HKCR "PSPP Syntax File"

    DeleteRegKey HKLM "${INSTDIR_REG_KEY}"

  EndStrCmp:

  ; Now remove shortcuts too
  RMDIR /r "$SMPROGRAMS\PSPP"
  Delete "$DESKTOP\PSPP.lnk"
  Delete "$DESKTOP\PSPPDebug.lnk"
  Delete "$DESKTOP\PSPP Manual.lnk"
  Delete "$DESKTOP\GDB Manual.lnk"

  # call userInfo plugin to get user info.  The plugin puts the result in the stack

SectionEnd

;----------------------------------------------------

Function UN.onInit
  !insertmacro UNINSTALL.LOG_BEGIN_UNINSTALL
FunctionEnd
;--------------------------------------------
