/* PSPP - a program for statistical analysis.
   Copyright (C) 2020 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "tex-glyphs.h"

const char *tex_macro[] =
  {
   /* TEX_NONE */
   "",
   /* TEX_VULGAR_FRAC */
   "%% Typset a vulgar fraction (without a /).  The lack\n"
   "%% of / is common in many typefaces (e.g. `Transport') and is easier to read.\n"
   "\\def\\vulgarfrac#1/#2{\\leavevmode"
   "\\raise.5ex\\hbox{\\the\\scriptfont0 #1}\\kern-.1em"
   "\\lower.25ex\\hbox{\\the\\scriptfont0 #2}}",
   /* TEX_OGONEK */
  "\\def\\ogonekx#1#2{#1\\hskip -#2\\llap{\\smash{\\lower1ex\\hbox{\\the\\textfont1 \\char\"2C}}}}\n"
   "\\def\\ogonek#1{\\ogonekx{#1}{0pt}}",
   /* TEX_THORN_UC */
   "\\def\\Thorn{{\\font\\xx=cmr7 \\xx \\rlap{\\raise 0.74ex\\hbox{I}}P}}",
   /* TEX_THORN_LC */
   "\\def\\thorn{{\\rlap {\\lower 0.7ex \\hbox{l}}b}}",
   /* TEX_GUILLEMET_LEFT */
   "\\def\\lguillemet{{\\raise0.5ex\\hbox{\\font\\xx=cmsy5 \\xx \\char\"1C}}}",
   /* TEX_GUILLEMET_RIGHT */
   "\\def\\rguillemet{{\\raise0.5ex\\hbox{\\font\\xx=cmsy5 \\xx \\char\"1D}}}",
   /* TEX_ETH */
   "\\def\\eth{\\rlap{\\hskip 0.08em\\raise 0.5ex\\hbox{\\the\\textfont0 \\char\"20}}"
   "\\rlap{\\raise 1.5ex\\hbox{\\hskip -0.04em\\vbox to 0pt{\\hbox{\\font\\xx=cmr17 \\xx \\`\\ }\\vss}}}o}",
   /* TEX_DOT */
   "\\def\\dotabove#1{{\\ifnum\\fam=7 \\raise1.5ex\\rlap{.}#1\\else\\.#1\\fi}}",
   /* TEX_DOUBLE_ACUTE */
   "\\def\\doubleacute#1{\\ifnum\\fam=7 {\\setbox0=\\hbox{#1}\\setbox1=\\hbox{o}\\dimen0=\\ht0\\advance\\dimen0 -\\ht1"
   " \\raise\\dimen0\\rlap{\\kern -0.25ex\\char\"13\\kern -0.8ex\\char\"13}#1}\\else\\H{#1}\\fi}"
};



const char *unsupported_glyph = "{\\tt\\char\"20}";


static const struct glyph control_codes [] =
  {
   {0x0009, "TAB", TEX_NONE, " "},
   {0x000A, "LINE FEED", TEX_NONE, "{\\hfil\\break}"}
  };

static const struct glyph basic_latin [] =
  {
   {0x0020, "SPACE", TEX_NONE, " "},
   {0x0021, "EXCLAMATION MARK", TEX_NONE, "!"},
   {0x0022, "QUOTATION MARK", TEX_NONE, "``"},
   {0x0023, "NUMBER SIGN", TEX_NONE, "\\#"},
   /* In the italic family, $ shows up as pound sterling.  So use
      the slanted typeface which is close enough.  */
   {0x0024, "DOLLAR SIGN", TEX_NONE, "{\\ifnum\\fam=4{\\sl\\$}\\else\\$\\fi}"},
   {0x0025, "PERCENT SIGN", TEX_NONE, "\\%"},
   {0x0026, "AMPERSAND", TEX_NONE, "\\&"},
   {0x0027, "APOSTROPHE", TEX_NONE, "'"},
   {0x0028, "LEFT PARENTHESIS", TEX_NONE, "("},
   {0x0029, "RIGHT PARENTHESIS", TEX_NONE, ")"},
   {0x002A, "ASTERISK", TEX_NONE, "*"},
   {0x002B, "PLUS SIGN", TEX_NONE, "+"},
   {0x002C, "COMMA", TEX_NONE, ","},
   {0x002D, "HYPHEN-MINUS", TEX_NONE, "-"},
   {0x002E, "FULL STOP", TEX_NONE, "."},
   {0x002F, "SOLIDUS", TEX_NONE, "/"},
   {0x0030, "DIGIT ZERO", TEX_NONE,  "0"},
   {0x0031, "DIGIT ONE", TEX_NONE,   "1"},
   {0x0032, "DIGIT TWO", TEX_NONE,   "2"},
   {0x0033, "DIGIT THREE", TEX_NONE, "3"},
   {0x0034, "DIGIT FOUR", TEX_NONE,  "4"},
   {0x0035, "DIGIT FIVE", TEX_NONE,  "5"},
   {0x0036, "DIGIT SIX", TEX_NONE,   "6"},
   {0x0037, "DIGIT SEVEN", TEX_NONE, "7"},
   {0x0038, "DIGIT EIGHT", TEX_NONE, "8"},
   {0x0039, "DIGIT NINE", TEX_NONE,  "9"},
   {0x003A, "COLON", TEX_NONE, ":"},
   {0x003B, "SEMICOLON", TEX_NONE, ";"},
   {0x003C, "LESS-THAN SIGN", TEX_NONE, "{\\ifnum\\fam=7 \\char\"3C\\else $<$\\fi}"},
   {0x003D, "EQUALS SIGN", TEX_NONE, "="},
   {0x003E, "GREATER-THAN SIGN", TEX_NONE, "{\\ifnum\\fam=7 \\char\"3E\\else $>$\\fi}"},
   {0x003F, "QUESTION MARK", TEX_NONE, "?"},
   {0x0040, "COMMERCIAL AT", TEX_NONE, "@"},
   {0x0041, "LATIN CAPITAL LETTER A", TEX_NONE, "A"},
   {0X0042, "LATIN CAPITAL LETTER B", TEX_NONE, "B"},
   {0X0043, "LATIN CAPITAL LETTER C", TEX_NONE, "C"},
   {0X0044, "LATIN CAPITAL LETTER D", TEX_NONE, "D"},
   {0X0045, "LATIN CAPITAL LETTER E", TEX_NONE, "E"},
   {0X0046, "LATIN CAPITAL LETTER F", TEX_NONE, "F"},
   {0X0047, "LATIN CAPITAL LETTER G", TEX_NONE, "G"},
   {0X0048, "LATIN CAPITAL LETTER H", TEX_NONE, "H"},
   {0X0049, "LATIN CAPITAL LETTER I", TEX_NONE, "I"},
   {0X004A, "LATIN CAPITAL LETTER J", TEX_NONE, "J"},
   {0X004B, "LATIN CAPITAL LETTER K", TEX_NONE, "K"},
   {0X004C, "LATIN CAPITAL LETTER L", TEX_NONE, "L"},
   {0X004D, "LATIN CAPITAL LETTER M", TEX_NONE, "M"},
   {0X004E, "LATIN CAPITAL LETTER N", TEX_NONE, "N"},
   {0X004F, "LATIN CAPITAL LETTER O", TEX_NONE, "O"},
   {0X0050, "LATIN CAPITAL LETTER P", TEX_NONE, "P"},
   {0X0051, "LATIN CAPITAL LETTER Q", TEX_NONE, "Q"},
   {0X0052, "LATIN CAPITAL LETTER R", TEX_NONE, "R"},
   {0X0053, "LATIN CAPITAL LETTER S", TEX_NONE, "S"},
   {0X0054, "LATIN CAPITAL LETTER T", TEX_NONE, "T"},
   {0X0055, "LATIN CAPITAL LETTER U", TEX_NONE, "U"},
   {0X0056, "LATIN CAPITAL LETTER V", TEX_NONE, "V"},
   {0X0057, "LATIN CAPITAL LETTER W", TEX_NONE, "W"},
   {0X0058, "LATIN CAPITAL LETTER X", TEX_NONE, "X"},
   {0X0059, "LATIN CAPITAL LETTER Y", TEX_NONE, "Y"},
   {0X005A, "LATIN CAPITAL LETTER Z", TEX_NONE, "Z"},
   {0x005B, "LEFT SQUARE BRACKET", TEX_NONE, "["},
   {0x005C, "REVERSE SOLIDUS", TEX_NONE, "{\\ifnum\\fam=7 \\char\"5C\\else $\\backslash$\\fi}" },
   {0x005D, "RIGHT SQUARE BRACKET", TEX_NONE, "]"},
   {0x005E, "CIRCUMFLEX ACCENT", TEX_NONE, "\\^{}"},
   {0x005F, "LOW LINE", TEX_NONE, "\\_"},
   {0x0060, "GRAVE ACCENT", TEX_NONE, "\\`{}"},
   {0x0061, "LATIN SMALL LETTER A", TEX_NONE, "a"},
   {0x0062, "LATIN SMALL LETTER B", TEX_NONE, "b"},
   {0x0063, "LATIN SMALL LETTER C", TEX_NONE, "c"},
   {0x0064, "LATIN SMALL LETTER D", TEX_NONE, "d"},
   {0x0065, "LATIN SMALL LETTER E", TEX_NONE, "e"},
   {0x0066, "LATIN SMALL LETTER F", TEX_NONE, "f"},
   {0x0067, "LATIN SMALL LETTER G", TEX_NONE, "g"},
   {0x0068, "LATIN SMALL LETTER H", TEX_NONE, "h"},
   {0x0069, "LATIN SMALL LETTER I", TEX_NONE, "i"},
   {0x006A, "LATIN SMALL LETTER J", TEX_NONE, "j"},
   {0x006B, "LATIN SMALL LETTER K", TEX_NONE, "k"},
   {0x006C, "LATIN SMALL LETTER L", TEX_NONE, "l"},
   {0x006D, "LATIN SMALL LETTER M", TEX_NONE, "m"},
   {0x006E, "LATIN SMALL LETTER N", TEX_NONE, "n"},
   {0x006F, "LATIN SMALL LETTER O", TEX_NONE, "o"},
   {0x0070, "LATIN SMALL LETTER P", TEX_NONE, "p"},
   {0x0071, "LATIN SMALL LETTER Q", TEX_NONE, "q"},
   {0x0072, "LATIN SMALL LETTER R", TEX_NONE, "r"},
   {0x0073, "LATIN SMALL LETTER S", TEX_NONE, "s"},
   {0x0074, "LATIN SMALL LETTER T", TEX_NONE, "t"},
   {0x0075, "LATIN SMALL LETTER U", TEX_NONE, "u"},
   {0x0076, "LATIN SMALL LETTER V", TEX_NONE, "v"},
   {0x0077, "LATIN SMALL LETTER W", TEX_NONE, "w"},
   {0x0078, "LATIN SMALL LETTER X", TEX_NONE, "x"},
   {0x0079, "LATIN SMALL LETTER Y", TEX_NONE, "y"},
   {0x007A, "LATIN SMALL LETTER Z", TEX_NONE, "z"},
   {0x007B, "LEFT CURLY BRACKET", TEX_NONE, "{\\ifnum\\fam=7 \\char\"7B\\else $\\{$\\fi}" },
   {0x007C, "VERTICAL LINE", TEX_NONE,  "{\\ifnum\\fam=7 \\char\"7C\\else {\\the\\textfont2 \\char\"6A}\\fi}" },
   {0x007D, "RIGHT CURLY BRACKET", TEX_NONE, "{\\ifnum\\fam=7 \\char\"7D\\else $\\}$\\fi}" },
   {0x007E, "TILDE", TEX_NONE, "{\\ifnum\\fam=7 \\char\"7E\\else {\\the\\textfont2 \\char\"18}\\fi}" },
  };


