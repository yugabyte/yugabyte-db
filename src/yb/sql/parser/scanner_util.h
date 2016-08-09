/*--------------------------------------------------------------------------------------------------
 * Portions Copyright (c) YugaByte, Inc.
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * API for the core scanner (flex machine)
 *
 * The core scanner is also used by PL/pgsql, so we provide a public API
 * for it.  However, the rest of the backend is only expected to use the
 * higher-level API provided by parser.h.
 *--------------------------------------------------------------------------------------------------
 */
#ifndef YB_SQL_PARSER_SCANNER_UTIL_H
#define YB_SQL_PARSER_SCANNER_UTIL_H

#include <stdlib.h>

#include "yb/sql/parser/scanner.h"

// Use standard memory allocation. We'll clean this up after adding our own memory allocator.
#define pstrdup strdup

// Number of elements in an array.
#define lengthof(array) (sizeof (array) / sizeof ((array)[0]))

// Maximum length for identifiers (e.g. table names, column names, function names).  Names actually
// are limited to one less byte than this, because the length must include a trailing zero byte.
//
// Changing this requires an initdb.
#define NAMEDATALEN 64

// Quote scanning states.
typedef enum {
  BACKSLASH_QUOTE_OFF,
  BACKSLASH_QUOTE_ON,
  BACKSLASH_QUOTE_SAFE_ENCODING
} BackslashQuoteType;

// Scanning location for reporting errors.
#define YbSql_YYLTYPE YYLTYPE

// Set the type of yyextra.  All state variables used by the scanner should
// be in yyextra, *not* statically allocated.
#define YY_EXTRA_TYPE YbSql_yy_extra_type *

int scanner_errposition(int location, YbSql_yyscan_t yyscanner);
void scanner_yyerror(const char *message, YbSql_yyscan_t yyscanner);

// Unicode support.
typedef unsigned int pg_wchar;

// UTF bit.
#define HIGHBIT               (0x80)
#define IS_HIGHBIT_SET(ch)    ((unsigned char)(ch) & HIGHBIT)

// Map a Unicode code point to UTF-8.  utf8string must have 4 bytes of space allocated.
unsigned char *unicode_to_utf8(pg_wchar c, unsigned char *utf8string);

// Maximum number of bytes for UTF character.
#define pg_utf8_max_mblen 4

// Return the byte length of a UTF8 character pointed to by s
//
// Note: in the current implementation we do not support UTF8 sequences
// of more than 4 bytes; hence do NOT return a value larger than 4.
// We return "1" for any leading byte that is either flat-out illegal or
// indicates a length larger than we support.
//
// pg_utf2wchar_with_len(), utf8_to_unicode(), pg_utf8_islegal(), and perhaps
// other places would need to be fixed to change this.
int pg_utf_mblen(const unsigned char *s);

// returns the length (counted in wchars) of a multibyte string
// (not necessarily NULL terminated)
int pg_mbstrlen_with_len(const char *mbstr, int limit);

// Verify mbstr to make sure that it is validly encoded in the specified
// encoding.
//
// mbstr is not necessarily zero terminated; length of mbstr is
// specified by len.
//
// If OK, return length of string in the encoding.
// If a problem is found, return -1 when noError is
// true; when noError is false, ereport() a descriptive message.
int pg_verify_mbstr_len(const char *mbstr, int len, bool noError);

// report_invalid_encoding: complain about invalid multibyte character
//
// note: len is remaining length of string, not length of character;
// len must be greater than zero, as we always examine the first byte.
void report_invalid_encoding(const char *mbstr, int len);

// Asserting UTF8 format.
int pg_utf8_verifier(const unsigned char *s, int len);

// Check for validity of a single UTF-8 encoded character
//
// This directly implements the rules in RFC3629.  The bizarre-looking
// restrictions on the second byte are meant to ensure that there isn't
// more than one encoding of a given Unicode character point; that is,
// you may not use a longer-than-necessary byte sequence with high order
// zero bits to represent a character that would fit in fewer bytes.
// To do otherwise is to create security hazards (eg, create an apparent
// non-ASCII character that decodes to plain ASCII).
//
// length is assumed to have been obtained by pg_utf_mblen(), and the
// caller must have checked that that many bytes are present in the buffer.
bool pg_utf8_islegal(const unsigned char *source, int length);

// pg_mbcliplen with specified encoding
int pg_encoding_mbcliplen(const char *mbstr, int len, int limit);

#endif // YB_SQL_PARSER_SCANNER_UTIL_H
