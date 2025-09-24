// Determine a canonical name for the current locale's character encoding.
//
// Copyright (C) 2000-2006, 2008-2023 Free Software Foundation, Inc.
//
// This file is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//
// This file is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
// details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Written by Bruno Haible <bruno@clisp.org>.  Translated to Rust by Ben Pfaff
// <blp@cs.stanford.edu>.

use std::sync::LazyLock;

fn map_aliases(s: &str) -> &'static str {
    #[cfg(target_os = "freebsd")]
    match s {
        "ARMSCII-8" => return "ARMSCII-8",
        "Big5" => return "BIG5",
        "C" => return "ASCII",
        "CP1131" => return "CP1131",
        "CP1251" => return "CP1251",
        "CP866" => return "CP866",
        "GB18030" => return "GB18030",
        "GB2312" => return "GB2312",
        "GBK" => return "GBK",
        "ISCII-DEV" => return "?",
        "ISO8859-1" => return "ISO-8859-1",
        "ISO8859-13" => return "ISO-8859-13",
        "ISO8859-15" => return "ISO-8859-15",
        "ISO8859-2" => return "ISO-8859-2",
        "ISO8859-5" => return "ISO-8859-5",
        "ISO8859-7" => return "ISO-8859-7",
        "ISO8859-9" => return "ISO-8859-9",
        "KOI8-R" => return "KOI8-R",
        "KOI8-U" => return "KOI8-U",
        "SJIS" => return "SHIFT_JIS",
        "US-ASCII" => return "ASCII",
        "eucCN" => return "GB2312",
        "eucJP" => return "EUC-JP",
        "eucKR" => return "EUC-KR",
        _ => (),
    };

    #[cfg(target_os = "netbsd")]
    match s {
        "646" => return "ASCII",
        "ARMSCII-8" => return "ARMSCII-8",
        "BIG5" => return "BIG5",
        "Big5-HKSCS" => return "BIG5-HKSCS",
        "CP1251" => return "CP1251",
        "CP866" => return "CP866",
        "GB18030" => return "GB18030",
        "GB2312" => return "GB2312",
        "ISO8859-1" => return "ISO-8859-1",
        "ISO8859-13" => return "ISO-8859-13",
        "ISO8859-15" => return "ISO-8859-15",
        "ISO8859-2" => return "ISO-8859-2",
        "ISO8859-4" => return "ISO-8859-4",
        "ISO8859-5" => return "ISO-8859-5",
        "ISO8859-7" => return "ISO-8859-7",
        "KOI8-R" => return "KOI8-R",
        "KOI8-U" => return "KOI8-U",
        "PT154" => return "PT154",
        "SJIS" => return "SHIFT_JIS",
        "eucCN" => return "GB2312",
        "eucJP" => return "EUC-JP",
        "eucKR" => return "EUC-KR",
        "eucTW" => return "EUC-TW",
        _ => (),
    };

    #[cfg(target_os = "openbsd")]
    match s {
        "646" => return "ASCII",
        "ISO8859-1" => return "ISO-8859-1",
        "ISO8859-13" => return "ISO-8859-13",
        "ISO8859-15" => return "ISO-8859-15",
        "ISO8859-2" => return "ISO-8859-2",
        "ISO8859-4" => return "ISO-8859-4",
        "ISO8859-5" => return "ISO-8859-5",
        "ISO8859-7" => return "ISO-8859-7",
        "US-ASCII" => return "ASCII",
        _ => (),
    };

    /* Darwin 7.5 has nl_langinfo(CODESET), but sometimes its value is
      useless:
      - It returns the empty string when LANG is set to a locale of the
        form ll_CC, although ll_CC/LC_CTYPE is a symlink to an UTF-8
        LC_CTYPE file.
      - The environment variables LANG, LC_CTYPE, LC_ALL are not set by
        the system; nl_langinfo(CODESET) returns "US-ASCII" in this case.
      - The documentation says:
          "... all code that calls BSD system routines should ensure
           that the const *char parameters of these routines are in UTF-8
           encoding. All BSD system functions expect their string
           parameters to be in UTF-8 encoding and nothing else."
        It also says
          "An additional caveat is that string parameters for files,
           paths, and other file-system entities must be in canonical
           UTF-8. In a canonical UTF-8 Unicode string, all decomposable
           characters are decomposed ..."
        but this is not true: You can pass non-decomposed UTF-8 strings
        to file system functions, and it is the OS which will convert
        them to decomposed UTF-8 before accessing the file system.
      - The Apple Terminal application displays UTF-8 by default.
      - However, other applications are free to use different encodings:
        - xterm uses ISO-8859-1 by default.
        - TextEdit uses MacRoman by default.
      We prefer UTF-8 over decomposed UTF-8-MAC because one should
      minimize the use of decomposed Unicode. Unfortunately, through the
      Darwin file system, decomposed UTF-8 strings are leaked into user
      space nevertheless.
      Then there are also the locales with encodings other than US-ASCII
      and UTF-8. These locales can be occasionally useful to users (e.g.
      when grepping through ISO-8859-1 encoded text files), when all their
      file names are in US-ASCII.
    */

    #[cfg(target_os = "macos")]
    match s {
        "ARMSCII-8" => return "ARMSCII-8",
        "Big5" => return "BIG5",
        "Big5HKSCS" => return "BIG5-HKSCS",
        "CP1131" => return "CP1131",
        "CP1251" => return "CP1251",
        "CP866" => return "CP866",
        "CP949" => return "CP949",
        "GB18030" => return "GB18030",
        "GB2312" => return "GB2312",
        "GBK" => return "GBK",
        "ISO8859-1" => return "ISO-8859-1",
        "ISO8859-13" => return "ISO-8859-13",
        "ISO8859-15" => return "ISO-8859-15",
        "ISO8859-2" => return "ISO-8859-2",
        "ISO8859-4" => return "ISO-8859-4",
        "ISO8859-5" => return "ISO-8859-5",
        "ISO8859-7" => return "ISO-8859-7",
        "ISO8859-9" => return "ISO-8859-9",
        "KOI8-R" => return "KOI8-R",
        "KOI8-U" => return "KOI8-U",
        "PT154" => return "PT154",
        "SJIS" => return "SHIFT_JIS",
        "eucCN" => return "GB2312",
        "eucJP" => return "EUC-JP",
        "eucKR" => return "EUC-KR",
        _ => (),
    };

    #[cfg(target_os = "aix")]
    match s {
        "GBK" => return "GBK",
        "IBM-1046" => return "CP1046",
        "IBM-1124" => return "CP1124",
        "IBM-1129" => return "CP1129",
        "IBM-1252" => return "CP1252",
        "IBM-850" => return "CP850",
        "IBM-856" => return "CP856",
        "IBM-921" => return "ISO-8859-13",
        "IBM-922" => return "CP922",
        "IBM-932" => return "CP932",
        "IBM-943" => return "CP943",
        "IBM-eucCN" => return "GB2312",
        "IBM-eucJP" => return "EUC-JP",
        "IBM-eucKR" => return "EUC-KR",
        "IBM-eucTW" => return "EUC-TW",
        "ISO8859-1" => return "ISO-8859-1",
        "ISO8859-15" => return "ISO-8859-15",
        "ISO8859-2" => return "ISO-8859-2",
        "ISO8859-5" => return "ISO-8859-5",
        "ISO8859-6" => return "ISO-8859-6",
        "ISO8859-7" => return "ISO-8859-7",
        "ISO8859-8" => return "ISO-8859-8",
        "ISO8859-9" => return "ISO-8859-9",
        "TIS-620" => return "TIS-620",
        "UTF-8" => return "UTF-8",
        "big5" => return "BIG5",
        _ => (),
    };

    #[cfg(windows)]
    match s {
        "CP1361" => return "JOHAB",
        "CP20127" => return "ASCII",
        "CP20866" => return "KOI8-R",
        "CP20936" => return "GB2312",
        "CP21866" => return "KOI8-RU",
        "CP28591" => return "ISO-8859-1",
        "CP28592" => return "ISO-8859-2",
        "CP28593" => return "ISO-8859-3",
        "CP28594" => return "ISO-8859-4",
        "CP28595" => return "ISO-8859-5",
        "CP28596" => return "ISO-8859-6",
        "CP28597" => return "ISO-8859-7",
        "CP28598" => return "ISO-8859-8",
        "CP28599" => return "ISO-8859-9",
        "CP28605" => return "ISO-8859-15",
        "CP38598" => return "ISO-8859-8",
        "CP51932" => return "EUC-JP",
        "CP51936" => return "GB2312",
        "CP51949" => return "EUC-KR",
        "CP51950" => return "EUC-TW",
        "CP54936" => return "GB18030",
        "CP65001" => return "UTF-8",
        "CP936" => return "GBK",
        _ => (),
    };

    String::from(s).leak()
}

