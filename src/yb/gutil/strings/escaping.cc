// Copyright 2008 Google Inc. All Rights Reserved.
// Authors: Numerous. See the .h for contact people.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/gutil/strings/escaping.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <limits>
#include <memory>
#include <vector>

#include "yb/gutil/charmap.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/port.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/utf/utf.h"  // for runetochar

using std::numeric_limits;
using std::vector;
using std::string;


namespace strings {

// These are used for the leave_nulls_escaped argument to CUnescapeInternal().
static bool kUnescapeNulls = false;
static bool kLeaveNullsEscaped = true;

// ----------------------------------------------------------------------
// EscapeStrForCSV()
//    Escapes the quotes in 'src' by doubling them. This is necessary
//    for generating CSV files (see SplitCSVLine).
//    Returns the number of characters written into dest (not counting
//    the \0) or -1 if there was insufficient space. Dest could end up
//    twice as long as src.
//
//    Example: [some "string" to test] --> [some ""string"" to test]
// ----------------------------------------------------------------------
size_t EscapeStrForCSV(const char* src, char* dest, size_t dest_len) {
  size_t used = 0;

  while (true) {
    if (*src == '\0' && used < dest_len) {
      dest[used] = '\0';
      return used;
    }

    if (used + 1 >= dest_len)  // +1 because we might require two characters
      return -1;

    if (*src == '"')
      dest[used++] = '"';

    dest[used++] = *src++;
  }
}

// ----------------------------------------------------------------------
// UnescapeCEscapeSequences()
//    This does all the unescaping that C does: \ooo, \r, \n, etc
//    Returns length of resulting string.
//    The implementation of \x parses any positive number of hex digits,
//    but it is an error if the value requires more than 8 bits, and the
//    result is truncated to 8 bits. The same is true for octals.
//
//    The second call stores its errors in a supplied string vector.
//    If the string vector pointer is NULL, it reports the errors with LOG().
//
//    *** DEPRECATED: Use CUnescape() in new code ***
//
//    NOTE: any changes to this function must also be reflected in the newer
//    CUnescape().
// ----------------------------------------------------------------------

#define IS_OCTAL_DIGIT(c) (((c) >= '0') && ((c) <= '7'))

size_t UnescapeCEscapeSequences(const char* source, char* dest) {
  return UnescapeCEscapeSequences(source, dest, nullptr);
}

size_t UnescapeCEscapeSequences(const char* source, char* dest, vector<string> *errors) {
  char* d = dest;
  const char* p = source;

  // Small optimization for case where source = dest and there's no escaping
  while ( p == d && *p != '\0' && *p != '\\' )
    p++, d++;

  while (*p != '\0') {
    if (*p != '\\') {
      *d++ = *p++;
    } else {
      switch ( *++p ) {                    // skip past the '\\'
        case '\0':
          LOG_STRING(ERROR, errors) << "String cannot end with \\";
          *d = '\0';
          return d - dest;   // we're done with p
        case 'a':  *d++ = '\a';  break;
        case 'b':  *d++ = '\b';  break;
        case 'f':  *d++ = '\f';  break;
        case 'n':  *d++ = '\n';  break;
        case 'r':  *d++ = '\r';  break;
        case 't':  *d++ = '\t';  break;
        case 'v':  *d++ = '\v';  break;
        case '\\': *d++ = '\\';  break;
        case '?':  *d++ = '\?';  break;    // \?  Who knew?
        case '\'': *d++ = '\'';  break;
        case '"':  *d++ = '\"';  break;
        case '0': case '1': case '2': case '3':  // octal digit: 1 to 3 digits
        case '4': case '5': case '6': case '7': {
          const char *octal_start = p;
          unsigned int ch = *p - '0';
          if ( IS_OCTAL_DIGIT(p[1]) )
            ch = ch * 8 + *++p - '0';
          if ( IS_OCTAL_DIGIT(p[1]) )      // safe (and easy) to do this twice
            ch = ch * 8 + *++p - '0';      // now points at last digit
          if (ch > 0xFF)
            LOG_STRING(ERROR, errors) << "Value of " <<
              "\\" << string(octal_start, p+1-octal_start) <<
              " exceeds 8 bits";
          *d++ = ch;
          break;
        }
        case 'x': case 'X': {
          if (!ascii_isxdigit(p[1])) {
            if (p[1] == '\0') {
              LOG_STRING(ERROR, errors) << "String cannot end with \\x";
            } else {
              LOG_STRING(ERROR, errors) <<
                "\\x cannot be followed by a non-hex digit: \\" << *p << p[1];
            }
            break;
          }
          unsigned int ch = 0;
          const char *hex_start = p;
          while (ascii_isxdigit(p[1]))  // arbitrarily many hex digits
            ch = (ch << 4) + hex_digit_to_int(*++p);
          if (ch > 0xFF)
            LOG_STRING(ERROR, errors) << "Value of " <<
              "\\" << string(hex_start, p+1-hex_start) << " exceeds 8 bits";
          *d++ = ch;
          break;
        }
        case 'u': {
          // \uhhhh => convert 4 hex digits to UTF-8
          char32 rune = 0;
          const char *hex_start = p;
          for (int i = 0; i < 4; ++i) {
            if (ascii_isxdigit(p[1])) {  // Look one char ahead.
              rune = (rune << 4) + hex_digit_to_int(*++p);  // Advance p.
            } else {
              LOG_STRING(ERROR, errors)
                << "\\u must be followed by 4 hex digits: \\"
                <<  string(hex_start, p+1-hex_start);
              break;
            }
          }
          d += runetochar(d, &rune);
          break;
        }
        case 'U': {
          // \Uhhhhhhhh => convert 8 hex digits to UTF-8
          char32 rune = 0;
          const char *hex_start = p;
          for (int i = 0; i < 8; ++i) {
            if (ascii_isxdigit(p[1])) {  // Look one char ahead.
              // Don't change rune until we're sure this
              // is within the Unicode limit, but do advance p.
              char32 newrune = (rune << 4) + hex_digit_to_int(*++p);
              if (newrune > 0x10FFFF) {
                LOG_STRING(ERROR, errors)
                  << "Value of \\"
                  << string(hex_start, p + 1 - hex_start)
                  << " exceeds Unicode limit (0x10FFFF)";
                break;
              } else {
                rune = newrune;
              }
            } else {
              LOG_STRING(ERROR, errors)
                << "\\U must be followed by 8 hex digits: \\"
                <<  string(hex_start, p+1-hex_start);
              break;
            }
          }
          d += runetochar(d, &rune);
          break;
        }
        default:
          LOG_STRING(ERROR, errors) << "Unknown escape sequence: \\" << *p;
      }
      p++;                                 // read past letter we escaped
    }
  }
  *d = '\0';
  return d - dest;
}

// ----------------------------------------------------------------------
// UnescapeCEscapeString()
//    This does the same thing as UnescapeCEscapeSequences, but creates
//    a new string. The caller does not need to worry about allocating
//    a dest buffer. This should be used for non performance critical
//    tasks such as printing debug messages. It is safe for src and dest
//    to be the same.
//
//    The second call stores its errors in a supplied string vector.
//    If the string vector pointer is NULL, it reports the errors with LOG().
//
//    In the first and second calls, the length of dest is returned. In the
//    the third call, the new string is returned.
//
//    *** DEPRECATED: Use CUnescape() in new code ***
//
// ----------------------------------------------------------------------
size_t UnescapeCEscapeString(const string& src, string* dest) {
  return UnescapeCEscapeString(src, dest, nullptr);
}

size_t UnescapeCEscapeString(const string& src, string* dest, vector<string> *errors) {
  CHECK(dest);
  dest->resize(src.size() + 1);
  auto len = UnescapeCEscapeSequences(src.c_str(), const_cast<char*>(dest->data()), errors);
  dest->resize(len);
  return len;
}

string UnescapeCEscapeString(const string& src) {
  std::unique_ptr<char[]> unescaped(new char[src.size() + 1]);
  auto len = UnescapeCEscapeSequences(src.c_str(), unescaped.get(), nullptr);
  return string(unescaped.get(), len);
}

// ----------------------------------------------------------------------
// CUnescapeInternal()
//    Implements both CUnescape() and CUnescapeForNullTerminatedString().
//
//    Unescapes C escape sequences and is the reverse of CEscape().
//
//    If 'source' is valid, stores the unescaped string and its size in
//    'dest' and 'dest_len' respectively, and returns true. Otherwise
//    returns false and optionally stores the error description in
//    'error'. Set 'error' to NULL to disable error reporting.
//
//    'dest' should point to a buffer that is at least as big as 'source'.
//    'source' and 'dest' may be the same.
//
//     NOTE: any changes to this function must also be reflected in the older
//     UnescapeCEscapeSequences().
// ----------------------------------------------------------------------
static bool CUnescapeInternal(const GStringPiece& source,
                              bool leave_nulls_escaped,
                              char* dest,
                              size_t* dest_len,
                              string* error) {
  char* d = dest;
  const char* p = source.data();
  const char* end = source.end();
  const char* last_byte = end - 1;

  // Small optimization for case where source = dest and there's no escaping
  while (p == d && p < end && *p != '\\')
    p++, d++;

  while (p < end) {
    if (*p != '\\') {
      *d++ = *p++;
    } else {
      if (++p > last_byte) {       // skip past the '\\'
        if (error) *error = "String cannot end with \\";
        return false;
      }
      switch (*p) {
        case 'a':  *d++ = '\a';  break;
        case 'b':  *d++ = '\b';  break;
        case 'f':  *d++ = '\f';  break;
        case 'n':  *d++ = '\n';  break;
        case 'r':  *d++ = '\r';  break;
        case 't':  *d++ = '\t';  break;
        case 'v':  *d++ = '\v';  break;
        case '\\': *d++ = '\\';  break;
        case '?':  *d++ = '\?';  break;    // \?  Who knew?
        case '\'': *d++ = '\'';  break;
        case '"':  *d++ = '\"';  break;
        case '0': case '1': case '2': case '3':  // octal digit: 1 to 3 digits
        case '4': case '5': case '6': case '7': {
          const char *octal_start = p;
          unsigned int ch = *p - '0';
          if (p < last_byte && IS_OCTAL_DIGIT(p[1]))
            ch = ch * 8 + *++p - '0';
          if (p < last_byte && IS_OCTAL_DIGIT(p[1]))
            ch = ch * 8 + *++p - '0';      // now points at last digit
          if (ch > 0xff) {
            if (error) {
              *error = "Value of \\" +
                  string(octal_start, p + 1 - octal_start) +
                  " exceeds 0xff";
            }
            return false;
          }
          if ((ch == 0) && leave_nulls_escaped) {
            // Copy the escape sequence for the null character
            const size_t octal_size = p + 1 - octal_start;
            *d++ = '\\';
            memcpy(d, octal_start, octal_size);
            d += octal_size;
            break;
          }
          *d++ = ch;
          break;
        }
        case 'x': case 'X': {
          if (p >= last_byte) {
            if (error) *error = "String cannot end with \\x";
            return false;
          } else if (!ascii_isxdigit(p[1])) {
            if (error) *error = "\\x cannot be followed by a non-hex digit";
            return false;
          }
          unsigned int ch = 0;
          const char *hex_start = p;
          while (p < last_byte && ascii_isxdigit(p[1]))
            // Arbitrarily many hex digits
            ch = (ch << 4) + hex_digit_to_int(*++p);
          if (ch > 0xFF) {
            if (error) {
              *error = "Value of \\" + string(hex_start, p + 1 - hex_start) +
                  " exceeds 0xff";
            }
            return false;
          }
          if ((ch == 0) && leave_nulls_escaped) {
            // Copy the escape sequence for the null character
            const size_t hex_size = p + 1 - hex_start;
            *d++ = '\\';
            memcpy(d, hex_start, hex_size);
            d += hex_size;
            break;
          }
          *d++ = ch;
          break;
        }
        case 'u': {
          // \uhhhh => convert 4 hex digits to UTF-8
          char32 rune = 0;
          const char *hex_start = p;
          if (p + 4 >= end) {
            if (error) {
              *error = "\\u must be followed by 4 hex digits: \\" +
                  string(hex_start, p + 1 - hex_start);
            }
            return false;
          }
          for (int i = 0; i < 4; ++i) {
            // Look one char ahead.
            if (ascii_isxdigit(p[1])) {
              rune = (rune << 4) + hex_digit_to_int(*++p);  // Advance p.
            } else {
              if (error) {
                *error = "\\u must be followed by 4 hex digits: \\" +
                    string(hex_start, p + 1 - hex_start);
              }
              return false;
            }
          }
          if ((rune == 0) && leave_nulls_escaped) {
            // Copy the escape sequence for the null character
            *d++ = '\\';
            memcpy(d, hex_start, 5);  // u0000
            d += 5;
            break;
          }
          d += runetochar(d, &rune);
          break;
        }
        case 'U': {
          // \Uhhhhhhhh => convert 8 hex digits to UTF-8
          char32 rune = 0;
          const char *hex_start = p;
          if (p + 8 >= end) {
            if (error) {
              *error = "\\U must be followed by 8 hex digits: \\" +
                  string(hex_start, p + 1 - hex_start);
            }
            return false;
          }
          for (int i = 0; i < 8; ++i) {
            // Look one char ahead.
            if (ascii_isxdigit(p[1])) {
              // Don't change rune until we're sure this
              // is within the Unicode limit, but do advance p.
              char32 newrune = (rune << 4) + hex_digit_to_int(*++p);
              if (newrune > 0x10FFFF) {
                if (error) {
                  *error = "Value of \\" +
                      string(hex_start, p + 1 - hex_start) +
                      " exceeds Unicode limit (0x10FFFF)";
                }
                return false;
              } else {
                rune = newrune;
              }
            } else {
              if (error) {
                *error = "\\U must be followed by 8 hex digits: \\" +
                    string(hex_start, p + 1 - hex_start);
              }
              return false;
            }
          }
          if ((rune == 0) && leave_nulls_escaped) {
            // Copy the escape sequence for the null character
            *d++ = '\\';
            memcpy(d, hex_start, 9);  // U00000000
            d += 9;
            break;
          }
          d += runetochar(d, &rune);
          break;
        }
        default: {
          if (error) *error = string("Unknown escape sequence: \\") + *p;
          return false;
        }
      }
      p++;                                 // read past letter we escaped
    }
  }
  *dest_len = d - dest;
  return true;
}

// ----------------------------------------------------------------------
// CUnescapeInternal()
//
//    Same as above but uses a C++ string for output. 'source' and 'dest'
//    may be the same.
// ----------------------------------------------------------------------
bool CUnescapeInternal(const GStringPiece& source,
                       bool leave_nulls_escaped,
                       string* dest,
                       string* error) {
  dest->resize(source.size());
  size_t dest_size;
  if (!CUnescapeInternal(source,
                         leave_nulls_escaped,
                         const_cast<char*>(dest->data()),
                         &dest_size,
                         error)) {
    return false;
  }
  dest->resize(dest_size);
  return true;
}

// ----------------------------------------------------------------------
// CUnescape()
//
// See CUnescapeInternal() for implementation details.
// ----------------------------------------------------------------------
bool CUnescape(const GStringPiece& source, char* dest, size_t* dest_len, string* error) {
  return CUnescapeInternal(source, kUnescapeNulls, dest, dest_len, error);
}

bool CUnescape(const GStringPiece& source, string* dest, string* error) {
  return CUnescapeInternal(source, kUnescapeNulls, dest, error);
}

// ----------------------------------------------------------------------
// CUnescapeForNullTerminatedString()
//
// See CUnescapeInternal() for implementation details.
// ----------------------------------------------------------------------
bool CUnescapeForNullTerminatedString(const GStringPiece& source,
                                      char* dest,
                                      size_t* dest_len,
                                      string* error) {
  return CUnescapeInternal(source, kLeaveNullsEscaped, dest, dest_len, error);
}

bool CUnescapeForNullTerminatedString(const GStringPiece& source,
                                      string* dest,
                                      string* error) {
  return CUnescapeInternal(source, kLeaveNullsEscaped, dest, error);
}

// ----------------------------------------------------------------------
// CEscapeString()
// CHexEscapeString()
// Utf8SafeCEscapeString()
// Utf8SafeCHexEscapeString()
//    Copies 'src' to 'dest', escaping dangerous characters using
//    C-style escape sequences. This is very useful for preparing query
//    flags. 'src' and 'dest' should not overlap. The 'Hex' version uses
//    hexadecimal rather than octal sequences. The 'Utf8Safe' version doesn't
//    touch UTF-8 bytes.
//    Returns the number of bytes written to 'dest' (not including the \0)
//    or -1 if there was insufficient space.
//
//    Currently only \n, \r, \t, ", ', \ and !ascii_isprint() chars are escaped.
// ----------------------------------------------------------------------
size_t CEscapeInternal(
    const char* src, size_t src_len, char* dest, size_t dest_len, bool use_hex, bool utf8_safe) {
  const char* src_end = src + src_len;
  size_t used = 0;
  bool last_hex_escape = false;  // true if last output char was \xNN

  for (; src < src_end; src++) {
    if (dest_len - used < 2)   // Need space for two letter escape
      return -1;

    bool is_hex_escape = false;
    unsigned char cur = *src;
    switch (cur) {
      case '\n': dest[used++] = '\\'; dest[used++] = 'n';  break;
      case '\r': dest[used++] = '\\'; dest[used++] = 'r';  break;
      case '\t': dest[used++] = '\\'; dest[used++] = 't';  break;
      case '\"': dest[used++] = '\\'; dest[used++] = '\"'; break;
      case '\'': dest[used++] = '\\'; dest[used++] = '\''; break;
      case '\\': dest[used++] = '\\'; dest[used++] = '\\'; break;
      default:
        // Note that if we emit \xNN and the src character after that is a hex
        // digit then that digit must be escaped too to prevent it being
        // interpreted as part of the character code by C.
        if ((!utf8_safe || cur < 0x80) &&
            (!ascii_isprint(cur) ||
             (last_hex_escape && ascii_isxdigit(cur)))) {
          if (dest_len - used < 4)  // need space for 4 letter escape
            return -1;
          snprintf(dest + used, dest_len - used, (use_hex ? "\\x%02x" : "\\%03o"), cur);
          is_hex_escape = use_hex;
          used += 4;
        } else {
          dest[used++] = cur;
          break;
        }
    }
    last_hex_escape = is_hex_escape;
  }

  if (dest_len - used < 1)   // make sure that there is room for \0
    return -1;

  dest[used] = '\0';   // doesn't count towards return value though
  return used;
}

size_t CEscapeString(const char* src, size_t src_len, char* dest, size_t dest_len) {
  return CEscapeInternal(src, src_len, dest, dest_len, false, false);
}

size_t CHexEscapeString(const char* src, size_t src_len, char* dest, size_t dest_len) {
  return CEscapeInternal(src, src_len, dest, dest_len, true, false);
}

size_t Utf8SafeCEscapeString(const char* src, size_t src_len, char* dest, size_t dest_len) {
  return CEscapeInternal(src, src_len, dest, dest_len, false, true);
}

size_t Utf8SafeCHexEscapeString(const char* src, size_t src_len, char* dest, size_t dest_len) {
  return CEscapeInternal(src, src_len, dest, dest_len, true, true);
}

// ----------------------------------------------------------------------
// CEscape()
// CHexEscape()
// Utf8SafeCEscape()
// Utf8SafeCHexEscape()
//    Copies 'src' to result, escaping dangerous characters using
//    C-style escape sequences. This is very useful for preparing query
//    flags. 'src' and 'dest' should not overlap. The 'Hex' version
//    hexadecimal rather than octal sequences. The 'Utf8Safe' version
//    doesn't touch UTF-8 bytes.
//
//    Currently only \n, \r, \t, ", ', \ and !ascii_isprint() chars are escaped.
// ----------------------------------------------------------------------
string CEscape(const GStringPiece& src) {
  const auto dest_length = src.size() * 4 + 1;  // Maximum possible expansion
  std::unique_ptr<char[]> dest(new char[dest_length]);
  const auto len = CEscapeInternal(src.data(), src.size(),
                                   dest.get(), dest_length, false, false);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

string CHexEscape(const GStringPiece& src) {
  const auto dest_length = src.size() * 4 + 1;  // Maximum possible expansion
  std::unique_ptr<char[]> dest(new char[dest_length]);
  const auto len = CEscapeInternal(src.data(), src.size(),
                                   dest.get(), dest_length, true, false);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

string Utf8SafeCEscape(const GStringPiece& src) {
  const auto dest_length = src.size() * 4 + 1;  // Maximum possible expansion
  std::unique_ptr<char[]> dest(new char[dest_length]);
  const auto len = CEscapeInternal(src.data(), src.size(),
                                   dest.get(), dest_length, false, true);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

string Utf8SafeCHexEscape(const GStringPiece& src) {
  const auto dest_length = src.size() * 4 + 1;  // Maximum possible expansion
  std::unique_ptr<char[]> dest(new char[dest_length]);
  const auto len = CEscapeInternal(src.data(), src.size(),
                                   dest.get(), dest_length, true, true);
  DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

// ----------------------------------------------------------------------
// BackslashEscape and BackslashUnescape
// ----------------------------------------------------------------------
void BackslashEscape(const GStringPiece& src,
                     const strings::CharSet& to_escape,
                     string* dest) {
  dest->reserve(dest->size() + src.size());
  for (const char *p = src.data(), *end = src.data() + src.size();
       p != end; ) {
    // Advance to next character we need to escape, or to end of source
    const char* next = p;
    while (next < end && !to_escape.Test(*next)) {
      next++;
    }
    // Append the whole run of non-escaped chars
    dest->append(p, next - p);
    if (next == end) break;
    // Char at *next needs to be escaped.  Append backslash followed by *next
    char c[2];
    c[0] = '\\';
    c[1] = *next;
    dest->append(c, 2);
    p = next + 1;
  }
}

void BackslashUnescape(const GStringPiece& src,
                       const strings::CharSet& to_unescape,
                       string* dest) {
  dest->reserve(dest->size() + src.size());
  bool escaped = false;
  for (const char* p = src.data(), *end = src.data() + src.size();
       p != end; ++p) {
    if (escaped) {
      if (!to_unescape.Test(*p)) {
        // Keep the backslash
        dest->push_back('\\');
      }
      dest->push_back(*p);
      escaped = false;
    } else if (*p == '\\') {
      escaped = true;
    } else {
      dest->push_back(*p);
    }
  }
}

// ----------------------------------------------------------------------
// int QuotedPrintableUnescape()
//
// Check out http://www.cis.ohio-state.edu/htbin/rfc/rfc2045.html for
// more details, only briefly implemented. But from the web...
// Quoted-printable is an encoding method defined in the MIME
// standard. It is used primarily to encode 8-bit text (such as text
// that includes foreign characters) into 7-bit US ASCII, creating a
// document that is mostly readable by humans, even in its encoded
// form. All MIME compliant applications can decode quoted-printable
// text, though they may not necessarily be able to properly display the
// document as it was originally intended. As quoted-printable encoding
// is implemented most commonly, printable ASCII characters (values 33
// through 126, excluding 61), tabs and spaces that do not appear at the
// end of lines, and end-of-line characters are not encoded. Other
// characters are represented by an equal sign (=) immediately followed
// by that character's hexadecimal value. Lines that are longer than 76
// characters are shortened by line breaks, with the equal sign marking
// where the breaks occurred.
//
// Note that QuotedPrintableUnescape is different from 'Q'-encoding as
// defined in rfc2047. In particular, This does not treat '_'s as spaces.
// See QEncodingUnescape().
// ----------------------------------------------------------------------

size_t QuotedPrintableUnescape(const char *source, size_t slen, char *dest, size_t szdest) {
  char* d = dest;
  const char* p = source;

  while ( p < source+slen && *p != '\0' && d < dest+szdest ) {
    switch (*p) {
      case '=':
        // If it's valid, convert to hex and insert or remove line-wrap.
        // In the case of line-wrap removal, we allow LF as well as CRLF.
        if ( p < source + slen - 1 ) {
          if ( p[1] == '\n' ) {
            p++;
          } else if ( p < source + slen - 2 ) {
            if ( ascii_isxdigit(p[1]) && ascii_isxdigit(p[2]) ) {
              *d++ = hex_digit_to_int(p[1])*16 + hex_digit_to_int(p[2]);
              p += 2;
            } else if ( p[1] == '\r' && p[2] == '\n' ) {
              p += 2;
            }
          }
        }
        p++;
        break;
      default:
        *d++ = *p++;
        break;
    }
  }
  return (d-dest);
}

// ----------------------------------------------------------------------
// size_t QEncodingUnescape()
//
// This is very similar to QuotedPrintableUnescape except that we convert
// '_'s into spaces. (See RFC 2047)
// ----------------------------------------------------------------------
size_t QEncodingUnescape(const char *source, size_t slen, char *dest, size_t szdest) {
  char* d = dest;
  const char* p = source;

  while ( p < source+slen && *p != '\0' && d < dest+szdest ) {
    switch (*p) {
      case '=':
        // If it's valid, convert to hex and insert or remove line-wrap.
        // In the case of line-wrap removal, the assumption is that this
        // is an RFC-compliant message with lines terminated by CRLF.
        if (p < source+slen-2) {
          if ( ascii_isxdigit(p[1]) && ascii_isxdigit(p[2]) ) {
            *d++ = hex_digit_to_int(p[1])*16 + hex_digit_to_int(p[2]);
            p += 2;
          } else if ( p[1] == '\r' && p[2] == '\n' ) {
            p += 2;
          }
        }
        p++;
        break;
      case '_':   // According to rfc2047, _'s are to be treated as spaces
        *d++ = ' ';
        p++;
        break;
      default:
        *d++ = *p++;
        break;
    }
  }
  return (d-dest);
}

size_t CalculateBase64EscapedLen(size_t input_len, bool do_padding) {
  // Base64 encodes three bytes of input at a time. If the input is not
  // divisible by three, we pad as appropriate.
  //
  // (from http://www.ietf.org/rfc/rfc3548.txt)
  // Special processing is performed if fewer than 24 bits are available
  // at the end of the data being encoded.  A full encoding quantum is
  // always completed at the end of a quantity.  When fewer than 24 input
  // bits are available in an input group, zero bits are added (on the
  // right) to form an integral number of 6-bit groups.  Padding at the
  // end of the data is performed using the '=' character.  Since all base
  // 64 input is an integral number of octets, only the following cases
  // can arise:


  // Base64 encodes each three bytes of input into four bytes of output.
  auto len = (input_len / 3) * 4;

  if (input_len % 3 == 0) {
    // (from http://www.ietf.org/rfc/rfc3548.txt)
    // (1) the final quantum of encoding input is an integral multiple of 24
    // bits; here, the final unit of encoded output will be an integral
    // multiple of 4 characters with no "=" padding,
  } else if (input_len % 3 == 1) {
    // (from http://www.ietf.org/rfc/rfc3548.txt)
    // (2) the final quantum of encoding input is exactly 8 bits; here, the
    // final unit of encoded output will be two characters followed by two
    // "=" padding characters, or
    len += 2;
    if (do_padding) {
      len += 2;
    }
  } else {  // (input_len % 3 == 2)
    // (from http://www.ietf.org/rfc/rfc3548.txt)
    // (3) the final quantum of encoding input is exactly 16 bits; here, the
    // final unit of encoded output will be three characters followed by one
    // "=" padding character.
    len += 3;
    if (do_padding) {
      len += 1;
    }
  }

  assert(len >= input_len);  // make sure we didn't overflow
  return len;
}

// Base64Escape does padding, so this calculation includes padding.
size_t CalculateBase64EscapedLen(size_t input_len) {
  return CalculateBase64EscapedLen(input_len, true);
}

// ----------------------------------------------------------------------
// size_t Base64Unescape() - base64 decoder
// size_t Base64Escape() - base64 encoder
// size_t WebSafeBase64Unescape() - Google's variation of base64 decoder
// size_t WebSafeBase64Escape() - Google's variation of base64 encoder
//
// Check out
// http://www.cis.ohio-state.edu/htbin/rfc/rfc2045.html for formal
// description, but what we care about is that...
//   Take the encoded stuff in groups of 4 characters and turn each
//   character into a code 0 to 63 thus:
//           A-Z map to 0 to 25
//           a-z map to 26 to 51
//           0-9 map to 52 to 61
//           +(- for WebSafe) maps to 62
//           /(_ for WebSafe) maps to 63
//   There will be four numbers, all less than 64 which can be represented
//   by a 6 digit binary number (aaaaaa, bbbbbb, cccccc, dddddd respectively).
//   Arrange the 6 digit binary numbers into three bytes as such:
//   aaaaaabb bbbbcccc ccdddddd
//   Equals signs (one or two) are used at the end of the encoded block to
//   indicate that the text was not an integer multiple of three bytes long.
// In the sorted variation, we instead use the mapping
//           .   maps to 0
//           0-9 map to 1-10
//           A-Z map to 11-37
//           _   maps to 38
//           a-z map to 39-63
// This mapping has the property that the output will be sorted in the same
// order as the input, i.e. a < b iff map(a) < map(b). It is web-safe and
// filename-safe.
// ----------------------------------------------------------------------

size_t Base64UnescapeInternal(
    const char *signed_src, size_t szsrc, char *dest, size_t szdest, const signed char* unbase64) {
  static const char kPad64 = '=';
  auto* src = static_cast<const unsigned char*>(static_cast<const void*>(signed_src));

  int decode = 0;
  size_t destidx = 0;
  int state = 0;
  unsigned int ch = 0;
  unsigned int temp = 0;

  // The GET_INPUT macro gets the next input character, skipping
  // over any whitespace, and stopping when we reach the end of the
  // string or when we read any non-data character.  The arguments are
  // an arbitrary identifier (used as a label for goto) and the number
  // of data bytes that must remain in the input to avoid aborting the
  // loop.
#define GET_INPUT(label, remain)                 \
  label:                                         \
    --szsrc;                                     \
    ch = *src++;                                 \
    decode = unbase64[ch];                       \
    if (decode < 0) {                            \
      if (ascii_isspace(ch) && szsrc >= remain)  \
        goto label;                              \
      state = 4 - remain;                        \
      break;                                     \
    }

  // if dest is null, we're just checking to see if it's legal input
  // rather than producing output.  (I suspect this could just be done
  // with a regexp...).  We duplicate the loop so this test can be
  // outside it instead of in every iteration.

  if (dest) {
    // This loop consumes 4 input bytes and produces 3 output bytes
    // per iteration.  We can't know at the start that there is enough
    // data left in the string for a full iteration, so the loop may
    // break out in the middle; if so 'state' will be set to the
    // number of input bytes read.

    while (szsrc >= 4)  {
      // We'll start by optimistically assuming that the next four
      // bytes of the string (src[0..3]) are four good data bytes
      // (that is, no nulls, whitespace, padding chars, or illegal
      // chars).  We need to test src[0..2] for nulls individually
      // before constructing temp to preserve the property that we
      // never read past a null in the string (no matter how long
      // szsrc claims the string is).

      if (!src[0] || !src[1] || !src[2] ||
          (temp = ((unbase64[src[0]] << 18) |
                   (unbase64[src[1]] << 12) |
                   (unbase64[src[2]] << 6) |
                   (unbase64[src[3]]))) & 0x80000000) {
        // Iff any of those four characters was bad (null, illegal,
        // whitespace, padding), then temp's high bit will be set
        // (because unbase64[] is -1 for all bad characters).
        //
        // We'll back up and resort to the slower decoder, which knows
        // how to handle those cases.

        GET_INPUT(first, 4);
        temp = decode;
        GET_INPUT(second, 3);
        temp = (temp << 6) | decode;
        GET_INPUT(third, 2);
        temp = (temp << 6) | decode;
        GET_INPUT(fourth, 1);
        temp = (temp << 6) | decode;
      } else {
        // We really did have four good data bytes, so advance four
        // characters in the string.

        szsrc -= 4;
        src += 4;
        decode = -1;
        ch = '\0';
      }

      // temp has 24 bits of input, so write that out as three bytes.

      if (destidx+3 > szdest) return -1;
      dest[destidx+2] = temp;
      temp >>= 8;
      dest[destidx+1] = temp;
      temp >>= 8;
      dest[destidx] = temp;
      destidx += 3;
    }
  } else {
    while (szsrc >= 4)  {
      if (!src[0] || !src[1] || !src[2] ||
          (temp = ((unbase64[src[0]] << 18) |
                   (unbase64[src[1]] << 12) |
                   (unbase64[src[2]] << 6) |
                   (unbase64[src[3]]))) & 0x80000000) {
        GET_INPUT(first_no_dest, 4);
        GET_INPUT(second_no_dest, 3);
        GET_INPUT(third_no_dest, 2);
        GET_INPUT(fourth_no_dest, 1);
      } else {
        szsrc -= 4;
        src += 4;
        decode = -1;
        ch = '\0';
      }
      destidx += 3;
    }
  }

#undef GET_INPUT

  // if the loop terminated because we read a bad character, return
  // now.
  if (decode < 0 && ch != '\0' && ch != kPad64 && !ascii_isspace(ch))
    return -1;

  if (ch == kPad64) {
    // if we stopped by hitting an '=', un-read that character -- we'll
    // look at it again when we count to check for the proper number of
    // equals signs at the end.
    ++szsrc;
    --src;
  } else {
    // This loop consumes 1 input byte per iteration.  It's used to
    // clean up the 0-3 input bytes remaining when the first, faster
    // loop finishes.  'temp' contains the data from 'state' input
    // characters read by the first loop.
    while (szsrc > 0)  {
      --szsrc;
      ch = *src++;
      decode = unbase64[ch];
      if (decode < 0) {
        if (ascii_isspace(ch)) {
          continue;
        } else if (ch == '\0') {
          break;
        } else if (ch == kPad64) {
          // back up one character; we'll read it again when we check
          // for the correct number of equals signs at the end.
          ++szsrc;
          --src;
          break;
        } else {
          return -1;
        }
      }

      // Each input character gives us six bits of output.
      temp = (temp << 6) | decode;
      ++state;
      if (state == 4) {
        // If we've accumulated 24 bits of output, write that out as
        // three bytes.
        if (dest) {
          if (destidx+3 > szdest) return -1;
          dest[destidx+2] = temp;
          temp >>= 8;
          dest[destidx+1] = temp;
          temp >>= 8;
          dest[destidx] = temp;
        }
        destidx += 3;
        state = 0;
        temp = 0;
      }
    }
  }

  // Process the leftover data contained in 'temp' at the end of the input.
  size_t expected_equals = 0;
  switch (state) {
    case 0:
      // Nothing left over; output is a multiple of 3 bytes.
      break;

    case 1:
      // Bad input; we have 6 bits left over.
      return -1;

    case 2:
      // Produce one more output byte from the 12 input bits we have left.
      if (dest) {
        if (destidx+1 > szdest) return -1;
        temp >>= 4;
        dest[destidx] = temp;
      }
      ++destidx;
      expected_equals = 2;
      break;

    case 3:
      // Produce two more output bytes from the 18 input bits we have left.
      if (dest) {
        if (destidx+2 > szdest) return -1;
        temp >>= 2;
        dest[destidx+1] = temp;
        temp >>= 8;
        dest[destidx] = temp;
      }
      destidx += 2;
      expected_equals = 1;
      break;

    default:
      // state should have no other values at this point.
      LOG(FATAL) << "This can't happen; base64 decoder state = " << state;
  }

  // The remainder of the string should be all whitespace, mixed with
  // exactly 0 equals signs, or exactly 'expected_equals' equals
  // signs.  (Always accepting 0 equals signs is a google extension
  // not covered in the RFC.)

  size_t equals = 0;
  while (szsrc > 0 && *src) {
    if (*src == kPad64)
      ++equals;
    else if (!ascii_isspace(*src))
      return -1;
    --szsrc;
    ++src;
  }

  return (equals == 0 || equals == expected_equals) ? destidx : -1;
}

// The arrays below were generated by the following code
// #include <sys/time.h>
// #include <stdlib.h>
// #include <string.h>
// main()
// {
//   static const char Base64[] =
//     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
//   char *pos;
//   int idx, i, j;
//   printf("    ");
//   for (i = 0; i < 255; i += 8) {
//     for (j = i; j < i + 8; j++) {
//       pos = strchr(Base64, j);
//       if ((pos == NULL) || (j == 0))
//         idx = -1;
//       else
//         idx = pos - Base64;
//       if (idx == -1)
//         printf(" %2d,     ", idx);
//       else
//         printf(" %2d/*%c*/,", idx, j);
//     }
//     printf("\n    ");
//   }
// }
//
// where the value of "Base64[]" was replaced by one of the base-64 conversion
// tables from the functions below.
static const signed char kUnBase64[] = {
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      62/*+*/, -1,      -1,      -1,      63/*/ */,
  52/*0*/, 53/*1*/, 54/*2*/, 55/*3*/, 56/*4*/, 57/*5*/, 58/*6*/, 59/*7*/,
  60/*8*/, 61/*9*/, -1,      -1,      -1,      -1,      -1,      -1,
  -1,       0/*A*/,  1/*B*/,  2/*C*/,  3/*D*/,  4/*E*/,  5/*F*/,  6/*G*/,
  07/*H*/,  8/*I*/,  9/*J*/, 10/*K*/, 11/*L*/, 12/*M*/, 13/*N*/, 14/*O*/,
  15/*P*/, 16/*Q*/, 17/*R*/, 18/*S*/, 19/*T*/, 20/*U*/, 21/*V*/, 22/*W*/,
  23/*X*/, 24/*Y*/, 25/*Z*/, -1,      -1,      -1,      -1,      -1,
  -1,      26/*a*/, 27/*b*/, 28/*c*/, 29/*d*/, 30/*e*/, 31/*f*/, 32/*g*/,
  33/*h*/, 34/*i*/, 35/*j*/, 36/*k*/, 37/*l*/, 38/*m*/, 39/*n*/, 40/*o*/,
  41/*p*/, 42/*q*/, 43/*r*/, 44/*s*/, 45/*t*/, 46/*u*/, 47/*v*/, 48/*w*/,
  49/*x*/, 50/*y*/, 51/*z*/, -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1
};
static const signed char kUnWebSafeBase64[] = {
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      62/*-*/, -1,      -1,
  52/*0*/, 53/*1*/, 54/*2*/, 55/*3*/, 56/*4*/, 57/*5*/, 58/*6*/, 59/*7*/,
  60/*8*/, 61/*9*/, -1,      -1,      -1,      -1,      -1,      -1,
  -1,       0/*A*/,  1/*B*/,  2/*C*/,  3/*D*/,  4/*E*/,  5/*F*/,  6/*G*/,
  07/*H*/,  8/*I*/,  9/*J*/, 10/*K*/, 11/*L*/, 12/*M*/, 13/*N*/, 14/*O*/,
  15/*P*/, 16/*Q*/, 17/*R*/, 18/*S*/, 19/*T*/, 20/*U*/, 21/*V*/, 22/*W*/,
  23/*X*/, 24/*Y*/, 25/*Z*/, -1,      -1,      -1,      -1,      63/*_*/,
  -1,      26/*a*/, 27/*b*/, 28/*c*/, 29/*d*/, 30/*e*/, 31/*f*/, 32/*g*/,
  33/*h*/, 34/*i*/, 35/*j*/, 36/*k*/, 37/*l*/, 38/*m*/, 39/*n*/, 40/*o*/,
  41/*p*/, 42/*q*/, 43/*r*/, 44/*s*/, 45/*t*/, 46/*u*/, 47/*v*/, 48/*w*/,
  49/*x*/, 50/*y*/, 51/*z*/, -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1,
  -1,      -1,      -1,      -1,      -1,      -1,      -1,      -1
};

size_t Base64Unescape(const char *src, size_t szsrc, char *dest, size_t szdest) {
  return Base64UnescapeInternal(src, szsrc, dest, szdest, kUnBase64);
}

size_t WebSafeBase64Unescape(const char *src, size_t szsrc, char *dest, size_t szdest) {
  return Base64UnescapeInternal(src, szsrc, dest, szdest, kUnWebSafeBase64);
}

static bool Base64UnescapeInternal(const char* src, size_t slen, string* dest,
                                   const signed char* unbase64) {
  // Determine the size of the output string.  Base64 encodes every 3 bytes into
  // 4 characters.  any leftover chars are added directly for good measure.
  // This is documented in the base64 RFC: http://www.ietf.org/rfc/rfc3548.txt
  const size_t dest_len = 3 * (slen / 4) + (slen % 4);

  dest->clear();
  dest->resize(dest_len);

  // We are getting the destination buffer by getting the beginning of the
  // string and converting it into a char *.
  const auto len = Base64UnescapeInternal(src, slen, string_as_array(dest), dest->size(), unbase64);
  if (len < 0) {
    dest->clear();
    return false;
  }

  // could be shorter if there was padding
  DCHECK_LE(len, dest_len);
  dest->resize(len);

  return true;
}

bool Base64Unescape(const char *src, size_t slen, string* dest) {
  return Base64UnescapeInternal(src, slen, dest, kUnBase64);
}

bool WebSafeBase64Unescape(const char *src, size_t slen, string* dest) {
  return Base64UnescapeInternal(src, slen, dest, kUnWebSafeBase64);
}

size_t Base64EscapeInternal(
    const unsigned char *src, size_t szsrc, char *dest, size_t szdest, const char *base64,
    bool do_padding) {
  static const char kPad64 = '=';

  if (szsrc <= 0) return 0;

  char *cur_dest = dest;
  const unsigned char *cur_src = src;

  // Three bytes of data encodes to four characters of cyphertext.
  // So we can pump through three-byte chunks atomically.
  while (szsrc > 2) { /* keep going until we have less than 24 bits */
    if ((szdest -= 4) < 0) return 0;
    cur_dest[0] = base64[cur_src[0] >> 2];
    cur_dest[1] = base64[((cur_src[0] & 0x03) << 4) + (cur_src[1] >> 4)];
    cur_dest[2] = base64[((cur_src[1] & 0x0f) << 2) + (cur_src[2] >> 6)];
    cur_dest[3] = base64[cur_src[2] & 0x3f];

    cur_dest += 4;
    cur_src += 3;
    szsrc -= 3;
  }

  /* now deal with the tail (<=2 bytes) */
  switch (szsrc) {
    case 0:
      // Nothing left; nothing more to do.
      break;
    case 1:
      // One byte left: this encodes to two characters, and (optionally)
      // two pad characters to round out the four-character cypherblock.
      if ((szdest -= 2) < 0) return 0;
      cur_dest[0] = base64[cur_src[0] >> 2];
      cur_dest[1] = base64[(cur_src[0] & 0x03) << 4];
      cur_dest += 2;
      if (do_padding) {
        if ((szdest -= 2) < 0) return 0;
        cur_dest[0] = kPad64;
        cur_dest[1] = kPad64;
        cur_dest += 2;
      }
      break;
    case 2:
      // Two bytes left: this encodes to three characters, and (optionally)
      // one pad character to round out the four-character cypherblock.
      if ((szdest -= 3) < 0) return 0;
      cur_dest[0] = base64[cur_src[0] >> 2];
      cur_dest[1] = base64[((cur_src[0] & 0x03) << 4) + (cur_src[1] >> 4)];
      cur_dest[2] = base64[(cur_src[1] & 0x0f) << 2];
      cur_dest += 3;
      if (do_padding) {
        if ((szdest -= 1) < 0) return 0;
        cur_dest[0] = kPad64;
        cur_dest += 1;
      }
      break;
    default:
      // Should not be reached: blocks of 3 bytes are handled
      // in the while loop before this switch statement.
      LOG_ASSERT(false) << "Logic problem? szsrc = " << szsrc;
      break;
  }
  return (cur_dest - dest);
}

static const char kBase64Chars[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char kWebSafeBase64Chars[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t Base64Escape(const unsigned char *src, size_t szsrc, char *dest, size_t szdest) {
  return Base64EscapeInternal(src, szsrc, dest, szdest, kBase64Chars, true);
}
size_t WebSafeBase64Escape(
    const unsigned char *src, size_t szsrc, char *dest, size_t szdest, bool do_padding) {
  return Base64EscapeInternal(src, szsrc, dest, szdest,
                              kWebSafeBase64Chars, do_padding);
}

void Base64EscapeInternal(const unsigned char* src, size_t szsrc,
                          string* dest, bool do_padding,
                          const char* base64_chars) {
  const auto calc_escaped_size =
    CalculateBase64EscapedLen(szsrc, do_padding);
  dest->clear();
  dest->resize(calc_escaped_size, '\0');
  const auto escaped_len = Base64EscapeInternal(src, szsrc,
                                                string_as_array(dest),
                                                dest->size(),
                                                base64_chars,
                                                do_padding);
  DCHECK_EQ(calc_escaped_size, escaped_len);
}

void Base64Escape(const unsigned char *src, size_t szsrc,
                  string* dest, bool do_padding) {
  Base64EscapeInternal(src, szsrc, dest, do_padding, kBase64Chars);
}

void WebSafeBase64Escape(const unsigned char *src, size_t szsrc,
                         string *dest, bool do_padding) {
  Base64EscapeInternal(src, szsrc, dest, do_padding, kWebSafeBase64Chars);
}

void Base64Escape(const string& src, string* dest) {
  Base64Escape(reinterpret_cast<const unsigned char*>(src.data()),
               src.size(), dest, true);
}

void WebSafeBase64Escape(const string& src, string* dest) {
  WebSafeBase64Escape(reinterpret_cast<const unsigned char*>(src.data()),
                      src.size(), dest, false);
}

void WebSafeBase64EscapeWithPadding(const string& src, string* dest) {
  WebSafeBase64Escape(reinterpret_cast<const unsigned char*>(src.data()),
                      src.size(), dest, true);
}

// Returns true iff c is in the Base 32 alphabet.
bool ValidBase32Byte(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7') || c == '=';
}

// Mapping from number of Base32 escaped characters (0 through 8) to number of
// unescaped bytes.  8 Base32 escaped characters represent 5 unescaped bytes.
// For N < 8, then number of unescaped bytes is less than 5.  Note that in
// valid input, N can only be 0, 2, 4, 5, 7, or 8 (corresponding to 0, 1, 2,
// 3, 4, or 5 unescaped bytes).
//
// We use 5 for invalid values of N to be safe, since this is used to compute
// the length of the buffer to hold unescaped data.
//
// See http://tools.ietf.org/html/rfc4648#section-6 for details.
static const size_t kBase32NumUnescapedBytes[] = {
  0, 5, 1, 5, 2, 3, 5, 4, 5
};

size_t Base32Unescape(const char* src, size_t slen, char* dest, size_t szdest) {
  size_t destidx = 0;
  char escaped_bytes[8];
  unsigned char unescaped_bytes[5];
  while (slen > 0) {
    // Collect the next 8 escaped bytes and convert to upper case.  If there
    // are less than 8 bytes left, pad with '=', but keep track of the number
    // of non-padded bytes for later.
    size_t non_padded_len = 8;
    for (size_t i = 0; i < 8; ++i) {
      escaped_bytes[i] = (i < slen) ? ascii_toupper(src[i]) : '=';
      if (!ValidBase32Byte(escaped_bytes[i])) {
        return -1;
      }
      // Stop counting escaped bytes at first '='.
      if (escaped_bytes[i] == '=' && non_padded_len == 8) {
        non_padded_len = i;
      }
    }

    // Convert the 8 escaped bytes to 5 unescaped bytes and copy to dest.
    EightBase32DigitsToFiveBytes(escaped_bytes, unescaped_bytes);
    const auto num_unescaped = kBase32NumUnescapedBytes[non_padded_len];
    for (size_t i = 0; i < num_unescaped; ++i) {
      if (destidx == szdest) {
        // No more room in dest, so terminate early.
        return -1;
      }
      dest[destidx] = unescaped_bytes[i];
      ++destidx;
    }
    src += 8;
    slen -= 8;
  }
  return destidx;
}

bool Base32Unescape(const char* src, size_t slen, string* dest) {
  // Determine the size of the output string.
  const auto dest_len = 5 * (slen / 8) + kBase32NumUnescapedBytes[slen % 8];

  dest->clear();
  dest->resize(dest_len);

  // We are getting the destination buffer by getting the beginning of the
  // string and converting it into a char *.
  const auto len = Base32Unescape(src, slen, string_as_array(dest), dest->size());
  if (len < 0) {
    dest->clear();
    return false;
  }

  // Could be shorter if there was padding.
  DCHECK_LE(len, dest_len);
  dest->resize(len);

  return true;
}

void GeneralFiveBytesToEightBase32Digits(const unsigned char *in_bytes,
                                         char *out, const char *alphabet) {
  // It's easier to just hard code this.
  // The conversion isbased on the following picture of the division of a
  // 40-bit block into 8 5-byte words:
  //
  //       5   3  2  5  1  4   4 1  5  2  3   5
  //     |:::::::|:::::::|:::::::|:::::::|:::::::
  //     +----+----+----+----+----+----+----+----
  //
  out[0] = alphabet[in_bytes[0] >> 3];
  out[1] = alphabet[(in_bytes[0] & 0x07) << 2 | in_bytes[1] >> 6];
  out[2] = alphabet[(in_bytes[1] & 0x3E) >> 1];
  out[3] = alphabet[(in_bytes[1] & 0x01) << 4 | in_bytes[2] >> 4];
  out[4] = alphabet[(in_bytes[2] & 0x0F) << 1 | in_bytes[3] >> 7];
  out[5] = alphabet[(in_bytes[3] & 0x7C) >> 2];
  out[6] = alphabet[(in_bytes[3] & 0x03) << 3 | in_bytes[4] >> 5];
  out[7] = alphabet[(in_bytes[4] & 0x1F)];
}

static size_t GeneralBase32Escape(
    const unsigned char *src, size_t szsrc, char *dest, size_t szdest, const char *alphabet) {
  static const char kPad32 = '=';

  if (szsrc == 0) return 0;

  char *cur_dest = dest;
  const unsigned char *cur_src = src;

  // Five bytes of data encodes to eight characters of cyphertext.
  // So we can pump through three-byte chunks atomically.
  while (szsrc > 4) {  // keep going until we have less than 40 bits
    if ( szdest < 8) return 0;
    szdest -= 8;

    GeneralFiveBytesToEightBase32Digits(cur_src, cur_dest, alphabet);

    cur_dest += 8;
    cur_src += 5;
    szsrc -= 5;
  }

  // Now deal with the tail (<=4 bytes).
  if (szsrc > 0) {
    if ( szdest < 8) return 0;
    szdest -= 8;
    unsigned char last_chunk[5];
    memcpy(last_chunk, cur_src, szsrc);

    for (size_t i = szsrc; i < 5; ++i) {
      last_chunk[i] = '\0';
    }

    GeneralFiveBytesToEightBase32Digits(last_chunk, cur_dest, alphabet);
    auto filled = (szsrc * 8) / 5 + 1;
    cur_dest += filled;

    // Add on the padding.
    for (size_t i = 0; i < (8 - filled); ++i) {
      *(cur_dest++) = kPad32;
    }
  }

  return cur_dest - dest;
}

static bool GeneralBase32Escape(const string& src, string* dest,
                                const char *alphabet) {
  const auto max_escaped_size = CalculateBase32EscapedLen(src.length());
  dest->clear();
  dest->resize(max_escaped_size + 1, '\0');
  const auto escaped_len =
      GeneralBase32Escape(reinterpret_cast<const unsigned char *>(src.c_str()),
                          src.length(),  &*dest->begin(), dest->size(),
                          alphabet);

  DCHECK_LE(max_escaped_size, escaped_len);

  if (escaped_len < 0) {
    dest->clear();
    return false;
  }

  dest->resize(escaped_len);
  return true;
}

static const char Base32Alphabet[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', '2', '3', '4', '5', '6', '7'
  };

size_t Base32Escape(const unsigned char* src, size_t szsrc, char* dest, size_t szdest) {
  return GeneralBase32Escape(src, szsrc, dest, szdest, Base32Alphabet);
}

bool Base32Escape(const string& src, string* dest) {
  return GeneralBase32Escape(src, dest, Base32Alphabet);
}

void FiveBytesToEightBase32Digits(const unsigned char *in_bytes, char *out) {
  GeneralFiveBytesToEightBase32Digits(in_bytes, out, Base32Alphabet);
}

static const char Base32HexAlphabet[] = {
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
  'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
  'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
  };

size_t Base32HexEscape(const unsigned char* src, size_t szsrc, char* dest, size_t szdest) {
  return GeneralBase32Escape(src, szsrc, dest, szdest, Base32HexAlphabet);
}

bool Base32HexEscape(const string& src, string* dest) {
  return GeneralBase32Escape(src, dest, Base32HexAlphabet);
}

size_t CalculateBase32EscapedLen(size_t input_len) {
  DCHECK_LE(input_len, numeric_limits<size_t>::max() / 8);
  size_t intermediate_result = 8 * input_len + 4;
  size_t len = intermediate_result / 5;
  len = (len + 7) & ~7;
  return len;
}

// ----------------------------------------------------------------------
// EightBase32DigitsToTenHexDigits()
//   Converts an 8-digit base32 string to a 10-digit hex string.
//
//   *in must point to 8 base32 digits.
//   *out must point to 10 bytes.
//
//   Base32 uses A-Z,2-7 to represent the numbers 0-31.
//   See RFC3548 at http://www.ietf.org/rfc/rfc3548.txt
//   for details on base32.
// ----------------------------------------------------------------------


void EightBase32DigitsToTenHexDigits(const char *in, char *out) {
  unsigned char bytes[5];
  EightBase32DigitsToFiveBytes(in, bytes);
  b2a_hex(bytes, out, 5);
}

void EightBase32DigitsToFiveBytes(const char *signed_in, unsigned char *bytes_out) {
  auto* in = static_cast<const unsigned char*>(static_cast<const void*>(signed_in));

  static const char Base32InverseAlphabet[] = {
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      26/*2*/, 27/*3*/, 28/*4*/, 29/*5*/, 30/*6*/, 31/*7*/,
    99,      99,      99,      99,      99,      00/*=*/, 99,      99,
    99,       0/*A*/,  1/*B*/,  2/*C*/,  3/*D*/,  4/*E*/,  5/*F*/,  6/*G*/,
     7/*H*/,  8/*I*/,  9/*J*/, 10/*K*/, 11/*L*/, 12/*M*/, 13/*N*/, 14/*O*/,
    15/*P*/, 16/*Q*/, 17/*R*/, 18/*S*/, 19/*T*/, 20/*U*/, 21/*V*/, 22/*W*/,
    23/*X*/, 24/*Y*/, 25/*Z*/, 99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99,
    99,      99,      99,      99,      99,      99,      99,      99
  };

  // Convert to raw bytes. It's easier to just hard code this.
  bytes_out[0] = Base32InverseAlphabet[in[0]] << 3 |
                 Base32InverseAlphabet[in[1]] >> 2;

  bytes_out[1] = Base32InverseAlphabet[in[1]] << 6 |
                 Base32InverseAlphabet[in[2]] << 1 |
                 Base32InverseAlphabet[in[3]] >> 4;

  bytes_out[2] = Base32InverseAlphabet[in[3]] << 4 |
                 Base32InverseAlphabet[in[4]] >> 1;

  bytes_out[3] = Base32InverseAlphabet[in[4]] << 7 |
                 Base32InverseAlphabet[in[5]] << 2 |
                 Base32InverseAlphabet[in[6]] >> 3;

  bytes_out[4] = Base32InverseAlphabet[in[6]] << 5 |
                 Base32InverseAlphabet[in[7]];
}

// ----------------------------------------------------------------------
// TenHexDigitsToEightBase32Digits()
//   Converts a 10-digit hex string to an 8-digit base32 string.
//
//   *in must point to 10 hex digits.
//   *out must point to 8 bytes.
//
//   See RFC3548 at http://www.ietf.org/rfc/rfc3548.txt
//   for details on base32.
// ----------------------------------------------------------------------
void TenHexDigitsToEightBase32Digits(const char *in, char *out) {
  unsigned char bytes[5];

  // Convert hex to raw bytes.
  a2b_hex(in, bytes, 5);
  FiveBytesToEightBase32Digits(bytes, out);
}

// ----------------------------------------------------------------------
// EscapeFileName / UnescapeFileName
// ----------------------------------------------------------------------
static const Charmap escape_file_name_exceptions(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"  // letters
    "0123456789"  // digits
    "-_.");

void EscapeFileName(const GStringPiece& src, string* dst) {
  // Reserve at least src.size() chars
  dst->reserve(dst->size() + src.size());

  for (char c : src) {
    // We do not use "isalpha" because we want the behavior to be
    // independent of the current locale settings.
    if (escape_file_name_exceptions.contains(c)) {
      dst->push_back(c);

    } else if (c == '/') {
      dst->push_back('~');

    } else {
      char tmp[2];
      b2a_hex(reinterpret_cast<const unsigned char*>(&c), tmp, 1);
      dst->push_back('%');
      dst->append(tmp, 2);
    }
  }
}

void UnescapeFileName(const GStringPiece& src_piece, string* dst) {
  const char* src = src_piece.data();
  const auto len = src_piece.size();
  for (size_t i = 0; i < len; ++i) {
    const char c = src[i];
    if (c == '~') {
      dst->push_back('/');

    } else if ((c == '%') && (i + 2 < len)) {
      unsigned char tmp[1];
      a2b_hex(src + i + 1, &tmp[0], 1);
      dst->push_back(tmp[0]);
      i += 2;

    } else {
      dst->push_back(c);
    }
  }
}

static char hex_value[256] = {
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 0, 0, 0, 0, 0, 0,  // '0'..'9'
  0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 'A'..'F'
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 'a'..'f'
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static char hex_char[] = "0123456789abcdef";

// This is a templated function so that T can be either a char*
// or a string.  This works because we use the [] operator to access
// individual characters at a time.
template <typename T>
static void a2b_hex_t(const char* a, T b, size_t num) {
  for (size_t i = 0; i < num; i++) {
    b[i] = (hex_value[a[i * 2] & 0xFF] << 4)
         + (hex_value[a[i * 2 + 1] & 0xFF]);
  }
}

string a2b_bin(const string& a, bool byte_order_msb) {
  string result;
  const char *data = a.c_str();
  auto num_bytes = (a.size()+7)/8;
  for (size_t byte_offset = 0; byte_offset < num_bytes; ++byte_offset) {
    unsigned char c = 0;
    for (size_t bit_offset = 0; bit_offset < 8; ++bit_offset) {
      if (*data == '\0')
        break;
      if (*data++ != '0') {
        size_t bits_to_shift = (byte_order_msb) ? 7-bit_offset : bit_offset;
        c |= (1 << bits_to_shift);
      }
    }
    result.append(1, c);
  }
  return result;
}

// This is a templated function so that T can be either a char*
// or a string.  This works because we use the [] operator to access
// individual characters at a time.
template <typename T>
static void b2a_hex_t(const unsigned char* b, T a, size_t num) {
  for (size_t i = 0; i < num; i++) {
    a[i * 2 + 0] = hex_char[b[i] >> 4];
    a[i * 2 + 1] = hex_char[b[i] & 0xf];
  }
}

string b2a_bin(const string& b, bool byte_order_msb) {
  string result;
  for (char c : b) {
    for (size_t bit_offset = 0; bit_offset < 8; ++bit_offset) {
      auto x = (byte_order_msb) ? 7-bit_offset : bit_offset;
      result.append(1, (c & (1 << x)) ? '1' : '0');
    }
  }
  return result;
}

void b2a_hex(const unsigned char* b, char* a, size_t num) {
  b2a_hex_t<char*>(b, a, num);
}

void a2b_hex(const char* a, unsigned char* b, size_t num) {
  a2b_hex_t<unsigned char*>(a, b, num);
}

void a2b_hex(const char* a, char* b, size_t num) {
  a2b_hex_t<char*>(a, b, num);
}

string b2a_hex(const char* b, size_t len) {
  string result;
  result.resize(len << 1);
  b2a_hex_t<string&>(reinterpret_cast<const unsigned char*>(b), result, len);
  return result;
}

std::string b2a_hex(const std::string_view& b) {
  return b2a_hex(b.data(), b.size());
}

std::string a2b_hex(const std::string_view& a) {
  std::string result;
  a2b_hex(a.data(), &result, a.size()/2);

  return result;
}

void b2a_hex(const unsigned char* from, string* to, size_t num) {
  to->resize(num << 1);
  b2a_hex_t<string&>(from, *to, num);
}

void a2b_hex(const char* from, string* to, size_t num) {
  to->resize(num);
  a2b_hex_t<string&>(from, *to, num);
}

const char* kDontNeedShellEscapeChars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.=/:,@";

string ShellEscape(GStringPiece src) {
  if (!src.empty() &&  // empty string needs quotes
      src.find_first_not_of(kDontNeedShellEscapeChars) == GStringPiece::npos) {
    // only contains chars that don't need quotes; it's fine
    return src.ToString();
  } else if (src.find('\'') == GStringPiece::npos) {
    // no single quotes; just wrap it in single quotes
    return StrCat("'", src, "'");
  } else {
    // needs double quote escaping
    string result = "\"";
    for (char c : src) {
      switch (c) {
        case '\\':
        case '$':
        case '"':
        case '`':
          result.push_back('\\');
      }
      result.push_back(c);
    }
    result.push_back('"');
    return result;
  }
}

static const char kHexTable[513]=
  "000102030405060708090a0b0c0d0e0f"
  "101112131415161718191a1b1c1d1e1f"
  "202122232425262728292a2b2c2d2e2f"
  "303132333435363738393a3b3c3d3e3f"
  "404142434445464748494a4b4c4d4e4f"
  "505152535455565758595a5b5c5d5e5f"
  "606162636465666768696a6b6c6d6e6f"
  "707172737475767778797a7b7c7d7e7f"
  "808182838485868788898a8b8c8d8e8f"
  "909192939495969798999a9b9c9d9e9f"
  "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
  "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
  "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
  "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
  "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
  "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";

//------------------------------------------------------------------------
// ByteStringToAscii
//  Reads at most bytes_to_read from binary_string and prints it to
//  ascii_string in downcased hex.
//------------------------------------------------------------------------
void ByteStringToAscii(string const &binary_string, size_t bytes_to_read, string* ascii_string) {
  if (binary_string.size() < bytes_to_read) {
    bytes_to_read = binary_string.size();
  }

  CHECK_GE(bytes_to_read, 0);
  ascii_string->resize(bytes_to_read*2);

  string::const_iterator in = binary_string.begin();
  string::iterator out = ascii_string->begin();

  for (size_t i = 0; i < bytes_to_read; i++) {
    const unsigned char c = *in; // '*in' is 'signed char'.
    *out++ = kHexTable[c*2];
    *out++ = kHexTable[c*2 + 1];
    ++in;
  }
}

//------------------------------------------------------------------------
// ByteStringFromAscii
//  Converts the hex from ascii_string into binary data and
//  writes the binary data into binary_string.
//  Empty input successfully converts to empty output.
//  Returns false and may modify output if it is
//  unable to parse the hex string.
//------------------------------------------------------------------------
bool ByteStringFromAscii(string const& hex_string, string* binary_string) {
  binary_string->clear();

  if ((hex_string.size()%2) != 0) {
    return false;
  }

  int value = 0;
  for (size_t i = 0; i < hex_string.size(); i++) {
    char c = hex_string[i];

    if (!ascii_isxdigit(c)) {
      return false;
    }

    if (ascii_isdigit(c)) {
      value += c - '0';
    } else if (ascii_islower(c)) {
      value += 10 + c - 'a';
    } else {
      value += 10 + c - 'A';
    }

    if (i & 1) {
      binary_string->push_back(value);
      value = 0;
    } else {
      value <<= 4;
    }
  }

  return true;
}

// ----------------------------------------------------------------------
// CleanStringLineEndings()
//   Clean up a multi-line string to conform to Unix line endings.
//   Reads from src and appends to dst, so usually dst should be empty.
//
//   If there is no line ending at the end of a non-empty string, it can
//   be added automatically.
//
//   Four different types of input are correctly handled:
//
//     - Unix/Linux files: line ending is LF, pass through unchanged
//
//     - DOS/Windows files: line ending is CRLF: convert to LF
//
//     - Legacy Mac files: line ending is CR: convert to LF
//
//     - Garbled files: random line endings, covert gracefully
//                      lonely CR, lonely LF, CRLF: convert to LF
//
//   @param src The multi-line string to convert
//   @param dst The converted string is appended to this string
//   @param auto_end_last_line Automatically terminate the last line
//
//   Limitations:
//
//     This does not do the right thing for CRCRLF files created by
//     broken programs that do another Unix->DOS conversion on files
//     that are already in CRLF format.  For this, a two-pass approach
//     brute-force would be needed that
//
//       (1) determines the presence of LF (first one is ok)
//       (2) if yes, removes any CR, else convert every CR to LF

void CleanStringLineEndings(const string& src, string* dst,
                            bool auto_end_last_line) {
  if (dst->empty()) {
    dst->append(src);
    CleanStringLineEndings(dst, auto_end_last_line);
  } else {
    string tmp = src;
    CleanStringLineEndings(&tmp, auto_end_last_line);
    dst->append(tmp);
  }
}

void CleanStringLineEndings(string* str, bool auto_end_last_line) {
  size_t output_pos = 0;
  bool r_seen = false;
  auto len = str->size();

  char* p = string_as_array(str);

  for (size_t input_pos = 0; input_pos < len;) {
    if (!r_seen && input_pos + 8 < len) {
      uint64 v = UNALIGNED_LOAD64(p + input_pos);
      // Loop over groups of 8 bytes at a time until we come across
      // a word that has a byte whose value is less than or equal to
      // '\r' (i.e. could contain a \n (0x0a) or a \r (0x0d) ).
      //
      // We use a has_less macro that quickly tests a whole 64-bit
      // word to see if any of the bytes has a value < N.
      //
      // For more details, see:
      //   http://graphics.stanford.edu/~seander/bithacks.html#HasLessInWord
#define has_less(x, n) (((x)-~0ULL/255*(n))&~(x)&~0ULL/255*128)
      if (!has_less(v, '\r' + 1)) {
#undef has_less
        // No byte in this word has a value that could be a \r or a \n
        if (output_pos != input_pos)
          UNALIGNED_STORE64(p + output_pos, v);
        input_pos += 8;
        output_pos += 8;
        continue;
      }
    }
    string::const_reference in = p[input_pos];
    if (in == '\r') {
      if (r_seen)
        p[output_pos++] = '\n';
      r_seen = true;
    } else if (in == '\n') {
      if (input_pos != output_pos)
        p[output_pos++] = '\n';
      else
        output_pos++;
      r_seen = false;
    } else {
      if (r_seen)
        p[output_pos++] = '\n';
      r_seen = false;
      if (input_pos != output_pos)
        p[output_pos++] = in;
      else
        output_pos++;
    }
    input_pos++;
  }
  if (r_seen || (auto_end_last_line
                 && output_pos > 0
                 && p[output_pos - 1] != '\n')) {
    str->resize(output_pos + 1);
    str->operator[](output_pos) = '\n';
  } else if (output_pos < len) {
    str->resize(output_pos);
  }
}


}  // namespace strings
