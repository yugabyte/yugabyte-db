//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for INSERT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_DML_H_
#define YB_SQL_PTREE_PT_DML_H_

#include "yb/client/client.h"

#include "yb/sql/ptree/column_desc.h"
#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/pt_bcall.h"
#include "yb/sql/ptree/column_arg.h"
#include "yb/common/table_properties_constants.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// Counter of operators on each column. "gt" includes ">" and ">=". "lt" includes "<" and "<=".
class ColumnOpCounter {
 public:
  ColumnOpCounter() : gt_count_(0), lt_count_(0), eq_count_(0) { }
  int gt_count() const { return gt_count_; }
  int lt_count() const { return lt_count_; }
  int eq_count() const { return eq_count_; }

  void increase_gt() { gt_count_++; }
  void increase_lt() { lt_count_++; }
  void increase_eq() { eq_count_++; }

  bool isValid() {
    // 1. each condition type can be set at most once
    // 2. equality and inequality (less/greater) conditions cannot appear together
    return (gt_count_ <= 1 && lt_count_ <= 1 && eq_count_ <= 1) &&
           (eq_count_ == 0 || gt_count_ == 0 && lt_count_ == 0);
  }

 private:
  int gt_count_;
  int lt_count_;
  int eq_count_;
};

// State variables for where clause.
class WhereExprState {
 public:
  WhereExprState(MCList<ColumnOp> *ops,
                 MCVector<ColumnOp> *key_ops,
                 MCList<PartitionKeyOp> *partition_key_ops,
                 MCVector<ColumnOpCounter> *op_counters,
                 ColumnOpCounter *partition_key_counter,
                 bool write_only)
      : ops_(ops),
        key_ops_(key_ops),
        partition_key_ops_(partition_key_ops),
        op_counters_(op_counters),
        partition_key_counter_(partition_key_counter),
        write_only_(write_only) {
  }

  CHECKED_STATUS AnalyzeColumnOp(SemContext *sem_context,
                                 const PTRelationExpr *expr,
                                 const ColumnDesc *col_desc,
                                 PTExpr::SharedPtr value);


  CHECKED_STATUS AnalyzePartitionKeyOp(SemContext *sem_context,
                                       const PTRelationExpr *expr,
                                       PTExpr::SharedPtr value);

 private:
  // Operators on all columns.
  MCList<ColumnOp> *ops_;

  // Operators on key columns.
  MCVector<ColumnOp> *key_ops_;

  MCList<PartitionKeyOp> *partition_key_ops_;

  // Counters of '=', '<', and '>' operators for each column in the where expression.
  MCVector<ColumnOpCounter> *op_counters_;

  // conters on conditions on the partition key (i.e. using `token`)
  ColumnOpCounter *partition_key_counter_;

  // update, insert, delete.
  bool write_only_;
};

//--------------------------------------------------------------------------------------------------
// This class represents the data of collection type. PostgreSql syntax rules dictate how we form
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
                     bool write_only,
                     PTExpr::SharedPtr ttl_seconds = nullptr);
  virtual ~PTDmlStmt();

  template<typename... TypeArgs>
  inline static PTDmlStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDmlStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Lookup table from the metadata database.
  CHECKED_STATUS LookupTable(SemContext *sem_context);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  // Semantic-analyzing the where clause.
  CHECKED_STATUS AnalyzeWhereClause(SemContext *sem_context, const PTExpr::SharedPtr& where_clause);

  // Semantic-analyzing the if clause.
  CHECKED_STATUS AnalyzeIfClause(SemContext *sem_context, const PTExpr::SharedPtr& if_clause);

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

  const MCList<PartitionKeyOp>& partition_key_ops() const {
    return partition_key_ops_;
  }

  bool has_ttl() const {
    return ttl_seconds_ != nullptr;
  }

  PTExpr::SharedPtr ttl_seconds() const {
    CHECK_NOTNULL(ttl_seconds_.get());
    return ttl_seconds_;
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

  // Add column ref to be read.
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

  // Access for column_args.
  const MCSet<int32>& column_refs() const {
    return column_refs_;
  }

  // Access for column_args.
  const MCSet<int32>& static_column_refs() const {
    return static_column_refs_;
  }

  // Reset to clear and release previous semantics analysis results.
  virtual void Reset() override;

 protected:
  // Protected functions.
  CHECKED_STATUS AnalyzeWhereExpr(SemContext *sem_context, PTExpr *expr);

  // Semantic-analyzing the USING TTL clause.
  CHECKED_STATUS AnalyzeUsingClause(SemContext *sem_context);

  // Semantic-analyzing the bind variables for hash columns.
  CHECKED_STATUS AnalyzeHashColumnBindVars(SemContext *sem_context);

  // Does column_args_ contain static columns only (i.e. writing static column only)?
  bool StaticColumnArgsOnly() const;

  // The sematic analyzer will decorate this node with the following information.
  std::shared_ptr<client::YBTable> table_;

  // Is the table a system table?
  bool is_system_;

  // Table columns.
  MCVector<ColumnDesc> table_columns_;
  int num_key_columns_;
  int num_hash_key_columns_;

  // Where operator list.
  // - When reading (SELECT), key_where_ops_ has only HASH (partition) columns.
  // - When writing (UPDATE & DELETE), key_where_ops_ has both has (partition) & range columns.
  // This is just a workaround for UPDATE and DELETE. Backend supports only single row. It also
  // requires that conditions on columns are ordered the same way as they were defined in
  // CREATE TABLE statement.
  MCVector<ColumnOp> key_where_ops_;
  MCList<ColumnOp> where_ops_;

  // restrictions involving all hash/partition columns -- i.e. read requests using Token builtin
  MCList<PartitionKeyOp> partition_key_ops_;

  // Predicate for write operator (UPDATE & DELETE).
  bool write_only_;
  PTExpr::SharedPtr ttl_seconds_;

  // Bind variables set up by during parsing.
  MCVector<PTBindVar*> bind_variables_;

  // List of bind variables associated with hash columns ordered by their column ids.
  MCSet<PTBindVar*, PTBindVar::HashColCmp> hash_col_bindvars_;

  // Semantic phase will decorate the following fields.
  MCSharedPtr<MCVector<ColumnArg>> column_args_;

  // Columns that are being referenced by this statement. The tservers will need to read these
  // columns when processing the statements. These are different from selected columns whose values
  // must be sent back to the proxy from the tservers.
  MCSet<int32> column_refs_;
  MCSet<int32> static_column_refs_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_DML_H_