static const struct glyph extended_latin [] =
  {
   { 0x00A0, "NO-BREAK SPACE", TEX_NONE, "~" },
   { 0x00A1, "INVERTED EXCLAMATION MARK", TEX_NONE, "!`" },
   { 0x00A2, "CENT SIGN", TEX_NONE, "\\rlap /c" },
   { 0x00A3, "POUND SIGN", TEX_NONE, "{\\it \\$}" },
   { 0x00A4, "CURRENCY SIGN", TEX_NONE,
     "\\rlap{\\kern 0.028em\\raise 0.2ex\\hbox{\\the\\textfont2\\char\"0E}}"
     "{\\ifnum\\fam=7\\kern -0.3ex\\fi"
     "\\rlap{\\raise 1.05ex\\hbox{.}}\\rlap{\\kern 0.28em\\raise 1.05ex\\hbox{.}}"
     "\\rlap{\\raise 0.28ex\\hbox{.}}{\\kern 0.28em\\raise 0.28ex\\hbox{.}}"
     "}" },
   { 0x00A5, "YEN SIGN", TEX_NONE, "\\rlap Y=" },
   { 0x00A6, "BROKEN BAR", TEX_NONE, "{\\thinspace\\rlap{\\hbox{\\vrule height 0.7ex depth 0pt}}{\\raise 0.9ex\\hbox{\\vrule height 0.7ex depth 0pt}}}" },
   { 0x00A7, "SECTION SIGN", TEX_NONE, "{\\S}" },
   { 0x00A8, "DIAERESIS", TEX_NONE, "\\\"{}" },
   { 0x00A9, "COPYRIGHT SIGN", TEX_NONE, "{\\copyright}" },
   { 0x00AA, "FEMININE ORDINAL INDICATOR", TEX_NONE, "$\\rm ^{\\b a}$" },
   { 0x00AB, "LEFT-POINTING DOUBLE ANGLE QUOTATION MARK", TEX_GUILLEMET_LEFT, "{\\lguillemet}" },
   { 0x00AC, "NOT SIGN", TEX_NONE, "$\\neg$" },
   { 0x00AD, "SOFT HYPHEN", TEX_NONE, "\\-" },
   { 0x00AE, "REGISTERED SIGN", TEX_NONE, "{\\font\\sc=cmr7 \\rlap {\\sc \\hskip 2pt\\relax R}$\\bigcirc$}" },
   { 0x00AF, "MACRON", TEX_NONE, "\\={}" },
   { 0x00B0, "DEGREE SIGN", TEX_NONE, "$^\\circ$" },
   { 0x00B1, "PLUS-MINUS SIGN", TEX_NONE, "$\\pm$" },
   { 0x00B2, "SUPERSCRIPT TWO", TEX_NONE, "$^2$" },
   { 0x00B3, "SUPERSCRIPT THREE", TEX_NONE, "$^3$" },
   { 0x00B4, "ACUTE ACCENT", TEX_NONE, "\\'{}" },
   { 0x00B5, "MICRO SIGN", TEX_NONE, "{\\the\\textfont1\\char\"16}" },
   { 0x00B6, "PILCROW SIGN", TEX_NONE, "{\\P}" },
   { 0x00B7, "MIDDLE DOT", TEX_NONE, "$\\cdot$" },
   { 0x00B8, "CEDILLA", TEX_NONE, "\\c{}" },
   { 0x00B9, "SUPERSCRIPT ONE", TEX_NONE, "$^1$" },
   { 0x00BA, "MASCULINE ORDINAL INDICATOR", TEX_NONE, "$\\rm ^{\\b o}$" },
   { 0x00BB, "RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK", TEX_GUILLEMET_RIGHT, "{\\rguillemet}" },
   { 0x00BC, "VULGAR FRACTION ONE QUARTER", TEX_VULGAR_FRAC, "\\vulgarfrac 1/4" },
   { 0x00BD, "VULGAR FRACTION ONE HALF", TEX_VULGAR_FRAC, "\\vulgarfrac 1/2" },
   { 0x00BE, "VULGAR FRACTION THREE QUARTERS", TEX_VULGAR_FRAC, "\\vulgarfrac 3/4" },
   { 0x00BF, "INVERTED QUESTION MARK", TEX_NONE, "?`" },
   { 0x00C0, "LATIN CAPITAL LETTER A WITH GRAVE", TEX_NONE, "\\`A" },
   { 0x00C1, "LATIN CAPITAL LETTER A WITH ACUTE", TEX_NONE, "\\'A" },
   { 0x00C2, "LATIN CAPITAL LETTER A WITH CIRCUMFLEX", TEX_NONE, "\\^A" },
   { 0x00C3, "LATIN CAPITAL LETTER A WITH TILDE", TEX_NONE, "\\~A" },
   { 0x00C4, "LATIN CAPITAL LETTER A WITH DIAERESIS", TEX_NONE, "\\\"A" },
   { 0x00C5, "LATIN CAPITAL LETTER A WITH RING ABOVE", TEX_NONE, "{\\AA}" },
   { 0x00C6, "LATIN CAPITAL LETTER AE", TEX_NONE, "{\\AE}" },
   { 0x00C7, "LATIN CAPITAL LETTER C WITH CEDILLA", TEX_NONE, "\\c C" },
   { 0x00C8, "LATIN CAPITAL LETTER E WITH GRAVE", TEX_NONE, "\\`E" },
   { 0x00C9, "LATIN CAPITAL LETTER E WITH ACUTE", TEX_NONE, "\\'E" },
   { 0x00CA, "LATIN CAPITAL LETTER E WITH CIRCUMFLEX", TEX_NONE, "\\^E" },
   { 0x00CB, "LATIN CAPITAL LETTER E WITH DIAERESIS", TEX_NONE, "\\\"E" },
   { 0x00CC, "LATIN CAPITAL LETTER I WITH GRAVE", TEX_NONE, "\\`I" },
   { 0x00CD, "LATIN CAPITAL LETTER I WITH ACUTE", TEX_NONE, "\\'I" },
   { 0x00CE, "LATIN CAPITAL LETTER I WITH CIRCUMFLEX", TEX_NONE, "\\^I" },
   { 0x00CF, "LATIN CAPITAL LETTER I WITH DIAERESIS", TEX_NONE, "\\\"I" },
   /* 0x00D0 and 0x0110 are indistinguishable */
   { 0x00D0, "LATIN CAPITAL LETTER ETH", TEX_NONE, "\\rlap{\\raise0.4ex\\hbox{-}}D" },
   { 0x00D1, "LATIN CAPITAL LETTER N WITH TILDE", TEX_NONE, "\\~N" },
   { 0x00D2, "LATIN CAPITAL LETTER O WITH GRAVE", TEX_NONE, "\\`O" },
   { 0x00D3, "LATIN CAPITAL LETTER O WITH ACUTE", TEX_NONE, "\\'O" },
   { 0x00D4, "LATIN CAPITAL LETTER O WITH CIRCUMFLEX", TEX_NONE, "\\^O" },
   { 0x00D5, "LATIN CAPITAL LETTER O WITH TILDE", TEX_NONE, "\\~O" },
   { 0x00D6, "LATIN CAPITAL LETTER O WITH DIAERESIS", TEX_NONE, "\\\"O" },
   { 0x00D7, "MULTIPLICATION SIGN", TEX_NONE, "{\\the\\textfont2\\char\"02}" },
   { 0x00D8, "LATIN CAPITAL LETTER O WITH STROKE", TEX_NONE, "{\\O}" },
   { 0x00D9, "LATIN CAPITAL LETTER U WITH GRAVE", TEX_NONE, "\\`U" },
   { 0x00DA, "LATIN CAPITAL LETTER U WITH ACUTE", TEX_NONE, "\\'U" },
   { 0x00DB, "LATIN CAPITAL LETTER U WITH CIRCUMFLEX", TEX_NONE, "\\^U" },
   { 0x00DC, "LATIN CAPITAL LETTER U WITH DIAERESIS", TEX_NONE, "\\\"U" },
   { 0x00DD, "LATIN CAPITAL LETTER Y WITH ACUTE", TEX_NONE, "\\'Y" },
   { 0x00DE, "LATIN CAPITAL LETTER THORN", TEX_THORN_UC, "{\\Thorn}" },
   { 0x00DF, "LATIN SMALL LETTER SHARP S", TEX_NONE, "{\\ss}" },
   { 0x00E0, "LATIN SMALL LETTER A WITH GRAVE", TEX_NONE, "\\`a" },
   { 0x00E1, "LATIN SMALL LETTER A WITH ACUTE", TEX_NONE, "\\'a" },
   { 0x00E2, "LATIN SMALL LETTER A WITH CIRCUMFLEX", TEX_NONE, "\\^a" },
   { 0x00E3, "LATIN SMALL LETTER A WITH TILDE", TEX_NONE, "\\~a" },
   { 0x00E4, "LATIN SMALL LETTER A WITH DIAERESIS", TEX_NONE, "\\\"a" },
   { 0x00E5, "LATIN SMALL LETTER A WITH RING ABOVE", TEX_NONE, "{\\aa}" },
   { 0x00E6, "LATIN SMALL LETTER AE", TEX_NONE, "{\\ae}" },
   { 0x00E7, "LATIN SMALL LETTER C WITH CEDILLA", TEX_NONE, "\\c c" },
   { 0x00E8, "LATIN SMALL LETTER E WITH GRAVE", TEX_NONE, "\\`e" },
   { 0x00E9, "LATIN SMALL LETTER E WITH ACUTE", TEX_NONE, "\\'e" },
   { 0x00EA, "LATIN SMALL LETTER E WITH CIRCUMFLEX", TEX_NONE, "\\^e" },
   { 0x00EB, "LATIN SMALL LETTER E WITH DIAERESIS", TEX_NONE, "\\\"e" },
   { 0x00EC, "LATIN SMALL LETTER I WITH GRAVE", TEX_NONE, "{\\`\\i}" },
   { 0x00ED, "LATIN SMALL LETTER I WITH ACUTE", TEX_NONE, "{\\'\\i}" },
   { 0x00EE, "LATIN SMALL LETTER I WITH CIRCUMFLEX", TEX_NONE, "{\\^\\i}" },
   { 0x00EF, "LATIN SMALL LETTER I WITH DIAERESIS", TEX_NONE, "{\\\"\\i}" },
   { 0x00F0, "LATIN SMALL LETTER ETH", TEX_ETH, "{\\eth}" },
   { 0x00F1, "LATIN SMALL LETTER N WITH TILDE", TEX_NONE, "\\~n" },
   { 0x00F2, "LATIN SMALL LETTER O WITH GRAVE", TEX_NONE, "\\`o" },
   { 0x00F3, "LATIN SMALL LETTER O WITH ACUTE", TEX_NONE, "\\'o" },
   { 0x00F4, "LATIN SMALL LETTER O WITH CIRCUMFLEX", TEX_NONE, "\\^o" },
   { 0x00F5, "LATIN SMALL LETTER O WITH TILDE", TEX_NONE, "\\~o" },
   { 0x00F6, "LATIN SMALL LETTER O WITH DIAERESIS", TEX_NONE, "\\\"o" },
   { 0x00F7, "DIVISION SIGN", TEX_NONE, "{\\the\\textfont2\\char\"04}" },
   { 0x00F8, "LATIN SMALL LETTER O WITH STROKE", TEX_NONE, "{\\o}" },
   { 0x00F9, "LATIN SMALL LETTER U WITH GRAVE", TEX_NONE, "\\`u" },
   { 0x00FA, "LATIN SMALL LETTER U WITH ACUTE", TEX_NONE, "\\'u" },
   { 0x00FB, "LATIN SMALL LETTER U WITH CIRCUMFLEX", TEX_NONE, "\\^u" },
   { 0x00FC, "LATIN SMALL LETTER U WITH DIAERESIS", TEX_NONE, "\\\"u" },
   { 0x00FD, "LATIN SMALL LETTER Y WITH ACUTE", TEX_NONE, "\\'y" },
   { 0x00FE, "LATIN SMALL LETTER THORN", TEX_THORN_LC, "{\\thorn}" },
   { 0x00FF, "LATIN SMALL LETTER Y WITH DIAERESIS", TEX_NONE, "\\\"y" },
   { 0x0100, "LATIN CAPITAL LETTER A WITH MACRON", TEX_NONE, "\\=A" },
   { 0x0101, "LATIN SMALL LETTER A WITH MACRON", TEX_NONE, "\\=a" },
   { 0x0102, "LATIN CAPITAL LETTER A WITH BREVE", TEX_NONE, "\\u A" },
   { 0x0103, "LATIN SMALL LETTER A WITH BREVE", TEX_NONE, "\\u a" },
   { 0x0104, "LATIN CAPITAL LETTER A WITH OGONEK", TEX_OGONEK, "\\ogonek{A}" },
   { 0x0105, "LATIN SMALL LETTER A WITH OGONEK", TEX_OGONEK, "\\ogonek{a}" },
   { 0x0106, "LATIN CAPITAL LETTER C WITH ACUTE", TEX_NONE, "\\'C" },
   { 0x0107, "LATIN SMALL LETTER C WITH ACUTE", TEX_NONE, "\\'c" },
   { 0x0108, "LATIN CAPITAL LETTER C WITH CIRCUMFLEX", TEX_NONE, "\\^C" },
   { 0x0109, "LATIN SMALL LETTER C WITH CIRCUMFLEX", TEX_NONE, "\\^c" },
   { 0x010A, "LATIN CAPITAL LETTER C WITH DOT ABOVE", TEX_DOT, "\\dotabove{C}" },
   { 0x010B, "LATIN SMALL LETTER C WITH DOT ABOVE", TEX_DOT, "\\dotabove{c}" },
   { 0x010C, "LATIN CAPITAL LETTER C WITH CARON", TEX_NONE, "\\v C" },
   { 0x010D, "LATIN SMALL LETTER C WITH CARON", TEX_NONE, "\\v c" },
   { 0x010E, "LATIN CAPITAL LETTER D WITH CARON", TEX_NONE, "\\v D" },
   { 0x010F, "LATIN SMALL LETTER D WITH CARON", TEX_NONE, "\\v d" },
   { 0x0110, "LATIN CAPITAL LETTER D WITH STROKE", TEX_NONE, "\\rlap{\\raise0.4ex\\hbox{-}}D" },
   { 0x0111, "LATIN SMALL LETTER D WITH STROKE", TEX_NONE, "\\hbox{d\\kern-0.32em{\\raise0.8ex\\hbox{-}}}" },
   { 0x0112, "LATIN CAPITAL LETTER E WITH MACRON", TEX_NONE, "\\=E" },
   { 0x0113, "LATIN SMALL LETTER E WITH MACRON", TEX_NONE, "\\=e" },
   { 0x0114, "LATIN CAPITAL LETTER E WITH BREVE", TEX_NONE, "\\u E" },
   { 0x0115, "LATIN SMALL LETTER E WITH BREVE", TEX_NONE, "\\u e" },
   { 0x0116, "LATIN CAPITAL LETTER E WITH DOT ABOVE", TEX_DOT, "\\dotabove{E}" },
   { 0x0117, "LATIN SMALL LETTER E WITH DOT ABOVE", TEX_DOT, "\\dotabove{e}" },
   { 0x0118, "LATIN CAPITAL LETTER E WITH OGONEK", TEX_OGONEK, "\\ogonek{E}" },
   { 0x0119, "LATIN SMALL LETTER E WITH OGONEK", TEX_OGONEK, "\\ogonek{e}" },
   { 0x011A, "LATIN CAPITAL LETTER E WITH CARON", TEX_NONE, "\\v E" },
   { 0x011B, "LATIN SMALL LETTER E WITH CARON", TEX_NONE, "\\v e" },
   { 0x011C, "LATIN CAPITAL LETTER G WITH CIRCUMFLEX", TEX_NONE, "\\^G" },
   { 0x011D, "LATIN SMALL LETTER G WITH CIRCUMFLEX", TEX_NONE, "\\^g" },
   { 0x011E, "LATIN CAPITAL LETTER G WITH BREVE", TEX_NONE, "\\u G" },
   { 0x011F, "LATIN SMALL LETTER G WITH BREVE", TEX_NONE, "\\u g" },
   { 0x0120, "LATIN CAPITAL LETTER G WITH DOT ABOVE", TEX_DOT, "\\dotabove{G}" },
   { 0x0121, "LATIN SMALL LETTER G WITH DOT ABOVE", TEX_DOT, "\\dotabove{g}" },
   { 0x0122, "LATIN CAPITAL LETTER G WITH CEDILLA", TEX_NONE, "\\c G" },
   /* Because of the descender, a conventional cedilla on a 'g' looks ugly.  */
   { 0x0123, "LATIN SMALL LETTER G WITH CEDILLA", TEX_NONE, "{\\accent96 g}" },
   { 0x0124, "LATIN CAPITAL LETTER H WITH CIRCUMFLEX", TEX_NONE, "\\^H" },
   { 0x0125, "LATIN SMALL LETTER H WITH CIRCUMFLEX", TEX_NONE, "\\^h" },
   { 0x0126, "LATIN CAPITAL LETTER H WITH STROKE", TEX_NONE, "\\rlap{\\raise 1.1ex\\vbox{\\hrule width 0.77em\\vss}}H" },
   { 0x0127, "LATIN SMALL LETTER H WITH STROKE", TEX_NONE, "\\hbox{{\\raise0.8ex\\hbox{-}}\\kern-0.35em h}" },
   { 0x0128, "LATIN CAPITAL LETTER I WITH TILDE", TEX_NONE, "\\~I" },
   { 0x0129, "LATIN SMALL LETTER I WITH TILDE", TEX_NONE, "{\\~\\i}" },
   { 0x012A, "LATIN CAPITAL LETTER I WITH MACRON", TEX_NONE, "\\=I" },
   { 0x012B, "LATIN SMALL LETTER I WITH MACRON", TEX_NONE, "{\\=\\i}" },
   { 0x012C, "LATIN CAPITAL LETTER I WITH BREVE", TEX_NONE, "\\u I" },
   { 0x012D, "LATIN SMALL LETTER I WITH BREVE", TEX_NONE, "{\\u \\i}" },
   { 0x012E, "LATIN CAPITAL LETTER I WITH OGONEK", TEX_OGONEK, "\\ogonek{I}" },
   { 0x012F, "LATIN SMALL LETTER I WITH OGONEK", TEX_OGONEK, "\\ogonek{i}" },
   { 0x0130, "LATIN CAPITAL LETTER I WITH DOT ABOVE", TEX_DOT, "\\dotabove{I}" },
   { 0x0131, "LATIN SMALL LETTER DOTLESS I", TEX_NONE, "{\\i}" },
   { 0x0132, "LATIN CAPITAL LIGATURE IJ", TEX_NONE, "\\hbox{I\\kern -0.05em J}" },
   { 0x0133, "LATIN SMALL LIGATURE IJ", TEX_NONE, "\\hbox{i\\kern -0.1em j}" },
   { 0x0134, "LATIN CAPITAL LETTER J WITH CIRCUMFLEX", TEX_NONE, "\\^J" },
   { 0x0135, "LATIN SMALL LETTER J WITH CIRCUMFLEX", TEX_NONE, "{\\^\\j}" },
   { 0x0136, "LATIN CAPITAL LETTER K WITH CEDILLA", TEX_NONE, "\\c K" },
   { 0x0137, "LATIN SMALL LETTER K WITH CEDILLA", TEX_NONE, "\\c k" },
   { 0x0138, "LATIN SMALL LETTER KRA", TEX_NONE, "{\\font\\xx=cmr7\\xx K}" },
   { 0x0139, "LATIN CAPITAL LETTER L WITH ACUTE", TEX_NONE, "\\'L" },
   { 0x013A, "LATIN SMALL LETTER L WITH ACUTE", TEX_NONE, "\\'l" },
   { 0x013B, "LATIN CAPITAL LETTER L WITH CEDILLA", TEX_NONE, "\\c L" },
   { 0x013C, "LATIN SMALL LETTER L WITH CEDILLA", TEX_NONE, "\\c l" },
   { 0x013D, "LATIN CAPITAL LETTER L WITH CARON", TEX_NONE, "\\v L" },
   { 0x013E, "LATIN SMALL LETTER L WITH CARON", TEX_NONE, "\\v l" },
   { 0x013F, "LATIN CAPITAL LETTER L WITH MIDDLE DOT", TEX_NONE, "\\hbox{\\rlap{\\kern0.27em\\raise0.3ex\\hbox{$\\cdot$}}L}" },
   { 0x0140, "LATIN SMALL LETTER L WITH MIDDLE DOT", TEX_NONE,   "\\hbox{l\\kern-0.12em\\raise0.3ex\\hbox{$\\cdot$}}" },
   { 0x0141, "LATIN CAPITAL LETTER L WITH STROKE", TEX_NONE, "{\\ifnum\\fam=7 \\lower 0.4ex\\rlap{\\kern -0.13em\\'{}}L\\else\\L\\fi}" },
   { 0x0142, "LATIN SMALL LETTER L WITH STROKE", TEX_NONE, "{\\ifnum\\fam=7 \\lower 0.4ex\\rlap{\\kern -0.05em\\'{}}l\\else\\l\\fi}" },
   { 0x0143, "LATIN CAPITAL LETTER N WITH ACUTE", TEX_NONE, "\\'N" },
   { 0x0144, "LATIN SMALL LETTER N WITH ACUTE", TEX_NONE, "\\'n" },
   { 0x0145, "LATIN CAPITAL LETTER N WITH CEDILLA", TEX_NONE, "\\c N" },
   { 0x0146, "LATIN SMALL LETTER N WITH CEDILLA", TEX_NONE, "\\c n" },
   { 0x0147, "LATIN CAPITAL LETTER N WITH CARON", TEX_NONE, "\\v N" },
   { 0x0148, "LATIN SMALL LETTER N WITH CARON", TEX_NONE, "\\v n" },
   { 0x0149, "LATIN SMALL LETTER N PRECEDED BY APOSTROPHE", TEX_NONE, "\\hbox{'\\kern -0.1em n}" },
   { 0x014A, "LATIN CAPITAL LETTER ENG", TEX_NONE, 0 },
   { 0x014B, "LATIN SMALL LETTER ENG", TEX_NONE, 0 },
   { 0x014C, "LATIN CAPITAL LETTER O WITH MACRON", TEX_NONE, "\\=O" },
   { 0x014D, "LATIN SMALL LETTER O WITH MACRON", TEX_NONE, "\\=o" },
   { 0x014E, "LATIN CAPITAL LETTER O WITH BREVE", TEX_NONE, "\\u O" },
   { 0x014F, "LATIN SMALL LETTER O WITH BREVE", TEX_NONE, "\\u o" },
   { 0x0150, "LATIN CAPITAL LETTER O WITH DOUBLE ACUTE", TEX_DOUBLE_ACUTE, "\\doubleacute{O}" },
   { 0x0151, "LATIN SMALL LETTER O WITH DOUBLE ACUTE", TEX_DOUBLE_ACUTE, "\\doubleacute{o}" },
   { 0x0152, "LATIN CAPITAL LIGATURE OE", TEX_NONE, "{\\OE}" },
   { 0x0153, "LATIN SMALL LIGATURE OE", TEX_NONE, "{\\oe}" },
   { 0x0154, "LATIN CAPITAL LETTER R WITH ACUTE", TEX_NONE, "\\'R" },
   { 0x0155, "LATIN SMALL LETTER R WITH ACUTE", TEX_NONE, "\\'r" },
   { 0x0156, "LATIN CAPITAL LETTER R WITH CEDILLA", TEX_NONE, "\\c R" },
   { 0x0157, "LATIN SMALL LETTER R WITH CEDILLA", TEX_NONE, "\\c r" },
   { 0x0158, "LATIN CAPITAL LETTER R WITH CARON", TEX_NONE, "\\v R" },
   { 0x0159, "LATIN SMALL LETTER R WITH CARON", TEX_NONE, "\\v r" },
   { 0x015A, "LATIN CAPITAL LETTER S WITH ACUTE", TEX_NONE, "\\'S" },
   { 0x015B, "LATIN SMALL LETTER S WITH ACUTE", TEX_NONE, "\\'s" },
   { 0x015C, "LATIN CAPITAL LETTER S WITH CIRCUMFLEX", TEX_NONE, "\\^S" },
   { 0x015D, "LATIN SMALL LETTER S WITH CIRCUMFLEX", TEX_NONE, "\\^s" },
   { 0x015E, "LATIN CAPITAL LETTER S WITH CEDILLA", TEX_NONE, "\\c S" },
   { 0x015F, "LATIN SMALL LETTER S WITH CEDILLA", TEX_NONE, "\\c s" },
   { 0x0160, "LATIN CAPITAL LETTER S WITH CARON", TEX_NONE, "\\v S" },
   { 0x0161, "LATIN SMALL LETTER S WITH CARON", TEX_NONE, "\\v s" },
   { 0x0162, "LATIN CAPITAL LETTER T WITH CEDILLA", TEX_NONE, "\\c T" },
   { 0x0163, "LATIN SMALL LETTER T WITH CEDILLA", TEX_NONE, "\\c t" },
   { 0x0164, "LATIN CAPITAL LETTER T WITH CARON", TEX_NONE, "\\v T" },
   { 0x0165, "LATIN SMALL LETTER T WITH CARON", TEX_NONE, "\\v t" },
   { 0x0166, "LATIN CAPITAL LETTER T WITH STROKE", TEX_NONE, "\\rlap{\\raise 0.35ex\\hbox{\\kern0.22em -}}T" },
   { 0x0167, "LATIN SMALL LETTER T WITH STROKE", TEX_NONE, "\\hbox{{\\raise0.16ex\\hbox{-}}\\kern-0.35em t}" },
   { 0x0168, "LATIN CAPITAL LETTER U WITH TILDE", TEX_NONE, "\\~U" },
   { 0x0169, "LATIN SMALL LETTER U WITH TILDE", TEX_NONE, "\\~u" },
   { 0x016A, "LATIN CAPITAL LETTER U WITH MACRON", TEX_NONE, "\\=U" },
   { 0x016B, "LATIN SMALL LETTER U WITH MACRON", TEX_NONE, "\\=u" },
   { 0x016C, "LATIN CAPITAL LETTER U WITH BREVE", TEX_NONE, "\\u U" },
   { 0x016D, "LATIN SMALL LETTER U WITH BREVE", TEX_NONE, "\\u u" },
   { 0x016E, "LATIN CAPITAL LETTER U WITH RING ABOVE", TEX_NONE, "\\accent23 U" },
   { 0x016F, "LATIN SMALL LETTER U WITH RING ABOVE", TEX_NONE, "\\accent23 u" },
   { 0x0170, "LATIN CAPITAL LETTER U WITH DOUBLE ACUTE", TEX_DOUBLE_ACUTE, "\\doubleacute{U}" },
   { 0x0171, "LATIN SMALL LETTER U WITH DOUBLE ACUTE", TEX_DOUBLE_ACUTE, "\\doubleacute{u}" },
   { 0x0172, "LATIN CAPITAL LETTER U WITH OGONEK", TEX_OGONEK, "\\ogonekx{U}{0.08em}" },
   { 0x0173, "LATIN SMALL LETTER U WITH OGONEK", TEX_OGONEK, "\\ogonek{u}" },
   { 0x0174, "LATIN CAPITAL LETTER W WITH CIRCUMFLEX", TEX_NONE, "\\^W" },
   { 0x0175, "LATIN SMALL LETTER W WITH CIRCUMFLEX", TEX_NONE, "\\^w" },
   { 0x0176, "LATIN CAPITAL LETTER Y WITH CIRCUMFLEX", TEX_NONE, "\\^Y" },
   { 0x0177, "LATIN SMALL LETTER Y WITH CIRCUMFLEX", TEX_NONE, "\\^y" },
   { 0x0178, "LATIN CAPITAL LETTER Y WITH DIAERESIS", TEX_NONE, "\\\"Y" },
   { 0x0179, "LATIN CAPITAL LETTER Z WITH ACUTE", TEX_NONE, "\\'Z" },
   { 0x017A, "LATIN SMALL LETTER Z WITH ACUTE", TEX_NONE, "\\'z" },
   { 0x017B, "LATIN CAPITAL LETTER Z WITH DOT ABOVE", TEX_DOT, "\\dotabove{Z}" },
   { 0x017C, "LATIN SMALL LETTER Z WITH DOT ABOVE", TEX_DOT, "\\dotabove{z}" },
   { 0x017D, "LATIN CAPITAL LETTER Z WITH CARON", TEX_NONE, "\\v Z" },
   { 0x017E, "LATIN SMALL LETTER Z WITH CARON", TEX_NONE, "\\v z" },
   { 0x017F, "LATIN SMALL LETTER LONG S", TEX_NONE, 0 },
  };



