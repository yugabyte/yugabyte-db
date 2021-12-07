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

#ifndef YB_YQL_PGGATE_PG_DML_READ_H_
#define YB_YQL_PGGATE_PG_DML_READ_H_

#include <list>

#include "yb/docdb/docdb_fwd.h"
#include "yb/gutil/ref_counted.h"

#include "yb/yql/pggate/pg_dml.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DML_READ
//--------------------------------------------------------------------------------------------------
// Scan Scenarios:
//
// 1. SequentialScan or PrimaryIndexScan (class PgSelect)
//    - YugaByte does not have a separate table for PrimaryIndex.
//    - The target table descriptor, where data is read and returned, is the main table.
//    - The binding table descriptor, whose column is bound to values, is also the main table.
//
// 2. IndexOnlyScan (Class PgSelectIndex)
//    - This special case is optimized where data is read from index table.
//    - The target table descriptor, where data is read and returned, is the index table.
//    - The binding table descriptor, whose column is bound to values, is also the index table.
//
// 3. IndexScan SysTable / UserTable (Class PgSelect and Nested PgSelectIndex)
//    - YugaByte will use the binds to query base-ybctid in the index table, which is then used
//      to query data from the main table.
//    - The target table descriptor, where data is read and returned, is the main table.
//    - The binding table descriptor, whose column is bound to values, is the index table.

class PgDmlRead : public PgDml {
 public:
  PgDmlRead(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id,
           const PgObjectId& index_id, const PgPrepareParameters *prepare_params);
  virtual ~PgDmlRead();

  StmtOp stmt_op() const override { return StmtOp::STMT_SELECT; }

  virtual CHECKED_STATUS Prepare() = 0;

  // Allocate binds.
  virtual void PrepareBinds();

  // Set forward (or backward) scan.
  void SetForwardScan(const bool is_forward_scan);

  // Bind a range column with a BETWEEN condition.
  CHECKED_STATUS BindColumnCondBetween(int attr_num, PgExpr *attr_value, PgExpr *attr_value_end);

  // Bind a column with an IN condition.
  CHECKED_STATUS BindColumnCondIn(int attnum, int n_attr_values, PgExpr **attr_values);

  CHECKED_STATUS BindHashCode(bool start_valid, bool start_inclusive,
                                uint64_t start_hash_val, bool end_valid,
                                bool end_inclusive, uint64_t end_hash_val);

  // Execute.
  virtual CHECKED_STATUS Exec(const PgExecParameters *exec_params);

  void SetCatalogCacheVersion(const uint64_t catalog_cache_version) override {
    DCHECK_NOTNULL(read_req_)->set_ysql_catalog_version(catalog_cache_version);
  }

 protected:
  // Allocate column protobuf.
  PgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) override;
  PgsqlExpressionPB *AllocColumnBindConditionExprPB(PgColumn *col);
  PgsqlExpressionPB *AllocIndexColumnBindPB(PgColumn *col);

  // Allocate protobuf for target.
  PgsqlExpressionPB *AllocTargetPB() override;

  // Allocate column expression.
  PgsqlExpressionPB *AllocColumnAssignPB(PgColumn *col) override;

  // Add column refs to protobuf read request.
  void SetColumnRefs();

  // References mutable request from template operation of doc_op_.
  PgsqlReadRequestPB *read_req_ = nullptr;

 private:
  // Indicates that current operation reads concrete row by specifying row's DocKey.
  bool IsConcreteRowRead() const;
  CHECKED_STATUS ProcessEmptyPrimaryBinds();
  bool CanBuildYbctidsFromPrimaryBinds();
  Result<std::vector<std::string>> BuildYbctidsFromPrimaryBinds();
  CHECKED_STATUS SubstitutePrimaryBindsWithYbctids(const PgExecParameters* exec_params);
  CHECKED_STATUS MoveBoundKeyInOperator(PgColumn* col, const PgsqlConditionPB& in_operator);
  CHECKED_STATUS CopyBoundValue(
      const PgColumn& col, const PgsqlExpressionPB& src, QLValuePB* dest) const;
  Result<docdb::PrimitiveValue> BuildKeyColumnValue(
      const PgColumn& col, const PgsqlExpressionPB& src, PgsqlExpressionPB* dest);
  Result<docdb::PrimitiveValue> BuildKeyColumnValue(
      const PgColumn& col, const PgsqlExpressionPB& src);
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DML_READ_H_
