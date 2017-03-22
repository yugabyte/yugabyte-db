//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Different results of processing a statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_STMT_RESULT_H_
#define YB_SQL_UTIL_STMT_RESULT_H_

#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/schema.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"
#include "yb/sql/ptree/pt_select.h"

namespace yb {
namespace sql {

// Result of preparing a statement.
class PreparedResult {
 public:
  explicit PreparedResult(const PTDmlStmt *stmt);

  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<ColumnSchema>& bind_variable_schemas() const { return bind_variable_schemas_; }
  const std::vector<ColumnSchema>& column_schemas() const { return column_schemas_; }

 private:
  const client::YBTableName table_name_;
  const std::vector<ColumnSchema> bind_variable_schemas_;
  const std::vector<ColumnSchema> column_schemas_;
};

// Result of rows returned from executing a statement.
class RowsResult {
 public:
  explicit RowsResult(client::YBqlReadOp* op);
  explicit RowsResult(client::YBqlWriteOp* op);

  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<ColumnSchema>& column_schemas() const { return column_schemas_; }
  const std::string& rows_data() const { return rows_data_; }
  const std::string& paging_state() const { return paging_state_; }
  void clear_paging_state() { paging_state_.clear(); }
  YQLClient client() const { return client_; }

  // Parse the rows data and return it as a row block. It is the caller's responsibility to free
  // the row block after use.
  YQLRowBlock* GetRowBlock() const;

 private:
  const client::YBTableName table_name_;
  const std::vector<ColumnSchema> column_schemas_;
  const std::string rows_data_;
  const YQLClient client_;
  std::string paging_state_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_UTIL_STMT_RESULT_H_
