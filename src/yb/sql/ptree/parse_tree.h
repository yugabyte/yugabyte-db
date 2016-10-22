//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//
// This modules includes declarations for parse tree and base class for tree nodes. The parser
// (parser_gram.y) will create these nodes and link them together to from parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PARSE_TREE_H_
#define YB_SQL_PTREE_PARSE_TREE_H_

#include "yb/sql/util/base_types.h"

namespace yb {
namespace sql {

// TreeNode base class.
class TreeNode : public MCBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<TreeNode> SharedPtr;
  typedef MCSharedPtr<const TreeNode> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit TreeNode(MemoryContext *memctx = nullptr);
  virtual ~TreeNode();

  template<typename... TypeArgs>
  inline static TreeNode::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<TreeNode>(memctx, std::forward<TypeArgs>(args)...);
  }
};
using TreeNodeList = MCList<TreeNode::SharedPtr>;

// TreeNode base class.
class PTListNode : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTListNode> SharedPtr;
  typedef MCSharedPtr<const PTListNode> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit PTListNode(MemoryContext *memory_context);
  virtual ~PTListNode();

  void Append(const TreeNode::SharedPtr& tnode);
  void Prepend(const TreeNode::SharedPtr& tnode);

  template<typename... TypeArgs>
  inline static PTListNode::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  TreeNodeList node_list_;
};

// Parse Tree
class ParseTree {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ParseTree> UniPtr;
  typedef std::unique_ptr<const ParseTree> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  ParseTree();
  ~ParseTree();

  // Access functions to root_.
  void set_root(const TreeNode::SharedPtr& root) {
    root_ = root;
  }
  const TreeNode::SharedPtr& root() {
    return root_;
  }

  // Access function to ptree_mem_.
  MemoryContext *PTreeMem() const {
    return ptree_mem_.get();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Private data members.
  TreeNode::SharedPtr root_;

  // Parse tree memory pool. This pool is used to allocate parse tree and its nodes. This pool
  // should be part of the generated parse tree that is stored within parse_context. Once the
  // parse tree is destructed, it's also gone.
  MemoryContext::UniPtr ptree_mem_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PARSE_TREE_H_
