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
//
//
// Tree node definitions for INSERT statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <iosfwd>

#include "yb/client/client_fwd.h"

#include "yb/common/common_fwd.h"

#include "yb/util/memory/arena.h"
#include "yb/util/memory/mc_types.h"

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

#include "yb/yql/cql/ql/ptree/column_arg.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Counter of operators on each column. "gt" includes ">" and ">=". "lt" includes "<" and "<=".
class ColumnOpCounter {
 public:
  ColumnOpCounter() {}
  int gt_count() const { return gt_count_; }
  int lt_count() const { return lt_count_; }
  int eq_count() const { return eq_count_; }
  int in_count() const { return in_count_; }
  int contains_count() const { return contains_count_; }
  int contains_key_count() const { return contains_key_count_; }

  void increase_gt(bool col_arg = false) { !col_arg ? gt_count_++ : partial_col_gt_count_++; }
  void increase_lt(bool col_arg = false) { !col_arg ? lt_count_++ : partial_col_lt_count_++; }
  void increase_eq(bool col_arg = false) { !col_arg ? eq_count_++ : partial_col_eq_count_++; }
  void increase_in(bool col_arg = false) { !col_arg ? in_count_++ : partial_col_in_count_++; }
  void increase_contains_key() { contains_key_count_++; }
  void increase_contains() { contains_count_++; }

  bool is_valid() {
    // A. At most one condition can be set for a column (except for CONTAINS AND CONTAINS KEY).
    // B. More than one condition can be set for a partial column such as col[1] or col->'a'.
    // C. Conditions on a column and its partial member cannot co-exist in the same statement.
    // Note: we allow adding multiple CONTAINS AND CONTAINS KEY condition on a column
    if (in_count_ + eq_count_ + gt_count_ > 1 || in_count_ + eq_count_ + lt_count_ > 1 ||
        (in_count_ + eq_count_ + gt_count_ + lt_count_ > 0 &&
        partial_col_eq_count_ + partial_col_gt_count_ + partial_col_in_count_ +
            partial_col_lt_count_ + contains_key_count_ + contains_count_ > 0)) {
      return false;
    }
    // D. Both inequality (less and greater) set together.
    if (gt_count_ + lt_count_ > 2 || (gt_count_ + lt_count_ == 2 && gt_count_ != lt_count_)) {
      return false;
    }
    return true;
  }

  bool IsEmpty() const {
    return gt_count_ == 0 && lt_count_ == 0 && eq_count_ == 0 && in_count_ == 0 &&
           contains_key_count_ == 0 && contains_count_ == 0;
  }

 private:
  // These are counts for regular columns.
  int gt_count_ = 0;
  int lt_count_ = 0;
  int eq_count_ = 0;
  int in_count_ = 0;
  int contains_key_count_ = 0;
  int contains_count_ = 0;

  // These are counts for partial columns like json(c1->'a') and collection(c1[0]) operators.
  int partial_col_gt_count_ = 0;
  int partial_col_lt_count_ = 0;
  int partial_col_eq_count_ = 0;
  int partial_col_in_count_ = 0;
};

class AnalyzeStepState {
 public:
  explicit AnalyzeStepState(MCList<PartitionKeyOp> *partition_key_ops)
      : partition_key_ops_(partition_key_ops) {}

  virtual ~AnalyzeStepState() = default;

  virtual Status AnalyzePartitionKeyOp(SemContext *sem_context,
                                       const PTRelationExpr *expr,
                                       PTExprPtr value);
 private:
  MCList<PartitionKeyOp> *partition_key_ops_;
};

// State variables for where clause.
class WhereExprState : public AnalyzeStepState {
 public:
  WhereExprState(MCList<ColumnOp> *ops,
                 MCVector<ColumnOp> *key_ops,
                 MCList<SubscriptedColumnOp> *subscripted_col_ops,
                 MCList<JsonColumnOp> *json_col_ops,
                 MCList<PartitionKeyOp> *partition_key_ops,
                 MCVector<ColumnOpCounter> *op_counters,
                 ColumnOpCounter *partition_key_counter,
                 TreeNodeOpcode statement_type,
                 MCList<FuncOp> *func_ops,
                 MCList<MultiColumnOp> *multi_col_ops)
    : AnalyzeStepState(partition_key_ops),
      ops_(ops),
      key_ops_(key_ops),
      subscripted_col_ops_(subscripted_col_ops),
      json_col_ops_(json_col_ops),
      op_counters_(op_counters),
      partition_key_counter_(partition_key_counter),
      statement_type_(statement_type),
      func_ops_(func_ops),
      multi_colum_ops_(multi_col_ops) {
  }

