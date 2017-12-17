//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for CREATE INDEX statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_create_index.h"

#include "yb/client/client.h"

#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

using std::shared_ptr;
using std::to_string;
using client::YBColumnSchema;
using client::YBSchema;
using client::YBTableName;

//--------------------------------------------------------------------------------------------------

PTCreateIndex::PTCreateIndex(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             const MCSharedPtr<MCString>& name,
                             const PTQualifiedName::SharedPtr& table_name,
                             const PTListNode::SharedPtr& columns,
                             const bool create_if_not_exists,
                             const PTTablePropertyListNode::SharedPtr& ordering_list,
                             const PTListNode::SharedPtr& covering)
    : PTCreateTable(memctx, loc, table_name, columns, create_if_not_exists, ordering_list),
      name_(name),
      covering_(covering),
      column_descs_(memctx),
      column_definitions_(memctx) {
}

PTCreateIndex::~PTCreateIndex() {
}

CHECKED_STATUS PTCreateIndex::Analyze(SemContext *sem_context) {
  // Look up indexed table.
  bool is_system_ignored;
  RETURN_NOT_OK(relation_->AnalyzeName(sem_context, OBJECT_TABLE));
  RETURN_NOT_OK(sem_context->LookupTable(relation_->ToTableName(), relation_->loc(),
                                         true /* write_table */, &table_, &is_system_ignored,
                                         &column_descs_, &num_key_columns_, &num_hash_key_columns_,
                                         &column_definitions_));

  // Save context state, and set "this" as current create-table statement in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_create_table_stmt(this);

  // Analyze index table like a regular table for the primary key definitions.
  RETURN_NOT_OK(PTCreateTable::Analyze(sem_context));

  // Add remaining primary key columns from the indexed table.
  const YBSchema& schema = table_->schema();
  for (int idx = 0; idx < num_key_columns_; idx++) {
    const MCString col_name(schema.Column(idx).name().c_str(), sem_context->PTempMem());
    PTColumnDefinition* col = sem_context->GetColumnDefinition(col_name);
    if (!col->is_primary_key()) {
      RETURN_NOT_OK(AppendPrimaryColumn(sem_context, col));
    }
  }

  // Add covering columns.
  if (covering_ != nullptr) {
    RETURN_NOT_OK((covering_->Apply<SemContext, PTName>(sem_context,
                                                        &PTName::SetupCoveringIndexColumn)));
  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTCreateIndex::PrintSemanticAnalysisResult(SemContext *sem_context) {
  PTCreateTable::PrintSemanticAnalysisResult(sem_context);
}

}  // namespace ql
}  // namespace yb
