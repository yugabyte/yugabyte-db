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
 * stringinfo.h
 *        Declarations/definitions for "StringInfo" functions.
 *
 * StringInfo provides an indefinitely-extensible string data type.
 * It can be used to buffer either ordinary C strings (null-terminated text)
 * or arbitrary binary data.  All storage is allocated with palloc().
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/lib/stringinfo.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_STRINGINFO_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_STRINGINFO_H_

#include "yb/yql/pgsql/ybpostgres/pg_defs.h"

namespace yb {
namespace pgapi {

/*
 * Convert any encoding to server encoding.
 *
 * See the notes about string conversion functions at the top of this file.
 *
 * Unlike the other string conversion functions, this will apply validation
 * even if encoding == DatabaseEncoding->encoding.  This is because this is
 * used to process data coming in from outside the database, and we never
 * want to just assume validity.
 */
// We don't support various encoding yet, so define a few macros in place of encoder.
//   const char *pg_any_to_server(const char *s, int len, int encoding);
//   const char *pg_client_to_server(const char *s, int len);
//   const char *pg_server_to_client(const char *s, int len);
#define pg_any_to_server(s, len, encoding) s
#define pg_client_to_server(s, len) s
#define pg_server_to_client(s, len) s
#define pfree(p)

/*-------------------------
 * StringInfoData holds information about an extensible string.
 *              data    is the current buffer for the string (allocated with palloc).
 *              len             is the current string length.  There is guaranteed to be
 *                              a terminating '\0' at data[len], although this is not very
 *                              useful when the string holds binary data rather than text.
 *              maxlen  is the allocated size in bytes of 'data', i.e. the maximum
 *                              string size (including the terminating '\0' char) that we can
 *                              currently store in 'data' without having to reallocate
 *                              more space.  We must always have maxlen > len.
 *              cursor  is initialized to zero by makeStringInfo or initStringInfo,
 *                              but is not otherwise touched by the stringinfo.c routines.
 *                              Some routines use it to scan through a StringInfo.
 *-------------------------
 */
// Postgres has StringInfo every where. It is defined here so that we can use Postgres code.
struct StringInfoData {
  typedef std::shared_ptr<StringInfoData> SharedPtr;

  explicit StringInfoData(int ctype = 0) : content_type(ctype) {
  }
  size_t size() const {
    return data.size();
  }
  const char *buffer() const {
    return data.c_str();
  }
  const char *buffer(size_t offset) const {
    DCHECK_GT(data.size(), offset);
    return data.c_str() + offset;
  }
  void null_terminate() {
    data.append("");
  }

  string data;
  int content_type = 0;
};

typedef StringInfoData::SharedPtr StringInfo;


/*------------------------
 * There are two ways to create a StringInfo object initially:
 *
 * StringInfo stringptr = makeStringInfo();
 *              Both the StringInfoData and the data buffer are palloc'd.
 *
 * StringInfoData string;
 * initStringInfo(&string);
 *              The data buffer is palloc'd but the StringInfoData is just local.
 *              This is the easiest approach for a StringInfo object that will
 *              only live as long as the current routine.
 *
 * To destroy a StringInfo, pfree() the data buffer, and then pfree() the
 * StringInfoData if it was palloc'd.  There's no special support for this.
 *
 * NOTE: some routines build up a string using StringInfo, and then
 * release the StringInfoData but return the data string itself to their
 * caller.  At that point the data string looks like a plain palloc'd
 * string.
 *-------------------------
 */

/*------------------------
 * makeStringInfo
 * Create an empty 'StringInfoData' & return a pointer to it.
 */
extern StringInfo makeStringInfo(int content_type = 0);

/*------------------------
 * initStringInfo
 * Initialize a StringInfoData struct (with previously undefined contents)
 * to describe an empty string.
 */
extern void initStringInfo(StringInfo *str, int content_type = 0);

/*------------------------
 * resetStringInfo
 * Clears the current content of the StringInfo, if any. The
 * StringInfo remains valid.
 */
extern void resetStringInfo(const StringInfo& str);

/*------------------------
 * appendStringInfo
 * Format text data under the control of fmt (an sprintf-style format string)
 * and append it to whatever is already in str.  More space is allocated
 * to str if necessary.  This is sort of like a combination of sprintf and
 * strcat.
 */
extern void appendStringInfo(const StringInfo& str, const char *fmt, ...);

/*------------------------
 * appendStringInfoVA
 * Attempt to format text data under the control of fmt (an sprintf-style
 * format string) and append it to whatever is already in str.  If successful
 * return zero; if not (because there's not enough space), return an estimate
 * of the space needed, without modifying str.  Typically the caller should
 * pass the return value to enlargeStringInfo() before trying again; see
 * appendStringInfo for standard usage pattern.
 */
extern int appendStringInfoVA(const StringInfo& str, const char *fmt, va_list args);

/*------------------------
 * appendStringInfoString
 * Append a null-terminated string to str.
 * Like appendStringInfo(str, "%s", s) but faster.
 */
extern void appendStringInfoString(const StringInfo& str, const char *s);

/*------------------------
 * appendStringInfoChar
 * Append a single byte to str.
 * Like appendStringInfo(str, "%c", ch) but much faster.
 */
extern void appendStringInfoChar(const StringInfo& str, char ch);

/*------------------------
 * appendStringInfoSpaces
 * Append a given number of spaces to str.
 */
extern void appendStringInfoSpaces(const StringInfo& str, int count);

/*------------------------
 * appendBinaryStringInfo
 * Append arbitrary binary data to a StringInfo, allocating more space
 * if necessary.
 */
extern void appendBinaryStringInfo(const StringInfo& str, const char *data, int datalen);
extern void appendBinaryStringInfo(const StringInfo& str, const uint8_t *data, int datalen);

/*------------------------
 * enlargeStringInfo
 * Make sure a StringInfo's buffer can hold at least 'needed' more bytes.
 */
extern void enlargeStringInfo(const StringInfo& str, int needed);

}  // namespace pgapi
}  // namespace yb

#endif // YB_YQL_PGSQL_YBPOSTGRES_PG_STRINGINFO_H_
