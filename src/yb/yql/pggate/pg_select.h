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

#ifndef YB_YQL_PGGATE_PG_SELECT_H_
#define YB_YQL_PGGATE_PG_SELECT_H_

#include <list>

#include "yb/gutil/ref_counted.h"

#include "yb/yql/pggate/pg_dml.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// SELECT
//--------------------------------------------------------------------------------------------------

class PgSelect : public PgDml {
 public:
  // Public types.
  typedef scoped_refptr<PgSelect> ScopedRefPtr;

  // Constructors.
  PgSelect(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id);
  virtual ~PgSelect();

  StmtOp stmt_op() const override { return StmtOp::STMT_SELECT; }

  void UseIndex(const PgObjectId& index_id);

  // Prepare SELECT before execution.
  CHECKED_STATUS Prepare();

  // Setup internal structures for binding values during prepare.
  void PrepareColumns();

  // Find the index column associated with the given "attr_num".
  CHECKED_STATUS FindIndexColumn(int attr_num, PgColumn **col);

  // Bind an index column with an expression.
  CHECKED_STATUS BindIndexColumn(int attnum, PgExpr *attr_value);

  // Bind a column with an EQUALS condition.
  CHECKED_STATUS BindColumnCondEq(int attnum, PgExpr *attr_value);

  // Bind a range column with a BETWEEN condition.
  CHECKED_STATUS BindColumnCondBetween(int attr_num, PgExpr *attr_value, PgExpr *attr_value_end);

  // Bind a column with an IN condition.
  CHECKED_STATUS BindColumnCondIn(int attnum, int n_attr_values, PgExpr **attr_values);

  // Set forward (or backward) scan.
  void SetForwardScan(const bool is_forward_scan) {
    DCHECK_NOTNULL(read_req_)->set_is_forward_scan(is_forward_scan);
  }

  // Execute.
  CHECKED_STATUS Exec(const PgExecParameters *exec_params);

  void SetCatalogCacheVersion(const uint64_t catalog_cache_version) override {
    DCHECK_NOTNULL(read_req_)->set_ysql_catalog_version(catalog_cache_version);
  }

 private:
  // Allocate column protobuf.
  PgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) override;
  PgsqlExpressionPB *AllocColumnBindConditionExprPB(PgColumn *col);
  PgsqlExpressionPB *AllocIndexColumnBindPB(PgColumn *col);

  // Allocate protobuf for target.
  PgsqlExpressionPB *AllocTargetPB() override;
  PgsqlExpressionPB *AllocIndexTargetPB();

  // Allocate column expression.
  PgsqlExpressionPB *AllocColumnAssignPB(PgColumn *col) override;

  // Delete allocated target for columns that have no bind-values.
  CHECKED_STATUS DeleteEmptyPrimaryBinds();

  // Load index.
  CHECKED_STATUS LoadIndex();

  PgObjectId index_id_;
  PgTableDesc::ScopedRefPtr index_desc_;

  // Protobuf instruction.
  std::shared_ptr<client::YBPgsqlReadOp> read_op_;
  PgsqlReadRequestPB *read_req_ = nullptr;
  PgsqlReadRequestPB *index_req_ = nullptr;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_SELECT_H_
