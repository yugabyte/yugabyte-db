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

#include "yb/docdb/docdb_pgapi.h"

#include "yb/util/status.h"
#include "yb/common/ql_expr.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/pg_value.h"

using yb::pggate::PgValueFromPB;
using yb::pggate::PgValueToPB;

namespace yb {
namespace docdb {

#define PG_RETURN_NOT_OK(status) \
  do { \
    if (status.err_code != 0) { \
      std::string msg; \
      if (status.err_msg != NULL) { \
        msg = std::string(status.err_msg); \
      } else { \
        msg = std::string("Unexpected error while evaluating expression"); \
      } \
      YbgResetMemoryContext(); \
      return STATUS(QLError, msg); \
    } \
  } while(0);


//-----------------------------------------------------------------------------
// Types
//-----------------------------------------------------------------------------

class DocPgTypeAnalyzer {
 public:
  const YBCPgTypeEntity* GetTypeEntity(int32_t type_oid) {
    const auto iter = type_map_.find(type_oid);
    if (iter != type_map_.end()) {
      return iter->second;
    }
    return nullptr;
  }

 private:
  DocPgTypeAnalyzer() {
    // Setup type mapping.
    const YBCPgTypeEntity *type_table;
    int count;

    YbgGetTypeTable(&type_table, &count);
    for (int idx = 0; idx < count; idx++) {
        const YBCPgTypeEntity *type_entity = &type_table[idx];
        type_map_[type_entity->type_oid] = type_entity;
    }
  }

  // Mapping table of YugaByte and PostgreSQL datatypes.
  std::unordered_map<int, const YBCPgTypeEntity *> type_map_;

  friend class Singleton<DocPgTypeAnalyzer>;
  DISALLOW_COPY_AND_ASSIGN(DocPgTypeAnalyzer);
};

//-----------------------------------------------------------------------------
// Expressions/Values
//-----------------------------------------------------------------------------

const YBCPgTypeEntity* DocPgGetTypeEntity(YbgTypeDesc pg_type) {
    return Singleton<DocPgTypeAnalyzer>::get()->GetTypeEntity(pg_type.type_id);
}

Status DocPgEvalExpr(const std::string& expr_str,
                     int32_t col_attrno,
                     int32_t ret_typeid,
                     int32_t ret_typemod,
                     const QLTableRow& table_row,
                     const Schema *schema,
                     QLValue* result) {
  PG_RETURN_NOT_OK(YbgPrepareMemoryContext());

  char *expr_cstring = const_cast<char *>(expr_str.c_str());
  YbgTypeDesc pg_type = {ret_typeid, ret_typemod};
  const YBCPgTypeEntity *ret_type = DocPgGetTypeEntity(pg_type);
  YBCPgTypeAttrs type_attrs = { pg_type.type_mod };

  // Create the context expression evaluation.
  // Since we currently only allow referencing the target col just set min/max attr to col_attno.
  // TODO Eventually this context should be created once per row and contain all (referenced)
  //      column values. Then the context can be reused for all expressions.
  YbgExprContext expr_ctx;
  PG_RETURN_NOT_OK(YbgExprContextCreate(col_attrno, col_attrno, &expr_ctx));

  // Set the column values (used to resolve scan variables in the expression).
  for (const ColumnId& col_id : schema->column_ids()) {
    auto column = schema->column_by_id(col_id);
    SCHECK(column.ok(), InternalError, "Invalid Schema");

    if (column->order() == col_attrno) {
      const QLValuePB* val = table_row.GetColumn(col_id.rep());
      bool is_null = false;
      uint64_t datum = 0;
      RETURN_NOT_OK(PgValueFromPB(ret_type, type_attrs, *val, &datum, &is_null));
      PG_RETURN_NOT_OK(YbgExprContextAddColValue(expr_ctx, column->order(), datum, is_null));
    }
  }

  // Evaluate the expression and get the result.
  bool is_null = false;
  uint64_t datum;
  PG_RETURN_NOT_OK(YbgEvalExpr(expr_cstring, expr_ctx, &datum, &is_null));

  RETURN_NOT_OK(PgValueToPB(ret_type, datum, is_null, result));

  PG_RETURN_NOT_OK(YbgResetMemoryContext());

  return Status::OK();
}


}  // namespace docdb
}  // namespace yb
