//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"

namespace yb {
namespace sql {

using std::vector;
using std::shared_ptr;
using client::YBClient;
using client::YBSession;
using client::YBClientBuilder;

//--------------------------------------------------------------------------------------------------

YbSqlTestBase::YbSqlTestBase() : ybsql_(new YbSql()) {
  session_contexts_.reserve(1);
}

YbSqlTestBase::~YbSqlTestBase() {
}

//--------------------------------------------------------------------------------------------------

void YbSqlTestBase::CreateSimulatedCluster() {
  // Start mini-cluster with 1 tserver, config client options
  cluster_.reset(new MiniCluster(env_.get(), MiniClusterOptions()));
  ASSERT_OK(cluster_->Start());
  YBClientBuilder builder;
  builder.add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str());
  builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
  ASSERT_OK(builder.Build(&client_));
}

//--------------------------------------------------------------------------------------------------

SessionContext *YbSqlTestBase::CreateSessionContext(shared_ptr<YBSession> session) {
  CHECK(client_ != nullptr) << "Cannot create session context without a valid YB client";

  const int max_id = session_contexts_.size();
  if (session_contexts_.capacity() == max_id) {
    session_contexts_.reserve(max_id + 10);
  }

  SessionContext *session_context = new SessionContext(max_id + 1, client_, session);
  session_contexts_.emplace_back(session_context);
  return session_context;
}

SessionContext *YbSqlTestBase::GetSessionContext(int session_id) {
  if (session_contexts_.size() < session_id) {
    return nullptr;
  }
  return session_contexts_[session_id - 1].get();
}

//--------------------------------------------------------------------------------------------------

SessionContext *YbSqlTestBase::CreateNewConnectionContext() {
  CHECK(client_ != nullptr) << "Cannot create new connection without a valid YB client";

  shared_ptr<YBSession> session = client_->NewSession();
  session->SetTimeoutMillis(kSessionTimeoutMs);
  Status s = session->SetFlushMode(YBSession::MANUAL_FLUSH);
  CHECK(s.ok());
  return CreateSessionContext(session);
}

}  // namespace sql
}  // namespace yb
