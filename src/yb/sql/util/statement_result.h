//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Different results of processing a statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_STATEMENT_RESULT_H_
#define YB_SQL_UTIL_STATEMENT_RESULT_H_

#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/schema.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"
#include "yb/sql/ptree/pt_select.h"

namespace yb {
namespace sql {

//------------------------------------------------------------------------------------------------
// Result of preparing a statement. Only DML statement will return a prepared result that describes
// the schemas of the bind variables used and, for SELECT statement, the schemas of the columns
// selected.
class PreparedResult {
 public:
  // Public types.
  typedef std::unique_ptr<PreparedResult> UniPtr;
  typedef std::unique_ptr<const PreparedResult> UniPtrConst;

  // Constructors.
  explicit PreparedResult(const PTDmlStmt *tnode);
  virtual ~PreparedResult();

  // Accessors.
  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<ColumnSchema>& bind_variable_schemas() const { return bind_variable_schemas_; }
  const std::vector<ColumnSchema>& column_schemas() const { return column_schemas_; }

 private:
  const client::YBTableName table_name_;
  const std::vector<ColumnSchema> bind_variable_schemas_;
  const std::vector<ColumnSchema> column_schemas_;
};

//------------------------------------------------------------------------------------------------
// Result of executing a statement. Different possible types of results are listed below.
class ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<ExecutedResult> SharedPtr;
  typedef std::shared_ptr<const ExecutedResult> SharedPtrConst;

  // Constructors.
  ExecutedResult() { }
  virtual ~ExecutedResult() { }

  // Execution result types.
  enum class Type {
    SET_KEYSPACE = 1,
    ROWS         = 2
  };

  virtual const Type type() const = 0;
};

// Callback to be called after a statement is executed. When execution fails, a not-ok status is
// passed. When it succeeds, an ok status and the execution result are passed. When there is no
// result (i.e. void), a nullptr is passed.
typedef Callback<void(const Status&, ExecutedResult::SharedPtr)> StatementExecutedCallback;

//------------------------------------------------------------------------------------------------
// Result of "USE <keyspace>" statement.
class SetKeyspaceResult : public ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<SetKeyspaceResult> SharedPtr;
  typedef std::shared_ptr<const SetKeyspaceResult> SharedPtrConst;

  // Constructors.
  explicit SetKeyspaceResult(const std::string& keyspace) : keyspace_(keyspace) { }
  virtual ~SetKeyspaceResult() override { };

  // Result type.
  virtual const Type type() const override { return Type::SET_KEYSPACE; }

  // Accessor function for keyspace.
  const std::string& keyspace() const { return keyspace_; }

 private:
  const std::string keyspace_;
};

//------------------------------------------------------------------------------------------------
// Result of rows returned from executing a DML statement.
class RowsResult : public ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<RowsResult> SharedPtr;
  typedef std::shared_ptr<const RowsResult> SharedPtrConst;

  // Constructors.
  explicit RowsResult(client::YBqlOp *op);
  // Used only to mock empty rows for system tables.
  explicit RowsResult(client::YBTable* table, const std::string& rows_data);
  virtual ~RowsResult() override;

  // Result type.
  virtual const Type type() const override { return Type::ROWS; }

  // Accessor functions.
  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<ColumnSchema>& column_schemas() const { return column_schemas_; }
  const std::string& rows_data() const { return rows_data_; }
  const std::string& paging_state() const { return paging_state_; }
  YQLClient client() const { return client_; }

  // Parse the rows data and return it as a row block. It is the caller's responsibility to free
  // the row block after use.
  YQLRowBlock *GetRowBlock() const;

 private:
  const client::YBTableName table_name_;
  const std::vector<ColumnSchema> column_schemas_;
  const std::string rows_data_;
  const YQLClient client_;
  std::string paging_state_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_UTIL_STATEMENT_RESULT_H_
