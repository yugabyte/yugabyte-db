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

#ifndef YB_SQL_PTREE_PT_INSERT_H_
#define YB_SQL_PTREE_PT_INSERT_H_

#include "yb/client/client.h"

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_select.h"
#include "yb/sql/ptree/column_desc.h"
#include "yb/sql/ptree/pt_dml.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class PTInsertStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTInsertStmt> SharedPtr;
  typedef MCSharedPtr<const PTInsertStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTInsertStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               PTQualifiedName::SharedPtr relation,
               PTQualifiedNameListNode::SharedPtr columns,
               PTCollection::SharedPtr value_clause,
               PTExpr::SharedPtr if_clause = nullptr,
               PTExpr::SharedPtr ttl_seconds = nullptr);
  virtual ~PTInsertStmt();

  template<typename... TypeArgs>
  inline static PTInsertStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTInsertStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTInsertStmt;
  }

  // Table name.
  client::YBTableName table_name() const override {
    return relation_->ToTableName();
  }

  // Returns location of table name.
  const YBLocation& table_loc() const override {
    return relation_->loc();
  }

  // IF clause.
  const PTExpr::SharedPtr& if_clause() const {
    return if_clause_;
  }

 private:
  // The parser will constructs the following tree nodes.
  PTQualifiedName::SharedPtr relation_;
  PTQualifiedNameListNode::SharedPtr columns_;
  PTCollection::SharedPtr value_clause_;
  PTExpr::SharedPtr if_clause_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_INSERT_H_
