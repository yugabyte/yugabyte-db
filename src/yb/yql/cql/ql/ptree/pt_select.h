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

#pragma once

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"

#include "yb/qlexpr/qlexpr_fwd.h"

namespace yb {
namespace ql {

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
            YBLocationPtr loc,
            const PTExprPtr& order_expr,
            const Direction direction,
            const NullPlacement null_placement);
  virtual ~PTOrderBy();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTOrderBy;
  }

  template<typename... TypeArgs>
  inline static PTOrderBy::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTOrderBy>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override;

  Direction direction() const {
    return direction_;
  }

  NullPlacement null_placement() const {
    return null_placement_;
  }

  PTExprPtr order_expr() const {
    return order_expr_;
  }

 private:
  PTExprPtr order_expr_;
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
             YBLocationPtr loc,
             const PTQualifiedName::SharedPtr& name,
             MCSharedPtr<MCString> alias);
  virtual ~PTTableRef();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTTableRef;
  }

  template<typename... TypeArgs>
  inline static PTTableRef::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTTableRef>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override;

  client::YBTableName table_name() const {
    return name_->ToTableName();
  }

 private:
  PTQualifiedName::SharedPtr name_;
  MCSharedPtr<MCString> alias_;
};

using PTTableRefListNode = TreeListNode<PTTableRef>;

//--------------------------------------------------------------------------------------------------
// State variables for INDEX analysis.
class SelectScanInfo : public MCBase, public AnalyzeStepState {
 public:
  // Public types.
  typedef MCSharedPtr<SelectScanInfo> SharedPtr;

  // Constructor.
  explicit SelectScanInfo(MemoryContext *memctx,
                          size_t num_columns,
                          MCList<PartitionKeyOp> *partition_key_ops,
                          MCVector<const PTExpr*> *scan_filtering_exprs,
                          MCMap<MCString, ColumnDesc> *scan_column_map);

  // Collecting references to columns.
  const ColumnDesc* GetColumnDesc(const SemContext *sem_context, const MCString& col_name);

  // Collecting references to filter expressions.
  Status AddFilteringExpr(SemContext *sem_context, const PTRelationExpr *expr);

  // Collecting references of operators on WHERE clause.
  Status AddWhereExpr(SemContext *sem_context,
                      const PTRelationExpr *expr,
                      const ColumnDesc *col_desc,
                      PTExprPtr value,
                      PTExprListNode::SharedPtr col_args = nullptr);

  // Setup for analyzing where clause.
  void set_analyze_where(bool val) { analyze_where_ = val; }
  bool analyze_where() const { return analyze_where_; }

  // Setup for analyzing if clause.
  void set_analyze_if(bool val) { analyze_if_ = val; }
  bool analyze_if() const { return analyze_if_; }

  // Setup for analyzing order by clause.
  void StartOrderbyAnalysis(MCMap<MCString, ColumnDesc> *column_map) {
    analyze_orderby_ = true;
    scan_column_map_ = column_map;
  }
  void FinishOrderbyAnalysis() {
    analyze_orderby_ = false;
    scan_column_map_ = nullptr;
  }

  // Direct access functions.
  const MCList<ColumnOp>& col_ops() const {
    return col_ops_;
  }

  const MCVector<ColumnOpCounter>& col_op_counters() const {
    return col_op_counters_;
  }

  const MCList<JsonColumnOp>& col_json_ops() const {
    return col_json_ops_;
  }

  const MCList<SubscriptedColumnOp>& col_subscript_ops() const {
    return col_subscript_ops_;
  }

 private:
  // Processing state.
  bool analyze_where_ = false;
  bool analyze_if_ = false;
  bool analyze_orderby_ = false;

  // Reference list from where clause
  MCList<ColumnOp> col_ops_;
  MCVector<ColumnOpCounter> col_op_counters_;

  MCList<JsonColumnOp> col_json_ops_;
  MCList<SubscriptedColumnOp> col_subscript_ops_;

  // All filter expression from WHERE and IF clauses.
  MCVector<const PTExpr*> *scan_filtering_exprs_ = nullptr;

  // Index columns.
  MCMap<MCString, ColumnDesc> *scan_column_map_ = nullptr;
};

//--------------------------------------------------------------------------------------------------
// Chosen index.
class SelectScanSpec {
 public:
  SelectScanSpec() { }

  const TableId& index_id() const {
    return index_id_;
  }

  void set_index_id(const TableId& val) {
    index_id_ = val;
  }

