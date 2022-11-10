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
// Tree node definitions for ALTER TABLE statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_table_property.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

typedef enum AlterColumnType : int {
  ALTER_ADD,
  ALTER_DROP,
  ALTER_RENAME,
  ALTER_TYPE
} ModColumnType;

const std::string supported_properties[] = {"ttl"};

//--------------------------------------------------------------------------------------------------
// Drop/rename/alter type column operation details

class PTAlterColumnDefinition : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTAlterColumnDefinition> SharedPtr;
  typedef MCSharedPtr<const PTAlterColumnDefinition> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTAlterColumnDefinition(MemoryContext *memctx,
              YBLocationPtr loc,
              PTQualifiedName::SharedPtr name,
              const MCSharedPtr<MCString>& new_name,
              const PTBaseType::SharedPtr& datatype,
              AlterColumnType type);
  virtual ~PTAlterColumnDefinition();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTAlterColumnDefinition;
  }

  template<typename... TypeArgs>
  inline static PTAlterColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                  TypeArgs&& ... args) {
    return MCMakeShared<PTAlterColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  // Qualified name of column that's already present.
  PTQualifiedName::SharedPtr old_name() const {
    return name_;
  }

  AlterColumnType mod_type() const {
    return type_;
  }

  const PTBaseType::SharedPtr& datatype() const {
    return datatype_;
  }

  std::shared_ptr<QLType> ql_type() const {
    return datatype_->ql_type();
  }

  // New string name of column to be created or altered.
  const MCSharedPtr<MCString> new_name() const {
    return new_name_;
  }

 private:
  PTQualifiedName::SharedPtr name_;
  const MCSharedPtr<MCString> new_name_;
  const PTBaseType::SharedPtr datatype_;
  AlterColumnType type_;
};

//--------------------------------------------------------------------------------------------------
// ALTER TABLE

class PTAlterTable : public TreeNode {
 public:
  // Public types.
  typedef MCSharedPtr<PTAlterTable> SharedPtr;
  typedef MCSharedPtr<const PTAlterTable> SharedPtrConst;

  // Node type.
  PTAlterTable(MemoryContext *memctx,
               YBLocationPtr loc,
               PTQualifiedName::SharedPtr name,
               const PTListNode::SharedPtr& commands);
  virtual ~PTAlterTable();

  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTAlterTable;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTAlterTable::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&& ... args) {
    return MCMakeShared<PTAlterTable>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTQualifiedName::SharedPtr& table_name() const {
    return name_;
  }

  // Table name.
  client::YBTableName yb_table_name() const {
    return name_->ToTableName();
  }

  // Column modifications to be made.
  const MCList<PTAlterColumnDefinition* >& mod_columns() const {
    return mod_columns_;
  }

  // Table property modifications to be made.
  const MCList<PTTableProperty* >& mod_props() const {
    return mod_props_;
  }

  const std::shared_ptr<client::YBTable>& table() const {
    return table_;
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;

  void PrintSemanticAnalysisResult(SemContext *sem_context);

  Status AppendModColumn(SemContext *sem_context, PTAlterColumnDefinition *column);

  Status AppendAlterProperty(SemContext *sem_context, PTTableProperty *prop);

  Status ToTableProperties(TableProperties *table_properties) const;

 private:
  PTQualifiedName::SharedPtr name_;
  PTListNode::SharedPtr commands_;

  std::shared_ptr<client::YBTable> table_;
  MCVector<ColumnDesc> table_columns_;
  MCList<PTAlterColumnDefinition *> mod_columns_;
  MCList<PTTableProperty *> mod_props_;
};

}  // namespace ql
}  // namespace yb
