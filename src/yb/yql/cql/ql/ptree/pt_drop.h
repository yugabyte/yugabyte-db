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
// Tree node definitions for DROP statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// DROP <OBJECT> statement (<OBJECT> can be TABLE, KEYSPACE, etc.).

class PTDropStmt : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDropStmt> SharedPtr;
  typedef MCSharedPtr<const PTDropStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTDropStmt(MemoryContext *memctx,
             YBLocationPtr loc,
             ObjectType drop_type,
             PTQualifiedNameListNode::SharedPtr names,
             bool drop_if_exists);
  virtual ~PTDropStmt();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTDropStmt;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTDropStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDropStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  ObjectType drop_type() const {
    return drop_type_;
  }

  bool drop_if_exists() const {
    return drop_if_exists_;
  }

  // Name of the object being dropped.
  const PTQualifiedName::SharedPtr name() const {
    return names_->element(0);
  }

  client::YBTableName yb_table_name() const {
    return names_->element(0)->ToTableName();
  }

 private:
  ObjectType drop_type_;
  PTQualifiedNameListNode::SharedPtr names_;

  // Set to true for DROP IF EXISTS statements.
  bool drop_if_exists_;
};

}  // namespace ql
}  // namespace yb
