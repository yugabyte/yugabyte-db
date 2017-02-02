//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for CREATE TABLE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_CREATE_TABLE_H_
#define YB_SQL_PTREE_PT_CREATE_TABLE_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_type.h"
#include "yb/sql/ptree/pt_name.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// Constraints.

enum class PTConstraintType {
  kNone = 0,
  kPrimaryKey,
  kUnique,
  kNotNull,
};

class PTConstraint : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTConstraint> SharedPtr;
  typedef MCSharedPtr<const PTConstraint> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTConstraint(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTConstraint() {
  }

  virtual PTConstraintType constraint_type() = 0;
};

class PTPrimaryKey : public PTConstraint {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPrimaryKey> SharedPtr;
  typedef MCSharedPtr<const PTPrimaryKey> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTPrimaryKey(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               const PTListNode::SharedPtr& columns_ = nullptr);
  virtual ~PTPrimaryKey();

  virtual PTConstraintType constraint_type() OVERRIDE {
    return PTConstraintType::kPrimaryKey;
  }

  template<typename... TypeArgs>
  inline static PTPrimaryKey::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimaryKey>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;

 private:
  PTListNode::SharedPtr columns_;
};

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
                     const MCString::SharedPtr& name,
                     const PTBaseType::SharedPtr& datatype,
                     const PTListNode::SharedPtr& constraints);
  virtual ~PTColumnDefinition();

  template<typename... TypeArgs>
  inline static PTColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;

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

  // Access function for order_.
  int32_t order() const {
    return order_;
  }
  void set_order(int32 order) {
    order_ = order;
  }

  const char *yb_name() const {
    return name_->c_str();
  }

  const PTBaseType::SharedPtr& datatype() const {
    return datatype_;
  }

  client::YBColumnSchema::DataType sql_type() const {
    return datatype_->sql_type();
  }

 private:
  const MCString::SharedPtr name_;
  PTBaseType::SharedPtr datatype_;
  PTListNode::SharedPtr constraints_;
  bool is_primary_key_;
  bool is_hash_key_;
  int32_t order_;
};

//--------------------------------------------------------------------------------------------------
// CREATE TABLE statement.

class PTCreateTable : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateTable> SharedPtr;
  typedef MCSharedPtr<const PTCreateTable> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateTable(MemoryContext *memctx,
                YBLocation::SharedPtr loc,
                const PTQualifiedName::SharedPtr& name,
                const PTListNode::SharedPtr& elements,
                bool create_if_not_exists);
  virtual ~PTCreateTable();

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTCreateTable;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCreateTable::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateTable>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // column lists.
  const MCList<PTColumnDefinition *>& columns() const {
    return columns_;
  }

  const MCList<PTColumnDefinition *>& primary_columns() const {
    return primary_columns_;
  }

  const MCList<PTColumnDefinition *>& hash_columns() const {
    return hash_columns_;
  }

  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

  CHECKED_STATUS AppendColumn(SemContext *sem_context, PTColumnDefinition *column);

  CHECKED_STATUS AppendPrimaryColumn(SemContext *sem_context, PTColumnDefinition *column);

  CHECKED_STATUS AppendHashColumn(SemContext *sem_context, PTColumnDefinition *column);

  CHECKED_STATUS CheckPrimaryType(SemContext *sem_context, const PTBaseType::SharedPtr& datatype);

  // Table name.
  const char *yb_table_name() const {
    return relation_->last_name().c_str();
  }

  // Returns location of table name.
  const YBLocation& name_loc() const {
    return relation_->loc();
  }

  // Returns location of table columns.
  const YBLocation& columns_loc() const {
    return elements_->loc();
  }

 private:
  PTQualifiedName::SharedPtr relation_;
  PTListNode::SharedPtr elements_;

  MCList<PTColumnDefinition *> columns_;
  MCList<PTColumnDefinition *> primary_columns_;
  MCList<PTColumnDefinition *> hash_columns_;

  bool create_if_not_exists_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_CREATE_TABLE_H_
