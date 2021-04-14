//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for CREATE INDEX statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_CREATE_INDEX_H_
#define YB_YQL_CQL_QL_PTREE_PT_CREATE_INDEX_H_

#include "yb/client/client.h"
#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_column_definition.h"

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
                YBLocation::SharedPtr loc,
                bool is_backfill_deferred,
                bool is_unique,
                const MCSharedPtr<MCString>& name,
                const PTQualifiedName::SharedPtr& table_name,
                const PTListNode::SharedPtr& columns,
                bool create_if_not_exists,
                const PTTablePropertyListNode::SharedPtr& ordering_list,
                const PTListNode::SharedPtr& covering);
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
  const PTListNode::SharedPtr& covering() const {
    return covering_;
  }

  client::YBTableName yb_table_name() const override {
    return client::YBTableName(YQL_DATABASE_CQL,
                               PTCreateTable::yb_table_name().namespace_name().c_str(),
                               name_->c_str());
  }

  client::YBTableName indexed_table_name() const {
    return PTCreateTable::yb_table_name();
  }

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

  CHECKED_STATUS AppendIndexColumn(SemContext *sem_context, PTColumnDefinition *column);

  virtual CHECKED_STATUS ToTableProperties(TableProperties *table_properties) const override;

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

 private:
  // Is it a unique index?
  const bool is_unique_ = false;
  // Should backfill be deferred
  const bool is_backfill_deferred_ = false;
  // Index name.
  MCSharedPtr<MCString> name_;
  // Additional covering columns.
  const PTListNode::SharedPtr covering_;

  // The semantic analyzer will decorate the following information.
  bool is_local_ = false;
  std::shared_ptr<client::YBTable> table_;
  MCVector<ColumnDesc> column_descs_;

  // Auto-include columns are primary-key columns in the data-table being indexed that are not yet
  // declared as part of the INDEX.
  MCList<PTIndexColumn::SharedPtr> auto_includes_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_CREATE_INDEX_H_
