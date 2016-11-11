//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for SELECT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_SELECT_H_
#define YB_SQL_PTREE_PT_SELECT_H_

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_name.h"
#include "yb/sql/ptree/pt_expr.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

// This class represents the data of collection type. PostgreSql syntax rules dictate how we form
// the hierarchy of our C++ classes, so classes for VALUES and SELECT clause must share the same
// base class.
// - VALUES (x, y, z)
// - (SELECT x, y, z FROM tab)
// Functionalities of this class should be "protected" to make sure that PTCollection instances are
// not created and used by application.
class PTCollection : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCollection> SharedPtr;
  typedef MCSharedPtr<const PTCollection> SharedPtrConst;

 protected:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor. Define them in protected section to prevent application from
  // declaring them.
  PTCollection(MemoryContext *memctx, YBLocation::SharedPtr loc)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTCollection() {
  }
};

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
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;
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
             MCString::SharedPtr alias);
  virtual ~PTTableRef();

  template<typename... TypeArgs>
  inline static PTTableRef::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTTableRef>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  PTQualifiedName::SharedPtr name_;
  MCString::SharedPtr alias_;
};

using PTTableRefListNode = TreeListNode<PTTableRef>;

//--------------------------------------------------------------------------------------------------
// This class represents SELECT statement.
class PTSelectStmt : public PTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTSelectStmt> SharedPtr;
  typedef MCSharedPtr<const PTSelectStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTSelectStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
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
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Execution opcode.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTSelectStmt;
  }

  virtual void SetOrderByClause(PTListNode::SharedPtr order_by_clause) {
    order_by_clause = order_by_clause_;
  }

  virtual void SetLimitClause(PTExpr::SharedPtr limit_clause) {
    limit_clause_ = limit_clause;
  }

  // Returns location of table name.
  const YBLocation& name_loc() const {
    return from_clause_->loc();
  }

 private:
  PTListNode::SharedPtr target_;
  PTTableRefListNode::SharedPtr from_clause_;
  PTExpr::SharedPtr where_clause_;
  PTListNode::SharedPtr group_by_clause_;
  PTListNode::SharedPtr having_clause_;
  PTListNode::SharedPtr order_by_clause_;
  PTExpr::SharedPtr limit_clause_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_SELECT_H_
