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
#include "yb/common/ysql_rowblock.h"

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

  virtual CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBSqlWriteOp> yb_op);

  virtual CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBSqlReadOp> yb_op);

  virtual std::shared_ptr<client::YBTable> GetTableDesc(const char *table_name,
                                                        bool refresh_metadata);

  // Access function for read_op. If there's an error in execution, read_op_ would be null.
  const std::shared_ptr<client::YBSqlReadOp>& read_op() const {
    return read_op_;
  }

  // Construct a row_block and send it back.
  std::shared_ptr<YSQLRowBlock> row_block() const {
    if (read_op_ == nullptr) {
      // There isn't any query result.
      return nullptr;
    }
    return std::shared_ptr<YSQLRowBlock>(read_op_->GetRowBlock());
  }

 private:
  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> write_session_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> read_session_;

  // Result for apply. CQL uses read_op_ to form a response.
  // TODO(neil): Need to find a better way to send the response back instead of the whole operator.
  std::shared_ptr<client::YBSqlReadOp> read_op_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_UTIL_SQL_ENV_H_
