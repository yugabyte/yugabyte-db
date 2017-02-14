//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the semantic analytical process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_SEM_CONTEXT_H_
#define YB_SQL_PTREE_SEM_CONTEXT_H_

#include "yb/sql/util/sql_env.h"
#include "yb/sql/ptree/process_context.h"
#include "yb/sql/ptree/column_desc.h"
#include "yb/sql/ptree/pt_create_table.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

struct SymbolEntry {
  // Parse tree node for column. It's used for table creation.
  PTColumnDefinition *column_;

  // Parse tree node for table. It's used for table creation.
  PTCreateTable *table_;

  // Column description. It's used for DML statements including select.
  // Not part of a parse tree, but it is allocated within the parse tree pool because it us
  // persistent metadata. It represents a column during semantic and execution phases.
  ColumnDesc *column_desc_;

  SymbolEntry() : column_(nullptr), table_(nullptr), column_desc_(nullptr) {
  }
};

//--------------------------------------------------------------------------------------------------

enum class ConversionMode : int {
  kImplicit = 0,                // Implicit conversion (automatic).
  kExplicit = 1,                // Explicit conversion is available.
  kNotAllowed = 2,              // Not available.
};

//--------------------------------------------------------------------------------------------------

class SemContext : public ProcessContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<SemContext> UniPtr;
  typedef std::unique_ptr<const SemContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  SemContext(const char *sql_stmt,
             size_t stmt_len,
             ParseTree::UniPtr parse_tree,
             SqlEnv *sql_env,
             int retry_count);
  virtual ~SemContext();

  //------------------------------------------------------------------------------------------------
  // Symbol table support.
  CHECKED_STATUS MapSymbol(const MCString& name, PTColumnDefinition *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, PTCreateTable *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, ColumnDesc *entry);

  // Access functions to current processing symbol.
  SymbolEntry *current_processing_id() {
    return &current_processing_id_;
  }
  void set_current_processing_id(const SymbolEntry& new_id) {
    current_processing_id_ = new_id;
  }

  //------------------------------------------------------------------------------------------------
  // Access functions to current processing table and column.
  PTColumnDefinition *current_column() {
    return current_processing_id_.column_;
  }
  void set_current_column(PTColumnDefinition *column) {
    current_processing_id_.column_ = column;
  }

  PTCreateTable *current_table() {
    return current_processing_id_.table_;
  }
  void set_current_table(PTCreateTable *table) {
    current_processing_id_.table_ = table;
  }

  // Find table descriptor from metadata server.
  std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name) {
    // If "retry_count_" is greater than 0, we want to reload metadata from master server.
    return sql_env_->GetTableDesc(table_name, retry_count_ > 0);
  }

  // Find table descriptor from metadata server.
  std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name,
                                                bool refresh_metadata) {
    return sql_env_->GetTableDesc(table_name, refresh_metadata);
  }

  // Find column descriptor from symbol table.
  PTColumnDefinition *GetColumnDefinition(const MCString& col_name) const;

  // Find column descriptor from symbol table.
  const ColumnDesc *GetColumnDesc(const MCString& col_name) const;

  // Check if the rhs and lhs datatypes are compatible. Their conversion mode must be implicit.
  bool IsConvertible(client::YBColumnSchema::DataType lhs_type,
                     client::YBColumnSchema::DataType rhs_type) const {
    return (GetConversionMode(lhs_type, rhs_type) == ConversionMode::kImplicit);
  }

  bool IsComparable(client::YBColumnSchema::DataType lhs_type,
                    client::YBColumnSchema::DataType rhs_type) const;

  const std::string& CurrentKeyspace() const {
    return sql_env_->CurrentKeyspace();
  }

  // Access function to retry counter.
  int retry_count() {
    return retry_count_;
  }

 private:
  // Find conversion mode from 'rhs_type' to 'lhs_type'.
  ConversionMode GetConversionMode(client::YBColumnSchema::DataType lhs_type,
                                   client::YBColumnSchema::DataType rhs_type) const;

  // Find symbol.
  const SymbolEntry *SeekSymbol(const MCString& name) const;

  // Symbol table.
  MCMap<MCString, SymbolEntry, MCString::MapCmp> symtab_;

  // Current processing symbol.
  SymbolEntry current_processing_id_;

  // Session.
  SqlEnv *sql_env_;

  // Force to refresh metadata.
  int retry_count_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_SEM_CONTEXT_H_