#[cfg(unix)]
mod inner {
    use std::{
        ffi::{CStr, CString, c_int},
        ptr::null,
    };

    use libc::{self, CODESET, LC_CTYPE, nl_langinfo, setlocale};

    unsafe fn string_from_pointer(s: *const i8) -> Option<String> {
        if s.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(s).to_string_lossy().into() })
        }
    }

    fn set_locale(category: c_int, locale: Option<&str>) -> Option<String> {
        unsafe {
            let locale = locale.map(|s| CString::new(s).unwrap());
            let locale_ptr = locale.as_ref().map_or(null(), |s| s.as_ptr());
            string_from_pointer(setlocale(category, locale_ptr))
        }
    }

    pub fn locale_charset() -> Option<String> {
        unsafe {
            let saved_locale = set_locale(LC_CTYPE, None);
            set_locale(LC_CTYPE, Some(""));
            let codeset = string_from_pointer(nl_langinfo(CODESET));
            set_locale(LC_CTYPE, saved_locale.as_deref());
            codeset
        }
    }
}

#[cfg(windows)]
mod inner {
    use libc::{LC_CTYPE, setlocale};
    use std::ffi::{CStr, CString};
    use windows_sys::Win32::Globalization::GetACP;

    fn current_locale() -> Option<String> {
        unsafe {
            let empty_cstr = CString::new("").unwrap();
            let locale = setlocale(LC_CTYPE, empty_cstr.as_ptr());
            if locale.is_null() {
                None
            } else {
                Some(CStr::from_ptr(locale).to_string_lossy().into())
            }
        }
    }

    pub fn locale_charset() -> Option<String> {
        let Some(current_locale) = current_locale() else {
            return None;
        };
        let codepage = if let Some((_, pdot)) = current_locale.rsplit_once('.') {
            format!("CP{pdot}")
        } else {
            format!("CP{}", unsafe { GetACP() })
        };
        Some(match codepage.as_str() {
            "CP65001" | "CPutf8" => String::from("UTF-8"),
            _ => codepage,
        })
    }
}

#[cfg(not(any(unix, windows)))]
mod inner {
    pub fn locale_charse() -> String {
        String::from("UTF-8")
    }
}

/// Returns the character set used by the locale configured in the operating
/// system.
pub fn locale_charset() -> &'static str {
    static LOCALE_CHARSET: LazyLock<&'static str> =
        LazyLock::new(|| map_aliases(&inner::locale_charset().unwrap_or(String::from("UTF-8"))));
    &LOCALE_CHARSET
}
