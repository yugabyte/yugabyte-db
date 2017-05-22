//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for SELECT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_SELECT_H_
#define YB_SQL_PTREE_PT_SELECT_H_

#include "yb/client/client.h"

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_name.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/pt_dml.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// This class represents VALUES clause
class PTValues : public PTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTValues> SharedPtr;
  typedef MCSharedPtr<const PTValues> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTValues(MemoryContext *memctx,
           YBLocation::SharedPtr loc,
           PTExprListNode::SharedPtr tuple);
  virtual ~PTValues();

  template<typename... TypeArgs>
  inline static PTValues::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTValues>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Add a tree node at the end.
  void Append(const PTExprListNode::SharedPtr& tnode);
  void Prepend(const PTExprListNode::SharedPtr& tnode);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Access function for tuples_.
  const TreeListNode<PTExprListNode>& tuples() {
    return tuples_;
  }

  // Number of provided tuples.
  virtual int TupleCount() const {
    return tuples_.size();
  }
  PTExprListNode::SharedPtr Tuple(int index) const;

 private:
  TreeListNode<PTExprListNode> tuples_;
};

//--------------------------------------------------------------------------------------------------
// ORDER BY.
class PTOrderBy : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTOrderBy> SharedPtr;
  typedef MCSharedPtr<const PTOrderBy> SharedPtrConst;

  enum Direction : int8_t { kASC = 0, kDESC };

  enum NullPlacement : int8_t { kFIRST = 0, kLAST };

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTOrderBy(MemoryContext *memctx,
            YBLocation::SharedPtr loc,
            const PTExpr::SharedPtr& name,
            const Direction direction,
            const NullPlacement null_placement);
  virtual ~PTOrderBy();

  template<typename... TypeArgs>
  inline static PTOrderBy::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTOrderBy>(memctx, std::forward<TypeArgs>(args)...);
  }

  Direction direction() const {
    return direction_;
  }

  NullPlacement null_placement() const {
    return null_placement_;
  }

 private:
  PTExpr::SharedPtr name_;
  Direction direction_;
  NullPlacement null_placement_;
};

//--------------------------------------------------------------------------------------------------
// FROM <table ref list>.
class PTTableRef : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTableRef> SharedPtr;
  typedef MCSharedPtr<const PTTableRef> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTTableRef(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTQualifiedName::SharedPtr& name,
             MCSharedPtr<MCString> alias);
  virtual ~PTTableRef();

  template<typename... TypeArgs>
  inline static PTTableRef::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTTableRef>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  client::YBTableName table_name() const {
    return name_->ToTableName();
  }

 private:
  PTQualifiedName::SharedPtr name_;
  MCSharedPtr<MCString> alias_;
};

using PTTableRefListNode = TreeListNode<PTTableRef>;

//--------------------------------------------------------------------------------------------------
// This class represents SELECT statement.
class PTSelectStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTSelectStmt> SharedPtr;
  typedef MCSharedPtr<const PTSelectStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTSelectStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               bool distinct,
               PTListNode::SharedPtr target,
               PTTableRefListNode::SharedPtr from_clause,
               PTExpr::SharedPtr where_clause,
               PTListNode::SharedPtr group_by_clause,
               PTListNode::SharedPtr having_clause,
               PTListNode::SharedPtr order_by_clause,
               PTExpr::SharedPtr limit_clause);
  virtual ~PTSelectStmt();

  template<typename... TypeArgs>
  inline static PTSelectStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTSelectStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  CHECKED_STATUS AnalyzeLimitClause(SemContext *sem_context);
  CHECKED_STATUS AnalyzeTarget(TreeNode *target, SemContext *sem_context);
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Execution opcode.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTSelectStmt;
  }

  virtual void SetOrderByClause(PTListNode::SharedPtr order_by_clause) {
    order_by_clause_ = order_by_clause;
  }

  virtual void SetLimitClause(PTExpr::SharedPtr limit_clause) {
    limit_clause_ = limit_clause;
  }

  bool distinct() const {
    return distinct_;
  }

  bool has_limit() const {
    return limit_clause_ != nullptr;
  }

  virtual PTExpr::SharedPtr limit() const {
    if (!has_limit()) {
      return nullptr;
    }
    return limit_clause_;
  }

  // Selected columns.
  const MCVector<const ColumnDesc*> &selected_columns() const {
    CHECK(selected_columns_ != nullptr) << "selected columns not set up";
    return *selected_columns_;
  }

  // Returns table name.
  virtual client::YBTableName table_name() const override {
    // CQL only allows one table at a time.
    return from_clause_->element(0)->table_name();
  }

  // Returns location of table name.
  virtual const YBLocation& table_loc() const override {
    return from_clause_->loc();
  }

  // Reset to clear and release previous semantics analysis results.
  virtual void Reset() override;

 private:
  // The following members represent different components of SELECT statement. However, Cassandra
  // doesn't support all of SQL syntax and semantics.
  //
  // SELECT [DISTINCT] <target_>
  //   FROM      <from_clause_>
  //   WHERE     <where_clause_>
  //   GROUP BY  <group_by_clause_> HAVING <having_clause_>
  //   ORDER BY  <order_by_clause_>
  //   LIMIT     <limit_clause_>
  const bool distinct_;
  PTListNode::SharedPtr target_;
  PTTableRefListNode::SharedPtr from_clause_;
  PTExpr::SharedPtr where_clause_;
  PTListNode::SharedPtr group_by_clause_;
  PTListNode::SharedPtr having_clause_;
  PTListNode::SharedPtr order_by_clause_;
  PTExpr::SharedPtr limit_clause_;

  // Members that will be constructed by semantic analyzer.
  MCSharedPtr<MCVector<const ColumnDesc*>> selected_columns_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_SELECT_H_