static const struct glyph punctuation [] =
{
   {0x2000, "EN QUAD", TEX_NONE, "\\kern.5em" },
   {0x2001, "EM QUAD", TEX_NONE, "\\kern1em" },
   {0x2002, "EN SPACE", TEX_NONE, "\\kern.5em" },
   {0x2003, "EM SPACE", TEX_NONE, "\\kern1em" },
   {0x2004, "THREE-PER-EM SPACE", TEX_NONE, "\\kern0.333em" },
   {0x2005, "FOUR-PER-EM SPACE", TEX_NONE, "\\kern0.250em" },
   {0x2006, "SIX-PER-EM SPACE", TEX_NONE, "\\kern0.166em" },
   {0x2007, "FIGURE SPACE", TEX_NONE, "\\kern1ex" },
   {0x2008, "PUNCTUATION SPACE", TEX_NONE, "{\\thinspace}" },
   {0x2009, "THIN SPACE", TEX_NONE, "{\\thinspace}" },
   {0x200A, "HAIR SPACE", TEX_NONE, "{\\hskip 1pt}" },
   {0x200B, "ZERO WIDTH SPACE", TEX_NONE, "{}" },
   {0x200C, "ZERO WIDTH NON-JOINER", TEX_NONE, "{}" },
   {0x200D, "ZERO WIDTH JOINER", TEX_NONE, "{}" },
   {0x200E, "LEFT-TO-RIGHT MARK", TEX_NONE, 0 },
   {0x200F, "RIGHT-TO-LEFT MARK", TEX_NONE, 0 },
   {0x2010, "HYPHEN", TEX_NONE, "-" },
   {0x2011, "NON-BREAKING HYPHEN", TEX_NONE, "\\hbox{-}" },
   {0x2012, "FIGURE DASH", TEX_NONE, "--" },
   {0x2013, "EN DASH", TEX_NONE, "--" },
   {0x2014, "EM DASH", TEX_NONE, "---" },
   {0x2015, "HORIZONTAL BAR", TEX_NONE, "---" },
   {0x2016, "DOUBLE VERTICAL LINE", TEX_NONE, "{\\the\\textfont2 \\char\"6B}" },
   {0x2017, "DOUBLE LOW LINE", TEX_NONE, "{\\the\\textfont2 \\lower0.4ex\\rlap{\\char\"00}\\lower0.8ex\\hbox{\\char\"00}}" },
   {0x2018, "LEFT SINGLE QUOTATION MARK", TEX_NONE, "`" },
   {0x2019, "RIGHT SINGLE QUOTATION MARK", TEX_NONE, "'" },
   {0x201A, "SINGLE LOW-9 QUOTATION MARK", TEX_NONE, "," },
   {0x201B, "SINGLE HIGH-REVERSED-9 QUOTATION MARK", TEX_NONE, 0 },
   {0x201C, "LEFT DOUBLE QUOTATION MARK", TEX_NONE, "``" },
   {0x201D, "RIGHT DOUBLE QUOTATION MARK", TEX_NONE, "''" },
   {0x201E, "DOUBLE LOW-9 QUOTATION MARK", TEX_NONE, ",," },
   {0x201F, "DOUBLE HIGH-REVERSED-9 QUOTATION MARK", TEX_NONE, 0 },
   {0x2020, "DAGGER", TEX_NONE, "{\\dag}" },
   {0x2021, "DOUBLE DAGGER", TEX_NONE, "{\\ddag}" },
   {0x2022, "BULLET", TEX_NONE, "{\\the\\textfont2 \\char\"0F}" },
   {0x2023, "TRIANGULAR BULLET", TEX_NONE, "{\\the\\textfont1 \\char\"2E}" },
   {0x2024, "ONE DOT LEADER", TEX_NONE, "\\hbox{.}" },
   {0x2025, "TWO DOT LEADER", TEX_NONE, "\\hbox{.\\kern 0.15em.}" },
   /* Ellipsis could be done with $\dots$ but that means a font change which we
      want to avoid if possible.  */
   {0x2026, "HORIZONTAL ELLIPSIS", TEX_NONE, "\\hbox{.\\kern 0.15em.\\kern 0.15em.}" },
   {0x2027, "HYPHENATION POINT", TEX_NONE, "$\\cdot$" },
   {0x2028, "LINE SEPARATOR", TEX_NONE, "{\\break}" },
   {0x2029, "PARAGRAPH SEPARATOR", TEX_NONE, "{\\par}" },
   {0x202A, "LEFT-TO-RIGHT EMBEDDING", TEX_NONE, 0 },
   {0x202B, "RIGHT-TO-LEFT EMBEDDING", TEX_NONE, 0 },
   {0x202C, "POP DIRECTIONAL FORMATTING", TEX_NONE, 0 },
   {0x202D, "LEFT-TO-RIGHT OVERRIDE", TEX_NONE, 0 },
   {0x202E, "RIGHT-TO-LEFT OVERRIDE", TEX_NONE, 0 },
   {0x202F, "NARROW NO-BREAK SPACE", TEX_NONE, "\\hbox{\\thinspace}" },
   {0x2030, "PER MILLE SIGN", TEX_NONE, "{\\font\\xx=\\ifnum\\fam=6 wasyb10\\else wasy10\\fi \\xx \\char\"68}" },
   {0x2031, "PER TEN THOUSAND SIGN", TEX_NONE, 0 },
   {0x2032, "PRIME", TEX_NONE, "$'$" },
   {0x2033, "DOUBLE PRIME", TEX_NONE, "$''$" },
   {0x2034, "TRIPLE PRIME", TEX_NONE, "$'''$" },
   {0x2035, "REVERSED PRIME", TEX_NONE, 0 },
   {0x2036, "REVERSED DOUBLE PRIME", TEX_NONE, 0 },
   {0x2037, "REVERSED TRIPLE PRIME", TEX_NONE, 0 },
   {0x2038, "CARET", TEX_NONE, "\\^{ }" },
   {0x2039, "SINGLE LEFT-POINTING ANGLE QUOTATION MARK", TEX_NONE, "{\\raise0.5ex\\hbox{\\font\\xx=cmmi5 \\xx \\char\"3C}}" },
   {0x203A, "SINGLE RIGHT-POINTING ANGLE QUOTATION MARK", TEX_NONE, "{\\raise0.5ex\\hbox{\\font\\xx=cmmi5 \\xx \\char\"3E}}" },
   {0x203B, "REFERENCE MARK", TEX_NONE,
    "\\rlap{\\ifnum\\fam=7\\kern -0.3ex\\fi"
    "\\rlap{\\raise 1.2ex\\hbox{\\kern 1ex.}}"
    "\\rlap{\\raise 0.2ex\\hbox{\\kern 1ex.}}"
    "\\rlap{\\raise 0.7ex\\hbox{\\kern 1.5ex.}}"
    "\\rlap{\\raise 0.7ex\\hbox{\\kern 0.5ex.}}"
    "}"
    "{\\font\\xx=cmsy10 scaled\\magstep2\\xx\\char\"02}"
   },
   {0x203C, "DOUBLE EXCLAMATION MARK", TEX_NONE, "\\hbox{!\\kern -0.1em!}" },
   {0x203D, "INTERROBANG", TEX_NONE, "\\rlap{\\ifnum\\fam=7 \\else\\kern 0.1em\\fi!}?" },
   {0x203E, "OVERLINE", TEX_NONE, "\\raise 1ex \\hbox{\\the\\textfont0 \\char\"7B}"},
   {0x203F, "UNDERTIE", TEX_NONE, "{\\the\\textfont1 \\char\"05E}" },
   {0x2040, "CHARACTER TIE", TEX_NONE, "{\\the\\textfont1 \\char\"05F}" },
   {0x2041, "CARET INSERTION POINT", TEX_NONE, 0 },
   {0x2042, "ASTERISM", TEX_NONE, "\\vtop to 0pt{\\hbox{\\lower .8ex\\hbox{*}}\\vss}\\kern-0.55ex"
    "*\\kern-0.55ex\\vtop to 0pt{\\hbox{\\lower .8ex\\hbox{*}}\\vss}" },
   {0x2043, "HYPHEN BULLET", TEX_NONE, "\\raise 0.6ex\\hbox to 0.3em{\\leaders\\hrule height 1pt\\hfil}" },
   {0x2044, "FRACTION SLASH", TEX_NONE, "{\\it /\\/}" },
   {0x2045, "LEFT SQUARE BRACKET WITH QUILL", TEX_NONE, "\\rlap{[}{\\raise 0.1ex\\hbox{-}}" },
   {0x2046, "RIGHT SQUARE BRACKET WITH QUILL", TEX_NONE, "\\rlap{]}{\\raise 0.1ex\\hbox{-}}" },
   {0x2047, "DOUBLE QUESTION MARK", TEX_NONE, "?\\kern-0.2ex?" },
   {0x2048, "QUESTION EXCLAMATION MARK", TEX_NONE, "?\\kern-0.2ex!" },
   {0x2049, "EXCLAMATION QUESTION MARK", TEX_NONE, "!\\kern-0.2ex?" },
   {0x204A, "TIRONIAN SIGN ET", TEX_NONE, "\\raise 1ex\\rlap{\\the\\textfont3 \\char\"7D}/" },
   {0x204B, "REVERSED PILCROW SIGN", TEX_NONE, 0 },
   {0x204C, "BLACK LEFTWARDS BULLET", TEX_NONE, 0 },
   {0x204D, "BLACK RIGHTWARDS BULLET", TEX_NONE, 0 },
   {0x204E, "LOW ASTERISK", TEX_NONE, "\\lower 0.8ex\\hbox{*}" },
   {0x204F, "REVERSED SEMICOLON", TEX_NONE, 0 },
   {0x2050, "CLOSE UP", TEX_NONE,  "\\rlap{\\lower 0.8ex\\hbox{\\the\\textfont1 \\char\"05E}}\\raise 1.ex\\hbox{\\the\\textfont1 \\char\"05F}"},
   {0x2051, "TWO ASTERISKS ALIGNED VERTICALLY", TEX_NONE, "\\vtop to 0pt{\\rlap{\\lower 0.8ex\\hbox{*}}\\vss}*" },
   {0x2052, "COMMERCIAL MINUS SIGN", TEX_NONE, "{\\raise 1.3ex\\hbox{.}\\rlap{\\raise 0.2ex\\hbox{\\kern-0.25em/}}.}" },
   {0x2053, "SWUNG DASH", TEX_NONE, "\\lower 0.5ex\\hbox{\\the\\textfont3 \\char\"65}" },
   {0x2054, "INVERTED UNDERTIE", TEX_NONE, "\\lower 0.3ex\\hbox{\\the\\textfont1 \\char\"05F}" },
   {0x2055, "FLOWER PUNCTUATION MARK", TEX_NONE, 0 },
   {0x2056, "THREE DOT PUNCTUATION", TEX_NONE, "\\raise 0.25\\baselineskip\\hbox{.}\\raise 0.5\\baselineskip\\rlap{.}." },
   {0x2057, "QUADRUPLE PRIME", TEX_NONE, "$''''$" },
   {0x2058, "FOUR DOT PUNCTUATION", TEX_NONE,
    "\\raise 0.2\\baselineskip\\rlap{.}"
    "\\kern 0.22\\baselineskip"
    "\\lower 0.0\\baselineskip\\rlap{.}"
    "\\raise 0.4\\baselineskip\\rlap{.}"
    "\\kern 0.22\\baselineskip"
    "\\raise 0.2\\baselineskip\\hbox{.}"
   },
   {0x2059, "FIVE DOT PUNCTUATION", TEX_NONE,
    "\\lower 0.0\\baselineskip\\rlap{.}"
    "\\raise 0.4\\baselineskip\\rlap{.}"
    "\\kern 0.2\\baselineskip"
    "\\raise 0.2\\baselineskip\\rlap{.}"
    "\\kern 0.2\\baselineskip"
    "\\lower 0.0\\baselineskip\\rlap{.}"
    "\\raise 0.4\\baselineskip\\hbox{.}"
   },
   {0x205A, "TWO DOT PUNCTUATION", TEX_NONE, "\\raise 0.5\\baselineskip\\rlap{.}." },
   {0x205B, "FOUR DOT MARK", TEX_NONE,
    "\\raise 0.3\\baselineskip\\hbox{.}"
    "\\lower 0.1\\baselineskip\\rlap{.}"
    "\\raise 0.7\\baselineskip\\hbox{.}"
    "\\raise 0.3\\baselineskip\\hbox{.}"
   },
   {0x205C, "DOTTED CROSS", TEX_NONE,
    "\\rlap{\\ifnum\\fam=7\\kern -0.3ex\\fi"
    "\\raise 0.07\\baselineskip\\rlap{.}"
    "\\raise 0.31\\baselineskip\\rlap{.}"
    "\\kern 0.25\\baselineskip"
    "\\raise 0.07\\baselineskip\\rlap{.}"
    "\\raise 0.31\\baselineskip\\rlap{.}"
    "}\\kern 0.01\\baselineskip"
    "\\hbox to 0.5\\baselineskip{"
    "\\rlap{\\raise 0.225\\baselineskip\\hbox to 0.5\\baselineskip{\\leaders\\hrule height 0.5pt\\hfil}}"
    "\\kern 0.225\\baselineskip"
    "\\vbox to 0.5\\baselineskip{\\leaders\\vrule width 0.5pt\\vfil}"
    "\\hss}"
   },
   /* According to  https://unicode.org/charts/PDF/U2000.pdf  the vertical extent
    of the next two is the whole height of the line. */
   {0x205D, "TRICOLON", TEX_NONE,
    "\\smash{"
      "\\setbox0=\\hbox{.}"
      "\\dimen255=\\baselineskip \\advance\\dimen255 by -\\lineskip \\advance\\dimen255 by -\\ht255"
    "\\rlap{\\raise 1.0\\dimen255\\hbox{.}}"
    "\\rlap{\\raise 0.5\\dimen255\\hbox{.}}"
    "\\raise 0\\dimen255\\hbox{.}}" },
   {0x205E, "VERTICAL FOUR DOTS", TEX_NONE,
    "\\smash{"
      "\\setbox0=\\hbox{.}"
      "\\dimen255=\\baselineskip \\advance\\dimen255 by -\\lineskip \\advance\\dimen255 by -\\ht255"
    "\\rlap{\\raise 1.0\\dimen255\\hbox{.}}"
    "\\rlap{\\raise 0.666666\\dimen255\\hbox{.}}"
    "\\rlap{\\raise 0.333333\\dimen255\\hbox{.}}"
    "\\raise 0\\dimen255\\hbox{.}}" },
   {0x205F, "MEDIUM MATHEMATICAL SPACE", TEX_NONE, "{\\hskip 0.2222222em}" },
   {0x2060, "WORD JOINER", TEX_NONE, "{}" },
   {0x2061, "FUNCTION APPLICATION", TEX_NONE, "$$" },
   {0x2062, "INVISIBLE TIMES", TEX_NONE, "$$" },
   {0x2063, "INVISIBLE SEPARATOR", TEX_NONE, "$$" },
   {0x2064, "INVISIBLE PLUS", TEX_NONE, "$$" },
  };

