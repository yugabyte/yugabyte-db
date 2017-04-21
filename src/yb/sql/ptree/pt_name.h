//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for database object names such as table, column, or index names.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_NAME_H_
#define YB_SQL_PTREE_PT_NAME_H_

#include "yb/client/yb_table_name.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/list_node.h"

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
  explicit PTName(MemoryContext *memctx = nullptr,
                  YBLocation::SharedPtr loc = nullptr,
                  const MCString::SharedPtr& name = nullptr);
  virtual ~PTName();

  template<typename... TypeArgs>
  inline static PTName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTName>(memctx, std::forward<TypeArgs>(args)...);
  }

  CHECKED_STATUS SetupPrimaryKey(SemContext *sem_context);
  CHECKED_STATUS SetupHashAndPrimaryKey(SemContext *sem_context);

  const MCString& name() const {
    return *name_;
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
  PTNameAll(MemoryContext *memctx, YBLocation::SharedPtr loc);
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
  PTQualifiedName(MemoryContext *mctx,
                  YBLocation::SharedPtr loc,
                  const PTName::SharedPtr& ptname);
  PTQualifiedName(MemoryContext *mctx,
                  YBLocation::SharedPtr loc,
                  const MCString::SharedPtr& name);
  virtual ~PTQualifiedName();

  template<typename... TypeArgs>
  inline static PTQualifiedName::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTQualifiedName>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Forming qualified name by appending.
  void Append(const PTName::SharedPtr& ptname);

  // Forming qualified name by prepending.
  void Prepend(const PTName::SharedPtr& ptname);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  const MCString& first_name() const {
    return ptnames_.front()->name();
  }

  const MCString& last_name() const {
    return ptnames_.back()->name();
  }

  client::YBTableName ToTableName() const {
    // We support only names with 1 or 2 sub-names.
    // See Analyze() implementation.
    return (ptnames_.size() >= 2 ? client::YBTableName(first_name().c_str(), last_name().c_str())
        : client::YBTableName(last_name().c_str(), client::YBTableName::UNKNOWN_NAMESPACE));
  }

 private:
  MCList<PTName::SharedPtr> ptnames_;
};

using PTQualifiedNameListNode = TreeListNode<PTQualifiedName>;

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_NAME_H_
