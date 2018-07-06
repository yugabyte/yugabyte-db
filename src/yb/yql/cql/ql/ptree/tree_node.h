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

#ifndef YB_YQL_CQL_QL_PTREE_TREE_NODE_H_
#define YB_YQL_CQL_QL_PTREE_TREE_NODE_H_

#include "yb/yql/cql/ql/ptree/yb_location.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/util/enums.h"
#include "yb/util/status.h"
#include "yb/util/memory/mc_types.h"

namespace yb {
namespace ql {
class SemContext;

YB_DEFINE_ENUM(TreeNodeOpcode,
               (kNoOp)
               (kTreeNode)
               (kPTListNode)
               (kPTCreateKeyspace)
               (kPTUseKeyspace)
               (kPTCreateTable)
               (kPTAlterTable)
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
               (kPTGrantPermission)
               (kPTGrantRole)
               (kPTStartTransaction)
               (kPTCommit)

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
  explicit TreeNode(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr);
  virtual ~TreeNode();

  // Node type.
  virtual TreeNodeOpcode opcode() const {
    return TreeNodeOpcode::kTreeNode;
  }

  // shared_ptr support.
  template<typename... TypeArgs>
  inline static TreeNode::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<TreeNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Run semantics analysis on this node.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context);

  // Access functions to this node location.
  const YBLocation& loc() const {
    return *loc_;
  }
  void set_loc(const TreeNode& other) {
    loc_ = other.loc_;
  }
  const YBLocation::SharedPtr& loc_ptr() const {
    return loc_;
  }

 protected:
  YBLocation::SharedPtr loc_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_TREE_NODE_H_
