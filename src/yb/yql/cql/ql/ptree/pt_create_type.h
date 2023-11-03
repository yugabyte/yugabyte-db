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
// Tree node definitions for CREATE TYPE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Field of User-Defined Type

class PTTypeField : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTypeField> SharedPtr;
  typedef MCSharedPtr<const PTTypeField> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTTypeField(MemoryContext *memctx,
                     YBLocationPtr loc,
                     const MCSharedPtr<MCString>& name,
                     const PTBaseType::SharedPtr& datatype);
  virtual ~PTTypeField();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTTypeField;
  }

  template<typename... TypeArgs>
  inline static PTTypeField::SharedPtr MakeShared(MemoryContext *memctx,
                                                     TypeArgs&&... args) {
    return MCMakeShared<PTTypeField>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  const char *yb_name() const {
    return name_->c_str();
  }

  const PTBaseType::SharedPtr& datatype() const {
    return datatype_;
  }

  std::shared_ptr<QLType> ql_type() const {
    return datatype_->ql_type();
  }

 private:
  const MCSharedPtr<MCString> name_;
  PTBaseType::SharedPtr datatype_;
};

using PTTypeFieldListNode = TreeListNode<PTTypeField>;

//--------------------------------------------------------------------------------------------------
// CREATE TABLE statement.

class PTCreateType : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateType> SharedPtr;
  typedef MCSharedPtr<const PTCreateType> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateType(MemoryContext *memctx,
                YBLocationPtr loc,
                const PTQualifiedName::SharedPtr& name,
                const PTTypeFieldListNode::SharedPtr& fields,
                bool create_if_not_exists);
  virtual ~PTCreateType();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCreateType;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCreateType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateType>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // column lists.
  PTTypeFieldListNode::SharedPtr fields() const {
    return fields_;
  }

  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

  // Type name.
  PTQualifiedName::SharedPtr type_name() const {
    return name_;
  }

  client::YBTableName yb_type_name() const {
    return name_->ToTableName();
  }

 private:
  PTQualifiedName::SharedPtr name_;
  PTTypeFieldListNode::SharedPtr fields_;

  bool create_if_not_exists_;
};

}  // namespace ql
}  // namespace yb
