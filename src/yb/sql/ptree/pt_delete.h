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
// Tree node definitions for DELETE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_DELETE_H_
#define YB_SQL_PTREE_PT_DELETE_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_dml.h"
#include "yb/sql/ptree/pt_select.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class PTDeleteStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDeleteStmt> SharedPtr;
  typedef MCSharedPtr<const PTDeleteStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTDeleteStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               PTListNode::SharedPtr target,
               PTTableRef::SharedPtr relation,
               TreeNode::SharedPtr using_clause,
               PTExpr::SharedPtr where_clause,
               PTExpr::SharedPtr if_clause = nullptr);
  virtual ~PTDeleteStmt();

  template<typename... TypeArgs>
  inline static PTDeleteStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTDeleteStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

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
    return TreeNodeOpcode::kPTDeleteStmt;
  }

  CHECKED_STATUS AnalyzeTarget(TreeNode *target, SemContext *sem_context);

 private:
  PTListNode::SharedPtr target_;
  PTTableRef::SharedPtr relation_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_DELETE_H_
