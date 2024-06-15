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
