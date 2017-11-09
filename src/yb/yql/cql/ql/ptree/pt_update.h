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

#ifndef YB_YQL_CQL_QL_PTREE_PT_UPDATE_H_
#define YB_YQL_CQL_QL_PTREE_PT_UPDATE_H_

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class PTAssign : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTAssign> SharedPtr;
  typedef MCSharedPtr<const PTAssign> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTAssign(MemoryContext *memctx,
           YBLocation::SharedPtr loc,
           const PTQualifiedName::SharedPtr& lhs_,
           const PTExpr::SharedPtr& rhs_,
           const PTExprListNode::SharedPtr& = nullptr);
  virtual ~PTAssign();

  template<typename... TypeArgs>
  inline static PTAssign::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTAssign>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTAssign;
  }

  const ColumnDesc *col_desc() const {
    return col_desc_;
  }

  const PTExprListNode::SharedPtr subscript_args() const {
    return subscript_args_;
  }

  bool has_subscripted_column() const {
    return subscript_args_ != nullptr && subscript_args_->size() > 0;
  }

  PTExpr::SharedPtr rhs() {
    return rhs_;
  }

  bool require_column_read() const {
    return require_column_read_;
  }

 private:
  PTQualifiedName::SharedPtr lhs_;

  PTExpr::SharedPtr rhs_;

  // for assigning specific indexes for collection columns: e.g.: lhs[key1][key2] = value
  PTExprListNode::SharedPtr subscript_args_;

  // Semantic phase will fill in this value.
  const ColumnDesc *col_desc_;

  // Indicate if a column read is required to execute this assign statement.
  bool require_column_read_ = false;
};

using PTAssignListNode = TreeListNode<PTAssign>;

//--------------------------------------------------------------------------------------------------

class PTUpdateStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTUpdateStmt> SharedPtr;
  typedef MCSharedPtr<const PTUpdateStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTUpdateStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               PTTableRef::SharedPtr relation,
               PTAssignListNode::SharedPtr set_clause,
               PTExpr::SharedPtr where_clause,
               PTExpr::SharedPtr if_clause = nullptr,
               PTDmlUsingClause::SharedPtr using_clause = nullptr);
  virtual ~PTUpdateStmt();

  template<typename... TypeArgs>
  inline static PTUpdateStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTUpdateStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  CHECKED_STATUS AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context);

  // Table name.
  client::YBTableName table_name() const override {
    return relation_->table_name();
  }

  // Returns location of table name.
  const YBLocation& table_loc() const override {
    return relation_->loc();
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTUpdateStmt;
  }

  bool require_column_read() const {
    return require_column_read_;
  }

  const PTAssignListNode::SharedPtr& set_clause() const {
    return set_clause_;
  }

 private:
  PTTableRef::SharedPtr relation_;
  PTAssignListNode::SharedPtr set_clause_;

  // Indicate if a column read is required to execute this update statement.
  bool require_column_read_ = false;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_UPDATE_H_
