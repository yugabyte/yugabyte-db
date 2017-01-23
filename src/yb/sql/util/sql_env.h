//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SqlEnv defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_SQL_ENV_H_
#define YB_SQL_UTIL_SQL_ENV_H_

#include "yb/client/client.h"
#include "yb/sql/util/rows_result.h"

namespace yb {
namespace sql {

class SqlEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<SqlEnv> UniPtr;
  typedef std::unique_ptr<const SqlEnv> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & desructor.
  SqlEnv(std::shared_ptr<client::YBClient> client,
         std::shared_ptr<client::YBSession> write_session,
         std::shared_ptr<client::YBSession> read_session);

  virtual client::YBTableCreator *NewTableCreator();

  virtual CHECKED_STATUS DeleteTable(const string& name);

  virtual CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBqlWriteOp> yb_op);

  virtual CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op);

  virtual std::shared_ptr<client::YBTable> GetTableDesc(const char *table_name,
                                                        bool refresh_metadata);

  // Access function for rows_result. If the statement executed is a regular DML or there's an
  // error in execution, rows_result would be null.
  const RowsResult* rows_result() const {
    return rows_result_.get();
  }

  // Keyspace related methods.

  // Create a new keyspace with the given name.
  virtual CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name) {
    return client_->CreateNamespace(keyspace_name);
  }

  // Delete keyspace with the given name.
  virtual CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name) {
    return client_->DeleteNamespace(keyspace_name);
  }

  // Construct a row_block and send it back.
  std::shared_ptr<YQLRowBlock> row_block() const {
    if (rows_result_ == nullptr) {
      // There isn't any query result.
      return nullptr;
    }
    return std::shared_ptr<YQLRowBlock>(rows_result_->GetRowBlock());
  }

  // Reset all env states or variables before executing the next statement.
  void Reset();

 private:
  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> write_session_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> read_session_;

  // Rows resulted from executing the last statement.
  std::shared_ptr<RowsResult> rows_result_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_UTIL_SQL_ENV_H_
