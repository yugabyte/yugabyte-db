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
// Tree node definitions for TRANSACTION statements.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/transaction.pb.h"

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class PTStartTransaction : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTStartTransaction> SharedPtr;
  typedef MCSharedPtr<const PTStartTransaction> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTStartTransaction(MemoryContext *memctx, YBLocationPtr loc);
  virtual ~PTStartTransaction();

  template<typename... TypeArgs>
  inline static PTStartTransaction::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTStartTransaction>(memctx, std::forward<TypeArgs>(args)...);
  }

  IsolationLevel isolation_level() const {
    return isolation_level_;
  }
  void set_isolation_level(const IsolationLevel isolation_level) {
    isolation_level_ = isolation_level;
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTStartTransaction;
  }

 private:
  IsolationLevel isolation_level_ = SNAPSHOT_ISOLATION;
};

class PTCommit : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCommit> SharedPtr;
  typedef MCSharedPtr<const PTCommit> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCommit(MemoryContext *memctx, YBLocationPtr loc);
  virtual ~PTCommit();

  template<typename... TypeArgs>
  inline static PTCommit::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTCommit>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCommit;
  }
};

}  // namespace ql
}  // namespace yb
