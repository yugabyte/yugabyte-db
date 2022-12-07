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
// Tree node definitions for TRUNCATE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// TRUNCATE statement.

class PTTruncateStmt : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTruncateStmt> SharedPtr;
  typedef MCSharedPtr<const PTTruncateStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTTruncateStmt(MemoryContext *memctx,
                 YBLocationPtr loc,
                 PTQualifiedNameListNode::SharedPtr names);
  virtual ~PTTruncateStmt();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTTruncateStmt;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTTruncateStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTTruncateStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Name of the table being truncated.
  const PTQualifiedName::SharedPtr name() const {
    return names_->element(0);
  }

  // Id of the table being truncated.
  const std::string& table_id() const;

  client::YBTableName yb_table_name() const {
    return names_->element(0)->ToTableName();
  }

  const std::shared_ptr<client::YBTable>& table() const {
    return table_;
  }

 private:
  PTQualifiedNameListNode::SharedPtr names_;

  // The semantic analyzer will decorate the following information.
  std::shared_ptr<client::YBTable> table_;
};

}  // namespace ql
}  // namespace yb
