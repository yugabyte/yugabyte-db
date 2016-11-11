//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for INSERT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_INSERT_H_
#define YB_SQL_PTREE_PT_INSERT_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_select.h"
#include "yb/sql/ptree/argument.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class PTInsertStmt : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTInsertStmt> SharedPtr;
  typedef MCSharedPtr<const PTInsertStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTInsertStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               PTQualifiedName::SharedPtr relation,
               PTQualifiedNameListNode::SharedPtr columns,
               PTCollection::SharedPtr value_clause);
  virtual ~PTInsertStmt();

  template<typename... TypeArgs>
  inline static PTInsertStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTInsertStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTInsertStmt;
  }

  // Table name.
  const char *yb_table_name() const {
    return relation_->last_name().c_str();
  }

  // Returns location of table name.
  const YBLocation& name_loc() const {
    return relation_->loc();
  }

 private:
  PTQualifiedName::SharedPtr relation_;
  PTQualifiedNameListNode::SharedPtr columns_;
  PTCollection::SharedPtr value_clause_;
  MCVector<Argument> tuple_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_INSERT_H_