  bool use_primary_scan() const {
    return index_id_.empty();
  }

  bool covers_fully() const {
    return covers_fully_;
  }

  void set_covers_fully(bool val) {
    covers_fully_ = val;
  }

  bool is_forward_scan() const {
    return is_forward_scan_;
  }

  void set_is_forward_scan(bool val) {
    is_forward_scan_ = val;
  }

  void set_prefix_length(size_t prefix_length) {
    prefix_length_ = prefix_length;
  }

  size_t prefix_length() const {
    return prefix_length_;
  }

 private:
  TableId index_id_;
  bool covers_fully_ = false;
  bool is_forward_scan_ = true;
  size_t prefix_length_ = 0;
};

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
               YBLocationPtr loc,
               bool distinct,
               PTExprListNode::SharedPtr selected_exprs,
               PTTableRefListNode::SharedPtr from_clause,
               PTExprPtr where_clause,
               PTExprPtr if_clause,
               PTListNode::SharedPtr group_by_clause,
               PTListNode::SharedPtr having_clause,
               PTOrderByListNode::SharedPtr order_by_clause,
               PTExprPtr limit_clause,
               PTExprPtr offset_clause);

  // Construct a nested select tnode to select from the index.
  PTSelectStmt(MemoryContext *memctx,
               const PTSelectStmt& parent,
               PTExprListNode::SharedPtr selected_exprs,
               const SelectScanSpec& scan_spec);

  virtual ~PTSelectStmt();

  template<typename... TypeArgs>
  inline static PTSelectStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTSelectStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  // This function traverses the parse tree for SELECT statement a few times for different purposes.
  // (1) Analyze references.
  //     This step validate the references to tables, columns, and operators.
  // (2) Analyze scan plan.
  //     This step chooses an index to scan and save the scan-spec.
  // (3) Analyze clauses
  //     This step analyzes clauses according to the chosen scan spec to prepare for execution.
  //
  // NOTE:
  // The current design for SELECT analysis is bit different from common compilation practice.
  // extended and required more analysis, we can redo this work then.
  // - Normally, step (1) should have collected information for processes in steps (2) and (3).
  //   However, the existing implementation in YugaByte is different when choosing scan path.
  // - After step (2), the current design creates a duplicate of the parse tree for nested query
  //   and compile both SELECT twins to analyze PRIMARY and SECONDARY scan within the same context.
  // This adds unnecessary complexities to the compilation process. However, it affects all layers
  // in CQL, so we will keep it that way for now to avoid new bugs and extra work. If the CQL
  // language is extended further toward SQL, we can change this design.
  virtual Status Analyze(SemContext *sem_context) override;
  bool CoversFully(const qlexpr::IndexInfo& index_info,
                   const MCUnorderedMap<int32, uint16> &column_ref_cnts) const;

  // Explain scan path.
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  ExplainPlanPB AnalysisResultToPB() override;

  // Execution opcode.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTSelectStmt;
  }

  virtual void SetOrderByClause(PTOrderByListNode::SharedPtr order_by_clause) {
    order_by_clause_ = order_by_clause;
  }

  virtual void SetLimitClause(PTExprPtr limit_clause) {
    limit_clause_ = limit_clause;
  }

  virtual void SetOffsetClause(PTExprPtr offset_clause) {
    offset_clause_ = offset_clause;
  }

  bool distinct() const {
    return distinct_;
  }

  bool is_forward_scan() const {
    return is_forward_scan_;
  }

  const PTExprPtr& limit() const {
    return limit_clause_;
  }

  const PTExprPtr& offset() const {
    return offset_clause_;
  }

  const MCList<PTExprPtr>& selected_exprs() const {
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

  // For top-level SELECT it's the same as 'is_aggregate()'.
  // For child-SELECT it's the parent's 'is_aggregate()' value.
  bool is_top_level_aggregate() const {
    return IsTopLevelReadNode() ? is_aggregate_ : is_parent_aggregate_;
  }

  const SelectScanInfo *select_scan_info() const {
    return select_scan_info_;
  }

  const PTSelectStmt::SharedPtr& child_select() const {
    return child_select_;
  }

  const TableId& index_id() const {
    return index_id_;
  }

  bool covers_fully() const {
    return covers_fully_;
  }

  size_t prefix_length() const {
    return prefix_length_;
  }

  // Certain tables can be read by any authorized role specifically because they are being used
  // by the Cassandra driver:
  // system_schema.keyspaces
  // system_schema.columns
  // system_schema.tables
  // system.local
  // system.peers
  bool IsReadableByAllSystemTable() const;

  const std::shared_ptr<client::YBTable>& bind_table() const override;

  const std::shared_ptr<client::YBTable>& table() const {
    return PTDmlStmt::bind_table();
  }

  const MCVector<PTBindVar*> &bind_variables() const override {
    return child_select_ ? child_select_->bind_variables() : PTDmlStmt::bind_variables();
  }
  MCVector<PTBindVar*> &bind_variables() override {
    return child_select_ ? child_select_->bind_variables() : PTDmlStmt::bind_variables();
  }

  std::vector<int64_t> hash_col_indices() const override {
    return child_select_ ? child_select_->hash_col_indices() : PTDmlStmt::hash_col_indices();
  }

  // CQL does not have nested DML statement, but YugaByte processes INDEX query as a nested DML.
  // - Returns true by default for all SELECT nodes.
  // - Returns false for index query node nested inside SELECT node. Because the data from the
  //   INDEX nested node is used for another READ, it is not the top level READ node where all
  //   READ operations end.
  //     SELECT * FROM table WHERE primary_keys IN (SELECT primary_keys FROM index);
  //   The data from INDEX query (i.e. primary key) is used for another READ from PRIMARY table.
  // - There is one exception when nested node is promoted to become the top level READ node.
  //   Due to an optimization, when an INDEX fully-covers a SELECT statement, all data will be
  //   read from the INDEX.
  //     SELECT all-requested-data FROM <index>;
  //   In this case, the nested index node is the top treenode to query data from server. It is not
  //   the top level SELECT in this case.
  bool IsTopLevelReadNode() const override {
    return is_top_level_ || covers_fully_;
  }

  bool IsWriteOp() const override {
    return false;
  }

 private:
  // Analyze the components of a SELECT.
  Status LookupIndex(SemContext *sem_context);

  // Analyze clauses.
  Status AnalyzeFromClause(SemContext *sem_context);
  Status AnalyzeSelectList(SemContext *sem_context);
  Status AnalyzeDistinctClause(SemContext *sem_context);
  Status AnalyzeLimitClause(SemContext *sem_context);
  Status AnalyzeOffsetClause(SemContext *sem_context);
  Status ConstructSelectedSchema();

  // Routines for analysis and choosing scan plan.
  Status AnalyzeReferences(SemContext *sem_context);
  Status AnalyzeIndexes(SemContext *sem_context, SelectScanSpec *scan_spec);
  Status AnalyzeOrderByClause(SemContext *sem_context,
                              const TableId& index_id,
                              bool *is_forward_scan);
  Status SetupScanPath(SemContext *sem_context, const SelectScanSpec& scan_spec);

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
  //   OFFSET    <offset_clause_>
  const bool distinct_ = false;
  const PTExprListNode::SharedPtr selected_exprs_;
  const PTTableRefListNode::SharedPtr from_clause_;
  const PTListNode::SharedPtr group_by_clause_;
  const PTListNode::SharedPtr having_clause_;
  PTOrderByListNode::SharedPtr order_by_clause_;
  PTExprPtr limit_clause_;
  PTExprPtr offset_clause_;

  // -- The semantic analyzer will decorate this node with the following information --
  // Collecting all expressions that are selected.
  MCVector<const PTExpr*> covering_exprs_;

  // Collecting all expressions that a chosen index must cover to process the statement.
  MCVector<const PTExpr*> filtering_exprs_;

  bool is_forward_scan_ = true;
  bool is_aggregate_ = false;
  bool is_parent_aggregate_ = false;

  // Child select statement. Currently only a select statement using an index (covered or uncovered)
  // has a child select statement to query an index.
  PTSelectStmt::SharedPtr child_select_;

  // For nested select from an index: the index id and whether it covers the query fully.
  TableId index_id_;
  bool covers_fully_ = false;

  size_t prefix_length_ = 0;

  // Name of all columns the SELECT statement is referenced. Similar to the list "column_refs_",
  // but this is a list of column names instead of column ids.
  MCSet<std::string> referenced_index_colnames_;
  SelectScanInfo *select_scan_info_ = nullptr;

  // Flag for a top level SELECT.
  // Although CQL does not have nested SELECT, YugaByte treats INDEX query as a nested DML.
  bool is_top_level_ = true;
};

}  // namespace ql
}  // namespace yb
