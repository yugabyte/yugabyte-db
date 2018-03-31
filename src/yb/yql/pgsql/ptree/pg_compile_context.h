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
// Entry point for the semantic analytical process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_COMPILE_CONTEXT_H_
#define YB_YQL_PGSQL_PTREE_PG_COMPILE_CONTEXT_H_

#include "yb/yql/pgsql/util/pg_env.h"
#include "yb/yql/pgsql/util/pg_session.h"
#include "yb/yql/pgsql/ptree/pg_process_context.h"
#include "yb/yql/pgsql/ptree/column_desc.h"
#include "yb/yql/pgsql/ptree/pg_tcreate_table.h"
#include "yb/yql/pgsql/ptree/pg_sem_state.h"

#include "yb/yql/pgsql/proto/pg_proto.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

struct PgSymbolEntry {
  // Parse tree node for column. It's used for table creation.
  PgTColumnDefinition *column_;

  // Parse tree node for table. It's used for table creation.
  PgTCreateTable *create_table_;

  // Column description. It's used for DML statements including select.
  // Not part of a parse tree, but it is allocated within the parse tree pool because it us
  // persistent metadata. It represents a column during semantic and execution phases.
  //
  // TODO(neil) Add "column_read_count_" and potentially "column_write_count_" and use them for
  // error check wherever needed. Symbol tables and entries are destroyed after compilation, so
  // data that are used during compilation but not execution should be declared here.
  ColumnDesc *column_desc_;

  PgSymbolEntry()
      : column_(nullptr), create_table_(nullptr), column_desc_(nullptr) {
  }
};

//--------------------------------------------------------------------------------------------------

