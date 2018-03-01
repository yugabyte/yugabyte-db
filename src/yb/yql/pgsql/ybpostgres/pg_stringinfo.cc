//--------------------------------------------------------------------------------------------------
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
// All files that are prefixed with "pg_" redefine Postgresql interface. We use the same file and
// function names as Postgresql's code to ease future effort in updating our behavior to match with
// changes in Postgresql, but the contents of these functions are generally redefined to work with
// the rest of Yugabyte code.
//--------------------------------------------------------------------------------------------------

/*-------------------------------------------------------------------------
 *
 * stringinfo.c
 *
 * StringInfo provides an indefinitely-extensible string data type.
 * It can be used to buffer either ordinary C strings (null-terminated text)
 * or arbitrary binary data.  All storage is allocated with palloc().
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *        src/backend/lib/stringinfo.c
 *
 *-------------------------------------------------------------------------
 */

#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"

namespace yb {
namespace pgapi {

/*
 * makeStringInfo
 *
 * Create an empty 'StringInfoData' & return a pointer to it.
 */
StringInfo makeStringInfo(int content_type) {
  return std::make_shared<StringInfoData>(content_type);
}

/*
 * initStringInfo
 *
 * Initialize a StringInfoData struct (with previously undefined contents)
 * to describe an empty string.
 */
void
initStringInfo(StringInfo *str, int content_type) {
  *str = std::make_shared<StringInfoData>(content_type);
}

/*
 * resetStringInfo
 *
 * Reset the StringInfo: the data buffer remains valid, but its
 * previous content, if any, is cleared.
 */
void
resetStringInfo(const StringInfo& str) {
  str->data.clear();
  str->content_type = 0;
}

/*
 * appendStringInfo
 *
 * Format text data under the control of fmt (an sprintf-style format string)
 * and append it to whatever is already in str.  More space is allocated
 * to str if necessary.  This is sort of like a combination of sprintf and
 * strcat.
 */
void
appendStringInfo(const StringInfo& str, const char *fmt, ...) {
  for (;;) {
    va_list args;
    int needed;

    /* Try to format the data. */
    va_start(args, fmt);
    needed = appendStringInfoVA(str, fmt, args);
    va_end(args);

    if (needed == 0)
      break;                          /* success */

    /* Increase the buffer size and try again. */
    enlargeStringInfo(str, needed);
  }
}

/*
 * appendStringInfoVA
 *
 * Attempt to format text data under the control of fmt (an sprintf-style
 * format string) and append it to whatever is already in str.  If successful
 * return zero; if not (because there's not enough space), return an estimate
 * of the space needed, without modifying str.  Typically the caller should
 * pass the return value to enlargeStringInfo() before trying again; see
 * appendStringInfo for standard usage pattern.
 *
 * XXX This API is ugly, but there seems no alternative given the C spec's
 * restrictions on what can portably be done with va_list arguments: you have
 * to redo va_start before you can rescan the argument list, and we can't do
 * that from here.
 */
int
appendStringInfoVA(const StringInfo& str, const char *fmt, va_list args) {
  DCHECK(str);

  const size_t kMaxArgSize = 1024;
  char tempbuf[kMaxArgSize];
  size_t nprinted;

  /*
   * If there's hardly any space, don't bother trying, just fail to make the
   * caller enlarge the buffer first.  We have to guess at how much to
   * enlarge, since we're skipping the formatting work.
   */
  nprinted = vsnprintf(tempbuf, kMaxArgSize, fmt, args);
  if (nprinted < kMaxArgSize) {
    /* Success.  Note nprinted does not include trailing null. */
    str->data += tempbuf;
    return 0;
  }

  return nprinted;
}

/*
 * appendStringInfoString
 *
 * Append a null-terminated string to str.
 * Like appendStringInfo(str, "%s", s) but faster.
 */
void
appendStringInfoString(const StringInfo& str, const char *s) {
  DCHECK(s);
  str->data.append(s);
}

/*
 * appendStringInfoChar
 *
 * Append a single byte to str.
 * Like appendStringInfo(str, "%c", ch) but much faster.
 */
void
appendStringInfoChar(const StringInfo& str, char ch) {
  str->data += ch;
}

/*
 * appendStringInfoSpaces
 *
 * Append the specified number of spaces to a buffer.
 */
void
appendStringInfoSpaces(const StringInfo& str, int count) {
  if (count > 0) {
    str->data.resize(count, ' ');
  }
}

/*
 * appendBinaryStringInfo
 *
 * Append arbitrary binary data to a StringInfo, allocating more space
 * if necessary.
 */
void
appendBinaryStringInfo(const StringInfo& str, const char *data, int datalen) {
  // The caller must already check for the format of this string before calling such that casting
  // binary to ascii is acceptable.
  DCHECK(str);
  str->data.append(data, datalen);
}

void
appendBinaryStringInfo(const StringInfo& str, const uint8_t *data, int datalen) {
  // The caller must already check for the format of this string before calling such that casting
  // binary to ascii is acceptable.
  DCHECK(str);
  str->data.append(reinterpret_cast<const char*>(data), datalen);
}

/*
 * enlargeStringInfo
 *
 * Make sure there is enough space for 'needed' more bytes
 * ('needed' does not include the terminating null).
 *
 * External callers usually need not concern themselves with this, since
 * all stringinfo.c routines do it automatically.  However, if a caller
 * knows that a StringInfo will eventually become X bytes large, it
 * can save some palloc overhead by enlarging the buffer before starting
 * to store data in it.
 *
 * NB: because we use repalloc() to enlarge the buffer, the string buffer
 * will remain allocated in the same memory context that was current when
 * initStringInfo was called, even if another context is now current.
 * This is the desired and indeed critical behavior!
 */
void
enlargeStringInfo(const StringInfo& str, int needed) {
}

}  // namespace pgapi
}  // namespace yb
