//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for CREATE KEYSPACE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_CREATE_KEYSPACE_H_
#define YB_SQL_PTREE_PT_CREATE_KEYSPACE_H_

#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_keyspace_property.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// CREATE KEYSPACE statement.

class PTCreateKeyspace : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateKeyspace> SharedPtr;
  typedef MCSharedPtr<const PTCreateKeyspace> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateKeyspace(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   const MCSharedPtr<MCString>& name,
                   bool create_if_not_exists,
                   const PTKeyspacePropertyListNode::SharedPtr& keyspace_properties);
  virtual ~PTCreateKeyspace();

  template<typename... TypeArgs>
  inline static PTCreateKeyspace::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateKeyspace>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCreateKeyspace;
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

  // Keyspace name.
  const char* name() const {
    return name_->c_str();
  }

  PTKeyspacePropertyListNode::SharedPtr keyspace_properties() const {
    return keyspace_properties_;
  }

 private:
  MCSharedPtr<MCString> name_;
  bool create_if_not_exists_;
  const PTKeyspacePropertyListNode::SharedPtr keyspace_properties_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_CREATE_KEYSPACE_H_
