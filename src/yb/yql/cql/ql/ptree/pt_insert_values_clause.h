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
// Tree node definitions for INSERT INTO ... VALUES clause.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"

namespace yb {
namespace ql {

class PTInsertValuesClause : public PTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTInsertValuesClause> SharedPtr;
  typedef MCSharedPtr<const PTInsertValuesClause> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTInsertValuesClause(MemoryContext* memctx,
                       YBLocationPtr loc,
                       PTExprListNode::SharedPtr tuple);
  virtual ~PTInsertValuesClause();

  template<typename... TypeArgs>
  inline static PTInsertValuesClause::SharedPtr MakeShared(MemoryContext* memctx,
                                                           TypeArgs&& ... args) {
    return MCMakeShared<PTInsertValuesClause>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTInsertValuesClause;
  }

  // Add a tree node at the end.
  void Append(const PTExprListNode::SharedPtr& tnode);
  void Prepend(const PTExprListNode::SharedPtr& tnode);

  // Node semantics analysis.
  Status Analyze(SemContext* sem_context) override;
  void PrintSemanticAnalysisResult(SemContext* sem_context);

  // Access function for tuples_.
  const TreeListNode<PTExprListNode>& tuples() {
    return tuples_;
  }

  // Number of provided tuples.
  size_t TupleCount() const {
    return tuples_.size();
  }

  PTExprListNode::SharedPtr Tuple(int index) const;

 private:
  TreeListNode<PTExprListNode> tuples_;
};

}  // namespace ql
}  // namespace yb
