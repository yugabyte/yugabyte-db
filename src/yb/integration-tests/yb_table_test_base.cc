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

#include "yb/client/client.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"

#include "yb/master/master_client.proxy.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"

DECLARE_bool(enable_ysql);

using std::unique_ptr;
using std::shared_ptr;
using std::vector;
using std::string;

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTableCreator;
using client::YBTableType;
using client::YBTableName;
using strings::Substitute;

namespace integration_tests {

YBTableTestBase::~YBTableTestBase() {
}

const YBTableName YBTableTestBase::kDefaultTableName(
    YQL_DATABASE_CQL, "my_keyspace", "kv-table-test");

size_t YBTableTestBase::num_masters() {
  return kDefaultNumMasters;
}

size_t YBTableTestBase::num_tablet_servers() {
  return kDefaultNumTabletServers;
}

int YBTableTestBase::num_drives() {
  return kDefaultNumDrives;
}

int YBTableTestBase::num_tablets() {
  return CalcNumTablets(num_tablet_servers());
}

int YBTableTestBase::session_timeout_ms() {
  return kDefaultSessionTimeoutMs;
}

YBTableName YBTableTestBase::table_name() {
  if (table_names_.empty()) {
    table_names_.push_back(kDefaultTableName);
  }
  return table_names_[0];
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

bool YBTableTestBase::use_yb_admin_client() {
  return false;
}

bool YBTableTestBase::enable_ysql() {
  return kDefaultEnableYSQL;
}

YBTableTestBase::YBTableTestBase() {
}

void YBTableTestBase::BeforeCreateTable() {
}

void YBTableTestBase::BeforeStartCluster() {
}

void YBTableTestBase::SetUp() {
  YBTest::SetUp();

  Status mini_cluster_status;
  if (use_external_mini_cluster()) {
    auto opts = ExternalMiniClusterOptions {
        .num_masters = num_masters(),
        .num_tablet_servers = num_tablet_servers(),
        .num_drives = num_drives(),
        .master_rpc_ports = master_rpc_ports(),
        .enable_ysql = enable_ysql(),
        .extra_tserver_flags = {},
        .extra_master_flags = {},
        .cluster_id = {},
        .cluster_short_name = {},
    };
    CustomizeExternalMiniCluster(&opts);

    external_mini_cluster_.reset(new ExternalMiniCluster(opts));
    mini_cluster_status = external_mini_cluster_->Start();
  } else {
    auto opts = MiniClusterOptions {
        .num_masters = num_masters(),
        .num_tablet_servers = num_tablet_servers(),
        .num_drives = num_drives(),
        .master_env = env_.get(),
        .ts_env = ts_env_ ? ts_env_.get() : nullptr,
        .ts_rocksdb_env = ts_rocksdb_env_ ? ts_rocksdb_env_.get() : nullptr
    };
    LOG(INFO) << "opts: " << opts.ts_env;
    SetAtomicFlag(enable_ysql(), &FLAGS_enable_ysql);

    mini_cluster_.reset(new MiniCluster(opts));
    BeforeStartCluster();
    mini_cluster_status = mini_cluster_->Start();
  }
  if (!mini_cluster_status.ok()) {
    // We sometimes crash during cleanup if the cluster creation fails and don't get to report
    // the root cause, so log it here just in case.
    LOG(INFO) << "Failed starting the mini cluster: " << mini_cluster_status.ToString();
  }
  ASSERT_OK(mini_cluster_status);

  CreateClient();
  CreateAdminClient();

  BeforeCreateTable();

  CreateTable();
  OpenTable();
}

void YBTableTestBase::TearDown() {
  client_.reset();
  if (use_yb_admin_client()) {
    yb_admin_client_.reset();
  }
  if (use_external_mini_cluster()) {
    external_mini_cluster_->Shutdown();
  } else {
    mini_cluster_->Shutdown();
  }
  YBTest::TearDown();
}

vector<uint16_t> YBTableTestBase::master_rpc_ports() {
  vector<uint16_t> master_rpc_ports;
  for (size_t i = 0; i < num_masters(); ++i) {
    master_rpc_ports.push_back(0);
  }
  return master_rpc_ports;
}

void YBTableTestBase::CreateClient() {
  client_ = CreateYBClient();
}

std::unique_ptr<YBClient> YBTableTestBase::CreateYBClient() {
  YBClientBuilder builder;
  builder.default_rpc_timeout(MonoDelta::FromMilliseconds(client_rpc_timeout_ms()));
  if (use_external_mini_cluster()) {
    return CHECK_RESULT(external_mini_cluster_->CreateClient(&builder));
  } else {
    return CHECK_RESULT(mini_cluster_->CreateClient(&builder));
  }
}

void YBTableTestBase::CreateAdminClient() {
  if (use_yb_admin_client()) {
    string addrs;
    if (use_external_mini_cluster()) {
      addrs = external_mini_cluster_->GetMasterAddresses();
    }  else {
      addrs = mini_cluster_->GetMasterAddresses();
    }
    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        addrs, MonoDelta::FromMilliseconds(client_rpc_timeout_ms()));

    ASSERT_OK(yb_admin_client_->Init());
  }
}

void YBTableTestBase::OpenTable() {
  ASSERT_OK(table_.Open(table_name(), client_.get()));
  session_ = NewSession();
}

void YBTableTestBase::CreateRedisTable(const YBTableName& table_name) {
  CHECK(table_name.namespace_type() == YQLDatabase::YQL_DATABASE_REDIS);

  ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                table_name.namespace_type()));
  ASSERT_OK(NewTableCreator()->table_name(table_name)
                .table_type(YBTableType::REDIS_TABLE_TYPE)
                .num_tablets(num_tablets())
                .Create());
}

