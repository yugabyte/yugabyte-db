/*--------------------------------------------------------------------------------------------------
 * Portions Copyright (c) YugaByte, Inc.
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * Scanning utility functions.
 *--------------------------------------------------------------------------------------------------
 */
#include <algorithm>

#include "yb/gutil/macros.h"
#include "yb/sql/errcodes.h"
#include "yb/sql/parser/scanner_util.h"
#include "yb/util/logging.h"

using namespace std;

unsigned char *unicode_to_utf8(pg_wchar c, unsigned char *utf8string)
{
  if (c <= 0x7F)
  {
    utf8string[0] = c;
  }
  else if (c <= 0x7FF)
  {
    utf8string[0] = 0xC0 | ((c >> 6) & 0x1F);
    utf8string[1] = 0x80 | (c & 0x3F);
  }
  else if (c <= 0xFFFF)
  {
    utf8string[0] = 0xE0 | ((c >> 12) & 0x0F);
    utf8string[1] = 0x80 | ((c >> 6) & 0x3F);
    utf8string[2] = 0x80 | (c & 0x3F);
  }
  else
  {
    utf8string[0] = 0xF0 | ((c >> 18) & 0x07);
    utf8string[1] = 0x80 | ((c >> 12) & 0x3F);
    utf8string[2] = 0x80 | ((c >> 6) & 0x3F);
    utf8string[3] = 0x80 | (c & 0x3F);
  }

  return utf8string;
}

int pg_utf_mblen(const unsigned char *s) {
  int     len;

  if ((*s & 0x80) == 0)
    len = 1;
  else if ((*s & 0xe0) == 0xc0)
    len = 2;
  else if ((*s & 0xf0) == 0xe0)
    len = 3;
  else if ((*s & 0xf8) == 0xf0)
    len = 4;
#ifdef NOT_USED
  else if ((*s & 0xfc) == 0xf8)
    len = 5;
  else if ((*s & 0xfe) == 0xfc)
    len = 6;
#endif
  else
    len = 1;
  return len;
}

int pg_mbstrlen_with_len(const char *mbstr, int limit) {
  int     len = 0;

  while (limit > 0 && *mbstr) {
    int l = pg_utf_mblen((const unsigned char *)mbstr);

    limit -= l;
    mbstr += l;
    len++;
  }
  return len;
}

int pg_verify_mbstr_len(const char *mbstr, int len, bool noError) {
  int mb_len;

  mb_len = 0;
  while (len > 0) {
    int     l;

    /* fast path for ASCII-subset characters */
    if (!IS_HIGHBIT_SET(*mbstr))
    {
      if (*mbstr != '\0')
      {
        mb_len++;
        mbstr++;
        len--;
        continue;
      }
      if (noError)
        return -1;
      report_invalid_encoding(mbstr, len);
    }

    l = pg_utf8_verifier((const unsigned char *) mbstr, len);

    if (l < 0)
    {
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

void report_invalid_encoding(const char *mbstr, int len) {
  int     l = pg_utf_mblen((const unsigned char *)mbstr);
  char    buf[8 * 5 + 1];
  char     *p = buf;
  int     j,
        jlimit;

  jlimit = min(l, len);
  jlimit = min(jlimit, 8);  /* prevent buffer overrun */

  for (j = 0; j < jlimit; j++)
  {
    p += sprintf(p, "0x%02x", (unsigned char) mbstr[j]);
    if (j < jlimit - 1)
      p += sprintf(p, " ");
  }

  LOG(ERROR)
    << "Error " << ERRCODE_CHARACTER_NOT_IN_REPERTOIRE
    << " (Invalid byte sequence for UTF8): " << buf;
}

int pg_utf8_verifier(const unsigned char *s, int len) {
  int     l = pg_utf_mblen(s);

  if (len < l)
    return -1;

  if (!pg_utf8_islegal(s, l))
    return -1;

  return l;
}

bool pg_utf8_islegal(const unsigned char *source, int length) {
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

int pg_encoding_mbcliplen(const char *mbstr, int len, int limit) {
  int     clen = 0;
  int     l;

  while (len > 0 && *mbstr) {
    l = pg_utf_mblen((const unsigned char *) mbstr);
    if ((clen + l) > limit)
      break;
    clen += l;
    if (clen == limit)
      break;
    len -= l;
    mbstr += l;
  }
  return clen;
}
