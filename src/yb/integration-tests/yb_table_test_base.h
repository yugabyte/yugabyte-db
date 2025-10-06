// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>

#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "yb/client/schema.h"
#include "yb/client/table_handle.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/mini_master.h"

#include "yb/tools/tools_fwd.h"

#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace integration_tests {

// This is a common base class that SQLTableTest and RedisTableTest inherit from.
// In future some of the functionality may be migrated to sub-base classes when it becomes bigger.
// i.e. scan related functions may be moved down because it is only supported for SQL tables.
class YBTableTestBase : public YBTest {
 protected:
  YBTableTestBase();
  ~YBTableTestBase();

  void SetUp() override;
  void TearDown() override;

  virtual void BeforeCreateTable();
  virtual void BeforeStartCluster();

  virtual bool use_external_mini_cluster();
  virtual bool use_yb_admin_client();
  virtual int session_timeout_ms();
  virtual size_t num_masters();
  virtual size_t num_tablet_servers();
  virtual int num_drives();
  virtual int num_tablets();
  virtual int client_rpc_timeout_ms();
  virtual client::YBTableName table_name();
  virtual bool need_redis_table();
  virtual bool enable_ysql();

  void CreateRedisTable(const client::YBTableName& table_name);
  virtual void CreateTable();
  void OpenTable();
  virtual void DeleteTable();
  virtual Status PutKeyValue(yb::client::YBSession* session,
                             const std::string& key,
                             const std::string& value);
  virtual void PutKeyValue(const std::string& key, const std::string& value);
  virtual void PutKeyValueIgnoreError(const std::string& key, const std::string& value);
  void RestartCluster();
  std::vector<std::pair<std::string, std::string>> GetScanResults(const client::TableRange& range);
  void FetchTSMetricsPage();
  void WaitForLoadBalanceCompletion(
      yb::MonoDelta timeout = MonoDelta::FromMilliseconds(kDefaultLoadBalanceTimeoutMs));

  // These utility functions only work with external_mini_cluster_.
  template <class T>
  T GetMasterLeaderProxy() {
    DCHECK(use_external_mini_cluster());
    return external_mini_cluster_->GetLeaderMasterProxy<T>();
  }

  // Calls GetLoadOnTserver to get loads for the provided tservers.
  Result<std::vector<uint32_t>> GetTserverLoads(const std::vector<int>& ts_idxs);
  Result<uint32_t> GetLoadOnTserver(ExternalTabletServer* server);

  client::TableHandle table_;
  std::unique_ptr<client::YBClient> client_;
  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;
  bool table_exists_ = false;

  yb::MiniCluster* mini_cluster() {
    assert(!use_external_mini_cluster());
    return mini_cluster_.get();
  }

  yb::ExternalMiniCluster* external_mini_cluster() {
    assert(use_external_mini_cluster());
    return external_mini_cluster_.get();
  }

  virtual void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) {}

  std::vector<std::string> master_rpc_addresses_as_strings() {
    std::vector<std::string> host_ports;
    size_t num_masters = use_external_mini_cluster() ? external_mini_cluster()->num_masters()
                                                     : mini_cluster()->num_masters();
    for (size_t i = 0; i < num_masters; i++) {
      auto sock_addr = use_external_mini_cluster()
                           ? external_mini_cluster()->master(i)->bound_rpc_addr()
                           : mini_cluster()->mini_master(i)->bound_rpc_addr();
      host_ports.push_back(ToString(sock_addr));
    }
    return host_ports;
  }

  // This sets up common options for creating all tables the test needs to create.
  virtual std::unique_ptr<client::YBTableCreator> NewTableCreator();

  static constexpr int kDefaultNumMasters = 1;
  static constexpr int kDefaultNumTabletServers = 3;
  static constexpr int kDefaultNumDrives = 1;
  static constexpr int kDefaultSessionTimeoutMs = 60000;
  static constexpr int kDefaultClientRpcTimeoutMs = 30000;
  static constexpr int kDefaultLoadBalanceTimeoutMs = 60000;
  static constexpr bool kDefaultUsingExternalMiniCluster = false;
  static constexpr bool kDefaultEnableYSQL = true;
  static const client::YBTableName kDefaultTableName;

  // Set custom Env and rocksdb::Env to be used by MiniTabletServer, otherwise MiniTabletServer
  // will use own Env and rocksdb::Env.
  std::unique_ptr<Env> ts_env_;
  std::unique_ptr<rocksdb::Env> ts_rocksdb_env_;

  std::vector<uint16_t> master_rpc_ports();
  // Calls CreateYBClient and assigns it to local class field.
  void CreateClient();
  // Creates a ClientYB client without assigning it to the class field.
  std::unique_ptr<yb::client::YBClient> CreateYBClient();
  void CreateAdminClient();

  std::shared_ptr<yb::client::YBSession> NewSession();

  yb::client::YBSchema schema_;
  std::shared_ptr<yb::client::YBSession> session_;

  // Exactly one of the following two pointers will be set.
  std::unique_ptr<yb::MiniCluster> mini_cluster_;
  std::unique_ptr<yb::ExternalMiniCluster> external_mini_cluster_;

  // All the default tables that are pre-created. Used to skip the initial create table step, when
  // the given table has been already pre-created.
  std::vector<std::string> default_tables_created_;

  // For tests that use multiple tables, store the tables here.
  // For tests with a single table, this is equivalent to table_name().
  std::vector<client::YBTableName> table_names_;
};

}  // namespace integration_tests
}  // namespace yb
