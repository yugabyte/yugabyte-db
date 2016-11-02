//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SessionContext defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_SESSION_CONTEXT_H_
#define YB_SQL_SESSION_CONTEXT_H_

#include "yb/client/client.h"

namespace yb {
namespace sql {

class SessionContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<SessionContext> UniPtr;
  typedef std::unique_ptr<const SessionContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & desructor.
  SessionContext(int session_id,
                 std::shared_ptr<client::YBClient> client,
                 std::shared_ptr<client::YBSession> session);

  client::YBTableCreator* NewTableCreator() {
    return client_->NewTableCreator();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Session identifier. We are not using this now, but it might be useful later when we need to
  // report execution status of a specific execution / task to application.
  int session_id_;

  // "app_" reppresents the one who requests this execution, such as Cassandra.
  // Not sure if we need it here.
  // std::shared_ptr<> app_;

  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> session_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_SESSION_CONTEXT_H_
