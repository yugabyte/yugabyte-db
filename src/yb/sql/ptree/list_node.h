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

// List of tree nodes.
using TreeNodeList = MCList<TreeNode::SharedPtr>;

// Operations that apply to each treenode of this list.
template<typename ContextType, typename NodeType = TreeNode>
using TreeNodeOperator = std::function<ErrorCode(NodeType&, ContextType*)>;

// TreeNode base class.
class PTListNode : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTListNode> SharedPtr;
  typedef MCSharedPtr<const PTListNode> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit PTListNode(MemoryContext *memory_context, YBLocation::SharedPtr loc);
  virtual ~PTListNode();

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTListNode;
  }

  // Add a tree node at the end.
  void Append(const TreeNode::SharedPtr& tnode);

  // Add a tree node at the beginning.
  void Prepend(const TreeNode::SharedPtr& tnode);

  template<typename... TypeArgs>
  inline static PTListNode::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Run semantics analysis on this node.
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;

  // Apply an operator on each node in the list.
  template<typename ContextType, typename NodeType = TreeNode>
  ErrorCode Apply(ContextType *context,
                  TreeNodeOperator<ContextType, NodeType> node_op,
                  int max_nested_level = 0,
                  int max_nested_count = 0,
                  TreeNodeOperator<ContextType, NodeType> nested_node_op = nullptr) {

    ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
    int nested_level = 0;
    int nested_count = 0;

    for (auto tnode : node_list_) {
      if (tnode->opcode() != TreeNodeOpcode::kPTListNode) {
        // Cast the node from (TreeNode*) to the given template type.
        NodeType *node = static_cast<NodeType*>(tnode.get());
        // Call the given node operation on the node.
        err = node_op(*node, context);

      } else {
        if (++nested_count > max_nested_count) {
          err = ErrorCode::SYNTAX_ERROR;
          LOG(ERROR) << "Number of nested lists exceeds allowable limit";
          break;
        }

        if (++nested_level > max_nested_level) {
          err = ErrorCode::SYNTAX_ERROR;
          LOG(ERROR) << "Nested level of parenthesis exceeds allowable limit";
          break;
        }

        // Cast the node from (TreeNode*) to the given template type.
        PTListNode *node = static_cast<PTListNode*>(tnode.get());
        // Apply the operation to a nested list.
        err = node->Apply<ContextType, NodeType>(context,
                                                 nested_node_op,
                                                 max_nested_level - 1,
                                                 max_nested_count,
                                                 nested_node_op);
        nested_level--;
      }

      if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
        return err;
      }
    }

    return err;
  }

  // Access function to node_list_.
  const TreeNodeList& node_list() const {
    return node_list_;
  }

 private:
  TreeNodeList node_list_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_LIST_NODE_H_
