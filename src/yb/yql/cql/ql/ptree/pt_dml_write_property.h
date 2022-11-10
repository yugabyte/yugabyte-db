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

enum class DmlWritePropertyType : int {
  kDmlWriteProperty = 0,
  kDmlWritePropertyMap,
};

class PTDmlWriteProperty : public PTProperty {
 public:
  enum class KVProperty : int {
    kOptions
  };

  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDmlWriteProperty> SharedPtr;
  typedef MCSharedPtr<const PTDmlWriteProperty> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  // Constructor for DmlWritePropertyType::kDmlWriteProperty.
  PTDmlWriteProperty(MemoryContext *memctx,
                     YBLocationPtr loc,
                     const MCSharedPtr<MCString>& lhs_,
                     const PTExprPtr& rhs_);

  PTDmlWriteProperty(MemoryContext *memctx,
                     YBLocationPtr loc);

  virtual ~PTDmlWriteProperty();

  template<typename... TypeArgs>
  inline static PTDmlWriteProperty::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTDmlWriteProperty>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  DmlWritePropertyType property_type() const {
    return property_type_;
  }

 protected:
  bool IsValidProperty(const std::string& property_name) {
    return kPropertyDataTypes.find(property_name) != kPropertyDataTypes.end();
  }

  DmlWritePropertyType property_type_ = DmlWritePropertyType::kDmlWriteProperty;

 private:
  static const std::map<std::string, PTDmlWriteProperty::KVProperty> kPropertyDataTypes;
};

std::ostream& operator<<(std::ostream& os, const DmlWritePropertyType& property_type);

class PTDmlWritePropertyListNode : public TreeListNode<PTDmlWriteProperty> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDmlWritePropertyListNode> SharedPtr;
  typedef MCSharedPtr<const PTDmlWritePropertyListNode> SharedPtrConst;

  explicit PTDmlWritePropertyListNode(MemoryContext *memory_context,
                                      YBLocationPtr loc,
                                      const MCSharedPtr<PTDmlWriteProperty>& tnode = nullptr)
      : TreeListNode<PTDmlWriteProperty>(memory_context, loc, tnode) {
  }

  virtual ~PTDmlWritePropertyListNode() {
  }

  // Append a PTDmlWritePropertyListNode to this list.
  void AppendList(const MCSharedPtr<PTDmlWritePropertyListNode>& tnode_list) {
    if (tnode_list == nullptr) {
      return;
    }
    for (const auto& tnode : tnode_list->node_list()) {
      Append(tnode);
    }
  }

  template<typename... TypeArgs>
  inline static PTDmlWritePropertyListNode::SharedPtr MakeShared(MemoryContext *memctx,
                                                              TypeArgs&&...args) {
    return MCMakeShared<PTDmlWritePropertyListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override;

  bool ignore_null_jsonb_attributes();
};

class PTDmlWritePropertyMap : public PTDmlWriteProperty {
 public:
  enum class PropertyMapType : int {
    kOptions
  };
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDmlWritePropertyMap> SharedPtr;
  typedef MCSharedPtr<const PTDmlWritePropertyMap> SharedPtrConst;

  PTDmlWritePropertyMap(MemoryContext *memctx,
                        YBLocationPtr loc);

  virtual ~PTDmlWritePropertyMap();

  template<typename... TypeArgs>
  inline static PTDmlWritePropertyMap::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTDmlWritePropertyMap>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  void SetPropertyName(MCSharedPtr<MCString> property_name) {
    lhs_ = property_name;
  }

  void AppendMapElement(PTDmlWriteProperty::SharedPtr DmlWrite_property) {
    DCHECK_EQ(property_type_, DmlWritePropertyType::kDmlWritePropertyMap);
    map_elements_->Append(DmlWrite_property);
  }

  bool ignore_null_jsonb_attributes();

 private:
  Status AnalyzeOptions(SemContext *sem_context);

  static const std::map<std::string, PTDmlWritePropertyMap::PropertyMapType> kPropertyDataTypes;
  TreeListNode<PTDmlWriteProperty>::SharedPtr map_elements_;
};

struct Options {
  enum class Subproperty : int {
    kIgnoreNullJsonbAttributes
  };

  static const std::map<std::string, Subproperty> kSubpropertyDataTypes;
};

} // namespace ql
} // namespace yb
