//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// RowsResult represents rows resulted from the execution of a SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_ROWS_RESULT_H_
#define YB_SQL_UTIL_ROWS_RESULT_H_

#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/schema.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"

namespace yb {
namespace sql {

class RowsResult {
 public:
  explicit RowsResult(client::YBqlReadOp* op);
  explicit RowsResult(client::YBqlWriteOp* op);

  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<ColumnSchema>& column_schemas() const { return column_schemas_; }
  const std::string& rows_data() const { return rows_data_; }
  const std::string& next_read_key() const { return next_read_key_; }
  YQLClient client() const { return client_; }

  // Parse the rows data and return it as a row block. It is the caller's responsibility to free
  // the row block after use.
  YQLRowBlock* GetRowBlock() const;

 private:
  const client::YBTableName table_name_;
  const std::vector<ColumnSchema> column_schemas_;
  const std::string rows_data_;
  const YQLClient client_;
  const std::string next_read_key_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_UTIL_ROWS_RESULT_H_
