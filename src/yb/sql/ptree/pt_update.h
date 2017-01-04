//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for UPDATE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_UPDATE_H_
#define YB_SQL_PTREE_PT_UPDATE_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_dml.h"
#include "yb/sql/ptree/pt_select.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

class PTAssign : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTAssign> SharedPtr;
  typedef MCSharedPtr<const PTAssign> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTAssign(MemoryContext *memctx,
           YBLocation::SharedPtr loc,
           const PTQualifiedName::SharedPtr& lhs_,
           const PTExpr::SharedPtr& rhs_);
  virtual ~PTAssign();

  template<typename... TypeArgs>
  inline static PTAssign::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTAssign>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTAssign;
  }

  const ColumnDesc *col_desc() const {
    return col_desc_;
  }

  PTExpr::SharedPtr rhs() {
    return rhs_;
  }

 private:
  PTQualifiedName::SharedPtr lhs_;
  PTExpr::SharedPtr rhs_;

  // Semantic phase will fill in this value.
  const ColumnDesc *col_desc_;
};

using PTAssignListNode = TreeListNode<PTAssign>;

//--------------------------------------------------------------------------------------------------

class PTUpdateStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTUpdateStmt> SharedPtr;
  typedef MCSharedPtr<const PTUpdateStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTUpdateStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               PTTableRef::SharedPtr relation,
               PTAssignListNode::SharedPtr set_clause,
               PTExpr::SharedPtr where_clause,
               PTOptionExist option_exists = PTOptionExist::DEFAULT);
  virtual ~PTUpdateStmt();

  template<typename... TypeArgs>
  inline static PTUpdateStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTUpdateStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  ErrorCode AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context);

  // Access for column_args.
  const MCVector<ColumnArg>& column_args() const {
    return column_args_;
  }

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
    return TreeNodeOpcode::kPTUpdateStmt;
  }

 private:
  PTTableRef::SharedPtr relation_;
  PTAssignListNode::SharedPtr set_clause_;
  PTExpr::SharedPtr where_clause_;

  // Semantic phase will decorate the following field.
  MCVector<ColumnArg> column_args_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_UPDATE_H_
