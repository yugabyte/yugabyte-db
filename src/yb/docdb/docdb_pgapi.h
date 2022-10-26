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

#ifndef YB_DOCDB_DOCDB_PGAPI_H_
#define YB_DOCDB_DOCDB_PGAPI_H_

#include <map>
#include <unordered_map>
#include <vector>

#include "yb/common/column_id.h"
#include "yb/common/common_fwd.h"

#include "yb/master/master_replication.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/decimal.h"

#include "ybgate/ybgate_api.h"

namespace yb {
namespace docdb {

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
  ColumnIdRep var_colid;
  const YBCPgTypeEntity *var_type;
  YBCPgTypeAttrs var_type_attrs;
  DocPgVarRef() {}

  DocPgVarRef(ColumnIdRep var_colid, const YBCPgTypeEntity *var_type, int32_t var_typmod)
    : var_colid(var_colid), var_type(var_type), var_type_attrs({var_typmod})
  {}
};

const YBCPgTypeEntity* DocPgGetTypeEntity(YbgTypeDesc pg_type);

Status DocPgInit();

//-----------------------------------------------------------------------------
// Expressions/Values
//-----------------------------------------------------------------------------

Status DocPgPrepareExpr(const std::string& expr_str,
                        YbgPreparedExpr *expr,
                        DocPgVarRef *ret_type);

Status DocPgAddVarRef(const ColumnId& column_id,
                      int32_t attno,
                      int32_t typid,
                      int32_t typmod,
                      int32_t collid,
                      std::map<int, const DocPgVarRef> *var_map);

Status DocPgCreateExprCtx(const std::map<int, const DocPgVarRef>& var_map,
                          YbgExprContext *expr_ctx);

Status DocPgPrepareExprCtx(const QLTableRow& table_row,
                           const std::map<int, const DocPgVarRef>& var_map,
                           YbgExprContext expr_ctx);

Status DocPgEvalExpr(YbgPreparedExpr expr,
                     YbgExprContext expr_ctx,
                     uint64_t *datum,
                     bool *is_null);

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

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_DOCDB_PGAPI_H_
