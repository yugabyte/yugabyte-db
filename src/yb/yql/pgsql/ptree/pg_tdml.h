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

#ifndef YB_YQL_PGSQL_PTREE_PG_TDML_H_
#define YB_YQL_PGSQL_PTREE_PG_TDML_H_

#include "yb/yql/pgsql/ptree/column_desc.h"
#include "yb/yql/pgsql/ptree/list_node.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_texpr.h"
#include "yb/yql/pgsql/ptree/pg_tbcall.h"
#include "yb/yql/pgsql/ptree/column_arg.h"

namespace yb {
namespace pgsql {

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

//--------------------------------------------------------------------------------------------------
// This class represents the data of collection type. PostgreQL syntax rules dictate how we form
// the hierarchy of our C++ classes, so classes for VALUES and SELECT clause must share the same
// base class.
// - VALUES (x, y, z)
// - (SELECT x, y, z FROM tab)
// Functionalities of this class should be "protected" to make sure that PgTCollection instances are
// not created and used by application.
class PgTCollection : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTCollection> SharedPtr;
  typedef MCSharedPtr<const PgTCollection> SharedPtrConst;

 protected:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor. Define them in protected section to prevent application from
  // declaring them.
  PgTCollection(MemoryContext *memctx, PgTLocation::SharedPtr loc)
      : TreeNode(memctx, loc) {
  }
  virtual ~PgTCollection() {
  }
};

//--------------------------------------------------------------------------------------------------

class PgTDmlStmt : public PgTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTDmlStmt(MemoryContext *memctx,
                      PgTLocation::SharedPtr loc,
                      PgTExpr::SharedPtr where_clause = nullptr);
  virtual ~PgTDmlStmt();

  template<typename... TypeArgs>
  inline static PgTDmlStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTDmlStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Lookup table from the metadata database.
  CHECKED_STATUS LookupTable(PgCompileContext *compile_context);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Semantic-analyzing the where clause.
  CHECKED_STATUS AnalyzeWhereClause(PgCompileContext *compile_context,
                                    const PgTExpr::SharedPtr& where_clause);

  // Table name.
  virtual client::YBTableName table_name() const = 0;

  // Returns location of table name.
  virtual const PgTLocation& table_loc() const = 0;

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

  int num_partition_columns() const {
    return num_partition_columns_;
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

  const MCList <yb::pgsql::FuncOp>& func_ops() const {
    return func_ops_;
  }

  const PgTExpr::SharedPtr& where_clause() const {
    return where_clause_;
  }

  // Access for column_args.
  const MCVector<ColumnArg>& column_args() const {
    CHECK(column_args_ != nullptr) << "column arguments not set up";
    return *column_args_;
  }

  bool has_column_args() const {
    return (column_args_ != nullptr && column_args_->size() > 0);
  }

  // Add column ref to be read by DocDB.
  void AddColumnRef(const ColumnDesc& col_desc) {
    column_refs_.insert(col_desc.id());
  }

  // Add all column refs to be read by DocDB.
  void AddRefForAllColumns() {
    for (const auto col_desc : table_columns_) {
      AddColumnRef(col_desc);
    }
  }

  // Access for column_refs.
  const MCSet<int32>& column_refs() const {
    return column_refs_;
  }

  bool has_column_refs() const {
    return (column_refs_.size() > 0);
  }

  // Access for selected result.
  const std::shared_ptr<vector<ColumnSchema>>& selected_schemas() const {
    return selected_schemas_;
  }

  bool IsWriteOp() {
    return opcode() == TreeNodeOpcode::kPgTInsertStmt ||
           opcode() == TreeNodeOpcode::kPgTUpdateStmt ||
           opcode() == TreeNodeOpcode::kPgTDeleteStmt;
  }

 protected:
  // Protected functions.
  CHECKED_STATUS AnalyzeWhereExpr(PgCompileContext *compile_context, PgTExpr *expr);

  // The semantic analyzer will decorate this node with the following information.
  std::shared_ptr<client::YBTable> table_;

  // Is the table a system table?
  bool is_system_;

  // Table columns.
  // TODO(neil) Currently, we use "OID" for partition columns as a HACK.  Need to correct this.
  MCVector<ColumnDesc> table_columns_;
  int num_key_columns_;
  static const int num_partition_columns_ = 1;

  // OID column
  // Argument for "oid" column (our lone hash column) is always 0.
  MCString oid_name_;
  PgTExpr::SharedPtr oid_arg_;

  // CTID column
  // Argument for "ctid" column is always now().
  MCString ctid_name_;
  PgTExpr::SharedPtr ctid_arg_;

  // Where operator list.
  // - When reading (SELECT), key_where_ops_ has only HASH (partition) columns.
  // - When writing (UPDATE & DELETE), key_where_ops_ has both has (partition) & range columns.
  // This is just a workaround for UPDATE and DELETE. Backend supports only single row. It also
  // requires that conditions on columns are ordered the same way as they were defined in
  // CREATE TABLE statement.
  MCList<FuncOp> func_ops_;
  MCVector<ColumnOp> key_where_ops_;
  MCList<ColumnOp> where_ops_;

  // restrictions involving all hash/partition columns -- i.e. read requests using Token builtin
  MCList<PartitionKeyOp> partition_key_ops_;
  PgTExpr::SharedPtr where_clause_;

  // Semantic phase will decorate the following fields.
  MCSharedPtr<MCVector<ColumnArg>> column_args_;

  // Columns that are being referenced by this statement. The tservers will need to read these
  // columns when processing the statements. These are different from selected columns whose values
  // must be sent back to the proxy from the tservers.
  MCSet<int32> column_refs_;

  // TODO(neil) This should have been a resultset's row descriptor. However, because rowblock is
  // using schema, this must be declared as vector<ColumnSchema>.
  //
  // Selected schema - a vector pair<name, datatype> - is used when describing the result set.
  // NOTE: Only SELECT and DML with RETURN clause statements have outputs.
  //       We prepare this vector once at compile time and use it at execution times.
  std::shared_ptr<vector<ColumnSchema>> selected_schemas_;

  static const PgTExpr::SharedPtr kNullPointerRef;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TDML_H_
