//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for CREATE INDEX statements.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/ptree/pt_create_index.h"

#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/common/schema.h"
#include "yb/gutil/strings/ascii_ctype.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(cql_raise_index_where_clause_error, false,
            "Raise unsupported error if where clause is specified for create index");

namespace yb {
namespace ql {

using std::string;
using client::YBSchema;

//--------------------------------------------------------------------------------------------------

PTCreateIndex::PTCreateIndex(MemoryContext *memctx,
                             YBLocationPtr loc,
                             bool is_backfill_deferred,
                             bool is_unique,
                             const MCSharedPtr<MCString>& name,
                             const PTQualifiedNamePtr& table_name,
                             const PTListNodePtr& columns,
                             const bool create_if_not_exists,
                             const PTTablePropertyListNodePtr& ordering_list,
                             const PTListNodePtr& covering,
                             const PTExpr::SharedPtr& where_clause)
    : PTCreateTable(memctx, loc, table_name, columns, create_if_not_exists, ordering_list),
      is_unique_(is_unique),
      is_backfill_deferred_(is_backfill_deferred),
      name_(name),
      covering_(covering),
      is_local_(false),
      column_descs_(memctx),
      auto_includes_(memctx),
      where_clause_(where_clause),
      where_clause_column_refs_(nullptr) {
}

PTCreateIndex::~PTCreateIndex() {
}

namespace {

Status SetupCoveringColumn(PTIndexColumn *node, SemContext *sem_context) {
  RETURN_NOT_OK(node->SetupCoveringIndexColumn(sem_context));
  return Status::OK();
}

} // namespace

Status PTCreateIndex::Analyze(SemContext *sem_context) {
  // Look up indexed table.
  bool is_system_ignored;
  RETURN_NOT_OK(relation_->AnalyzeName(sem_context, ObjectType::TABLE));

  RETURN_NOT_OK(sem_context->LookupTable(relation_->ToTableName(), relation_->loc(),
                                         true /* write_table */,
                                         PermissionType::ALTER_PERMISSION,
                                         &table_, &is_system_ignored,
                                         &column_descs_));

  // Save context state, and set "this" as current create-table statement in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_create_table_stmt(this);

  // Analyze index table like a regular table for the primary key definitions.
  // If flag use_cassandra_authentication is enabled, we will not check for the create permission
  // on the table because creating an index requires the alter permission on the table.
  RETURN_NOT_OK(PTCreateTable::Analyze(sem_context));

  if (!name_) {
    string auto_name = relation_->last_name().c_str();

    for (const auto& column : hash_columns_) {
      auto_name += string("_") + column->yb_name();
    }

    for (const auto& column : primary_columns_) {
      auto_name += string("_") + column->yb_name();
    }

    auto_name += "_idx";
    string final_name;

    for (char& c : auto_name) {
      if (ascii_isalnum(c) || c == '_') { // Accepted a-z, A-Z, 0-9, _.
        final_name += c;
      }
    }

    LOG(INFO) << "Set automatic name for the new index: " << final_name;
    name_ = MCMakeShared<MCString>(sem_context->PTreeMem(), final_name.c_str());
  }

  // Add covering columns.
  if (covering_ != nullptr) {
    RETURN_NOT_OK((covering_->Apply<SemContext, PTIndexColumn>(sem_context, &SetupCoveringColumn)));
  }

  // Add remaining primary key columns from the indexed table. For non-unique index, add the columns
  // to the primary key of the index table to make the non-unique values unique. For unique index,
  // they should be added as non-primary-key columns.
  const YBSchema& schema = table_->schema();
  for (size_t idx = 0; idx < schema.num_key_columns(); idx++) {
    // Not adding key-column schema.columns(idx) to the INDEX metadata if it is already referred to
    // by one of the index-columns.
    const MCString key_name(schema.Column(idx).name().c_str(), sem_context->PTempMem());
    PTColumnDefinition *coldef = sem_context->GetColumnDefinition(key_name);
    if (coldef && coldef->colexpr()->opcode() == TreeNodeOpcode::kPTRef) {
      // Column is already defined as a part of INDEX.
      continue;
    }

    // Create a new treenode PTIndexColumn for column definition.
    MCSharedPtr<MCString> col_name =
      MCMakeShared<MCString>(sem_context->PSemMem(), key_name.c_str());
    PTQualifiedName::SharedPtr ref_name =
      PTQualifiedName::MakeShared(sem_context->PSemMem(), loc_ptr(), col_name);
    PTRef::SharedPtr col_ref = PTRef::MakeShared(sem_context->PSemMem(), loc_ptr(), ref_name);
    PTIndexColumn::SharedPtr col =
      PTIndexColumn::MakeShared(sem_context->PSemMem(), loc_ptr(), col_name, col_ref);
    RETURN_NOT_OK(col->Analyze(sem_context));

    // Add this key column to INDEX as it is not yet in the INDEX description.
    auto_includes_.push_back(col);
    RETURN_NOT_OK(AppendIndexColumn(sem_context, col.get()));
  }

  // Check whether the index is local, i.e. whether the hash keys match (including being in the
  // same order).
  is_local_ = true;
  if (schema.num_hash_key_columns() != hash_columns_.size()) {
    is_local_ = false;
  } else {
    int idx = 0;
    for (const auto& column : hash_columns_) {
      if (column->yb_name() != column_descs_[idx].name()) {
        is_local_ = false;
        break;
      }
      idx++;
    }
  }

  // Verify transactions and consistency settings.
  TableProperties table_properties;
  RETURN_NOT_OK(ToTableProperties(&table_properties));
  if (table_->InternalSchema().table_properties().is_transactional()) {
    if (!table_properties.is_transactional()) {
      return sem_context->Error(this,
                                "Transactions must be enabled in an index of a "
                                "transactions-enabled table.",
                                ErrorCode::INVALID_TABLE_DEFINITION);
    }
    if (table_properties.consistency_level() == YBConsistencyLevel::USER_ENFORCED) {
      return sem_context->Error(this,
                                "User-enforced consistency level not allowed in a "
                                "transactions-enabled index.",
                                ErrorCode::INVALID_TABLE_DEFINITION);
    }
  } else {
    if (table_properties.is_transactional()) {
      return sem_context->Error(this,
                                "Transactions cannot be enabled in an index of a table without "
                                "transactions enabled.",
                                ErrorCode::INVALID_TABLE_DEFINITION);
    }
    if (table_properties.consistency_level() != YBConsistencyLevel::USER_ENFORCED) {
      return sem_context->Error(this,
                                "Consistency level must be user-enforced in an index without "
                                "transactions enabled.",
                                ErrorCode::INVALID_TABLE_DEFINITION);
    }
  }

  if (is_local_) {
    LOG(WARNING) << "Creating local secondary index " << yb_table_name().ToString()
                 << " as global index.";
    is_local_ = false;
  }

  // If partial index (i.e., where clause predicate present in CREATE INDEX), analyze the index's
  // predicate.
  if (where_clause_.get()) {
    IdxPredicateState idx_predicate_state(sem_context->PTempMem(), opcode());
    SemState sem_state(sem_context, QLType::Create(DataType::BOOL), InternalType::kBoolValue);
    sem_state.SetIdxPredicateState(&idx_predicate_state);
    RETURN_NOT_OK(where_clause_->Analyze(sem_context));
    where_clause_column_refs_ = idx_predicate_state.column_refs();
  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

Status PTCreateIndex::AppendIndexColumn(SemContext *sem_context, PTColumnDefinition *column) {
  if (!is_unique_ && sem_context->GetColumnDesc(*column->name())->is_primary()) {
    return AppendPrimaryColumn(sem_context, column);
  }

  return AppendColumn(sem_context, column);
}

void PTCreateIndex::PrintSemanticAnalysisResult(SemContext *sem_context) {
  PTCreateTable::PrintSemanticAnalysisResult(sem_context);
}

Status PTCreateIndex::ToTableProperties(TableProperties *table_properties) const {
  table_properties->SetTransactional(true);
  table_properties->SetUseMangledColumnName(true);
  return PTCreateTable::ToTableProperties(table_properties);
}

const std::string& PTCreateIndex::indexed_table_id() const {
  return table_->id();
}

client::YBTableName PTCreateIndex::yb_table_name() const {
  return client::YBTableName(YQL_DATABASE_CQL,
                             PTCreateTable::yb_table_name().namespace_name().c_str(),
                             name_->c_str());
}

client::YBTableName PTCreateIndex::indexed_table_name() const {
  return PTCreateTable::yb_table_name();
}

}  // namespace ql
}  // namespace yb
