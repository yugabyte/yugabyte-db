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
// Tree node definitions for UPDATE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TUPDATE_H_
#define YB_YQL_PGSQL_PTREE_PG_TUPDATE_H_

#include "yb/yql/pgsql/ptree/list_node.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_tdml.h"
#include "yb/yql/pgsql/ptree/pg_tselect.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

class PgTAssign : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTAssign> SharedPtr;
  typedef MCSharedPtr<const PgTAssign> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTAssign(MemoryContext *memctx,
           PgTLocation::SharedPtr loc,
           const PgTQualifiedName::SharedPtr& lhs_,
           const PgTExpr::SharedPtr& rhs_);
  virtual ~PgTAssign();

  template<typename... TypeArgs>
  inline static PgTAssign::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PgTAssign>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTAssign;
  }

  const ColumnDesc *col_desc() const {
    return col_desc_;
  }

  PgTExpr::SharedPtr rhs() {
    return rhs_;
  }

  bool require_column_read() const {
    return require_column_read_;
  }

 private:
  PgTQualifiedName::SharedPtr lhs_;

  PgTExpr::SharedPtr rhs_;

  // Semantic phase will fill in this value.
  const ColumnDesc *col_desc_;

  // Indicate if a column read is required to execute this assign statement.
  bool require_column_read_ = false;
};

using PgTAssignListNode = TreeListNode<PgTAssign>;

//--------------------------------------------------------------------------------------------------

class PgTUpdateStmt : public PgTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTUpdateStmt> SharedPtr;
  typedef MCSharedPtr<const PgTUpdateStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTUpdateStmt(MemoryContext *memctx,
               PgTLocation::SharedPtr loc,
               PgTTableRef::SharedPtr relation,
               PgTAssignListNode::SharedPtr set_clause,
               PgTExpr::SharedPtr where_clause);
  virtual ~PgTUpdateStmt();

  template<typename... TypeArgs>
  inline static PgTUpdateStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PgTUpdateStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;
  CHECKED_STATUS AnalyzeSetExpr(PgTAssign *assign_expr, PgCompileContext *compile_context);

  // Table name.
  client::YBTableName table_name() const override {
    return relation_->table_name();
  }

  // Returns location of table name.
  const PgTLocation& table_loc() const override {
    return relation_->loc();
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTUpdateStmt;
  }

  bool require_column_read() const {
    return require_column_read_;
  }

  const PgTAssignListNode::SharedPtr& set_clause() const {
    return set_clause_;
  }

 private:
  PgTTableRef::SharedPtr relation_;
  PgTAssignListNode::SharedPtr set_clause_;

  // Indicate if a column read is required to execute this update statement.
  bool require_column_read_ = false;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TUPDATE_H_
