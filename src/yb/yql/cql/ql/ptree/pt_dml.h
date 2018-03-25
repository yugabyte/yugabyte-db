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

#ifndef YB_YQL_CQL_QL_PTREE_PT_DML_H_
#define YB_YQL_CQL_QL_PTREE_PT_DML_H_

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"
#include "yb/yql/cql/ql/ptree/pt_dml_using_clause.h"
#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/common/table_properties_constants.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Counter of operators on each column. "gt" includes ">" and ">=". "lt" includes "<" and "<=".
class ColumnOpCounter {
 public:
  ColumnOpCounter() : gt_count_(0), lt_count_(0), eq_count_(0), in_count_(0) { }
  int gt_count() const { return gt_count_; }
  int lt_count() const { return lt_count_; }
  int eq_count() const { return eq_count_; }
  int in_count() const { return in_count_; }

  void increase_gt() { gt_count_++; }
  void increase_lt() { lt_count_++; }
  void increase_eq() { eq_count_++; }
  void increase_in() { in_count_++; }

  bool isValid() {
    // A. at most one condition set, or
    // B. both inequality (less and greater) set together
    return (in_count_ + eq_count_ + gt_count_ + lt_count_ <= 1) ||
           (gt_count_ == 1 && lt_count_ == 1 && eq_count_ == 0 && in_count_ == 0);
  }

 private:
  int gt_count_;
  int lt_count_;
  int eq_count_;
  int in_count_;
};

// State variables for where clause.
class WhereExprState {
 public:

  WhereExprState(MCList<ColumnOp> *ops,
                 MCVector<ColumnOp> *key_ops,
                 MCList<SubscriptedColumnOp> *subscripted_col_ops,
                 MCList<PartitionKeyOp> *partition_key_ops,
                 MCVector<ColumnOpCounter> *op_counters,
                 ColumnOpCounter *partition_key_counter,
                 TreeNodeOpcode statement_type,
                 MCList<FuncOp> *func_ops)
    : ops_(ops),
      key_ops_(key_ops),
      subscripted_col_ops_(subscripted_col_ops),
      partition_key_ops_(partition_key_ops),
      op_counters_(op_counters),
      partition_key_counter_(partition_key_counter),
      statement_type_(statement_type),
      func_ops_(func_ops) {
  }

  CHECKED_STATUS AnalyzeColumnOp(SemContext *sem_context,
                                 const PTRelationExpr *expr,
                                 const ColumnDesc *col_desc,
                                 PTExpr::SharedPtr value,
                                 PTExprListNode::SharedPtr args = nullptr);

  CHECKED_STATUS AnalyzeColumnFunction(SemContext *sem_context,
                                       const PTRelationExpr *expr,
                                       PTExpr::SharedPtr value,
                                       PTBcall::SharedPtr call);

  CHECKED_STATUS AnalyzePartitionKeyOp(SemContext *sem_context,
                                       const PTRelationExpr *expr,
                                       PTExpr::SharedPtr value);

  MCList<FuncOp> *func_ops() {
    return func_ops_;
  }

 private:
  MCList<ColumnOp> *ops_;

  // Operators on key columns.
  MCVector<ColumnOp> *key_ops_;

  // Operators on subscripted columns (e.g. mp['x'] or lst[2]['x'])
  MCList<SubscriptedColumnOp> *subscripted_col_ops_;

  MCList<PartitionKeyOp> *partition_key_ops_;

  // Counters of '=', '<', and '>' operators for each column in the where expression.
  MCVector<ColumnOpCounter> *op_counters_;

  // Counters on conditions on the partition key (i.e. using `token`)
  ColumnOpCounter *partition_key_counter_;

  // update, insert, delete, select.
  TreeNodeOpcode statement_type_;

  MCList<FuncOp> *func_ops_;

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

 protected:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor. Define them in protected section to prevent application from
  // declaring them.
  PTCollection(MemoryContext *memctx, YBLocation::SharedPtr loc)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTCollection() {
  }
};

//--------------------------------------------------------------------------------------------------

