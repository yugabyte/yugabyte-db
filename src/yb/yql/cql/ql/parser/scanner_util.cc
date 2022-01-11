//--------------------------------------------------------------------------------------------------
// NOTE: All entities in this modules are copies of PostgreQL's code. We made some minor changes
// to avoid lint errors such as using '{' for if blocks and change the comment style from '/**/'
// to '//'.
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
// The following only applies to changes made to this file as part of YugaByte development.
//
// Copyright (c) YugaByte, Inc.
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
// Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
// Portions Copyright (c) 1994, Regents of the University of California
//
// Scanning utility functions.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/parser/scanner_util.h"

#include <algorithm>

#include "yb/gutil/macros.h"

namespace yb {
namespace ql {

using std::min;

//--------------------------------------------------------------------------------------------------

unsigned int hexval(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 0xA;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 0xA;

  LOG(ERROR) << "invalid hexadecimal digit";
  return 0; /* not reached */
}

//--------------------------------------------------------------------------------------------------

void downcase_truncate_identifier(char *result, const char *ident, int len, bool warn) {
  int i;

  // SQL99 specifies Unicode-aware case normalization, which we don't yet
  // have the infrastructure for.  Instead we use tolower() to provide a
  // locale-aware translation.  However, there are some locales where this
  // is not right either (eg, Turkish may do strange things with 'i' and
  // 'I').  Our current compromise is to use tolower() for characters with
  // the high bit set, as long as they aren't part of a multi-byte
  // character, and use an ASCII-only downcasing for 7-bit characters.
  for (i = 0; i < len; i++) {
    unsigned char ch = static_cast<unsigned char>(ident[i]);

    if (ch >= 'A' && ch <= 'Z') {
      ch += 'a' - 'A';
    }
    result[i] = static_cast<char>(ch);
  }
  result[i] = '\0';

  if (i >= NAMEDATALEN) {
    truncate_identifier(result, i, warn);
  }
}

void truncate_identifier(char *ident, size_t len, bool warn) {
  if (len >= NAMEDATALEN) {
    len = pg_encoding_mbcliplen(ident, len, NAMEDATALEN - 1);
    if (warn) {
      // We avoid using %.*s here because it can misbehave if the data
      // is not valid in what libc thinks is the prevailing encoding.
      char buf[NAMEDATALEN];

      memcpy(buf, ident, len);
      buf[len] = '\0';
      LOG(WARNING) << "SQL Warning: " << ErrorText(ErrorCode::NAME_TOO_LONG)
                   << "Identifier " << ident << " will be truncated to " << buf;
    }
    ident[len] = '\0';
  }
}

//--------------------------------------------------------------------------------------------------

bool scanner_isspace(char ch) {
  // This must match scan.l's list of {space} characters.
  return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f');
}

bool check_uescapechar(unsigned char escape) {
  if (isxdigit(escape)
    || escape == '+'
    || escape == '\''
    || escape == '"'
    || scanner_isspace(escape)) {
    return false;
  } else {
    return true;
  }
}

void check_unicode_value(pg_wchar c, char *loc) {
}

unsigned char *unicode_to_utf8(pg_wchar c, unsigned char *utf8string) {
  if (c <= 0x7F) {
    utf8string[0] = c;
  } else if (c <= 0x7FF) {
    utf8string[0] = 0xC0 | ((c >> 6) & 0x1F);
    utf8string[1] = 0x80 | (c & 0x3F);
  } else if (c <= 0xFFFF) {
    utf8string[0] = 0xE0 | ((c >> 12) & 0x0F);
    utf8string[1] = 0x80 | ((c >> 6) & 0x3F);
    utf8string[2] = 0x80 | (c & 0x3F);
  } else {
    utf8string[0] = 0xF0 | ((c >> 18) & 0x07);
    utf8string[1] = 0x80 | ((c >> 12) & 0x3F);
    utf8string[2] = 0x80 | ((c >> 6) & 0x3F);
    utf8string[3] = 0x80 | (c & 0x3F);
  }

  return utf8string;
}

//--------------------------------------------------------------------------------------------------

bool is_utf16_surrogate_first(pg_wchar c) {
  return (c >= 0xD800 && c <= 0xDBFF);
}

bool is_utf16_surrogate_second(pg_wchar c) {
  return (c >= 0xDC00 && c <= 0xDFFF);
}

pg_wchar surrogate_pair_to_codepoint(pg_wchar first, pg_wchar second) {
  return ((first & 0x3FF) << 10) + 0x10000 + (second & 0x3FF);
}

//--------------------------------------------------------------------------------------------------

size_t pg_utf_mblen(const unsigned char *s) {
  if ((*s & 0x80) == 0)
    return 1;
  else if ((*s & 0xe0) == 0xc0)
    return 2;
  else if ((*s & 0xf0) == 0xe0)
    return 3;
  else if ((*s & 0xf8) == 0xf0)
    return 4;
#ifdef NOT_USED
  else if ((*s & 0xfc) == 0xf8)
    return 5;
  else if ((*s & 0xfe) == 0xfc)
    return 6;
#endif
  else
    return 1;
}

size_t pg_mbstrlen_with_len(const char *mbstr, size_t limit) {
  size_t len = 0;

  while (limit > 0 && *mbstr) {
    auto l = pg_utf_mblen((const unsigned char *)mbstr);

    limit -= l;
    mbstr += l;
    len++;
  }
  return len;
}

size_t pg_verify_mbstr_len(const char *mbstr, size_t len, bool noError) {
  size_t mb_len = 0;
  while (len > 0) {
    /* fast path for ASCII-subset characters */
    if (!is_utf_highbit_set(static_cast<unsigned char>(*mbstr))) {
      if (*mbstr != '\0') {
        mb_len++;
        mbstr++;
        len--;
        continue;
      }
      if (noError) {
        return -1;
      }
      report_invalid_encoding(mbstr, len);
    }

    auto l = pg_utf8_verifier((const unsigned char *) mbstr, len);

    if (l < 0) {
      if (noError)
        return -1;
      report_invalid_encoding(mbstr, len);
    }

    mbstr += l;
    len -= l;
    mb_len++;
  }
  return mb_len;
}

//--------------------------------------------------------------------------------------------------

void report_invalid_encoding(const char *mbstr, size_t len) {
  auto    l = pg_utf_mblen((const unsigned char *)mbstr);
  char    buf[8 * 5 + 1];
  char   *p = buf;

  auto jlimit = min(l, len);
  jlimit = min<size_t>(jlimit, 8);  /* prevent buffer overrun */

  // The following NOLINTs are used as I tried to leave PostgreQL's code as is. Eventually, when we
  // use our own error reporting for QL interface, the NOLINTs should be gone then.
  for (size_t j = 0; j < jlimit; j++) {
    p += sprintf(p, "0x%02x", (unsigned char) mbstr[j]);  // NOLINT(*)
    if (j < jlimit - 1)
      p += sprintf(p, " ");  // NOLINT(*)
  }

  LOG(ERROR) << "SQL Error: " << ErrorText(ErrorCode::CHARACTER_NOT_IN_REPERTOIRE)
             << ". Invalid byte sequence for UTF8 \"" << buf << "\"";
}

//--------------------------------------------------------------------------------------------------

ssize_t pg_utf8_verifier(const unsigned char *s, size_t len) {
  auto l = pg_utf_mblen(s);

  if (len < l)
    return -1;

  if (!pg_utf8_islegal(s, l))
    return -1;

  return l;
}

//--------------------------------------------------------------------------------------------------

bool pg_utf8_islegal(const unsigned char *source, size_t length) {
  unsigned char a;

  switch (length) {
  default:
    /* reject lengths 5 and 6 for now */
    return false;

  case 4:
    a = source[3];
    if (a < 0x80 || a > 0xBF)
      return false;
    FALLTHROUGH_INTENDED;

  case 3:
    a = source[2];
    if (a < 0x80 || a > 0xBF)
      return false;
    FALLTHROUGH_INTENDED;

  case 2:
    a = source[1];
    switch (*source) {
    case 0xE0:
      if (a < 0xA0 || a > 0xBF)
        return false;
      break;
    case 0xED:
      if (a < 0x80 || a > 0x9F)
        return false;
      break;
    case 0xF0:
      if (a < 0x90 || a > 0xBF)
        return false;
      break;
    case 0xF4:
      if (a < 0x80 || a > 0x8F)
        return false;
      break;
    default:
      if (a < 0x80 || a > 0xBF)
        return false;
      break;
    }
    FALLTHROUGH_INTENDED;

  case 1:
    a = *source;
    if (a >= 0x80 && a < 0xC2)
      return false;
    if (a > 0xF4)
      return false;
    break;
  }
  return true;
}

//--------------------------------------------------------------------------------------------------

size_t pg_encoding_mbcliplen(const char *mbstr, size_t len, size_t limit) {
  size_t     clen = 0;

  while (clen < len && *mbstr) {
    auto l = pg_utf_mblen((const unsigned char *) mbstr);
    if ((clen + l) > limit)
      break;
    clen += l;
    if (clen == limit)
      break;
    mbstr += l;
  }
  return clen;
}

}  // namespace ql
}  // namespace yb
