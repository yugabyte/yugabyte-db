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

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "yb/common/pgsql_protocol.fwd.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/pg_doc_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_tools.h"

namespace yb::pggate {

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
  Status AppendColumnRef(PgColumnRef* colref, bool is_for_secondary_index) override;

  // Append a filter condition.
  // Supported expression kind is serialized Postgres expression.
  Status AppendQual(PgExpr* qual, bool is_for_secondary_index);

  // Allocate binds.
  virtual void PrepareBinds();

  // Set forward (or backward) scan.
  void SetForwardScan(bool is_forward_scan);

  // Set prefix length, in columns, of distinct index scans.
  void SetDistinctPrefixLength(int distinct_prefix_length);

  // Set scan bounds
  void SetHashBounds(uint16_t low_bound, uint16_t high_bound);

  // Bind a range column with a BETWEEN condition.
  Status BindColumnCondBetween(
      int attr_num, PgExpr* attr_value, bool start_inclusive,
      PgExpr* attr_value_end, bool end_inclusive);

  // Bind a column with an IN condition.
  Status BindColumnCondIn(PgExpr* lhs, int n_attr_values, PgExpr** attr_values);

  // Bind a column with an IS NOT NULL condition.
  Status BindColumnCondIsNotNull(int attr_num);

  void BindHashCode(const std::optional<Bound>& start, const std::optional<Bound>& end);

  // Limit scan to specific ybctid range for parallel scan.
  // Sets underlying request's bounds to specified values, also resets any psql operations
  // remaining from the previous range scan.
  Status BindRange(
      Slice lower_bound, bool lower_bound_inclusive, Slice upper_bound, bool upper_bound_inclusive);

  // Add a lower bound to the scan. If a lower bound has already been added
  // this call will set the lower bound to the stricter of the two bounds.
  Status AddRowLowerBound(
      YBCPgStatement handle, int n_col_values, PgExpr** col_values, bool is_inclusive);

  // Add an upper bound to the scan. If an upper bound has already been added
  // this call will set the upper bound to the stricter of the two bounds.
  Status AddRowUpperBound(
      YBCPgStatement handle, int n_col_values, PgExpr** col_values, bool is_inclusive);

  // Execute.
  Status Exec(const PgExecParameters* exec_params);
  void SetRequestedYbctids(std::reference_wrapper<const std::vector<Slice>> ybctids);

  Status ANNBindVector(PgExpr* vector);
  Status ANNSetPrefetchSize(int32_t prefetch_size);

  void SetCatalogCacheVersion(std::optional<PgOid> db_oid, uint64_t version) override {
    DoSetCatalogCacheVersion(read_req_.get(), db_oid, version);
  }

  void UpgradeDocOp(PgDocOp::SharedPtr doc_op);

  [[nodiscard]] const LWPgsqlReadRequestPB* read_req() const { return read_req_.get(); }

  [[nodiscard]] bool IsReadFromYsqlCatalog() const;

  [[nodiscard]] bool IsIndexOrderedScan() const;

  [[nodiscard]] virtual bool IsPgSelectIndex() const { return false; }

 protected:
  explicit PgDmlRead(const PgSession::ScopedRefPtr& pg_session);

  // Allocate column protobuf.
  Result<LWPgsqlExpressionPB*> AllocColumnBindPB(PgColumn* col, PgExpr* expr) override;
  LWPgsqlExpressionPB* AllocColumnBindConditionExprPB(PgColumn* col);
  LWPgsqlExpressionPB* AllocIndexColumnBindPB(PgColumn* col);

  // Allocate protobuf for target.
  LWPgsqlExpressionPB* AllocTargetPB() override;

  // Allocate column expression.
  LWPgsqlExpressionPB* AllocColumnAssignPB(PgColumn* col) override;

  // Add column refs to protobuf read request.
  void SetColumnRefs();

  // References mutable request from template operation of doc_op_.
  std::shared_ptr<LWPgsqlReadRequestPB> read_req_;

 private:
  [[nodiscard]] bool ActualValueForIsForSecondaryIndexArg(
      bool is_for_secondary_index) const;

  [[nodiscard]] ArenaList<LWPgsqlColRefPB>& ColRefPBs() override;

  // Indicates that current operation reads concrete row by specifying row's DocKey.
  [[nodiscard]] bool IsConcreteRowRead() const;
  Status ProcessEmptyPrimaryBinds();
  [[nodiscard]] bool IsAllPrimaryKeysBound() const;
  Result<std::vector<Slice>> BuildYbctidsFromPrimaryBinds();

  Status SubstitutePrimaryBindsWithYbctids(const PgExecParameters* params);
  Result<dockv::DocKey> EncodeRowKeyForBound(
      YBCPgStatement handle, size_t n_col_values, PgExpr** col_values, bool for_lower_bound);

  Status InitDocOp(const PgExecParameters* params, bool is_concrete_row_read);
  Status InitDocOp(const PgExecParameters* params) {
    return InitDocOp(params, IsConcreteRowRead());
  }

  // Holds original doc_op_ object after call of the UpgradeDocOp method.
  // Required to prevent structures related to request from being freed.
  PgDocOp::SharedPtr original_doc_op_;
};

}  // namespace yb::pggate
