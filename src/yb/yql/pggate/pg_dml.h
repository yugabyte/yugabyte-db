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
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "yb/util/memory/arena_list.h"

#include "yb/yql/pggate/pg_doc_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_table.h"
DECLARE_bool(ysql_enable_db_catalog_version_mode);

namespace yb::pggate {

class PgDmlRead;
class PgSelectIndex;

class YbctidProvider {
 public:
  virtual ~YbctidProvider() = default;
  virtual Result<std::optional<YbctidBatch>> Fetch() = 0;
};

class PgDml : public PgStatement {
 public:
  ~PgDml() override;

  // Append a target in SELECT or RETURNING.
  Status AppendTarget(PgExpr* target);

  // Append a column reference.
  // If any serialized Postgres expressions appended to other lists require explicit addition
  // of their column references. Those column references should have Postgres type information.
  // Other PgExpr kinds are automatically scanned and their column references are appended.
  virtual Status AppendColumnRef(PgColumnRef* colref, bool is_for_secondary_index);

  // Prepare column for both ends.
  // - Prepare protobuf to communicate with DocDB.
  // - Prepare PgExpr to send data back to Postgres layer.
  Result<const PgColumn&> PrepareColumnForRead(int attr_num, LWPgsqlExpressionPB* target_pb);
  Result<const PgColumn&> PrepareColumnForRead(int attr_num, LWQLExpressionPB* target_pb);
  Status PrepareColumnForWrite(PgColumn* pg_col, LWPgsqlExpressionPB* assign_pb);

  // Bind a column with an expression.
  // - For a secondary-index-scan, this bind specify the value of the secondary key which is used to
  //   query a row.
  // - For a primary-index-scan, this bind specify the value of the keys of the table.
  Status BindColumn(int attnum, PgExpr* attr_value);

  // Bind query vector to the current vector index search.
  Status ANNBindVector(PgExpr* query_vec);

  // Bind prefetch size to the current vector index search.
  Status ANNSetPrefetchSize(int32_t prefetch_size);

  // Bind the whole table.
  Status BindTable();

  // Assign an expression to a column.
  Status AssignColumn(int attnum, PgExpr* attr_value);

  // Fetch a row and return it to Postgres layer.
  Status Fetch(
      int32_t natts, uint64_t* values, bool* isnulls, PgSysColumns* syscols, bool* has_data);

  // Returns TRUE if docdb replies with more data.
  Result<bool> FetchDataFromServer();

  // Returns TRUE if desired row is found.
  Result<bool> GetNextRow(PgTuple* pg_tuple);

  virtual void SetCatalogCacheVersion(std::optional<PgOid> db_oid, uint64_t version) = 0;

  // Get column info on whether the column 'attr_num' is a hash key, a range
  // key, or neither.
  Result<YBCPgColumnInfo> GetColumnInfo(int attr_num) const;

  [[nodiscard]] bool has_aggregate_targets() const { return has_aggregate_targets_; }

 protected:
  class SecondaryIndexQueryWrapper {
   public:
    SecondaryIndexQueryWrapper(
        std::unique_ptr<PgDmlRead>&& query, std::reference_wrapper<const PgExecParameters*> params);
    Status Execute();
    void RequireReExecution() { is_executed_ = false; }
    PgDmlRead& query() const { return *query_; }

   private:
    const std::unique_ptr<PgDmlRead> query_;
    const PgExecParameters*& params_;
    bool is_executed_;
  };

  explicit PgDml(const PgSession::ScopedRefPtr& pg_session);

  // Allocate protobuf for a SELECTed expression.
  virtual LWPgsqlExpressionPB* AllocTargetPB() = 0;

  // Allocate protobuf for expression whose value is bounded to a column.
  virtual Result<LWPgsqlExpressionPB*> AllocColumnBindPB(PgColumn* col, PgExpr* expr) = 0;

  // Allocate protobuf for expression whose value is assigned to a column (SET clause).
  virtual LWPgsqlExpressionPB* AllocColumnAssignPB(PgColumn *col) = 0;

  // Specify target of the query in protobuf request.
  Status AppendTargetPB(PgExpr* target);

  // Update set values.
  Status UpdateAssignPBs();

