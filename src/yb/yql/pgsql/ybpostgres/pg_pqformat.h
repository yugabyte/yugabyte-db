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
 * pqformat.h
 *              Definitions for formatting and parsing frontend/backend messages
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/libpq/pqformat.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_PQFORMAT_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_PQFORMAT_H_

#include "yb/yql/pgsql/ybpostgres/pg_defs.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"

namespace yb {
namespace pgapi {

class PGPqFormatter {
 public:
  typedef std::shared_ptr<PGPqFormatter> SharedPtr;

  explicit PGPqFormatter(const StringInfo& read_buffer = nullptr);
  virtual ~PGPqFormatter() { }

  const StringInfo& read_buf() const {
    return read_buf_;
  }

  const StringInfo& send_buf() const {
    return send_buf_;
  }

  //------------------------------------------------------------------------------------------------
  // API for writing buffer.
  void pq_beginmessage(char msgtype);
  void pq_sendbyte(int byt);
  void pq_sendbytes(const char *data, int datalen);
  void pq_sendcountedtext(const char *str, int slen, bool countincludesself);
  void pq_sendtext(const char *str, int slen);
  void pq_sendstring(const char *str);
  void pq_send_ascii_string(const char *str);
  void pq_sendint(int i, int b);
  void pq_sendint64(int64 i);
  void pq_sendfloat4(float4 f); // This is standard 'float'. Postgresql defined double as float4.
  void pq_sendfloat8(float8 f); // This is standard 'double'. Postgresql defined double as float8.

  // TODO(neil) Not sure if we need these functions.
  // void pq_begintypsend();
  // bytea *pq_endtypsend();

#ifdef PGAPI_USING_POSTGRESQL_SOCKET_FLUSHING
  // These functions are not needed in YugaByte environment.
  void pq_endmessage();
  void pq_puttextmessage(char msgtype, const char *str);
  void pq_putemptymessage(char msgtype);
#endif

  //------------------------------------------------------------------------------------------------
  // API for reading buffer.
  int pq_getmsgbyte();
  unsigned int pq_getmsgint(int b);
  int64 pq_getmsgint64();
  float4 pq_getmsgfloat4();
  float8 pq_getmsgfloat8();
  const char *pq_getmsgbytes(int datalen);
  void pq_copymsgbytes(char *buf, int datalen);
  string pq_getmsgtext(int rawbytes, int *nbytes);
  const char *pq_getmsgstring();
  const char *pq_getmsgrawstring();
  CHECKED_STATUS pq_getmsgend();

 private:
  // Hold on the buffer so that all returned char buffers are valid when the formatter is in scope.
  StringInfo read_buf_;
  StringInfo send_buf_;
  size_t cursor_ = 0;
};

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PG_PQFORMAT_H_
