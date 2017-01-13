//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// List Node Declaration.
//
// This modules includes specifications for nodes that contain a list of tree node.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_LIST_NODE_H_
#define YB_SQL_PTREE_LIST_NODE_H_

#include "yb/sql/ptree/tree_node.h"

namespace yb {
namespace sql {

// Operations that apply to each treenode of this list.
template<typename ContextType, typename NodeType = TreeNode>
using TreeNodeOperator = std::function<Status(NodeType&, ContextType*)>;

template<typename ContextType, typename NodeType = TreeNode>
using TreeNodePtrOperator = std::function<Status(NodeType*, ContextType*)>;

// TreeNode base class.
template<typename NodeType = TreeNode, TreeNodeOpcode op = TreeNodeOpcode::kPTListNode>
class TreeListNode : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<TreeListNode> SharedPtr;
  typedef MCSharedPtr<const TreeListNode> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit TreeListNode(MemoryContext *memory_context,
                        YBLocation::SharedPtr loc,
                        const MCSharedPtr<NodeType>& tnode = nullptr)
      : TreeNode(memory_context, loc),
        node_list_(memory_context) {
    Append(tnode);
  }
  virtual ~TreeListNode() {
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return op;
  }

  // Add a tree node at the end.
  void Append(const MCSharedPtr<NodeType>& tnode) {
    if (tnode != nullptr) {
      node_list_.push_back(tnode);
    }
  }

  // Add a tree node at the beginning.
  void Prepend(const MCSharedPtr<NodeType>& tnode) {
    if (tnode != nullptr) {
      node_list_.push_front(tnode);
    }
  }

  template<typename... TypeArgs>
  inline static TreeListNode::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<TreeListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Run semantics analysis on this node.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE {
    for (auto tnode : node_list_) {
      RETURN_NOT_OK(tnode->Analyze(sem_context));
    }
    return Status::OK();
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context,
                                 TreeNodePtrOperator<SemContext, NodeType> node_op) {
    for (auto tnode : node_list_) {
      RETURN_NOT_OK(node_op(tnode.get(), sem_context));
    }
    return Status::OK();
  }

  // Apply an operator on each node in the list.
  template<typename ContextType, typename DerivedType = NodeType>
  CHECKED_STATUS Apply(ContextType *context,
                       TreeNodeOperator<ContextType, DerivedType> node_op,
                       int max_nested_level = 0,
                       int max_nested_count = 0,
                       TreeNodeOperator<ContextType, DerivedType> nested_node_op = nullptr) {

    int nested_level = 0;
    int nested_count = 0;

    for (auto tnode : node_list_) {
      if (tnode->opcode() != TreeNodeOpcode::kPTListNode) {
        // Cast the node from (TreeNode*) to the given template type.
        DerivedType *node = static_cast<DerivedType*>(tnode.get());
        // Call the given node operation on the node.
        RETURN_NOT_OK(node_op(*node, context));

      } else {
        if (++nested_count > max_nested_count) {
          return context->Error(loc(), "Number of nested lists exceeds allowable limit",
                                ErrorCode::SYNTAX_ERROR);
        }

        if (++nested_level > max_nested_level) {
          return context->Error(loc(), "Nested level of parenthesis exceeds allowable limit",
                                ErrorCode::SYNTAX_ERROR);
        }

        // Cast the node from (TreeNode*) to the given template type.
        TreeListNode *node = static_cast<TreeListNode*>(tnode.get());
        // Apply the operation to a nested list.
        RETURN_NOT_OK((node->Apply<ContextType, DerivedType>(context,
                                                             nested_node_op,
                                                             max_nested_level - 1,
                                                             max_nested_count,
                                                             nested_node_op)));
        nested_level--;
      }
    }

    return Status::OK();
  }

  // List count.
  int size() const {
    return node_list_.size();
  }

  // Access function to node_list_.
  const MCList<MCSharedPtr<NodeType>>& node_list() const {
    return node_list_;
  }

  // Returns the nth element.
  MCSharedPtr<NodeType> element(int n) const {
    DCHECK_GE(n, 0);
    for (const MCSharedPtr<NodeType>& tnode : node_list_) {
      if (n == 0) {
        return tnode;
      }
      n--;
    }
    return nullptr;
  }

 private:
  MCList<MCSharedPtr<NodeType>> node_list_;
};

using PTListNode = TreeListNode<>;

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_LIST_NODE_H_
