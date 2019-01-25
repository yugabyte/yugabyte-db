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
// Column Definition Tree node definition.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_COLUMN_DEFINITION_H
#define YB_YQL_CQL_QL_PTREE_PT_COLUMN_DEFINITION_H

#include "yb/common/schema.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Table column.

class PTColumnDefinition : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTColumnDefinition> SharedPtr;
  typedef MCSharedPtr<const PTColumnDefinition> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTColumnDefinition(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const MCSharedPtr<MCString>& name,
                     const PTBaseType::SharedPtr& datatype,
                     const PTListNode::SharedPtr& qualifiers);

  // By default copied column has same datatype, but it can be replaced by manually provided type.
  PTColumnDefinition(MemoryContext *memctx,
                     const PTColumnDefinition& column,
                     const PTExprListNode::SharedPtr& operators,
                     const PTBaseType::SharedPtr& datatype = nullptr);

  virtual ~PTColumnDefinition();

  template<typename... TypeArgs>
  inline static PTColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTColumnDefinition;
  }

  // Access function for is_primary_key_.
  bool is_primary_key() const {
    return is_primary_key_;
  }
  void set_is_primary_key() {
    is_primary_key_ = true;
  }

  // Access function for is_hash_key_.
  bool is_hash_key() const {
    return is_hash_key_;
  }
  void set_is_hash_key() {
    is_primary_key_ = true;
    is_hash_key_ = true;
  }

  // Access function for is_static_.
  bool is_static() const {
    return is_static_;
  }
  void set_is_static() {
    is_static_ = true;
  }

  // Access function for order_.
  int32_t order() const {
    return order_;
  }
  void set_order(int32 order) {
    order_ = order;
  }

  ColumnSchema::SortingType sorting_type() {
    return sorting_type_;
  }

  void set_sorting_type(ColumnSchema::SortingType sorting_type) {
    sorting_type_ = sorting_type;
  }

  const char *yb_name() const {
    return name_->c_str();
  }

  const PTBaseType::SharedPtr& datatype() const {
    return datatype_;
  }

  std::shared_ptr<QLType> ql_type() const {
    return datatype_->ql_type();
  }

  bool is_counter() const {
    return datatype_->is_counter();
  }

  // Access function for operators_.
  const PTExprListNode::SharedPtr& operators() const {
    return operators_;
  }
  void set_operators(const PTExprListNode::SharedPtr& operators) {
    operators_ = operators;
  }

 private:
  const MCSharedPtr<MCString> name_;
  PTBaseType::SharedPtr datatype_;
  PTListNode::SharedPtr qualifiers_;
  bool is_primary_key_;
  bool is_hash_key_;
  bool is_static_;
  int32_t order_;
  // Sorting order. Only relevant when this key is a primary key.
  ColumnSchema::SortingType sorting_type_;

  PTExprListNode::SharedPtr operators_; // Additional operators applied to column (like c->>'a').
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_COLUMN_DEFINITION_H
