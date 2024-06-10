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
//--------------------------------------------------------------------------------------------------

// This file contains the C++ API for the PG/YSQL backend library to be used by DocDB.
// Analogous to YCQL this should be used for write expressions (e.g. SET clause), filtering
// (e.g. WHERE clause), expression projections (e.g. SELECT targets) etc.
// Currently it just supports evaluating an YSQL expressions into a QLValues and is only used
// for the UPDATE .. SET clause.
//
// The implementation for these should typically call (and/or extend) either the PG/YSQL C API
// from pgapi.h or  directly the pggate C++ API.

#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/column_id.h"
#include "yb/common/common_fwd.h"
#include "yb/common/pgsql_error.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/master/master_replication.pb.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/decimal.h"

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

namespace yb::docdb {

//-----------------------------------------------------------------------------
// Types
//-----------------------------------------------------------------------------

struct DocPgParamDesc {
    int32_t attno;
    int32_t typid;
    int32_t typmod;

    DocPgParamDesc(int32_t attno, int32_t typid, int32_t typmod)
        : attno(attno), typid(typid), typmod(typmod)
    {}
};

struct DocPgVarRef {
  size_t var_col_idx;
  const YBCPgTypeEntity* var_type;
  YBCPgTypeAttrs var_type_attrs;
};

const YBCPgTypeEntity* DocPgGetTypeEntity(YbgTypeDesc pg_type);

Status DocPgInit();

//-----------------------------------------------------------------------------
// Expressions/Values
//-----------------------------------------------------------------------------

Status DocPgPrepareExpr(const std::string& expr_str,
                        YbgPreparedExpr *expr,
                        DocPgVarRef *ret_type);

Status DocPgAddVarRef(size_t column_idx,
                      int32_t attno,
                      int32_t typid,
                      int32_t typmod,
                      int32_t collid,
                      std::map<int, const DocPgVarRef> *var_map);

Status DocPgCreateExprCtx(const std::map<int, const DocPgVarRef>& var_map,
                          YbgExprContext *expr_ctx);

Status DocPgPrepareExprCtx(const dockv::PgTableRow& table_row,
                           const std::map<int, const DocPgVarRef>& var_map,
                           YbgExprContext expr_ctx);

Result<std::pair<uint64_t, bool>> DocPgEvalExpr(
    YbgPreparedExpr expr, YbgExprContext expr_ctx);

// Given a 'ql_value' with a binary value, interpret the binary value as a text
// array, and store the individual elements in 'ql_value_vec';
Result<std::vector<std::string>> ExtractTextArrayFromQLBinaryValue(const QLValuePB& ql_value);

Status SetValueFromQLBinary(
    const QLValuePB ql_value,
    const int pg_data_type,
    const std::unordered_map<uint32_t, std::string> &enum_oid_label_map,
    const std::unordered_map<uint32_t, std::vector<master::PgAttributePB>> &composite_atts_map,
    DatumMessagePB *cdc_datum_message = NULL);

Status SetValueFromQLBinaryHelper(
    const QLValuePB ql_value,
    const int elem_type,
    const std::unordered_map<uint32_t, std::string> &enum_oid_label_map,
    const std::unordered_map<uint32_t, std::vector<master::PgAttributePB>> &composite_atts_map,
    DatumMessagePB *cdc_datum_message = NULL);

void DeleteMemoryContextIfSet();

} // namespace yb::docdb
