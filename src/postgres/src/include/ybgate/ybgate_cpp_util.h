/*-------------------------------------------------------------------------
 *
 * ybgate_cpp_util.h
 *	  YbGate macro to convert YbgStatus into regular Status.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/include/ybgate/ybgate_cpp_util.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "yb/common/pgsql_error.h"
#include "yb/util/yb_pg_errcodes.h"
#include "ybgate/ybgate_api.h"

// Macro to convert YbgStatus into regular Status.
// Use it as a reference if you need to create a Status in DocDB to be displayed by Postgres.
// Basically, STATUS(QLError, "<message>"); makes a good base, it should correctly set location
// (filename and line number) and error message. The STATUS macro does not support function name.
// Location is optional, though filename and line number are typically present. If function name is
// needed, it can be added separately:
//   s = s.CloneAndAddErrorCode(FuncName(funcname));
// If error message needs to be translatable template, template arguments should be converted to
// a vector of strings and added to status this way:
//   s = s.CloneAndAddErrorCode(PgsqlMessageArgs(args));
// If needed SQL code may be added to status this way:
//   s = s.CloneAndAddErrorCode(PgsqlError(sqlerror));
// It is possible to define more custom codes to send around with a status.
// They should be handled on the Postgres side, see the HandleYBStatusAtErrorLevel macro in the
// pg_yb_utils.h for details.
#define PG_RETURN_NOT_OK(status) \
  do { \
    YbgStatus status_ = status; \
    if (YbgStatusIsError(status_)) { \
      const char *filename = YbgStatusGetFilename(status_); \
      int lineno = YbgStatusGetLineNumber(status_); \
      const char *funcname = YbgStatusGetFuncname(status_); \
      uint32_t sqlerror = YbgStatusGetSqlError(status_); \
      std::string msg = std::string(YbgStatusGetMessage(status_)); \
      std::vector<std::string> args; \
      int32_t s_nargs; \
      const char **s_args = YbgStatusGetMessageArgs(status_, &s_nargs); \
      if (s_nargs > 0) { \
        args.reserve(s_nargs); \
        for (int i = 0; i < s_nargs; i++) { \
          args.emplace_back(std::string(s_args[i])); \
        } \
      } \
      YbgStatusDestroy(status_); \
      Status s = Status(Status::kQLError, filename, lineno, msg); \
      s = s.CloneAndAddErrorCode(PgsqlError(static_cast<YBPgErrorCode>(sqlerror))); \
      if (funcname) { \
        s = s.CloneAndAddErrorCode(FuncName(funcname)); \
      } \
      return args.empty() ? s : s.CloneAndAddErrorCode(PgsqlMessageArgs(args)); \
    } \
    YbgStatusDestroy(status_); \
  } while(0)
