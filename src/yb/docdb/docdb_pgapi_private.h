//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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

// The half of the DocDB PG/YSQL C++ API whose declarations reference ybgate types (YbcPg*, Ybg*),
// which come from yb_pgbackend via ybgate/ybgate_api.h.  It is split out of docdb_pgapi.h so that
// the public header does not pull in ybgate, letting yb_docdb link yb_pgbackend PRIVATE.  A
// consumer of these declarations must depend on yb_pgbackend directly.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/status_fwd.h"

#include "ybgate/ybgate_api.h"

namespace yb::docdb {

struct DocPgVarRef {
  size_t var_col_idx;
  const YbcPgTypeEntity* var_type;
  YbcPgTypeAttrs var_type_attrs;
};

const YbcPgTypeEntity* DocPgGetTypeEntity(YbgTypeDesc pg_type);

//-----------------------------------------------------------------------------
// Expressions/Values
//-----------------------------------------------------------------------------

Status DocPgPrepareExpr(const std::string& expr_str,
                        YbgPreparedExpr *expr,
                        DocPgVarRef *ret_type,
                        const std::optional<int> version);

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

} // namespace yb::docdb
