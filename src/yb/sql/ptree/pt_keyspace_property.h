// Copyright (c) YugaByte, Inc.

#ifndef YB_SQL_PTREE_PT_KEYSPACE_PROPERTY_H_
#define YB_SQL_PTREE_PT_KEYSPACE_PROPERTY_H_

#include "yb/common/schema.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.pb.h"
#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/pt_property.h"
#include "yb/sql/ptree/tree_node.h"

namespace yb {
namespace sql {

enum class KeyspacePropertyType : int {
  kKVProperty = 0,
  kPropertyMap,
};

class PTKeyspaceProperty : public PTProperty {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTKeyspaceProperty> SharedPtr;
  typedef MCSharedPtr<const PTKeyspaceProperty> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTKeyspaceProperty(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const MCSharedPtr<MCString>& lhs_,
                     const PTExpr::SharedPtr& rhs_);

  PTKeyspaceProperty(MemoryContext *memctx,
                     YBLocation::SharedPtr loc);

  virtual ~PTKeyspaceProperty();

  template<typename... TypeArgs>
  inline static PTKeyspaceProperty::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTKeyspaceProperty>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  const TreeListNode<PTKeyspaceProperty>::SharedPtr map_elements() const {
    return map_elements_;
  }

  KeyspacePropertyType property_type() const {
    return property_type_;
  }

 protected:
  KeyspacePropertyType property_type_;

 private:
  TreeListNode<PTKeyspaceProperty>::SharedPtr map_elements_;
};


class PTKeyspacePropertyListNode : public TreeListNode<PTKeyspaceProperty> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTKeyspacePropertyListNode> SharedPtr;
  typedef MCSharedPtr<const PTKeyspacePropertyListNode> SharedPtrConst;

  explicit PTKeyspacePropertyListNode(MemoryContext *memory_context,
                                      YBLocation::SharedPtr loc,
                                      const MCSharedPtr<PTKeyspaceProperty>& tnode = nullptr)
      : TreeListNode<PTKeyspaceProperty>(memory_context, loc, tnode) {
  }

  virtual ~PTKeyspacePropertyListNode() {
  }

  // Append a PTKeyspacePropertyList to this list.
  void AppendList(const MCSharedPtr<PTKeyspacePropertyListNode>& tnode_list) {
    if (tnode_list == nullptr) {
      return;
    }
    for (const auto tnode : tnode_list->node_list()) {
      Append(tnode);
    }
  }

  template<typename... TypeArgs>
  inline static PTKeyspacePropertyListNode::SharedPtr MakeShared(MemoryContext *memctx,
                                                                 TypeArgs&&...args) {
    return MCMakeShared<PTKeyspacePropertyListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
};

class PTKeyspacePropertyMap : public PTKeyspaceProperty {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTKeyspacePropertyMap> SharedPtr;
  typedef MCSharedPtr<const PTKeyspacePropertyMap> SharedPtrConst;

  PTKeyspacePropertyMap(MemoryContext *memctx, YBLocation::SharedPtr loc);

  virtual ~PTKeyspacePropertyMap();

  template<typename... TypeArgs>
  inline static PTKeyspacePropertyMap::SharedPtr MakeShared(MemoryContext *memctx,
                                                            TypeArgs&&... args) {
    return MCMakeShared<PTKeyspacePropertyMap>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  void SetPropertyName(MCSharedPtr<MCString> property_name) {
    lhs_ = property_name;
  }

  void AppendMapElement(PTKeyspaceProperty::SharedPtr table_property) {
    map_elements_->Append(table_property);
  }

 private:
  TreeListNode<PTKeyspaceProperty>::SharedPtr map_elements_;
};

} // namespace sql
} // namespace yb

#endif // YB_SQL_PTREE_PT_KEYSPACE_PROPERTY_H_
