//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for DELETE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_DELETE_H_
#define YB_SQL_PTREE_PT_DELETE_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_dml.h"
#include "yb/sql/ptree/pt_select.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class PTDeleteStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTDeleteStmt> SharedPtr;
  typedef MCSharedPtr<const PTDeleteStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTDeleteStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               TreeNode::SharedPtr selections,
               PTTableRef::SharedPtr relation,
               TreeNode::SharedPtr using_clause,
               PTExpr::SharedPtr where_clause,
               PTExpr::SharedPtr if_clause = nullptr);
  virtual ~PTDeleteStmt();

  template<typename... TypeArgs>
  inline static PTDeleteStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTDeleteStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Table name.
  const char *table_name() const OVERRIDE {
    return relation_->table_name().c_str();
  }

  // Returns location of table name.
  const YBLocation& table_loc() const OVERRIDE {
    return relation_->loc();
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTDeleteStmt;
  }

  // IF clause.
  const PTExpr* if_clause() const {
    return if_clause_.get();
  }

 private:
  PTTableRef::SharedPtr relation_;
  PTExpr::SharedPtr where_clause_;
  PTExpr::SharedPtr if_clause_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_DELETE_H_
