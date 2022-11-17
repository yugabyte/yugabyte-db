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

#pragma once

#include "yb/gutil/strings/substitute.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_property.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

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
                     YBLocationPtr loc,
                     const MCSharedPtr<MCString>& lhs_,
                     const PTExprPtr& rhs_);

  PTKeyspaceProperty(MemoryContext *memctx,
                     YBLocationPtr loc);

  virtual ~PTKeyspaceProperty();

  template<typename... TypeArgs>
  inline static PTKeyspaceProperty::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTKeyspaceProperty>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  const TreeListNode<PTKeyspaceProperty>::SharedPtr map_elements() const {
    return map_elements_;
  }

  KeyspacePropertyType property_type() const {
    return property_type_;
  }

 protected:
  // Just need an arbitrary default value here.
  KeyspacePropertyType property_type_ = KeyspacePropertyType::kKVProperty;

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
                                      YBLocationPtr loc,
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
    for (const auto& tnode : tnode_list->node_list()) {
      Append(tnode);
    }
  }

  template<typename... TypeArgs>
  inline static PTKeyspacePropertyListNode::SharedPtr MakeShared(MemoryContext *memctx,
                                                                 TypeArgs&&...args) {
    return MCMakeShared<PTKeyspacePropertyListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override;
};

class PTKeyspacePropertyMap : public PTKeyspaceProperty {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTKeyspacePropertyMap> SharedPtr;
  typedef MCSharedPtr<const PTKeyspacePropertyMap> SharedPtrConst;

  PTKeyspacePropertyMap(MemoryContext *memctx, YBLocationPtr loc);

  virtual ~PTKeyspacePropertyMap();

  template<typename... TypeArgs>
  inline static PTKeyspacePropertyMap::SharedPtr MakeShared(MemoryContext *memctx,
                                                            TypeArgs&&... args) {
    return MCMakeShared<PTKeyspacePropertyMap>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
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

} // namespace ql
} // namespace yb