  Status AnalyzeColumnOp(SemContext *sem_context,
                         const PTRelationExpr *expr,
                         const ColumnDesc *col_desc,
                         PTExprPtr value,
                         PTExprListNodePtr args = nullptr);

  Status AnalyzeMultiColumnOp(SemContext *sem_context,
                              const PTRelationExpr *expr,
                              const std::vector<const ColumnDesc *> col_desc,
                              PTExprPtr value);

  Status AnalyzeColumnFunction(SemContext *sem_context,
                               const PTRelationExpr *expr,
                               PTExprPtr value,
                               PTBcallPtr call);

  Status AnalyzePartitionKeyOp(SemContext *sem_context,
                               const PTRelationExpr *expr,
                               PTExprPtr value) override;

  MCList<FuncOp> *func_ops() {
    return func_ops_;
  }

 private:
  MCList<ColumnOp> *ops_;

  // Operators on key columns.
  MCVector<ColumnOp> *key_ops_;

  // Operators on subscripted columns (e.g. mp['x'] or lst[2]['x'])
  MCList<SubscriptedColumnOp> *subscripted_col_ops_;

  // Operators on json columns (e.g. c1->'a'->'b'->>'c')
  MCList<JsonColumnOp> *json_col_ops_;

  // Counters of '=', '<', and '>' operators for each column in the where expression.
  MCVector<ColumnOpCounter> *op_counters_;

  // Counters on conditions on the partition key (i.e. using `token`)
  ColumnOpCounter *partition_key_counter_;

  // update, insert, delete, select.
  TreeNodeOpcode statement_type_;

  MCList<FuncOp> *func_ops_;

  MCList<MultiColumnOp> *multi_colum_ops_;
};

//--------------------------------------------------------------------------------------------------
// This class represents the data of collection type. PostgreQL syntax rules dictate how we form
// the hierarchy of our C++ classes, so classes for VALUES and SELECT clause must share the same
// base class.
// - VALUES (x, y, z)
// - (SELECT x, y, z FROM tab)
// Functionalities of this class should be "protected" to make sure that PTCollection instances are
// not created and used by application.
class PTCollection : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCollection> SharedPtr;
  typedef MCSharedPtr<const PTCollection> SharedPtrConst;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCollection;
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor. Define them in protected section to prevent application from
  // declaring them.
  PTCollection(MemoryContext *memctx, YBLocationPtr loc)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTCollection() {
  }
};

//--------------------------------------------------------------------------------------------------

class PTDmlStmt : public PTCollection {
 public:
  // Table column name to description map.
  using MCColumnMap = MCMap<MCString, ColumnDesc>;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTDmlStmt(MemoryContext *memctx,
            YBLocationPtr loc,
            PTExprPtr where_clause = nullptr,
            PTExprPtr if_clause = nullptr,
            bool else_error = false,
            PTDmlUsingClausePtr using_clause = nullptr,
            bool returns_status = false);
  // Clone a DML tnode for re-analysis.
  PTDmlStmt(MemoryContext *memctx, const PTDmlStmt& other, bool copy_if_clause);
  virtual ~PTDmlStmt();

  template<typename... TypeArgs>
  inline static PTDmlStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDmlStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  virtual ExplainPlanPB AnalysisResultToPB() = 0;

  // Find column descriptor. From the context, the column value will be marked to be read if
  // necessary when executing the QL statement.
  const ColumnDesc *GetColumnDesc(const SemContext *sem_context, const MCString& col_name);

  virtual bool IsDml() const override {
    return true;
  }

  // Table name.
  virtual client::YBTableName table_name() const = 0;

  // Returns location of table name.
  virtual const YBLocation& table_loc() const = 0;

  // Access functions.
  const std::shared_ptr<client::YBTable>& table() const {
    return table_;
  }

  bool is_system() const {
    return is_system_;
  }

  const MCColumnMap& column_map() const {
    return column_map_;
  }

  size_t num_columns() const;

  size_t num_key_columns() const;

  size_t num_hash_key_columns() const;

  std::string hash_key_columns() const;

  const MCVector<ColumnOp>& key_where_ops() const {
    return key_where_ops_;
  }

  const MCList<ColumnOp>& where_ops() const {
    return where_ops_;
  }

  const MCList<MultiColumnOp> &multi_col_where_ops() const {
    return multi_col_where_ops_;
  }

