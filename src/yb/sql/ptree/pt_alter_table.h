//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for ALTER TABLE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_ALTER_TABLE_H_
#define YB_SQL_PTREE_PT_ALTER_TABLE_H_

#include "yb/common/schema.h"
#include "yb/master/master.pb.h"
#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_table_property.h"
#include "yb/sql/ptree/pt_type.h"
#include "yb/sql/ptree/pt_name.h"
#include "yb/sql/ptree/pt_update.h"
#include "yb/sql/ptree/pt_option.h"
namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

typedef enum AlterColumnType : int {
  ALTER_ADD,
  ALTER_DROP,
  ALTER_RENAME,
  ALTER_TYPE
} ModColumnType;

const string supported_properties[] = {"ttl"};

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
              YBLocation::SharedPtr loc,
              PTQualifiedName::SharedPtr name,
              const MCSharedPtr<MCString>& new_name,
              const PTBaseType::SharedPtr& datatype,
              AlterColumnType type);
  virtual ~PTAlterColumnDefinition();

  template<typename... TypeArgs>
  inline static PTAlterColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                  TypeArgs&& ... args) {
    return MCMakeShared<PTAlterColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

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

  std::shared_ptr<YQLType> yql_type() const {
    return datatype_->yql_type();
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
// Table property updates

class PTAlterProperty : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTAlterProperty> SharedPtr;
  typedef MCSharedPtr<const PTAlterProperty> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTAlterProperty(MemoryContext *memctx,
                  YBLocation::SharedPtr loc,
                  const MCSharedPtr<MCString>& lhs,
                  const MCSharedPtr<MCString>& rhs);
  virtual ~PTAlterProperty();

  template<typename... TypeArgs>
  inline static PTAlterProperty::SharedPtr MakeShared(MemoryContext *memctx,
                                                      TypeArgs&& ... args) {
    return MCMakeShared<PTAlterProperty>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  const MCSharedPtr<MCString> property_name() {
    return lhs_;
  }

  const MCSharedPtr<MCString> property_value() {
    return rhs_;
  }

 private:
  const MCSharedPtr<MCString> lhs_;
  const MCSharedPtr<MCString> rhs_;
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
               YBLocation::SharedPtr loc,
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

  // Table name.
  client::YBTableName yb_table_name() const {
    return name_->ToTableName();
  }

  // Column modifications to be made.
  const MCList<PTAlterColumnDefinition* >& mod_columns() const {
    return mod_columns_;
  }

  // Table property modifications to be made.
  const MCList<PTAlterProperty* >& mod_props() const {
    return mod_props_;
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  void PrintSemanticAnalysisResult(SemContext *sem_context);

  CHECKED_STATUS AppendModColumn(SemContext *sem_context, PTAlterColumnDefinition *column);

  CHECKED_STATUS AppendAlterProperty(SemContext *sem_context, PTAlterProperty *prop);

 private:
  PTQualifiedName::SharedPtr name_;
  PTListNode::SharedPtr commands_;

  std::shared_ptr<client::YBTable> table_;
  MCVector<ColumnDesc> table_columns_;
  MCList<PTAlterColumnDefinition *> mod_columns_;
  MCList<PTAlterProperty *> mod_props_;

  int num_key_columns_;
  int num_hash_key_columns_;
  bool is_system_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_ALTER_TABLE_H_
