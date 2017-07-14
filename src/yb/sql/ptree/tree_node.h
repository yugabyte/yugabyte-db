//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//
// This modules includes declarations of the base class for tree nodes. Parser whose rules are
// defined in parser_gram.y will create these nodes and link them together to form parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_TREE_NODE_H_
#define YB_SQL_PTREE_TREE_NODE_H_

#include "yb/sql/ptree/yb_location.h"
#include "yb/sql/ptree/pt_option.h"
#include "yb/sql/util/errcodes.h"
#include "yb/util/status.h"
#include "yb/util/memory/mc_types.h"

namespace yb {
namespace sql {
class SemContext;

enum class TreeNodeOpcode {
  kNoOp = 0,
  kTreeNode,
  kPTListNode,
  kPTCreateKeyspace,
  kPTUseKeyspace,
  kPTCreateTable,
  kPTAlterTable,
  kPTCreateType,
  kPTDropStmt,
  kPTSelectStmt,
  kPTInsertStmt,
  kPTDeleteStmt,
  kPTUpdateStmt,

  // Expressions.
  kPTExpr,
  kPTRef,
  kPTSubscript,
  kPTAssign,
  kPTBindVar,
};

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

  // Reset to clear and release previous semantics analysis results.
  virtual void Reset() { /* no-op */ }

  // Run semantics analysis on this node.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context);

  // Access function to this node location.
  const YBLocation& loc() const {
    return *loc_;
  }

 protected:
  YBLocation::SharedPtr loc_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_TREE_NODE_H_