  const MCList<SubscriptedColumnOp>& subscripted_col_where_ops() const {
    return subscripted_col_where_ops_;
  }

  const MCList<JsonColumnOp>& json_col_where_ops() const {
    return json_col_where_ops_;
  }

  const MCList<PartitionKeyOp>& partition_key_ops() const {
    return partition_key_ops_;
  }

  const MCList <yb::ql::FuncOp>& func_ops() const {
    return func_ops_;
  }

  bool else_error() const {
    return else_error_;
  }

  bool returns_status() const {
    return returns_status_;
  }

  const PTExprPtr& where_clause() const {
    return where_clause_;
  }

  const PTExprPtr& if_clause() const {
    return if_clause_;
  }

  PTExprPtr ttl_seconds() const;

  PTExprPtr user_timestamp_usec() const;

  virtual const std::shared_ptr<client::YBTable>& bind_table() const {
    return table_;
  }

  virtual const MCVector<PTBindVar*> &bind_variables() const {
    return bind_variables_;
  }
  virtual MCVector<PTBindVar*> &bind_variables() {
    return bind_variables_;
  }

  // Compare 2 bind variables for their hash column ids.
  struct HashColCmp {
    bool operator()(const PTBindVar* v1, const PTBindVar* v2) const;
  };
  typedef MCSet<PTBindVar*, HashColCmp> PTBindVarSet;

  const PTBindVarSet& TEST_hash_col_bindvars() const {
    return hash_col_bindvars_;
  }

  virtual std::vector<int64_t> hash_col_indices() const;

  // Access for column_args.
  const MCVector<ColumnArg>& column_args() const {
    return *CHECK_NOTNULL(column_args_.get());
  }

  // Mutable acccess to column_args, used in PreExec phase
  MCVector<ColumnArg>& column_args() {
    return *CHECK_NOTNULL(column_args_.get());
  }

  // Add column ref to be read by DocDB.
  void AddColumnRef(const ColumnDesc& col_desc);

  // Add column ref to be read.
  void AddHashColumnBindVar(PTBindVar* bindvar);

  // Add all column refs to be read by DocDB.
  void AddRefForAllColumns();

  // Access for column_args.
  const MCSet<int32>& column_refs() const {
    return column_refs_;
  }

  // Access for column_args.
  const MCSet<int32>& static_column_refs() const {
    return static_column_refs_;
  }

  // Access for column_args.
  const MCVector<SubscriptedColumnArg>& subscripted_col_args() const {
    CHECK(subscripted_col_args_ != nullptr) << "subscripted-column arguments not set up";
    return *subscripted_col_args_;
  }

  const MCVector<JsonColumnArg>& json_col_args() const {
    CHECK(json_col_args_ != nullptr) << "json-column arguments not set up";
    return *json_col_args_;
  }

  // Access for selected result.
  const std::shared_ptr<std::vector<ColumnSchema>>& selected_schemas() const {
    return selected_schemas_;
  }

  virtual bool IsWriteOp() const = 0;

  bool RequiresTransaction() const;

  const MCUnorderedSet<std::shared_ptr<client::YBTable>>& pk_only_indexes() const {
    return pk_only_indexes_;
  }

  const MCUnorderedSet<TableId>& non_pk_only_indexes() const {
    return non_pk_only_indexes_;
  }

  // Does this DML modify the static or primary or multiple rows?
  bool ModifiesStaticRow() const {
    return modifies_static_row_;
  }
  bool ModifiesPrimaryRow() const {
    return modifies_primary_row_;
  }
  bool ModifiesMultipleRows() const {
    return modifies_multiple_rows_;
  }

  bool HasPrimaryKeysSet() const {
    DCHECK(!IsWriteOp());
    return select_has_primary_keys_set_;
  }

 protected:

  template <typename T>
  static std::string ConditionsToString(T conds) {
    std::stringstream s;
    bool first = true;
    for (const auto& col_op : conds) {
      if (first) {
        first = false;
      } else {
        s << " AND ";
      }
      col_op.OutputTo(&s);
    }
    return s.str();
  }

  std::string PartitionKeyToString(const MCList<PartitionKeyOp>& conds);

  // Lookup table from the metadata database.
  Status LookupTable(SemContext *sem_context);

  // Load table schema into symbol table.
  static void LoadSchema(SemContext *sem_context,
                         const client::YBTablePtr& table,
                         MCColumnMap* column_map,
                         bool is_index);

