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

#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/client/yb_op.h"

#include "yb/yql/redis/redisserver/redis_parser.h"
#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/util/curl_util.h"

using std::unique_ptr;
using std::shared_ptr;

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBScanner;
using client::YBScanBatch;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTableCreator;
using client::YBTableType;
using client::YBTableName;
using strings::Substitute;

namespace integration_tests {

const YBTableName YBTableTestBase::kDefaultTableName("my_keyspace", "kv-table-test");

int YBTableTestBase::num_masters() {
  return kDefaultNumMasters;
}

int YBTableTestBase::num_tablet_servers() {
  return kDefaultNumTabletServers;
}

int YBTableTestBase::num_tablets() {
  return CalcNumTablets(num_tablet_servers());
}

int YBTableTestBase::num_replicas() {
  return std::min(num_tablet_servers(), 3);
}

int YBTableTestBase::session_timeout_ms() {
  return kDefaultSessionTimeoutMs;
}

YBTableName YBTableTestBase::table_name() {
  return kDefaultTableName;
}

bool YBTableTestBase::need_redis_table() {
  return true;
}

int YBTableTestBase::client_rpc_timeout_ms() {
  return kDefaultClientRpcTimeoutMs;
}

bool YBTableTestBase::use_external_mini_cluster() {
  return kDefaultUsingExternalMiniCluster;
}

YBTableTestBase::YBTableTestBase() {
}

void YBTableTestBase::SetUp() {
  YBTest::SetUp();

  Status mini_cluster_status;
  if (use_external_mini_cluster()) {
    auto opts = ExternalMiniClusterOptions();
    opts.num_masters = num_masters();
    opts.master_rpc_ports = master_rpc_ports();
    opts.num_tablet_servers = num_tablet_servers();

    external_mini_cluster_.reset(new ExternalMiniCluster(opts));
    mini_cluster_status = external_mini_cluster_->Start();
  } else {
    auto opts = MiniClusterOptions();
    opts.num_masters = num_masters();
    opts.num_tablet_servers = num_tablet_servers();

    mini_cluster_.reset(new MiniCluster(env_.get(), opts));
    mini_cluster_status = mini_cluster_->Start();
  }
  if (!mini_cluster_status.ok()) {
    // We sometimes crash during cleanup if the cluster creation fails and don't get to report
    // the root cause, so log it here just in case.
    LOG(INFO) << "Failed starting the mini cluster: " << mini_cluster_status.ToString();
  }
  ASSERT_OK(mini_cluster_status);

  CreateClient();
  CreateTable();
  OpenTable();
}

void YBTableTestBase::TearDown() {
  DeleteTable();

  // Fetch the tablet server metrics page after we delete the table. [ENG-135].
  FetchTSMetricsPage();

  if (use_external_mini_cluster()) {
    external_mini_cluster_->Shutdown();
  } else {
    mini_cluster_->Shutdown();
  }
  YBTest::TearDown();
}

vector<uint16_t> YBTableTestBase::master_rpc_ports() {
  vector<uint16_t> master_rpc_ports;
  for (int i = 0; i < num_masters(); ++i) {
    master_rpc_ports.push_back(0);
  }
  return master_rpc_ports;
}

void YBTableTestBase::CreateClient() {
  client_ = CreateYBClient();
}

shared_ptr<yb::client::YBClient> YBTableTestBase::CreateYBClient() {
  shared_ptr<yb::client::YBClient> client;
  YBClientBuilder builder;
  builder.default_rpc_timeout(MonoDelta::FromMilliseconds(client_rpc_timeout_ms()));
  if (use_external_mini_cluster()) {
    CHECK_OK(external_mini_cluster_->CreateClient(&builder, &client));
  } else {
    CHECK_OK(mini_cluster_->CreateClient(&builder, &client));
  }
  return client;
}

void YBTableTestBase::OpenTable() {
  ASSERT_OK(table_.Open(table_name(), client_.get()));
  session_ = NewSession();
}

void YBTableTestBase::CreateRedisTable(shared_ptr<yb::client::YBClient> client,
                                       YBTableName table_name) {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name()));
  ASSERT_OK(NewTableCreator()->table_name(table_name)
                .table_type(YBTableType::REDIS_TABLE_TYPE)
                .Create());
}

void YBTableTestBase::CreateTable() {
  if (!table_exists_) {
    ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name().namespace_name()));

    YBSchemaBuilder b;
    b.AddColumn("k")->Type(BINARY)->NotNull()->HashPrimaryKey();
    b.AddColumn("v")->Type(BINARY)->NotNull();
    ASSERT_OK(b.Build(&schema_));

    ASSERT_OK(NewTableCreator()->table_name(table_name()).schema(&schema_).Create());
    table_exists_ = true;
  }
}

void YBTableTestBase::DeleteTable() {
  if (table_exists_) {
    ASSERT_OK(client_->DeleteTable(table_name()));
    table_exists_ = false;
  }
}

shared_ptr<YBSession> YBTableTestBase::NewSession() {
  shared_ptr<YBSession> session = client_->NewSession();
  session->SetTimeout(MonoDelta::FromMilliseconds(session_timeout_ms()));
  CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
  return session;
}

void YBTableTestBase::PutKeyValue(YBSession* session, string key, string value) {
  auto insert = table_.NewInsertOp();
  QLAddStringHashValue(insert->mutable_request(), key);
  table_.AddStringColumnValue(insert->mutable_request(), "v", value);
  ASSERT_OK(session->Apply(insert));
  ASSERT_OK(session->Flush());
}

void YBTableTestBase::PutKeyValue(string key, string value) {
  PutKeyValue(session_.get(), key, value);
}

void YBTableTestBase::RestartCluster() {
  DCHECK(!use_external_mini_cluster());
  CHECK_OK(mini_cluster_->RestartSync());
  ASSERT_NO_FATALS(CreateClient());
  ASSERT_NO_FATALS(OpenTable());
}

std::vector<std::pair<std::string, std::string>> YBTableTestBase::GetScanResults(
    const client::TableRange& range) {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto& row : range) {
    result.emplace_back(row.column(0).binary_value(), row.column(1).binary_value());
  }
  std::sort(result.begin(), result.end());
  return result;
}

void YBTableTestBase::FetchTSMetricsPage() {
  EasyCurl c;
  faststring buf;

  string addr;
  // TODO: unify external and in-process mini cluster interfaces.
  if (use_external_mini_cluster()) {
    if (external_mini_cluster_->num_tablet_servers() > 0) {
      addr = external_mini_cluster_->tablet_server(0)->bound_http_hostport().ToString();
    }
  } else {
    if (mini_cluster_->num_tablet_servers() > 0) {
      addr = ToString(mini_cluster_->mini_tablet_server(0)->bound_http_addr());
    }
  }

  if (!addr.empty()) {
    LOG(INFO) << "Fetching metrics from " << addr;
    ASSERT_OK(c.FetchURL(Substitute("http://$0/metrics", addr), &buf));
  }
}

std::unique_ptr<client::YBTableCreator> YBTableTestBase::NewTableCreator() {
  unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  table_creator->num_replicas(num_replicas());
  if (num_tablets() > 0) {
    table_creator->num_tablets(num_tablets());
  }
  table_creator->table_type(YBTableType::YQL_TABLE_TYPE);
  return table_creator;
}

}  // namespace integration_tests
}  // namespace yb
