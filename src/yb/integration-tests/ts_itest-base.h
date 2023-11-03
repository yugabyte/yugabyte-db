// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/tserver/tablet_server-test-base.h"

#include "yb/client/table_handle.h"

#include "yb/util/random.h"

DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(consensus_rpc_timeout_ms);
DECLARE_int32(num_tablet_servers);
DECLARE_int32(num_replicas);

#define ASSERT_ALL_REPLICAS_AGREE(count) \
  ASSERT_NO_FATALS(AssertAllReplicasAgree(count))

namespace yb {

class ExternalMiniCluster;
struct ExternalMiniClusterOptions;

namespace itest {

class ExternalMiniClusterFsInspector;

}

namespace tserver {

static const int kMaxRetries = 20;

// A base for tablet server integration tests.
class TabletServerIntegrationTestBase : public TabletServerTestBase {
 public:
  TabletServerIntegrationTestBase();
  ~TabletServerIntegrationTestBase();

  void AddExtraFlags(const std::string& flags_str, std::vector<std::string>* flags);

  void CreateCluster(const std::string& data_root_path,
                     const std::vector<std::string>& non_default_ts_flags = {},
                     const std::vector<std::string>& non_default_master_flags = {});

  virtual void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) {}

  // Creates TSServerDetails instance for each TabletServer and stores them
  // in 'tablet_servers_'.
  void CreateTSProxies();

  // Waits that all replicas for a all tablets of 'kTableName' table are online
  // and creates the tablet_replicas_ map.
  void WaitForReplicasAndUpdateLocations();

  // Returns the last committed leader of the consensus configuration. Tries to get it from master
  // but then actually tries to the get the committed consensus configuration to make sure.
  itest::TServerDetails* GetLeaderReplicaOrNull(const std::string& tablet_id);

  Status GetLeaderReplicaWithRetries(const std::string& tablet_id,
                                     itest::TServerDetails** leader,
                                     int max_attempts = 100);

  Status GetTabletLeaderUUIDFromMaster(const std::string& tablet_id,
                                       std::string* leader_uuid);

  itest::TServerDetails* GetReplicaWithUuidOrNull(const std::string& tablet_id,
                                                  const std::string& uuid);

  // Gets the locations of the consensus configuration and waits until all replicas
  // are available for all tablets.
  void WaitForTSAndReplicas();

  // Removes a set of servers from the replicas_ list.
  // Handy for controlling who to validate against after killing servers.
  void PruneFromReplicas(const std::unordered_set<std::string>& uuids);

  void GetOnlyLiveFollowerReplicas(const std::string& tablet_id,
                                   std::vector<itest::TServerDetails*>* followers);

  // Return the index within 'replicas' for the replica which is farthest ahead.
  int64_t GetFurthestAheadReplicaIdx(const std::string& tablet_id,
                                     const std::vector<itest::TServerDetails*>& replicas);

  Status ShutdownServerWithUUID(const std::string& uuid);

  Status RestartServerWithUUID(const std::string& uuid);

  // Since we're fault-tolerant we might mask when a tablet server is
  // dead. This returns Status::IllegalState() if fewer than 'num_tablet_servers'
  // are alive.
  Status CheckTabletServersAreAlive(size_t num_tablet_servers);

  void TearDown() override;

  Result<std::unique_ptr<client::YBClient>> CreateClient();

  // Create a table with a single tablet.
  void CreateTable();

  // Starts an external cluster with a single tablet and a number of replicas equal
  // to 'FLAGS_num_replicas'. The caller can pass 'ts_flags' to specify non-default
  // flags to pass to the tablet servers.
  void BuildAndStart(const std::vector<std::string>& ts_flags = std::vector<std::string>(),
                     const std::vector<std::string>& master_flags = std::vector<std::string>());

  void AssertAllReplicasAgree(size_t expected_result_count);

  client::YBTableType table_type();

 protected:
  std::unique_ptr<ExternalMiniCluster> cluster_;
  std::unique_ptr<itest::ExternalMiniClusterFsInspector> inspect_;

  // Maps server uuid to TServerDetails
  itest::TabletServerMap tablet_servers_;
  // Maps tablet to all replicas.
  itest::TabletReplicaMap tablet_replicas_;

  std::unique_ptr<client::YBClient> client_;
  client::TableHandle table_;
  std::string tablet_id_;

  ThreadSafeRandom random_;
};

}  // namespace tserver
}  // namespace yb
