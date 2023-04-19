//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for CREATE INDEX statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/pt_create_table.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// CREATE INDEX statement.

class PTCreateIndex : public PTCreateTable {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateIndex> SharedPtr;
  typedef MCSharedPtr<const PTCreateIndex> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateIndex(MemoryContext *memctx,
                YBLocationPtr loc,
                bool is_backfill_deferred,
                bool is_unique,
                const MCSharedPtr<MCString>& name,
                const PTQualifiedNamePtr& table_name,
                const PTListNodePtr& columns,
                bool create_if_not_exists,
                const PTTablePropertyListNodePtr& ordering_list,
                const PTListNodePtr& covering,
                const PTExprPtr& where_clause);
  virtual ~PTCreateIndex();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCreateIndex;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCreateIndex::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateIndex>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Accessor methods.
  const MCSharedPtr<MCString>& name() const {
    return name_;
  }
  bool is_unique() const {
    return is_unique_;
  }
  const PTListNodePtr& covering() const {
    return covering_;
  }

  client::YBTableName yb_table_name() const override;

  client::YBTableName indexed_table_name() const;

  const std::shared_ptr<client::YBTable>& indexed_table() const {
    return table_;
  }

  const std::string& indexed_table_id() const;

  bool is_local() const {
    return is_local_;
  }

  bool is_backfill_deferred() const {
    return is_backfill_deferred_;
  }

  const MCVector<ColumnDesc>& column_descs() const {
    return column_descs_;
  }

  const PTExprPtr& where_clause() const {
    return where_clause_;
  }

  Status AppendIndexColumn(SemContext *sem_context, PTColumnDefinition *column);

  virtual Status ToTableProperties(TableProperties *table_properties) const override;

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  const std::shared_ptr<std::set<uint32>>& where_clause_column_refs() const {
    return where_clause_column_refs_;
  }

 private:
  // Is it a unique index?
  const bool is_unique_ = false;
  // Should backfill be deferred
  const bool is_backfill_deferred_ = false;
  // Index name.
  MCSharedPtr<MCString> name_;
  // Additional covering columns.
  const PTListNodePtr covering_;

  // The semantic analyzer will decorate the following information.
  bool is_local_ = false;
  std::shared_ptr<client::YBTable> table_;
  MCVector<ColumnDesc> column_descs_;

  // Auto-include columns are primary-key columns in the data-table being indexed that are not yet
  // declared as part of the INDEX.
  MCList<PTIndexColumnPtr> auto_includes_;

  // Where clause is specified for partial indexes.
  PTExprPtr where_clause_;

  // Columns that are being referenced by the index predicate. There are populated in
  // IdxPredicateState during semantic analysis. We use this as a variable to pass them on to
  // the execution phase (since object of IdxPredicateState lives only till semantic analysis).
  std::shared_ptr<std::set<uint32>> where_clause_column_refs_;
};

class IdxPredicateState {
 public:
  explicit IdxPredicateState(MemoryContext *memctx, TreeNodeOpcode statement_type)
    : column_refs_(std::make_shared<std::set<uint32>>()) {
  }

  Status AnalyzeColumnOp(SemContext *sem_context,
                         const PTRelationExpr *expr,
                         const ColumnDesc *col_desc,
                         PTExprPtr value,
                         PTExprListNodePtr args = nullptr);

  std::shared_ptr<std::set<uint32>>& column_refs() {
    return column_refs_;
  }

 private:
  // Columns that are being referenced by the index predicate. These will later be stored in
  // IndexInfoPB so that other queries can use the column ids later when interacting with the
  // index.
  // TODO(Piyush): Use MCSet. Tried it, there were some issues when iterating over an MCSet.
  std::shared_ptr<std::set<uint32>> column_refs_;
};

}  // namespace ql
}  // namespace yb
