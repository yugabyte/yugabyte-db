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

#ifndef YB_YQL_PGSQL_PTREE_PG_TSELECT_H_
#define YB_YQL_PGSQL_PTREE_PG_TSELECT_H_

#include "yb/yql/pgsql/ptree/list_node.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_tname.h"
#include "yb/yql/pgsql/ptree/pg_texpr.h"
#include "yb/yql/pgsql/ptree/pg_tdml.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// This class represents VALUES clause
class PgTValues : public PgTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTValues> SharedPtr;
  typedef MCSharedPtr<const PgTValues> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTValues(MemoryContext *memctx,
           PgTLocation::SharedPtr loc,
           PgTExprListNode::SharedPtr tuple);
  virtual ~PgTValues();

  template<typename... TypeArgs>
  inline static PgTValues::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PgTValues>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Add a tree node at the end.
  void Append(const PgTExprListNode::SharedPtr& tnode);
  void Prepend(const PgTExprListNode::SharedPtr& tnode);

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Access function for tuples_.
  const TreeListNode<PgTExprListNode>& tuples() {
    return tuples_;
  }

  // Number of provided tuples.
  virtual int TupleCount() const {
    return tuples_.size();
  }
  PgTExprListNode::SharedPtr Tuple(int index) const;

 private:
  TreeListNode<PgTExprListNode> tuples_;
};

//--------------------------------------------------------------------------------------------------
// ORDER BY.
class PgTOrderBy : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTOrderBy> SharedPtr;
  typedef MCSharedPtr<const PgTOrderBy> SharedPtrConst;

  enum Direction : int8_t { kASC = 0, kDESC };

  enum NullPlacement : int8_t { kFIRST = 0, kLAST };

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTOrderBy(MemoryContext *memctx,
            PgTLocation::SharedPtr loc,
            const PgTExpr::SharedPtr& name,
            const Direction direction,
            const NullPlacement null_placement);
  virtual ~PgTOrderBy();

  template<typename... TypeArgs>
  inline static PgTOrderBy::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTOrderBy>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  Direction direction() const {
    return direction_;
  }

  NullPlacement null_placement() const {
    return null_placement_;
  }

  PgTExpr::SharedPtr name() const {
    return name_;
  }

 private:
  PgTExpr::SharedPtr name_;
  Direction direction_;
  NullPlacement null_placement_;
};

using PgTOrderByListNode = TreeListNode<PgTOrderBy>;

//--------------------------------------------------------------------------------------------------
// FROM <table ref list>.
class PgTTableRef : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTTableRef> SharedPtr;
  typedef MCSharedPtr<const PgTTableRef> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTTableRef(MemoryContext *memctx,
              PgTLocation::SharedPtr loc,
              const PgTQualifiedName::SharedPtr& name,
              MCSharedPtr<MCString> alias);
  virtual ~PgTTableRef();

  template<typename... TypeArgs>
  inline static PgTTableRef::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTTableRef>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  client::YBTableName table_name() const {
    return name_->ToTableName();
  }

 private:
  PgTQualifiedName::SharedPtr name_;
  MCSharedPtr<MCString> alias_;
};

using PgTTableRefListNode = TreeListNode<PgTTableRef>;

//--------------------------------------------------------------------------------------------------
// This class represents SELECT statement.
class PgTSelectStmt : public PgTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTSelectStmt> SharedPtr;
  typedef MCSharedPtr<const PgTSelectStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTSelectStmt(MemoryContext *memctx,
               PgTLocation::SharedPtr loc,
               bool distinct,
               PgTExprListNode::SharedPtr target,
               PgTTableRefListNode::SharedPtr from_clause,
               PgTExpr::SharedPtr where_clause,
               PTListNode::SharedPtr group_by_clause,
               PTListNode::SharedPtr having_clause,
               PgTOrderByListNode::SharedPtr order_by_clause,
               PgTExpr::SharedPtr limit_clause);
  virtual ~PgTSelectStmt();

  template<typename... TypeArgs>
  inline static PgTSelectStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PgTSelectStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Execution opcode.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTSelectStmt;
  }

  virtual void SetOrderByClause(PgTOrderByListNode::SharedPtr order_by_clause) {
    order_by_clause_ = order_by_clause;
  }

  virtual void SetLimitClause(PgTExpr::SharedPtr limit_clause) {
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

  virtual PgTExpr::SharedPtr limit() const {
    if (!has_limit()) {
      return nullptr;
    }
    return limit_clause_;
  }

  const MCList<PgTExpr::SharedPtr>& selected_exprs() const {
    return selected_exprs_->node_list();
  }

  // Returns table name.
  virtual client::YBTableName table_name() const override {
    // CQL only allows one table at a time.
    return from_clause_->element(0)->table_name();
  }

  // Returns location of table name.
  virtual const PgTLocation& table_loc() const override {
    return from_clause_->loc();
  }


  PgTOrderByListNode::SharedPtr order_by_clause() const {
    return order_by_clause_;
  }

  bool is_aggregate() const {
    return is_aggregate_;
  }

 private:

  CHECKED_STATUS AnalyzeDistinctClause(PgCompileContext *compile_context);
  CHECKED_STATUS AnalyzeOrderByClause(PgCompileContext *compile_context);
  CHECKED_STATUS AnalyzeLimitClause(PgCompileContext *compile_context);
  CHECKED_STATUS ConstructSelectedSchema();

  // The following members represent different components of SELECT statement. However, Cassandra
  // doesn't support all of SQL syntax and semantics.
  //
  // SELECT [DISTINCT] <selected_exprs_>
  //   FROM      <from_clause_>
  //   WHERE     <where_clause_>
  //   GROUP BY  <group_by_clause_> HAVING <having_clause_>
  //   ORDER BY  <order_by_clause_>
  //   LIMIT     <limit_clause_>
  const bool distinct_;
  bool is_forward_scan_;
  PgTExprListNode::SharedPtr selected_exprs_;
  PgTTableRefListNode::SharedPtr from_clause_;
  PTListNode::SharedPtr group_by_clause_;
  PTListNode::SharedPtr having_clause_;
  PgTOrderByListNode::SharedPtr order_by_clause_;
  PgTExpr::SharedPtr limit_clause_;
  bool is_aggregate_ = false;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TSELECT_H_
