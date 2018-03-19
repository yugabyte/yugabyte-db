//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// Tree node definitions for SELECT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_SELECT_H_
#define YB_YQL_CQL_QL_PTREE_PT_SELECT_H_

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"

namespace yb {
namespace ql {

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

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  Direction direction() const {
    return direction_;
  }

  NullPlacement null_placement() const {
    return null_placement_;
  }

  PTExpr::SharedPtr name() const {
    return name_;
  }

 private:
  PTExpr::SharedPtr name_;
  Direction direction_;
  NullPlacement null_placement_;
};

using PTOrderByListNode = TreeListNode<PTOrderBy>;

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
               PTExprListNode::SharedPtr selected_exprs,
               PTTableRefListNode::SharedPtr from_clause,
               PTExpr::SharedPtr where_clause,
               PTListNode::SharedPtr group_by_clause,
               PTListNode::SharedPtr having_clause,
               PTOrderByListNode::SharedPtr order_by_clause,
               PTExpr::SharedPtr limit_clause);
  virtual ~PTSelectStmt();

  template<typename... TypeArgs>
  inline static PTSelectStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTSelectStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Execution opcode.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTSelectStmt;
  }

  virtual void SetOrderByClause(PTOrderByListNode::SharedPtr order_by_clause) {
    order_by_clause_ = order_by_clause;
  }

  virtual void SetLimitClause(PTExpr::SharedPtr limit_clause) {
    limit_clause_ = limit_clause;
  }

  bool distinct() const {
    return distinct_;
  }

  bool is_forward_scan() const {
    return is_forward_scan_;
  }

  bool has_limit() const {
    return limit_clause_ != nullptr;
  }

  PTExpr::SharedPtr limit() const {
    return limit_clause_;
  }

  const MCList<PTExpr::SharedPtr>& selected_exprs() const {
    return selected_exprs_->node_list();
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

  PTOrderByListNode::SharedPtr order_by_clause() const {
    return order_by_clause_;
  }

  bool is_aggregate() const {
    return is_aggregate_;
  }

  bool use_index() const {
    return use_index_;
  }

  bool read_just_index() const {
    return read_just_index_;
  }

  const TableId& index_id() const {
    return index_id_;
  }

 private:
  CHECKED_STATUS AnalyzeIndexes(SemContext *sem_context);
  CHECKED_STATUS AnalyzeDistinctClause(SemContext *sem_context);
  CHECKED_STATUS AnalyzeOrderByClause(SemContext *sem_context);
  CHECKED_STATUS AnalyzeLimitClause(SemContext *sem_context);
  CHECKED_STATUS ConstructSelectedSchema();

  // --- The parser will decorate this node with the following information --

  // The following members represent different components of SELECT statement. However, Cassandra
  // doesn't support all of SQL syntax and semantics.
  //
  // SELECT [DISTINCT] <selected_exprs_>
  //   FROM      <from_clause_>
  //   WHERE     <where_clause_>
  //   GROUP BY  <group_by_clause_> HAVING <having_clause_>
  //   ORDER BY  <order_by_clause_>
  //   LIMIT     <limit_clause_>
  const bool distinct_ = false;
  const PTExprListNode::SharedPtr selected_exprs_;
  const PTTableRefListNode::SharedPtr from_clause_;
  const PTListNode::SharedPtr group_by_clause_;
  const PTListNode::SharedPtr having_clause_;
  PTOrderByListNode::SharedPtr order_by_clause_;
  PTExpr::SharedPtr limit_clause_;

  // -- The semantic analyzer will decorate this node with the following information --

  bool is_forward_scan_ = true;
  bool is_aggregate_ = false;

  // Index info.
  bool use_index_ = false;
  bool read_just_index_ = false;
  TableId index_id_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_SELECT_H_