class PTDmlStmt : public PTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTDmlStmt(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     PTExpr::SharedPtr where_clause = nullptr,
                     PTExpr::SharedPtr if_clause = nullptr,
                     PTDmlUsingClause::SharedPtr using_clause = nullptr);
  virtual ~PTDmlStmt();

  template<typename... TypeArgs>
  inline static PTDmlStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDmlStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Lookup table from the metadata database.
  CHECKED_STATUS LookupTable(SemContext *sem_context);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

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

  const MCVector<ColumnDesc>& table_columns() const {
    return table_columns_;
  }

  int num_columns() const {
    return table_columns_.size();
  }

  int num_key_columns() const {
    return num_key_columns_;
  }

  int num_hash_key_columns() const {
    return num_hash_key_columns_;
  }

  const MCVector<ColumnOp>& key_where_ops() const {
    return key_where_ops_;
  }

  const MCList<ColumnOp>& where_ops() const {
    return where_ops_;
  }

  const MCList<SubscriptedColumnOp>& subscripted_col_where_ops() const {
    return subscripted_col_where_ops_;
  }

  const MCList<PartitionKeyOp>& partition_key_ops() const {
    return partition_key_ops_;
  }

  const MCList <yb::ql::FuncOp>& func_ops() const {
    return func_ops_;
  }

  const PTExpr::SharedPtr& where_clause() const {
    return where_clause_;
  }

  const PTExpr::SharedPtr& if_clause() const {
    return if_clause_;
  }

  const PTExpr::SharedPtr& ttl_seconds() const {
    return using_clause_ != nullptr ? using_clause_->ttl_seconds() : kNullPointerRef;
  }

  const PTExpr::SharedPtr& user_timestamp_usec() const {
    return using_clause_ != nullptr ? using_clause_->user_timestamp_usec() : kNullPointerRef;
  }

  const MCVector<PTBindVar*> &bind_variables() const {
    return bind_variables_;
  }
  MCVector<PTBindVar*> &bind_variables() {
    return bind_variables_;
  }

  std::vector<int64_t> hash_col_indices() const {
    std::vector<int64_t> indices;
    indices.reserve(hash_col_bindvars_.size());
    for (const PTBindVar* bindvar : hash_col_bindvars_) {
      indices.emplace_back(bindvar->pos());
    }
    return indices;
  }

  // Access for column_args.
  const MCVector<ColumnArg>& column_args() const {
    CHECK(column_args_ != nullptr) << "column arguments not set up";
    return *column_args_;
  }

  // Add column ref to be read by DocDB.
  void AddColumnRef(const ColumnDesc& col_desc) {
    if (col_desc.is_static()) {
      static_column_refs_.insert(col_desc.id());
    } else {
      column_refs_.insert(col_desc.id());
    }
  }

  // Add column ref to be read.
  void AddHashColumnBindVar(PTBindVar* bindvar) {
    hash_col_bindvars_.insert(bindvar);
  }

  // Add all column refs to be read by DocDB.
  void AddRefForAllColumns() {
    for (const auto col_desc : table_columns_) {
      AddColumnRef(col_desc);
    }
  }

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

  // Access for selected result.
  const std::shared_ptr<vector<ColumnSchema>>& selected_schemas() const {
    return selected_schemas_;
  }

  bool IsWriteOp() const {
    return opcode() == TreeNodeOpcode::kPTInsertStmt ||
           opcode() == TreeNodeOpcode::kPTUpdateStmt ||
           opcode() == TreeNodeOpcode::kPTDeleteStmt;
  }

  bool RequireTransaction() const;

  const MCUnorderedMap<TableId, std::shared_ptr<client::YBTable>>& pk_only_indexes() const {
    return pk_only_indexes_;
  }

  const MCUnorderedSet<TableId>& non_pk_only_indexes() const {
    return non_pk_only_indexes_;
  }

  // For inter-statement dependency analysis -
  // Does this DML modify the hash or primary key?
  bool ModifiesHashKey() const {
    return modifies_hash_key_;
  }
  bool ModifiesPrimaryKey() const {
    return modifies_primary_key_;
  }
  // Does this DML read from the hash or primary key?
  bool ReadsHashKey(const bool has_usertimestamp) const {
    // A DML reads from the hash key if it reads a static column, or it modifies the hash key and
    // has a user-defined timestamp (which DocDB will require a read-modify-write by the timestamp).
    return !static_column_refs_.empty() || modifies_hash_key_ && has_usertimestamp;
  }
  bool ReadsPrimaryKey(const bool has_usertimestamp) const {
    // A DML reads from the primary key if there is a IF clause (TODO differentiate the case where
    // the IF clause references static columns only) or otherwise reads a non-static column (e.g.
    // counter update), or it modifies the primary key and has a user-defined timestamp (which
    // DocDB will require a read-modify-write by the timestamp).
    return if_clause_ != nullptr || !column_refs_.empty() ||
        modifies_primary_key_ && has_usertimestamp;
  }

 protected:
  // Semantic-analyzing the where clause.
  CHECKED_STATUS AnalyzeWhereClause(SemContext *sem_context, const PTExpr::SharedPtr& where_clause);

  // Semantic-analyzing the if clause.
  CHECKED_STATUS AnalyzeIfClause(SemContext *sem_context, const PTExpr::SharedPtr& if_clause);

  // Semantic-analyzing the USING TTL clause.
  CHECKED_STATUS AnalyzeUsingClause(SemContext *sem_context);

  // Semantic-analyzing the indexes for write operations.
  CHECKED_STATUS AnalyzeIndexesForWrites(SemContext *sem_context);

  // Protected functions.
  CHECKED_STATUS AnalyzeWhereExpr(SemContext *sem_context, PTExpr *expr);

  // Semantic-analyzing the bind variables for hash columns.
  CHECKED_STATUS AnalyzeHashColumnBindVars(SemContext *sem_context);

  // Semantic-analyzing statement for inter-statement dependency.
  CHECKED_STATUS AnalyzeInterDependency(SemContext *sem_context);

  // Does column_args_ contain static columns only (i.e. writing static column only)?
  bool StaticColumnArgsOnly() const;

  // --- The parser will decorate this node with the following information --

  const PTExpr::SharedPtr where_clause_;
  const PTExpr::SharedPtr if_clause_;
  const PTDmlUsingClause::SharedPtr using_clause_;
  MCVector<PTBindVar*> bind_variables_;

  // -- The semantic analyzer will decorate this node with the following information --

  std::shared_ptr<client::YBTable> table_;
  bool is_system_ = false; // Is the table a system table?
  MCVector<ColumnDesc> table_columns_; // Table columns.
  int num_key_columns_ = 0;
  int num_hash_key_columns_ = 0;

  // Where operator list.
  // - When reading (SELECT), key_where_ops_ has only HASH (partition) columns.
  // - When writing (UPDATE & DELETE), key_where_ops_ has both has (partition) & range columns.
  // This is just a workaround for UPDATE and DELETE. Backend supports only single row. It also
  // requires that conditions on columns are ordered the same way as they were defined in
  // CREATE TABLE statement.
  MCList<FuncOp> func_ops_;
  MCVector<ColumnOp> key_where_ops_;
  MCList<ColumnOp> where_ops_;
  MCList<SubscriptedColumnOp> subscripted_col_where_ops_;

  // restrictions involving all hash/partition columns -- i.e. read requests using Token builtin
  MCList<PartitionKeyOp> partition_key_ops_;

  // List of bind variables associated with hash columns ordered by their column ids.
  MCSet<PTBindVar*, PTBindVar::HashColCmp> hash_col_bindvars_;

  MCSharedPtr<MCVector<ColumnArg>> column_args_;
  MCSharedPtr<MCVector<SubscriptedColumnArg>> subscripted_col_args_;

  // Columns that are being referenced by this statement. The tservers will need to read these
  // columns when processing the statements. These are different from selected columns whose values
  // must be sent back to the proxy from the tservers.
  MCSet<int32> column_refs_;
  MCSet<int32> static_column_refs_;

  // TODO(neil) This should have been a resultset's row descriptor. However, because rowblock is
  // using schema, this must be declared as vector<ColumnSchema>.
  //
  // Selected schema - a vector pair<name, datatype> - is used when describing the result set.
  // NOTE: Only SELECT and DML with RETURN clause statements have outputs.
  //       We prepare this vector once at compile time and use it at execution times.
  std::shared_ptr<vector<ColumnSchema>> selected_schemas_;

  // The map of index ids that index primary key columns of the indexed table only and the
  // corresponding tables, and the set of indexes that do not.
  MCUnorderedMap<TableId, std::shared_ptr<client::YBTable>> pk_only_indexes_;
  MCUnorderedSet<TableId> non_pk_only_indexes_;

  // For inter-dependency analysis of DMLs in a batch/transaction: does this DML modify the hash
  // hash or primary key?
  bool modifies_hash_key_ = false;
  bool modifies_primary_key_ = false;

  static const PTExpr::SharedPtr kNullPointerRef;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_DML_H_
