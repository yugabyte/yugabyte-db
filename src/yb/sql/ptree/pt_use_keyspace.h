//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for USE KEYSPACE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_USE_KEYSPACE_H_
#define YB_SQL_PTREE_PT_USE_KEYSPACE_H_

#include "yb/sql/ptree/tree_node.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// USE KEYSPACE statement.

class PTUseKeyspace : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTUseKeyspace> SharedPtr;
  typedef MCSharedPtr<const PTUseKeyspace> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTUseKeyspace(MemoryContext *memctx,
                YBLocation::SharedPtr loc,
                const MCSharedPtr<MCString>& name);
  virtual ~PTUseKeyspace();

  template<typename... TypeArgs>
  inline static PTUseKeyspace::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTUseKeyspace>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTUseKeyspace;
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  const char* name() const {
    return name_->c_str();
  }

 private:
  MCSharedPtr<MCString> name_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_USE_KEYSPACE_H_
