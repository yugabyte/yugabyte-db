// Copyright (c) YugaByte, Inc.

#ifndef YB_SQL_PTREE_PT_TABLE_PROPERTY_H_
#define YB_SQL_PTREE_PT_TABLE_PROPERTY_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/tree_node.h"

namespace yb {
namespace sql {

class PTTableProperty : public TreeNode {
 public:
  static const char kDefaultTimeToLive[];
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTableProperty> SharedPtr;
  typedef MCSharedPtr<const PTTableProperty> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTTableProperty(MemoryContext *memctx,
           YBLocation::SharedPtr loc,
           const MCString::SharedPtr& lhs_,
           const PTExpr::SharedPtr& rhs_);
  virtual ~PTTableProperty();

  template<typename... TypeArgs>
  inline static PTTableProperty::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTTableProperty>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  MCString::SharedPtr lhs() const {
    return lhs_;
  }

  PTExpr::SharedPtr rhs() const {
    return rhs_;
  }

 private:
  static const std::map<std::string, client::YBColumnSchema::DataType> kPropertyDataTypes;
  MCString::SharedPtr lhs_;
  PTExpr::SharedPtr rhs_;
};

class PTTablePropertyListNode : public TreeListNode<PTTableProperty> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTablePropertyListNode> SharedPtr;
  typedef MCSharedPtr<const PTTablePropertyListNode> SharedPtrConst;

  explicit PTTablePropertyListNode(MemoryContext *memory_context,
                                   YBLocation::SharedPtr loc,
                                   const MCSharedPtr<PTTableProperty>& tnode = nullptr)
      : TreeListNode<PTTableProperty>(memory_context, loc, tnode) {
  }

  virtual ~PTTablePropertyListNode() {
  }

  template<typename... TypeArgs>
  inline static PTTablePropertyListNode::SharedPtr MakeShared(MemoryContext *memctx,
                                                              TypeArgs&&...args) {
    return MCMakeShared<PTTablePropertyListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;
};

} // namespace sql
} // namespace yb

#endif // YB_SQL_PTREE_PT_TABLE_PROPERTY_H_
