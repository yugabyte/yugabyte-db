//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//--------------------------------------------------------------------------------------------------

#include "yb/sql/test/ybsql-test-base.h"

namespace yb {
namespace sql {

using std::string;
using std::vector;
using std::shared_ptr;
using client::YBClient;
using client::YBSession;
using client::YBClientBuilder;

//--------------------------------------------------------------------------------------------------
const string YbSqlTestBase::kDefaultKeyspaceName("my_keyspace");

YbSqlTestBase::YbSqlTestBase() {
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
  metadata_cache_ = std::make_shared<client::YBMetaDataCache>(client_);
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kDefaultKeyspaceName));
}

//--------------------------------------------------------------------------------------------------
static void CallUseKeyspace(const YbSqlProcessor::UniPtr& processor, const string& keyspace_name) {
  // Workaround: it's implemented as a separate function just because ASSERT_OK can
  // call 'return void;' what can be incompatible with another return type.
  ASSERT_OK(processor->UseKeyspace(keyspace_name));
}

YbSqlProcessor *YbSqlTestBase::GetSqlProcessor() {
  if (client_ == nullptr) {
    CreateSimulatedCluster();
  }

  std::weak_ptr<rpc::Messenger> messenger;
  sql_processors_.emplace_back(new YbSqlProcessor(messenger, client_, metadata_cache_));
  CallUseKeyspace(sql_processors_.back(), kDefaultKeyspaceName);
  return sql_processors_.back().get();
}

}  // namespace sql
}  // namespace yb
