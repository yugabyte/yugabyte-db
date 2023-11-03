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

#pragma once

#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

// USING clause for INSERT, UPDATE and DELETE statements.
class PTDmlUsingClauseElement : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDmlUsingClauseElement> SharedPtr;
  typedef MCSharedPtr<const PTDmlUsingClauseElement> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructors and destructor.
  PTDmlUsingClauseElement(MemoryContext *memctx,
                          YBLocationPtr loc,
                          const MCSharedPtr<MCString>& name,
                          const PTExprPtr& value);

  virtual ~PTDmlUsingClauseElement();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTDmlUsingClauseElement;
  }

  template<typename... TypeArgs>
  inline static PTDmlUsingClauseElement::SharedPtr MakeShared(MemoryContext *memctx,
                                                       TypeArgs&&... args) {
    return MCMakeShared<PTDmlUsingClauseElement>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  const PTExprPtr value() {
    return value_;
  }

  bool IsTTL() const;

  bool IsTimestamp() const;

 private:
  const MCSharedPtr<MCString> name_;
  const PTExprPtr value_;
};

} // namespace ql
} // namespace yb