class PgCompileContext : public PgProcessContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<PgCompileContext> UniPtr;
  typedef std::unique_ptr<const PgCompileContext> UniPtrConst;

  typedef std::shared_ptr<PgCompileContext> SharedPtr;
  typedef std::shared_ptr<const PgCompileContext> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  PgCompileContext(const PgEnv::SharedPtr& pg_env,
                   const PgSession::SharedPtr& client_session,
                   const string& stmt);
  virtual ~PgCompileContext();

  // Access function for input_file_.
  std::istream *input_file() {
    return input_file_.get();
  }

  //------------------------------------------------------------------------------------------------
  // Parse tree to be constructed by the compilation process.
  const ParseTree::SharedPtr& parse_tree() {
    return parse_tree_;
  }

  // Returns the tree node of the statement being executed.
  const TreeNode* tree_root() const {
    return parse_tree_->root().get();
  }

  // Saves the generated parse tree from the parsing process to this context.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree);

  // Memory pool for semantic analysis of the parse tree of a statement.
  MemoryContext *PSemMem() const {
    return parse_tree_->PSemMem();
  }

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return parse_tree_->PTreeMem();
  }

  //------------------------------------------------------------------------------------------------
  // Access API to PgSession parameters.
  string session_database() const {
    return pg_session_->current_database();
  }

  //------------------------------------------------------------------------------------------------
  // Access API to PgEnv parameters.

  //------------------------------------------------------------------------------------------------
  // Methods that are used for parsing.
  //------------------------------------------------------------------------------------------------
  // Read a maximum of 'max_size' bytes from SQL statement of this parsing context into the
  // provided buffer 'buf'. Scanner will call this function when looking for next token.
  size_t Read(char* buf, size_t max_size);

  // Access function for trace_scanning_.
  bool trace_scanning() const {
    return trace_scanning_;
  }

  // Access function for trace_parsing_.
  bool trace_parsing() const {
    return trace_parsing_;
  }

  //------------------------------------------------------------------------------------------------
  // Methods that are used for semantic analysis.
  //------------------------------------------------------------------------------------------------
  // Symbol table support.
  CHECKED_STATUS MapSymbol(const MCString& name, PgTColumnDefinition *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, PgTCreateTable *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, ColumnDesc *entry);

  // Access functions to current processing symbol.
  PgSymbolEntry *current_processing_symbol() {
    return &current_processing_symbol_;
  }
  void set_current_processing_symbol(const PgSymbolEntry& new_sym) {
    current_processing_symbol_ = new_sym;
  }

  //------------------------------------------------------------------------------------------------
  // Load table schema into symbol table.
  CHECKED_STATUS LookupTable(
      const client::YBTableName& name,
      const PgTLocation& loc,
      bool write_table,
      std::shared_ptr<client::YBTable>* table,
      bool* is_system,
      MCVector<ColumnDesc>* col_descs,
      int* num_key_columns,
      int* num_partition_columns);

  //------------------------------------------------------------------------------------------------
  // Access functions to current processing table and column.
  PgTColumnDefinition *current_column() {
    return current_processing_symbol_.column_;
  }
  void set_current_column(PgTColumnDefinition *column) {
    current_processing_symbol_.column_ = column;
  }

  PgTCreateTable *current_create_table_stmt() {
    return current_processing_symbol_.create_table_;
  }

  void set_current_create_table_stmt(PgTCreateTable *table) {
    current_processing_symbol_.create_table_ = table;
  }

  // Find column descriptor from symbol table.
  PgTColumnDefinition *GetColumnDefinition(const MCString& col_name);

  // Find column descriptor from symbol table. From the context, the column value will be marked to
  // be read if necessary when executing the QL statement.
  const ColumnDesc *GetColumnDesc(const MCString& col_name);

  // Check if the the lhs_type is convertible to rhs_type.
  bool IsConvertible(const std::shared_ptr<QLType>& lhs_type,
                     const std::shared_ptr<QLType>& rhs_type) const;

  // Check if two types are comparable -- parametric types are never comparable so we only take
  // DataType not QLType as arguments
  bool IsComparable(DataType lhs_type, DataType rhs_type) const;

  // Access function to cache_used.
  bool cache_used() const { return cache_used_; }

  // Acess functions for semantic states.
  PgSemState *sem_state() const {
    return sem_state_;
  }

  const std::shared_ptr<QLType>& expr_expected_ql_type() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->expected_ql_type();
  }

  InternalType expr_expected_internal_type() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->expected_internal_type();
  }

  bool processing_column_definition() const {
    DCHECK(sem_state_) << "State variable is not set";
    return sem_state_->processing_column_definition();
  }

  const ColumnDesc *hash_col() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->hash_col();
  }

  const ColumnDesc *lhs_col() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->lhs_col();
  }

  bool processing_set_clause() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->processing_set_clause();
  }

  bool processing_assignee() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->processing_assignee();
  }

  bool allowing_aggregate() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->allowing_aggregate();
  }

  void set_sem_state(PgSemState *new_state, PgSemState **existing_state_holder) {
    *existing_state_holder = sem_state_;
    sem_state_ = new_state;
  }

  void reset_sem_state(PgSemState *previous_state) {
    sem_state_ = previous_state;
  }

  PgTDmlStmt *current_dml_stmt() const {
    return current_dml_stmt_;
  }

  void set_current_dml_stmt(PgTDmlStmt *stmt) {
    current_dml_stmt_ = stmt;
  }

  std::shared_ptr<client::YBTable> current_table() { return current_table_; }

  void set_current_table(std::shared_ptr<client::YBTable> table) {
    current_table_ = table;
  }

  //------------------------------------------------------------------------------------------------
  // Methods that are used for code generator
  //------------------------------------------------------------------------------------------------

 private:
  //------------------------------------------------------------------------------------------------
  // Context parameters.
  //------------------------------------------------------------------------------------------------
  // Session.
  PgEnv::SharedPtr pg_env_;
  PgSession::SharedPtr pg_session_;

  // Parse tree (output).
  ParseTree::SharedPtr parse_tree_;

  // We don't use istream (i.e. file) as input when parsing. In the future, if we also support file
  // as an SQL input, we need to define a constructor that takes a file as input and initializes
  // "input_file_" accordingly.
  std::unique_ptr<std::istream> input_file_;

  //------------------------------------------------------------------------------------------------
  // Code generator variables.
  //------------------------------------------------------------------------------------------------

  //------------------------------------------------------------------------------------------------
  // Parsing state variables.
  //------------------------------------------------------------------------------------------------
  size_t stmt_offset_ = 0;             // Starting point of the SQL statement to be scanned.
  bool trace_scanning_ = false;        // Scanner trace flag.
  bool trace_parsing_ = false;         // Parser trace flag.

  //------------------------------------------------------------------------------------------------
  // Semantic analyzing state variables.
  //------------------------------------------------------------------------------------------------
  // Find symbol.
  PgSymbolEntry *SeekSymbol(const MCString& name);

  // Symbol table.
  MCMap<MCString, PgSymbolEntry> symtab_;

  // Current processing symbol.
  PgSymbolEntry current_processing_symbol_;

  // Is metadata cache used?
  bool cache_used_ = false;

  // The current dml statement being processed.
  PgTDmlStmt *current_dml_stmt_ = nullptr;

  // The semantic analyzer will set the current table for dml queries.
  std::shared_ptr<client::YBTable> current_table_ = nullptr;

  // sem_state_ consists of state variables that are used to process one tree node. It is generally
  // set and reset at the beginning and end of the semantic analysis of one treenode.
  PgSemState *sem_state_ = nullptr;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_COMPILE_CONTEXT_H_
