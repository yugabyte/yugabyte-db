//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for database object names such as table, column, or index names.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_NAME_H_
#define YB_SQL_PTREE_PT_NAME_H_

#include <cstdlib>

#include "yb/sql/ptree/parse_tree.h"

namespace yb {
namespace sql {

// This class represents a name node.
class PTName : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTName> SharedPtr;
  typedef MCSharedPtr<const PTName> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTName(MemoryContext *memctx = nullptr, const MCString::SharedPtr& name = nullptr);
  virtual ~PTName();

  template<typename... TypeArgs>
  inline static PTName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTName>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  MCString::SharedPtr name_;
};

// This class represents "*" (i.e. all fields) in SQL statement.
class PTNameAll : public PTName {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTNameAll> SharedPtr;
  typedef MCSharedPtr<const PTNameAll> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTNameAll(MemoryContext *memctx);
  virtual ~PTNameAll();

  template<typename... TypeArgs>
  inline static PTNameAll::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTNameAll>(memctx, std::forward<TypeArgs>(args)...);
  }
};

// This class represents a qualified name (e.g. "a.m.t").
class PTQualifiedName : public PTName {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTQualifiedName> SharedPtr;
  typedef MCSharedPtr<const PTQualifiedName> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTQualifiedName(MemoryContext *mctx, const PTName::SharedPtr& ptname);
  explicit PTQualifiedName(MemoryContext *mctx, const MCString::SharedPtr& name);
  virtual ~PTQualifiedName();

  template<typename... TypeArgs>
  inline static PTQualifiedName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTQualifiedName>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Forming qualified name by appending.
  void Append(const PTName::SharedPtr& ptname);

  // Forming qualified name by prepending.
  void Prepend(const PTName::SharedPtr& ptname);

 private:
  MCList<PTName::SharedPtr> ptnames_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_NAME_H_
