//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for DROP statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_DROP_H_
#define YB_SQL_PTREE_PT_DROP_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_type.h"
#include "yb/sql/ptree/pt_name.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// DROP TABLE statement.

class PTDropStmt : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDropStmt> SharedPtr;
  typedef MCSharedPtr<const PTDropStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTDropStmt(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             PTQualifiedNameListNode::SharedPtr names,
             bool drop_if_exists);
  virtual ~PTDropStmt();

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTDropStmt;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTDropStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDropStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  bool drop_if_exists() const {
    return drop_if_exists_;
  }

  // Table name.
  const char* yb_table_name() const {
    return names_->element(0)->last_name().c_str();
  }

  // Returns location of table name.
  const YBLocation& name_loc() const {
    return names_->loc();
  }

 private:
  PTQualifiedNameListNode::SharedPtr names_;

  // Set to true for DROP IF EXISTS statements.
  bool drop_if_exists_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_DROP_H_