void YBTableTestBase::CreateTable() {
  const auto tn = table_name();
  if (!table_exists_) {
    ASSERT_OK(client_->CreateNamespaceIfNotExists(tn.namespace_name(), tn.namespace_type()));

    YBSchemaBuilder b;
    b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
    b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
    ASSERT_OK(b.Build(&schema_));

    ASSERT_OK(NewTableCreator()->table_name(tn).schema(&schema_).Create());
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
  return client_->NewSession(MonoDelta::FromMilliseconds(session_timeout_ms()));
}

Status YBTableTestBase::PutKeyValue(YBSession* session, const string& key, const string& value) {
  auto insert = table_.NewInsertOp();
  QLAddStringHashValue(insert->mutable_request(), key);
  table_.AddStringColumnValue(insert->mutable_request(), "v", value);
  return session->TEST_ApplyAndFlush(insert);
}

void YBTableTestBase::PutKeyValue(const string& key, const string& value) {
  ASSERT_OK(PutKeyValue(session_.get(), key, value));
}

void YBTableTestBase::PutKeyValueIgnoreError(const string& key, const string& value) {
  auto s ATTRIBUTE_UNUSED = PutKeyValue(session_.get(), key, value);
  if (!s.ok()) {
    LOG(WARNING) << "PutKeyValueIgnoreError: " << s;
  }
  return;
}

void YBTableTestBase::RestartCluster() {
  DCHECK(!use_external_mini_cluster());
  CHECK_OK(mini_cluster_->RestartSync());
  ASSERT_NO_FATALS(CreateClient());
  ASSERT_NO_FATALS(OpenTable());
}

Result<std::vector<uint32_t>> YBTableTestBase::GetTserverLoads(const std::vector<int>& ts_idxs) {
  std::vector<uint32_t> tserver_loads;
  for (const auto& ts_idx : ts_idxs) {
    tserver_loads.push_back(
        VERIFY_RESULT(GetLoadOnTserver(external_mini_cluster_->tablet_server(ts_idx))));
  }
  return tserver_loads;
}

Result<uint32_t> YBTableTestBase::GetLoadOnTserver(ExternalTabletServer* server) {
  auto proxy = GetMasterLeaderProxy<master::MasterClientProxy>();
  uint32_t count = 0;
  std::vector<string> replicas;
  // Need to get load from each table.
  for (const auto& table_name : table_names_) {
    master::GetTableLocationsRequestPB req;
    if (table_name.namespace_type() == YQL_DATABASE_PGSQL) {
      // Use table_id/namespace_id for SQL tables.
      req.mutable_table()->set_table_id(table_name.table_id());
      req.mutable_table()->mutable_namespace_()->set_id(table_name.namespace_id());
    } else {
      req.mutable_table()->set_table_name(table_name.table_name());
      req.mutable_table()->mutable_namespace_()->set_name(table_name.namespace_name());
    }
    req.set_max_returned_locations(num_tablets());
    master::GetTableLocationsResponsePB resp;

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromMilliseconds(client_rpc_timeout_ms()));
    RETURN_NOT_OK(proxy.GetTableLocations(req, &resp, &rpc));

    for (const auto& loc : resp.tablet_locations()) {
      for (const auto& replica : loc.replicas()) {
        if (replica.ts_info().permanent_uuid() == server->instance_id().permanent_uuid()) {
          replicas.push_back(loc.tablet_id());
          count++;
        }
      }
    }
  }

  LOG(INFO) << Format("For ts $0, tablets are $1 with count $2",
                      server->instance_id().permanent_uuid(), VectorToString(replicas), count);
  return count;
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

void YBTableTestBase::WaitForLoadBalanceCompletion(yb::MonoDelta timeout) {
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
    return !is_idle;
  }, timeout, "IsLoadBalancerActive"));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  }, timeout, "IsLoadBalancerIdle"));
}

std::unique_ptr<client::YBTableCreator> YBTableTestBase::NewTableCreator() {
  unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  if (num_tablets() > 0) {
    table_creator->num_tablets(num_tablets());
  }
  table_creator->table_type(YBTableType::YQL_TABLE_TYPE);
  return table_creator;
}

}  // namespace integration_tests
}  // namespace yb
