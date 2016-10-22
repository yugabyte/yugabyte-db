//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for CREATE TABLE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_CREATE_TABLE_H_
#define YB_SQL_PTREE_PT_CREATE_TABLE_H_

#include "yb/sql/ptree/parse_tree.h"
#include "yb/sql/ptree/pt_type.h"
#include "yb/sql/ptree/pt_name.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// Constraints.

enum class PTConstraintType {
  kUndefined = 0,
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
  PTConstraint() {
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
  explicit PTPrimaryKey(MemoryContext *memctx,
                        const PTListNode::SharedPtr& columns_ = nullptr);
  virtual ~PTPrimaryKey();

  virtual PTConstraintType constraint_type() {
    return PTConstraintType::kPrimaryKey;
  }

  template<typename... TypeArgs>
  inline static PTPrimaryKey::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimaryKey>(memctx, std::forward<TypeArgs>(args)...);
  }

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
                     const MCString::SharedPtr& name,
                     const PTBaseType::SharedPtr& datatype,
                     const PTListNode::SharedPtr& constraints);
  virtual ~PTColumnDefinition();

  template<typename... TypeArgs>
  inline static PTColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  const MCString::SharedPtr name_;
  PTBaseType::SharedPtr datatype_;
  PTListNode::SharedPtr constraints_;
};

//--------------------------------------------------------------------------------------------------
// Create table statement.

class PTCreateTable : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateTable> SharedPtr;
  typedef MCSharedPtr<const PTCreateTable> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateTable(MemoryContext *memctx,
                const PTQualifiedName::SharedPtr& name,
                const PTListNode::SharedPtr& elements);
  ~PTCreateTable();

  template<typename... TypeArgs>
  inline static PTCreateTable::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateTable>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  PTQualifiedName::SharedPtr relation_;
  PTListNode::SharedPtr elements_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_CREATE_TABLE_H_