static const struct glyph mathematical [] =
  {
   {0x2264, "LESS-THAN OR EQUAL TO", TEX_NONE, "$\\leq$" },
   {0x2265, "GREATER-THAN OR EQUAL TO", TEX_NONE, "$\\geq$" },
  };


static const struct glyph greek [] =
  {
   {0x0391, "GREEK CAPITAL LETTER ALPHA", TEX_NONE, "{\\the\\textfont1 \\char\"41}" },
   {0x0392, "GREEK CAPITAL LETTER BETA", TEX_NONE, "{\\the\\textfont1 \\char\"42}" },
   {0x0393, "GREEK CAPITAL LETTER GAMMA", TEX_NONE,"{\\the\\textfont1 \\char\"00}" },
   {0x0394, "GREEK CAPITAL LETTER DELTA", TEX_NONE, "{\\the\\textfont1 \\char\"01}" },
   {0x0395, "GREEK CAPITAL LETTER EPSILON", TEX_NONE, "{\\the\\textfont1 \\char\"45}" },
   {0x0396, "GREEK CAPITAL LETTER ZETA", TEX_NONE, "{\\the\\textfont1 \\char\"5A}" },
   {0x0397, "GREEK CAPITAL LETTER ETA", TEX_NONE, "{\\the\\textfont1 \\char\"48}" },
   {0x0398, "GREEK CAPITAL LETTER THETA", TEX_NONE, "{\\the\\textfont1 \\char\"02}" },
   {0x0399, "GREEK CAPITAL LETTER IOTA", TEX_NONE, "{\\the\\textfont1 \\char\"49}" },
   {0x039A, "GREEK CAPITAL LETTER KAPPA", TEX_NONE, "{\\the\\textfont1 \\char\"4B}" },
   {0x039B, "GREEK CAPITAL LETTER LAMDA", TEX_NONE, "{\\the\\textfont1 \\char\"03}" },
   {0x039C, "GREEK CAPITAL LETTER MU", TEX_NONE, "{\\the\\textfont1 \\char\"4D}" },
   {0x039D, "GREEK CAPITAL LETTER NU", TEX_NONE, "{\\the\\textfont1 \\char\"4E}" },
   {0x039E, "GREEK CAPITAL LETTER XI", TEX_NONE, "{\\the\\textfont1 \\char\"04}" },
   {0x039F, "GREEK CAPITAL LETTER OMICRON", TEX_NONE, "{\\the\\textfont1 \\char\"4F}" },
   {0x03A0, "GREEK CAPITAL LETTER PI", TEX_NONE, "{\\the\\textfont1 \\char\"05}" },
   {0x03A1, "GREEK CAPITAL LETTER RHO", TEX_NONE, "{\\the\\textfont1 \\char\"50}" },
   {0x03A2, "reserved", TEX_NONE, 0 },
   {0x03A3, "GREEK CAPITAL LETTER SIGMA", TEX_NONE, "{\\the\\textfont1 \\char\"06}" },
   {0x03A4, "GREEK CAPITAL LETTER TAU", TEX_NONE, "{\\the\\textfont1 \\char\"54}" },
   {0x03A5, "GREEK CAPITAL LETTER UPSILON", TEX_NONE, "{\\the\\textfont1 \\char\"59}" },
   {0x03A6, "GREEK CAPITAL LETTER PHI", TEX_NONE, "{\\the\\textfont1 \\char\"08}" },
   {0x03A7, "GREEK CAPITAL LETTER CHI", TEX_NONE, "{\\the\\textfont1 \\char\"58}" },
   {0x03A8, "GREEK CAPITAL LETTER PSI", TEX_NONE, "{\\the\\textfont1 \\char\"09}" },
   {0x03A9, "GREEK CAPITAL LETTER OMEGA", TEX_NONE, "{\\the\\textfont1 \\char\"0A}" },
   {0x03AA, "GREEK CAPITAL LETTER IOTA WITH DIALYTIKA", TEX_NONE, 0 },
   {0x03AB, "GREEK CAPITAL LETTER UPSILON WITH DIALYTIKA", TEX_NONE, 0 },
   {0x03AC, "GREEK SMALL LETTER ALPHA WITH TONOS", TEX_NONE, "\\rlap{\\kern -0.25ex\\it \\char\"13}{\\the\\textfont1 \\char\"0B}"},
   {0x03AD, "GREEK SMALL LETTER EPSILON WITH TONOS", TEX_NONE, "\\rlap{\\kern -0.25ex\\it \\char\"13}{\\the\\textfont1 \\char\"22}"},
   {0x03AE, "GREEK SMALL LETTER ETA WITH TONOS", TEX_NONE, "\\rlap{\\kern -0.25ex\\it \\char\"13}{\\the\\textfont1 \\char\"11}"},
   {0x03AF, "GREEK SMALL LETTER IOTA WITH TONOS", TEX_NONE, "\\rlap{\\the\\textfont1 \\char\"13}{\\kern -0.35ex\\it \\char\"13\\kern 0.1ex}" },
   {0x03B0, "GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS", TEX_NONE, 0 },
   {0x03B1, "GREEK SMALL LETTER ALPHA", TEX_NONE, "{\\the\\textfont1 \\char\"0B}" },
   {0x03B2, "GREEK SMALL LETTER BETA", TEX_NONE,  "{\\the\\textfont1 \\char\"0C}" },
   {0x03B3, "GREEK SMALL LETTER GAMMA", TEX_NONE, "{\\the\\textfont1 \\char\"0D}" },
   {0x03B4, "GREEK SMALL LETTER DELTA", TEX_NONE, "{\\the\\textfont1 \\char\"0E}" },
   /* Unicode prefers the squiggly epsilon */
   {0x03B5, "GREEK SMALL LETTER EPSILON", TEX_NONE, "{\\the\\textfont1 \\char\"22}" },
   {0x03B6, "GREEK SMALL LETTER ZETA", TEX_NONE,  "{\\the\\textfont1 \\char\"10}" },
   {0x03B7, "GREEK SMALL LETTER ETA", TEX_NONE,   "{\\the\\textfont1 \\char\"11}" },
   {0x03B8, "GREEK SMALL LETTER THETA", TEX_NONE, "{\\the\\textfont1 \\char\"12}" },
   {0x03B9, "GREEK SMALL LETTER IOTA", TEX_NONE,  "{\\the\\textfont1 \\char\"13}" },
   {0x03BA, "GREEK SMALL LETTER KAPPA", TEX_NONE, "{\\the\\textfont1 \\char\"14}" },
   {0x03BB, "GREEK SMALL LETTER LAMDA", TEX_NONE, "{\\the\\textfont1 \\char\"15}" },
   {0x03BC, "GREEK SMALL LETTER MU", TEX_NONE,    "{\\the\\textfont1 \\char\"16}" },
   {0x03BD, "GREEK SMALL LETTER NU", TEX_NONE,    "{\\the\\textfont1 \\char\"17}" },
   {0x03BE, "GREEK SMALL LETTER XI", TEX_NONE,    "{\\the\\textfont1 \\char\"18}" },
   {0x03BF, "GREEK SMALL LETTER OMICRON", TEX_NONE, "{\\the\\textfont1 \\char\"6F}" },
   {0x03C0, "GREEK SMALL LETTER PI", TEX_NONE, "{\\the\\textfont1 \\char\"19}" },
   {0x03C1, "GREEK SMALL LETTER RHO", TEX_NONE, "{\\the\\textfont1 \\char\"1A}" },
   {0x03C2, "GREEK SMALL LETTER FINAL SIGMA", TEX_NONE, "{\\the\\textfont1 \\char\"26}" },
   {0x03C3, "GREEK SMALL LETTER SIGMA", TEX_NONE, "{\\the\\textfont1 \\char\"1B}" },
   {0x03C4, "GREEK SMALL LETTER TAU", TEX_NONE, "{\\the\\textfont1 \\char\"1C}" },
   {0x03C5, "GREEK SMALL LETTER UPSILON", TEX_NONE, "{\\the\\textfont1 \\char\"1D}" },
   {0x03C6, "GREEK SMALL LETTER PHI", TEX_NONE, "{\\the\\textfont1 \\char\"27}" },
   {0x03C7, "GREEK SMALL LETTER CHI", TEX_NONE, "{\\the\\textfont1 \\char\"1F}" },
   {0x03C8, "GREEK SMALL LETTER PSI", TEX_NONE, "{\\the\\textfont1 \\char\"20}" },
   {0x03C9, "GREEK SMALL LETTER OMEGA", TEX_NONE, "{\\the\\textfont1 \\char\"21}" },
   {0x03CA, "GREEK SMALL LETTER IOTA WITH DIALYTIKA", TEX_NONE, 0 },
   {0x03CB, "GREEK SMALL LETTER UPSILON WITH DIALYTIKA", TEX_NONE, 0 },
   {0x03CC, "GREEK SMALL LETTER OMICRON WITH TONOS", TEX_NONE, "\\rlap{\\kern -0.25ex\\it \\char\"13}{\\the\\textfont1 \\char\"6F}"},
   {0x03CD, "GREEK SMALL LETTER UPSILON WITH TONOS", TEX_NONE, "\\rlap{\\kern -0.25ex\\it \\char\"13}{\\the\\textfont1 \\char\"1D}"},
   {0x03CE, "GREEK SMALL LETTER OMEGA WITH TONOS", TEX_NONE, "\\rlap{\\kern -0.25ex\\it \\char\"13}{\\the\\textfont1 \\char\"21}"},
   {0x03CF, "GREEK CAPITAL KAI SYMBOL", TEX_NONE, 0 }
  };

const struct glyph_block defined_blocks[] =
  {
   { control_codes, 2 },
   { basic_latin, 0x7F - 0x20 },
   { extended_latin, 0x180 - 0xA0 },
   { greek, 0x3D0 - 0x391},
   { punctuation, 0x65},
   { mathematical, 2},
   { 0, 0}
  };
