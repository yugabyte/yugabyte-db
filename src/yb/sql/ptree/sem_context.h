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
#include "yb/sql/ptree/sem_state.h"

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
  //
  // TODO(neil) Add "column_read_count_" and potentially "column_write_count_" and use them for
  // error check wherever needed. Symbol tables and entries are destroyed after compilation, so
  // data that are used during compilation but not execution should be declared here.
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
             SqlEnv *sql_env);
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

  PTCreateTable *current_create_table_stmt() {
    return current_processing_id_.table_;
  }

  void set_current_create_table_stmt(PTCreateTable *table) {
    current_processing_id_.table_ = table;
  }

  // Find table descriptor from metadata server.
  std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name);

  // Find column descriptor from symbol table.
  PTColumnDefinition *GetColumnDefinition(const MCString& col_name);

  // Find column descriptor from symbol table.
  // The parameter "reading_column" is to indicate if the column value is to be read by the
  // when executin the YQL statement before its execution.
  const ColumnDesc *GetColumnDesc(const MCString& col_name, bool reading_column);

  // Check if the expression `expr` can be implicitly converted to type `type`
  bool IsConvertible(PTExpr::SharedPtr expr, const std::shared_ptr<YQLType>& type) const {
    return IsConvertible(expr.get(), type);
  }
  bool IsConvertible(const PTExpr *expr, const std::shared_ptr<YQLType>& type) const;

  // Check if two types are comparable -- parametric types are never comparable so we only take
  // DataType not YQLType as arguments
  bool IsComparable(DataType lhs_type, DataType rhs_type) const;

  std::string CurrentKeyspace() const {
    return sql_env_->CurrentKeyspace();
  }

  // Access function to cache_used.
  bool cache_used() const { return cache_used_; }

  // Acess functions for semantic states.
  SemState *sem_state() const {
    return sem_state_;
  }

  const std::shared_ptr<YQLType>& expr_expected_yql_type() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->expected_yql_type();
  }

  InternalType expr_expected_internal_type() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->expected_internal_type();
  }

  WhereExprState *where_state() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->where_state();
  }

  bool processing_column_definition() const {
    DCHECK(sem_state_) << "State variable is not set";
    return sem_state_->processing_column_definition();
  }

  const MCSharedPtr<MCString>& bindvar_name() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->bindvar_name();
  }

  const ColumnDesc *updating_counter() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->updating_counter();
  }

  bool processing_if_clause() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->processing_if_clause();
  }

  void set_sem_state(SemState *new_state, SemState **existing_state_holder) {
    *existing_state_holder = sem_state_;
    sem_state_ = new_state;
  }

  void reset_sem_state(SemState *previous_state) {
    sem_state_ = previous_state;
  }

  PTDmlStmt *current_dml_stmt() const {
    return current_dml_stmt_;
  }

  void set_current_dml_stmt(PTDmlStmt *stmt) {
    current_dml_stmt_ = stmt;
  }

  std::shared_ptr<client::YBTable> current_table() { return current_table_; }

  void set_current_table(std::shared_ptr<client::YBTable> table) {
    current_table_ = table;
  }

 private:
  // Find symbol.
  SymbolEntry *SeekSymbol(const MCString& name);

  // Symbol table.
  MCMap<MCString, SymbolEntry> symtab_;

  // Current processing symbol.
  SymbolEntry current_processing_id_;

  // Session.
  SqlEnv *sql_env_;

  // Is metadata cache used?
  bool cache_used_;

  // The current dml statement being processed.
  PTDmlStmt *current_dml_stmt_;

  // The semantic analyzer will set the current table for dml queries.
  std::shared_ptr<client::YBTable> current_table_;

  // sem_state_ consists of state variables that are used to process one tree node. It is generally
  // set and reset at the beginning and end of the semantic analysis of one treenode.
  SemState *sem_state_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_SEM_CONTEXT_H_
