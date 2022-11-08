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
// API for the core scanner (flex machine)
//
// This module consists of a list of utility functions that are provided by PostgreSQL.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <stdlib.h>

#include "yb/yql/cql/ql/parser/scanner.h"

namespace yb {
namespace ql {

// Maximum number of bytes for UTF character.
#define pg_utf8_max_mblen 4

// Convert character to hex.
unsigned int hexval(unsigned char c);

// downcase_truncate_identifier() --- do appropriate downcasing and
// truncation of an unquoted identifier.  Optionally warn of truncation.
//
// Returns a palloc'd string containing the adjusted identifier.
//
// Note: in some usages the passed string is not null-terminated.
//
// Note: the API of this function is designed to allow for downcasing
// transformations that increase the string length, but we don't yet
// support that.  If you want to implement it, you'll need to fix
// SplitIdentifierString() in utils/adt/varlena.c.
void downcase_truncate_identifier(char *result, const char *ident, int len, bool warn);

// truncate_identifier() --- truncate an identifier to NAMEDATALEN-1 bytes.
//
// The given string is modified in-place, if necessary.  A warning is
// issued if requested.
//
// We require the caller to pass in the string length since this saves a
// strlen() call in some common usages.
void truncate_identifier(char *ident, size_t len, bool warn);

// is 'escape' acceptable as Unicode escape character (UESCAPE syntax) ?
//
// scanner_isspace() --- return TRUE if flex scanner considers char whitespace
//
// This should be used instead of the potentially locale-dependent isspace()
// function when it's important to match the lexer's behavior.
//
// In principle we might need similar functions for isalnum etc, but for the
// moment only isspace seems needed.
bool scanner_isspace(char ch);

// is 'escape' acceptable as Unicode escape character (UESCAPE syntax) ?
bool check_uescapechar(unsigned char escape);

// Unicode checker.
void check_unicode_value(pg_wchar c, char *loc);

// Map a Unicode code point to UTF-8.  utf8string must have 4 bytes of space allocated.
unsigned char *unicode_to_utf8(pg_wchar c, unsigned char *utf8string);

// Predicate for value of higher bits of a wchar.
bool is_utf16_surrogate_first(pg_wchar c);

// Predicate for value of lower bits of a wchar.
bool is_utf16_surrogate_second(pg_wchar c);

// Get wchar value from the given surrogate first and second.
pg_wchar surrogate_pair_to_codepoint(pg_wchar first, pg_wchar second);

// Return the byte length of a UTF8 character pointed to by s
//
// Note: in the current implementation we do not support UTF8 sequences
// of more than 4 bytes; hence do NOT return a value larger than 4.
// We return "1" for any leading byte that is either flat-out illegal or
// indicates a length larger than we support.
//
// pg_utf2wchar_with_len(), utf8_to_unicode(), pg_utf8_islegal(), and perhaps
// other places would need to be fixed to change this.
size_t pg_utf_mblen(const unsigned char *s);

// returns the length (counted in wchars) of a multibyte string
// (not necessarily NULL terminated)
size_t pg_mbstrlen_with_len(const char *mbstr, size_t limit);

// Verify mbstr to make sure that it is validly encoded in the specified
// encoding.
//
// mbstr is not necessarily zero terminated; length of mbstr is
// specified by len.
//
// If OK, return length of string in the encoding.
// If a problem is found, return -1 when noError is
// true; when noError is false, ereport() a descriptive message.
size_t pg_verify_mbstr_len(const char *mbstr, size_t len, bool noError);

// report_invalid_encoding: complain about invalid multibyte character
//
// note: len is remaining length of string, not length of character;
// len must be greater than zero, as we always examine the first byte.
void report_invalid_encoding(const char *mbstr, size_t len);

// Asserting UTF8 format.
ssize_t pg_utf8_verifier(const unsigned char *s, size_t len);

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
bool pg_utf8_islegal(const unsigned char *source, size_t length);

// pg_mbcliplen with specified encoding
size_t pg_encoding_mbcliplen(const char *mbstr, size_t len, size_t limit);

}  // namespace ql
}  // namespace yb