  // Compatibility: set deprecated column_refs for legacy nodes
  // We are deprecating PgsqlColumnRefsPB protobuf since it does not allow to transfer Postgres
  // type information required to evaluate serialized Postgres expressions.
  // It is being replaced by list of PgsqlColRefPB entries, which is set by ColRefsToPB.
  // While there is are chance of cluster being upgraded from older version, we have to populate
  // both.
  void ColumnRefsToPB(LWPgsqlColumnRefsPB* column_refs);

  // Transfer columns information from target_.columns() to the request's col_refs list field.
  void ColRefsToPB();

  Status UpdateRequestWithYbctids(
      const std::vector<Slice>& ybctids, KeepOrder keep_order = KeepOrder::kFalse);

  template<class Request>
  static void DoSetCatalogCacheVersion(
      Request* req, std::optional<PgOid> db_oid, uint64_t version) {
    auto& request = *DCHECK_NOTNULL(req);
    if (db_oid) {
      DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
      request.set_ysql_db_catalog_version(version);
      request.set_ysql_db_oid(*db_oid);
    } else {
      request.set_ysql_catalog_version(version);
    }
  }

  Result<bool> ProcessProvidedYbctids();

  SecondaryIndexQueryWrapper* SecondaryIndex() {
      return secondary_index_ ? &*secondary_index_ : nullptr; }
  PgDmlRead* SecondaryIndexQuery() const {
      return secondary_index_ ? &secondary_index_->query() : nullptr; }
  YbctidProvider* ybctid_provider() const { return ybctid_provider_.get(); }

  void SetYbctidProvider(std::unique_ptr<YbctidProvider>&& ybctid_provider);
  void SetSecondaryIndex(std::unique_ptr<PgSelectIndex>&& index);

  Status DoAppendColumnRef(PgColumnRef* colref, bool is_for_secondary_index);

  // -----------------------------------------------------------------------------------------------
  // Data members that define the DML statement.
  // Targets of statements (Output parameter).
  // - "target_desc_" is the table descriptor where data will be read from.
  // - "targets_" are either selected or returned expressions by DML statements.
  PgTable target_;
  std::vector<PgFetchedTarget*> targets_;
  bool has_aggregate_targets_ = false;

  // bind_desc_ is the descriptor of the table whose key columns' values will be specified by the
  // the DML statement being executed.
  // - For primary key binding, "bind_desc_" is the descriptor of the main table as we don't have
  //   a separated primary-index table.
  // - For secondary key binding, "bind_desc_" is the descriptor of teh secondary index table.
  //   The bound values will be used to read base_ybctid which is then used to read actual data
  //   from the main table.
  PgTable bind_;

  // -----------------------------------------------------------------------------------------------
  // Data members for generated protobuf.
  // NOTE:
  // - Where clause processing data is not supported yet.
  // - Some protobuf structure are also set up in PgColumn class.

  // Column associated values (expressions) to be used by DML statements.
  // - When expression are constructed, we bind them with their associated protobuf.
  // - These expressions might not yet have values for place_holders or literals.
  // - During execution, the place_holder values are updated, and the statement protobuf need to
  //   be updated accordingly.
  //
  // * Bind values are used to identify the selected rows to be operated on.
  // * Set values are used to hold columns' new values in the selected rows.
  bool ybctid_bind_ = false;

  std::unordered_map<LWPgsqlExpressionPB*, PgExpr*> expr_assigns_;

  // Used for colocated TRUNCATE that doesn't bind any columns.
  bool bind_table_ = false;

  // DML Operator.
  PgDocOp::SharedPtr doc_op_;

  //------------------------------------------------------------------------------------------------
  // Data members for navigating the output / result-set from either selected or returned targets.
  std::list<PgDocResult> rowsets_;
  int64_t current_row_order_ = 0;

  // Yugabyte has a few IN/OUT parameters of statement execution, "pg_exec_params_" is used to sent
  // OUT value back to postgres.
  const PgExecParameters* pg_exec_params_ = nullptr;

 private:
  [[nodiscard]] virtual ArenaList<LWPgsqlColRefPB>& ColRefPBs() = 0;

  std::optional<SecondaryIndexQueryWrapper> secondary_index_;
  std::unique_ptr<YbctidProvider> ybctid_provider_;
};

}  // namespace yb::pggate
