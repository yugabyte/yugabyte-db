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

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/pt_dml_write_property.h"

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
           YBLocationPtr loc,
           const PTQualifiedName::SharedPtr& lhs_,
           const PTExprPtr& rhs_,
           const PTExprListNode::SharedPtr& subscript_args = nullptr,
           const PTExprListNode::SharedPtr& json_ops = nullptr);
  virtual ~PTAssign();

  template<typename... TypeArgs>
  inline static PTAssign::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTAssign>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
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

  const PTExprListNode::SharedPtr json_ops() const {
    return json_ops_;
  }

  bool has_subscripted_column() const {
    return subscript_args_ != nullptr && subscript_args_->size() > 0;
  }

  bool has_json_ops() const {
    return json_ops_ != nullptr && json_ops_->size() > 0;
  }

  PTExprPtr rhs() {
    return rhs_;
  }

  bool require_column_read() const {
    return require_column_read_;
  }

 private:
  PTQualifiedName::SharedPtr lhs_;

  PTExprPtr rhs_;

  // for assigning specific indexes for collection columns: e.g.: lhs[key1][key2] = value
  PTExprListNode::SharedPtr subscript_args_;

  // Denotes json operators applied to a column e.g.: c1->'a'->'b'->'c'.
  PTExprListNode::SharedPtr json_ops_;

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
               YBLocationPtr loc,
               PTTableRef::SharedPtr relation,
               PTAssignListNode::SharedPtr set_clause,
               PTExprPtr where_clause,
               PTExprPtr if_clause = nullptr,
               bool else_error = false,
               PTDmlUsingClausePtr using_clause = nullptr,
               const bool return_status = false,
               PTDmlWritePropertyListNode::SharedPtr update_properties = nullptr);
  virtual ~PTUpdateStmt();

  template<typename... TypeArgs>
  inline static PTUpdateStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTUpdateStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  Status AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context);
  ExplainPlanPB AnalysisResultToPB() override;

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

  const PTDmlWritePropertyListNode::SharedPtr& update_properties() const {
    return update_properties_;
  }

  bool IsWriteOp() const override {
    return true;
  }

 private:
  // --- The parser will decorate this node with the following information --

  PTTableRef::SharedPtr relation_;
  PTAssignListNode::SharedPtr set_clause_;

  // -- The semantic analyzer will decorate this node with the following information --

  // Indicate if a column read is required to execute this update statement.
  bool require_column_read_ = false;

  // Properties added via the WITH clause in UPDATE statement.
  PTDmlWritePropertyListNode::SharedPtr update_properties_ = nullptr;
};

}  // namespace ql
}  // namespace yb
