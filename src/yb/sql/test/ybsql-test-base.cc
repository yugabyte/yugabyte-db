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

YbSqlTestBase::YbSqlTestBase() {
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

SqlEnv *YbSqlTestBase::CreateSqlEnv() {
  if (client_ == nullptr) {
    CreateSimulatedCluster();
  }

  const int max_id = sql_envs_.size();
  if (sql_envs_.capacity() == max_id) {
    sql_envs_.reserve(max_id + 10);
  }

  std::weak_ptr<rpc::Messenger> messenger;
  SqlEnv *sql_env = new SqlEnv(messenger, client_, table_cache_);
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

YbSqlProcessor *YbSqlTestBase::GetSqlProcessor() {
  if (client_ == nullptr) {
    CreateSimulatedCluster();
  }

  for (const YbSqlProcessor::UniPtr& processor : sql_processors_) {
    if (!processor->is_used()) {
      return processor.get();
    }
  }

  std::weak_ptr<rpc::Messenger> messenger;
  const int size = sql_processors_.size();
  sql_processors_.reserve(std::max<int>(size * 2, size + 10));
  sql_processors_.emplace_back(new YbSqlProcessor(messenger, client_, table_cache_));
  return sql_processors_.back().get();
}

}  // namespace sql
}  // namespace yb
