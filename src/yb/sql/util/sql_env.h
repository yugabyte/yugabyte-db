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

#include "yb/sql/sql_session.h"

namespace yb {
namespace sql {

class SqlEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<SqlEnv> UniPtr;
  typedef std::unique_ptr<const SqlEnv> UniPtrConst;
  static const int kSessionTimeoutMs = 60000;

  //------------------------------------------------------------------------------------------------
  // Constructor & desructor.
  SqlEnv(std::shared_ptr<client::YBClient> client, std::shared_ptr<client::YBTableCache> cache);

  // Set the SQL session to use in this SQL environment.
  virtual void set_sql_session(SqlSession::SharedPtr sql_session) {
    sql_session_ = sql_session;
  }

  virtual client::YBTableCreator *NewTableCreator();

  virtual CHECKED_STATUS DeleteTable(const client::YBTableName& name);

  virtual CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBqlWriteOp> yb_op);

  virtual CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op);


  virtual std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name,
                                                        bool refresh_cache,
                                                        bool *cache_used);

  // Keyspace related methods.

  // Create a new keyspace with the given name.
  virtual CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name) {
    return client_->CreateNamespace(keyspace_name);
  }

  // Delete keyspace with the given name.
  virtual CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name) {
    return client_->DeleteNamespace(keyspace_name);
  }

  // Use keyspace with the given name.
  virtual CHECKED_STATUS UseKeyspace(const std::string& keyspace_name);

  virtual std::string CurrentKeyspace() const {
    CHECK(sql_session_ != nullptr) << "SQL session is not set";
    return sql_session_->current_keyspace();
  }

 private:
  // Process YBOperation status.
  CHECKED_STATUS ProcessOpStatus(const client::YBOperation* op,
                                 const Status& s,
                                 client::YBSession *session) const;

  // Persistent attributes.

  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // YBTableCache, a cache to avoid creating a new table for each call.
  std::shared_ptr<client::YBTableCache> table_cache_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> write_session_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> read_session_;

  // Transient attributes.

  // SQL session that this SQL environment is using currently.
  SqlSession::SharedPtr sql_session_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_UTIL_SQL_ENV_H_
