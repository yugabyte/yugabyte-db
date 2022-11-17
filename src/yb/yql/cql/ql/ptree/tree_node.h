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
// Parse Tree Declaration.
//
// This modules includes declarations of the base class for tree nodes. Parser whose rules are
// defined in parser_gram.y will create these nodes and link them together to form parse tree.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/util/enums.h"

#include "yb/util/memory/mc_types.h"

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace ql {

class SemContext;

YB_DEFINE_ENUM(TreeNodeOpcode,
               (kNoOp)
               (kPTListNode)
               (kPTCreateKeyspace)
               (kPTUseKeyspace)
               (kPTAlterKeyspace)
               (kPTCreateTable)
               (kPTAlterTable)
               (kPTTypeField)
               (kPTCreateType)
               (kPTCreateIndex)
               (kPTTruncateStmt)
               (kPTDropStmt)
               (kPTSelectStmt)
               (kPTInsertStmt)
               (kPTDeleteStmt)
               (kPTUpdateStmt)
               (kPTCreateRole)
               (kPTAlterRole)
               (kPTGrantRevokePermission)
               (kPTGrantRevokeRole)
               (kPTStartTransaction)
               (kPTCommit)
               (kPTName)
               (kPTProperty)
               (kPTStatic)
               (kPTConstraint)
               (kPTCollection)
               (kPTPrimitiveType)
               (kPTColumnDefinition)
               (kPTAlterColumnDefinition)
               (kPTDmlUsingClauseElement)
               (kPTTableRef)
               (kPTOrderBy)
               (kPTRoleOption)
               (kPTExplainStmt)
               (kPTInsertValuesClause)
               (kPTInsertJsonClause)

               // Expressions.
               (kPTExpr)
               (kPTRef)
               (kPTSubscript)
               (kPTAllColumns)
               (kPTAssign)
               (kPTBindVar)
               (kPTJsonOp));

// TreeNode base class.
class TreeNode : public MCBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<TreeNode> SharedPtr;
  typedef MCSharedPtr<const TreeNode> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit TreeNode(MemoryContext *memctx = nullptr, YBLocationPtr loc = nullptr);
  virtual ~TreeNode();

  // Node type.
  virtual TreeNodeOpcode opcode() const = 0;

  // shared_ptr support.
  template<typename... TypeArgs>
  inline static TreeNode::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<TreeNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Run semantics analysis on this node.
  virtual Status Analyze(SemContext *sem_context);

  // Is this a DML statement?
  virtual bool IsDml() const {
    return false;
  }

  // Is this treenode a top level node that is used to query data from tablet server?
  virtual bool IsTopLevelReadNode() const {
    return false;
  }

  // Access functions to this node location.
  const YBLocation& loc() const {
    return *loc_;
  }
  void set_loc(const TreeNode& other) {
    loc_ = other.loc_;
  }
  const YBLocationPtr& loc_ptr() const {
    return loc_;
  }

  void set_internal() {
    internal_ = true;
  }

 protected:
  YBLocationPtr loc_;

  bool internal_ = false;
};

}  // namespace ql
}  // namespace yb
