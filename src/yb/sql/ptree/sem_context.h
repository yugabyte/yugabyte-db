//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the semantic analytical process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_SEM_CONTEXT_H_
#define YB_SQL_PTREE_SEM_CONTEXT_H_

#include "yb/sql/session_context.h"
#include "yb/sql/ptree/process_context.h"
#include "yb/sql/ptree/pt_create_table.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

struct SymbolEntry {
  PTColumnDefinition *column_;
  PTCreateTable *table_;

  SymbolEntry() : column_(nullptr), table_(nullptr) {
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
             SessionContext *session_context,
             int retry_count);
  virtual ~SemContext();

  //------------------------------------------------------------------------------------------------
  // Symbol table support.
  void MapSymbol(const MCString& name, PTColumnDefinition *entry);
  void MapSymbol(const MCString& name, PTCreateTable *entry);
  const SymbolEntry *SeekSymbol(const MCString& name);

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

  std::shared_ptr<client::YBTable> GetTableDesc(const char *table_name) {
    // If "retry_count_" is greater than 0, we want to reload metadata from master server.
    return session_context_->GetTableDesc(table_name, retry_count_ > 0);
  }

  // Find conversion mode from 'rhs_type' to 'lhs_type'.
  ConversionMode GetConversionMode(client::YBColumnSchema::DataType lhs_type,
                                   client::YBColumnSchema::DataType rhs_type);

  // Check if the rhs and lhs datatypes are compatible. Their conversion mode must be implicit.
  bool IsCompatible(client::YBColumnSchema::DataType lhs_type,
                    client::YBColumnSchema::DataType rhs_type) {
    return (GetConversionMode(lhs_type, rhs_type) == ConversionMode::kImplicit);
  }

  // Access function to retry counter.
  int retry_count() {
    return retry_count_;
  }

 private:
  // Symbol table.
  MCMap<MCString, SymbolEntry> symtab_;

  // Current processing symbol.
  SymbolEntry current_processing_id_;

  // Session.
  SessionContext *session_context_;

  // Force to refresh metadata.
  int retry_count_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_SEM_CONTEXT_H_
