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
// Tree node definitions for ALTER KEYSPACE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_keyspace_property.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// ALTER KEYSPACE statement.

class PTAlterKeyspace : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTAlterKeyspace> SharedPtr;
  typedef MCSharedPtr<const PTAlterKeyspace> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTAlterKeyspace(MemoryContext *memctx,
                  YBLocationPtr loc,
                  const MCSharedPtr<MCString>& name,
                  const PTKeyspacePropertyListNode::SharedPtr& keyspace_properties);
  virtual ~PTAlterKeyspace();

  template<typename... TypeArgs>
  inline static PTAlterKeyspace::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTAlterKeyspace>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTAlterKeyspace;
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Keyspace name.
  const char* name() const {
    return name_->c_str();
  }

  PTKeyspacePropertyListNode::SharedPtr keyspace_properties() const {
    return keyspace_properties_;
  }

 private:
  MCSharedPtr<MCString> name_;
  const PTKeyspacePropertyListNode::SharedPtr keyspace_properties_;
};

}  // namespace ql
}  // namespace yb
