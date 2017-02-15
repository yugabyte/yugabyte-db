//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

namespace yb {
namespace sql {

using std::vector;
using std::shared_ptr;
using client::YBClient;
using client::YBSession;
using client::YBClientBuilder;

//--------------------------------------------------------------------------------------------------

YbSqlTestBase::YbSqlTestBase() : ybsql_(new YbSql()) {
  sql_envs_.reserve(1);
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
  table_cache_ = std::make_shared<client::YBTableCache>(client_);
}

//--------------------------------------------------------------------------------------------------

SqlEnv *YbSqlTestBase::CreateSqlEnv(shared_ptr<YBSession> write_session,
                                    shared_ptr<YBSession> read_session) {
  CHECK(client_ != nullptr) << "Cannot create session context without a valid YB client";

  const int max_id = sql_envs_.size();
  if (sql_envs_.capacity() == max_id) {
    sql_envs_.reserve(max_id + 10);
  }

  SqlEnv *sql_env = new SqlEnv(client_, table_cache_, write_session, read_session);
  sql_envs_.emplace_back(sql_env);
  return sql_env;
}

SqlEnv *YbSqlTestBase::GetSqlEnv(int session_id) {
  if (sql_envs_.size() < session_id) {
    return nullptr;
  }
  return sql_envs_[session_id - 1].get();
}

//--------------------------------------------------------------------------------------------------

SqlEnv *YbSqlTestBase::CreateNewConnectionContext() {
  CHECK(client_ != nullptr) << "Cannot create new connection without a valid YB client";

  shared_ptr<YBSession> write_session = client_->NewSession();
  write_session->SetTimeoutMillis(kSessionTimeoutMs);
  Status s = write_session->SetFlushMode(YBSession::MANUAL_FLUSH);
  CHECK(s.ok());

  shared_ptr<YBSession> read_session = client_->NewSession(true);
  read_session->SetTimeoutMillis(kSessionTimeoutMs);
  s = read_session->SetFlushMode(YBSession::MANUAL_FLUSH);
  CHECK(s.ok());

  return CreateSqlEnv(write_session, read_session);
}

//--------------------------------------------------------------------------------------------------

SqlProcessor *YbSqlTestBase::GetSqlProcessor() {
  CHECK(client_ != nullptr) << "Cannot create new sql processor without a valid YB client";

  for (const SqlProcessor::UniPtr& processor : sql_processors_) {
    if (!processor->is_used()) {
      return processor.get();
    }
  }

  const int size = sql_processors_.size();
  sql_processors_.reserve(std::max<int>(size * 2, size + 10));
  sql_processors_.emplace_back(new SqlProcessor(client_, table_cache_));
  return sql_processors_.back().get();
}

}  // namespace sql
}  // namespace yb