  // Semantic-analyzing the where clause.
  Status AnalyzeWhereClause(SemContext *sem_context);

  // Semantic-analyzing the if clause.
  Status AnalyzeIfClause(SemContext *sem_context);

  // Semantic-analyzing the USING TTL clause.
  Status AnalyzeUsingClause(SemContext *sem_context);

  // Semantic-analyzing the indexes for write operations.
  Status AnalyzeIndexesForWrites(SemContext *sem_context);

  // Protected functions.
  Status AnalyzeWhereExpr(SemContext *sem_context, PTExpr *expr);

  // Semantic-analyzing the bind variables for hash columns.
  Status AnalyzeHashColumnBindVars(SemContext *sem_context);

  // Semantic-analyzing the modified columns for inter-statement dependency.
  Status AnalyzeColumnArgs(SemContext *sem_context);

  // Does column_args_ contain static columns only (i.e. writing static column only)?
  bool StaticColumnArgsOnly() const;

  // --- The parser will decorate this node with the following information --

  const PTExprPtr where_clause_;
  const PTExprPtr if_clause_;
  const bool else_error_ = false;
  const PTDmlUsingClausePtr using_clause_;
  const bool returns_status_ = false;
  MCVector<PTBindVar*> bind_variables_;

  // -- The semantic analyzer will decorate this node with the following information --

  // Is the target table a system table?
  bool is_system_ = false;

  // Target table and column name->description map.
  client::YBTablePtr table_;
  MCColumnMap column_map_;

  // Where operator list.
  // - When reading (SELECT), key_where_ops_ has only HASH (partition) columns.
  // - When writing (UPDATE & DELETE), key_where_ops_ has both has (partition) & range columns.
  // This is just a workaround for UPDATE and DELETE. Backend supports only single row. It also
  // requires that conditions on columns are ordered the same way as they were defined in
  // CREATE TABLE statement.
  MCList<FuncOp> func_ops_;
  MCVector<ColumnOp> key_where_ops_;
  MCList<ColumnOp> where_ops_;
  MCList<MultiColumnOp> multi_col_where_ops_;
  MCList<SubscriptedColumnOp> subscripted_col_where_ops_;
  MCList<JsonColumnOp> json_col_where_ops_;

  // restrictions involving all hash/partition columns -- i.e. read requests using Token builtin
  MCList<PartitionKeyOp> partition_key_ops_;

  // List of bind variables associated with hash columns ordered by their column ids.
  PTBindVarSet hash_col_bindvars_;

  MCSharedPtr<MCVector<ColumnArg>> column_args_;
  MCSharedPtr<MCVector<SubscriptedColumnArg>> subscripted_col_args_;
  MCSharedPtr<MCVector<JsonColumnArg>> json_col_args_;

  // Columns that are being referenced by this statement. The tservers will need to read these
  // columns when processing the statements. These are different from selected columns whose values
  // must be sent back to the proxy from the tservers.
  MCSet<int32> column_refs_;
  MCSet<int32> static_column_refs_;

  // Ref count of occurrences of cols in where/if clauses. This is used to check, in case of a
  // partial index scan, if there are more refs of a column after partial index predicate covers
  // some refs of the col.
  MCUnorderedMap<int32, uint16> column_ref_cnts_;

  // TODO(neil) This should have been a resultset's row descriptor. However, because rowblock is
  // using schema, this must be declared as vector<ColumnSchema>.
  //
  // Selected schema - a vector pair<name, datatype> - is used when describing the result set.
  // NOTE: Only SELECT and DML with RETURN clause statements have outputs.
  //       We prepare this vector once at compile time and use it at execution times.
  std::shared_ptr<std::vector<ColumnSchema>> selected_schemas_;

  // The set of indexes that index primary key columns of the indexed table only and the set of
  // indexes that do not.
  MCUnorderedSet<client::YBTablePtr> pk_only_indexes_;
  MCUnorderedSet<TableId> non_pk_only_indexes_;

  // For inter-dependency analysis of DMLs in a batch/transaction
  bool modifies_primary_row_ = false;
  bool modifies_static_row_ = false;
  bool modifies_multiple_rows_ = false; // Currently only allowed for (range) deletes.

  // For optimizing SELECT queries with IN condition on hash key: does this SELECT have all primary
  // key columns set with '=' or 'IN' conditions.
  bool select_has_primary_keys_set_ = false;
  bool has_incomplete_hash_ = false;
};

}  // namespace ql
}  // namespace yb
