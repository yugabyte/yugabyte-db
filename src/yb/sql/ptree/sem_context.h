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
             bool refresh_cache);
  virtual ~SemContext();

  // Memory pool for semantic analysis of the parse tree of a statement.
  MemoryContext *PSemMem() const {
    return parse_tree_->PSemMem();
  }

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
  std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name);

  // Find column descriptor from symbol table.
  PTColumnDefinition *GetColumnDefinition(const MCString& col_name) const;

  // Find column descriptor from symbol table.
  const ColumnDesc *GetColumnDesc(const MCString& col_name) const;

  // Check if the expression `expr` can be implicitly converted to type `type`
  bool IsConvertible(PTExpr::SharedPtr expr, YQLType type) const;

  // Check if two types are comparable -- parametric types are never comparable so we only take
  // DataType not YQLType as arguments
  bool IsComparable(DataType lhs_type, DataType rhs_type) const;

  std::string CurrentKeyspace() const {
    return sql_env_->CurrentKeyspace();
  }

  // Access function to cache_used.
  bool cache_used() const { return cache_used_; }

 private:
  // Find symbol.
  const SymbolEntry *SeekSymbol(const MCString& name) const;

  // Symbol table.
  MCMap<MCString, SymbolEntry, MCString::MapCmp> symtab_;

  // Current processing symbol.
  SymbolEntry current_processing_id_;

  // Session.
  SqlEnv *sql_env_;

  // Force to refresh cache.
  const bool refresh_cache_;

  // Is metadata cache used?
  bool cache_used_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_SEM_CONTEXT_H_
