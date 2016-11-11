//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode implementation for INSERT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/sem_context.h"
#include "yb/sql/ptree/pt_insert.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

PTInsertStmt::PTInsertStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTQualifiedName::SharedPtr relation,
                           PTQualifiedNameListNode::SharedPtr columns,
                           PTCollection::SharedPtr value_clause)
    : TreeNode(memctx, loc),
      relation_(relation),
      columns_(columns),
      value_clause_(value_clause),
      tuple_(memctx) {
}

PTInsertStmt::~PTInsertStmt() {
}

ErrorCode PTInsertStmt::Analyze(SemContext *sem_context) {
  // Clear tuple_ as this call might be a reentrance due to metadata mismatch.
  tuple_.clear();

  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Get table descriptor.
  err = relation_->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }
  VLOG(3) << "Loading table descriptor for " << yb_table_name();
  std::shared_ptr<YBTable> table = sem_context->GetTableDesc(yb_table_name());
  if (table == nullptr) {
    err = ErrorCode::FDW_TABLE_NOT_FOUND;
    sem_context->Error(relation_->loc(), err);
    return err;
  }
  const YBSchema& schema = table->schema();
  const int num_columns = schema.num_columns();
  VLOG(3) << "Table " << yb_table_name() << " has " << num_columns << " columns";
  CHECK_GE(num_columns, 0) << "Wrong column number in table descriptor for " << yb_table_name();

  // Check the selected columns. Cassandra only supports inserting one tuple / row at a time.
  PTValues *value_clause = static_cast<PTValues *>(value_clause_.get());
  if (value_clause->TupleCount() == 0) {
    err = ErrorCode::TOO_FEW_ARGUMENTS;
    sem_context->Error(value_clause_->loc(), err);
    return err;
  }
  const MCList<PTExpr::SharedPtr>& exprs = value_clause->Tuple(0)->node_list();

  tuple_.resize(num_columns);
  if (columns_) {
    // Mismatch between column names and their values.
    const MCList<PTQualifiedName::SharedPtr>& names = columns_->node_list();
    if (names.size() != exprs.size()) {
      if (names.size() > exprs.size()) {
        err = ErrorCode::TOO_FEW_ARGUMENTS;
      } else {
        err = ErrorCode::TOO_MANY_ARGUMENTS;
      }
      sem_context->Error(value_clause_->loc(), err);
      return err;
    }

    // Mismatch between arguments and columns.
    MCList<PTExpr::SharedPtr>::const_iterator iter = exprs.begin();
    for (PTQualifiedName::SharedPtr name : names) {
      int idx;
      YBColumnSchema::DataType col_type;
      for (idx = 0; idx < num_columns; idx++) {
        const YBColumnSchema col = schema.Column(idx);
        if (strcmp(name->last_name().c_str(), col.name().c_str()) == 0) {
          col_type = col.type();
          break;
        }
      }

      // Check that the column exists.
      if (idx == num_columns) {
        err = ErrorCode::UNDEFINED_COLUMN;
        sem_context->Error(name->loc(), err);
        return err;
      }

      // Check that the column is not a duplicate.
      if (tuple_[idx].IsInitialized()) {
        err = ErrorCode::DUPLICATE_COLUMN;
        sem_context->Error((*iter)->loc(), err);
        return err;
      }

      // Check that the datatypes are compatible.
      if (!sem_context->IsCompatible(col_type, (*iter)->yb_data_type())) {
        err = ErrorCode::DATATYPE_MISMATCH;
        sem_context->Error((*iter)->loc(), err);
        return err;
      }

      // TODO(neil) Currently column_index is the same as idx but needs to be corrected.
      tuple_[idx].Init(idx, false, false, col_type, *iter);
      iter++;
    }
  } else {
    // Check number of arguments.
    if (exprs.size() != num_columns) {
      if (exprs.size() > num_columns) {
        err = ErrorCode::TOO_MANY_ARGUMENTS;
      } else {
        err = ErrorCode::TOO_FEW_ARGUMENTS;
      }
      sem_context->Error(value_clause_->loc(), err);
      return err;
    }

    // Check that the argument datatypes are compatible with all columns.
    int idx = 0;
    for (auto expr : exprs) {
      const YBColumnSchema col = schema.Column(idx);
      const YBColumnSchema::DataType col_type = col.type();
      if (!sem_context->IsCompatible(col_type, expr->yb_data_type())) {
        err = ErrorCode::DATATYPE_MISMATCH;
        sem_context->Error(expr->loc(), err);
        return err;
      }

      tuple_[idx].Init(idx, false, false, col_type, expr);
      idx++;
    }
  }

  // Now check that each primary key is associated with an argument.
  // NOTE: we assumed that primary_indexes and arguments are sorted by column_index.
  std::vector<int> primary_indexes;
  schema.GetPrimaryKeyColumnIndexes(&primary_indexes);
  MCVector<Argument>::iterator arg_iter = tuple_.begin();
  for (int primary_index : primary_indexes) {
    while (arg_iter != tuple_.end()) {
      if (arg_iter->index() == primary_index) {
        arg_iter++;
        break;
      }
      arg_iter++;
    }

    if (arg_iter == tuple_.end()) {
      err = ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY;
      sem_context->Error(value_clause_->loc(), err);
      return err;
    }
  }

  return err;
}

void PTInsertStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):";
  for (const Argument& arg : tuple_) {
    if (arg.IsInitialized()) {
      VLOG(3) << "ARG: " << arg.index()
              << ", Hash: " << arg.is_hash()
              << ", Primary: " << arg.is_primary()
              << ", Expected Type: " << arg.expected_type()
              << ", Expr Type: " << arg.expr()->yb_data_type();
    }
  }
}

}  // namespace sql
}  // namespace yb
