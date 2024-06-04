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

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_call_home.h"
#include "yb/server/call_home-test-util.h"
#include "yb/server/call_home.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master-test_base.h"
#include "yb/master/master.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_heartbeat.proxy.h"
#include "yb/master/master_error.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/server/server_base.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"
#include "yb/util/user.h"

using std::shared_ptr;
using std::make_shared;
using std::string;
using std::vector;

DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(TEST_simulate_slow_table_create_secs);
DECLARE_bool(TEST_return_error_if_namespace_not_found);
DECLARE_bool(TEST_hang_on_namespace_transition);
DECLARE_int32(TEST_sys_catalog_write_rejection_percentage);
DECLARE_bool(TEST_tablegroup_master_only);
DECLARE_bool(TEST_simulate_port_conflict_error);
DECLARE_bool(master_register_ts_check_desired_host_port);
DECLARE_string(use_private_ip);
DECLARE_bool(master_join_existing_universe);
DECLARE_bool(master_enable_universe_uuid_heartbeat_check);

METRIC_DECLARE_counter(block_cache_misses);
METRIC_DECLARE_counter(block_cache_hits);

namespace yb {
namespace master {

using strings::Substitute;

class MasterTest : public MasterTestBase {
 public:
  string GetWebserverDir() { return GetTestPath("webserver-docroot"); }

  void TestRegisterDistBroadcastDupPrivate(string use_private_ip, bool only_check_used_host_port);

  Result<TSHeartbeatResponsePB> SendHeartbeat(
      TSToMasterCommonPB common, TSRegistrationPB registration);

  Result<scoped_refptr<NamespaceInfo>> FindNamespaceByName(
      YQLDatabase db_type, const std::string& name);
};

Result<TSHeartbeatResponsePB> MasterTest::SendHeartbeat(
    TSToMasterCommonPB common, TSRegistrationPB registration) {
  SysClusterConfigEntryPB config =
      VERIFY_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();

  TSHeartbeatRequestPB req;
  TSHeartbeatResponsePB resp;
  req.mutable_common()->Swap(&common);
  req.mutable_registration()->Swap(&registration);
  req.set_universe_uuid(universe_uuid);
  RETURN_NOT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
  return resp;
}

TEST_F(MasterTest, TestPingServer) {
  // Ping the server.
  server::PingRequestPB req;
  server::PingResponsePB resp;

  rpc::ProxyCache proxy_cache(client_messenger_.get());
  server::GenericServiceProxy generic_proxy(&proxy_cache, mini_master_->bound_rpc_addr());
  ASSERT_OK(generic_proxy.Ping(req, &resp, ResetAndGetController()));
}

static void MakeHostPortPB(const std::string& host, uint32_t port, HostPortPB* pb) {
  pb->set_host(host);
  pb->set_port(port);
}

CloudInfoPB MakeCloudInfoPB(std::string cloud, std::string region, std::string zone) {
  CloudInfoPB result;
  *result.mutable_placement_cloud() = std::move(cloud);
  *result.mutable_placement_region() = std::move(region);
  *result.mutable_placement_zone() = std::move(zone);
  return result;
}

// Test that shutting down a MiniMaster without starting it does not
// SEGV.
TEST_F(MasterTest, TestShutdownWithoutStart) {
  MiniMaster m(Env::Default(), "/xxxx", AllocateFreePort(), AllocateFreePort(), 0);
  m.Shutdown();
}

TEST_F(MasterTest, TestCallHome) {
  const auto webserver_dir = GetWebserverDir();
  CHECK_OK(env_->CreateDir(webserver_dir));
  TestCallHome<Master, MasterCallHome>(
      webserver_dir, {"version_info", "masters", "tservers", "tables"}, mini_master_->master());
}

// This tests whether the enabling/disabling of callhome is happening dynamically
// during runtime.
TEST_F(MasterTest, TestCallHomeFlag) {
  const auto webserver_dir = GetWebserverDir();
  CHECK_OK(env_->CreateDir(webserver_dir));
  TestCallHomeFlag<Master, MasterCallHome>(webserver_dir, mini_master_->master());
}

TEST_F(MasterTest, TestGFlagsCallHome) {
  CHECK_OK(env_->CreateDir(GetWebserverDir()));
  TestGFlagsCallHome<Master, MasterCallHome>(mini_master_->master());
}

TEST_F(MasterTest, TestHeartbeatRequestWithEmptyUUID) {
  TSHeartbeatRequestPB req;
  TSHeartbeatResponsePB resp;
  req.mutable_common()->mutable_ts_instance()->set_permanent_uuid("");
  req.mutable_common()->mutable_ts_instance()->set_instance_seqno(1);
  auto status = proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController());
  ASSERT_FALSE(status.ok());
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "Recevied Empty UUID");
}

class MasterTestSkipUniverseUuidCheck : public MasterTest {
  void SetUp() override {
    FLAGS_master_enable_universe_uuid_heartbeat_check = false;
    MasterTest::SetUp();
  }
};

TEST_F(MasterTestSkipUniverseUuidCheck, TestUniverseUuidUpgrade) {
  // Start the master with FLAGS_master_enable_universe_uuid_heartbeat_check set to false,
  // restart master, and set this flag to true (to simulate autoflag behavior). Ensure that after
  // setting the flag to true, universe_uuid is eventually set.
  master::SysClusterConfigEntryPB config =
      ASSERT_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  ASSERT_EQ(config.universe_uuid(), "");

  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  FLAGS_master_enable_universe_uuid_heartbeat_check = true;

  config.Clear();
  ASSERT_OK(WaitFor([this]() {
    auto config_result = mini_master_->catalog_manager().GetClusterConfig();
    if (!config_result.ok()) {
      return false;
    }
    return !config_result->universe_uuid().empty();
  }, MonoDelta::FromSeconds(30), "Wait for universe_uuid set in cluster config"));
}

TEST_F(MasterTest, TestUniverseUuidDisabled) {
  const string kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  {
    // Try a heartbeat with an invalid universe_uuid passed into the request. When
    // FLAGS_master_enable_universe_uuid_heartbeat_check is false, the response should still be
    // valid.
    FLAGS_master_enable_universe_uuid_heartbeat_check = false;
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    auto fake_uuid = Uuid::Generate();
    req.set_universe_uuid(fake_uuid.ToString());
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
    ASSERT_FALSE(resp.has_error());
  }
}

TEST_F(MasterTest, TestUniverseUuidMismatch) {
  const string kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  {
    // Try a heartbeat with an invalid universe_uuid passed into the request.
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    auto fake_uuid = Uuid::Generate();
    req.set_universe_uuid(fake_uuid.ToString());
    auto status = proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController());
    ASSERT_FALSE(status.ok());
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "wrong universe_uuid");
  }
}

TEST_F(MasterTest, TestNoUniverseUuid) {
  const string kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  SysClusterConfigEntryPB config =
      ASSERT_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();
  ASSERT_NE(universe_uuid, "");
  {
    // Try a heartbeat with no universe_uuid passed into the request.
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
    // The response should contain the universe_uuid.
    ASSERT_EQ(resp.universe_uuid(), universe_uuid);
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::INVALID_REQUEST);
  }
}

TEST_F(MasterTest, TestEmptyStringUniverseUuid) {
  const string kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  SysClusterConfigEntryPB config =
      ASSERT_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();
  ASSERT_NE(universe_uuid, "");
  {
    // Try a heartbeat with an empty universe_uuid passed into the request.
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.set_universe_uuid("");
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
    // The response should contain the universe_uuid, but the response should have an error.
    ASSERT_EQ(resp.universe_uuid(), universe_uuid);
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::INVALID_REQUEST);
  }
}

TEST_F(MasterTest, TestMatchingUniverseUuid) {
  const string kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  SysClusterConfigEntryPB config =
      ASSERT_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();
  ASSERT_NE(universe_uuid, "");
  {
    // Try a heartbeat with the correct universe_uuid passed into the request.
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.set_universe_uuid(universe_uuid);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
    ASSERT_FALSE(resp.has_error());
  }
}

TEST_F(MasterTest, TestRegisterAndHeartbeat) {
  const char *kTsUUID = "my-ts-uuid";

  TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(kTsUUID);
  common.mutable_ts_instance()->set_instance_seqno(1);

  SysClusterConfigEntryPB config =
      ASSERT_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();

  // Try a heartbeat. The server hasn't heard of us, so should ask us to re-register.
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.set_universe_uuid(universe_uuid);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_TRUE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  vector<shared_ptr<TSDescriptor> > descs;
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(0, descs.size()) << "Should not have registered anything";

  shared_ptr<TSDescriptor> ts_desc;
  ASSERT_FALSE(mini_master_->master()->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));

  // Register the fake TS, without sending any tablet report.
  TSRegistrationPB fake_reg;
  MakeHostPortPB("localhost", 1000, fake_reg.mutable_common()->add_private_rpc_addresses());
  MakeHostPortPB("localhost", 2000, fake_reg.mutable_common()->add_http_addresses());

  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.mutable_registration()->CopyFrom(fake_reg);
    req.set_universe_uuid(universe_uuid);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
    ASSERT_TRUE(resp.has_tablet_report_limit());
  }

  descs.clear();
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(1, descs.size()) << "Should have registered the TS";
  TSRegistrationPB reg = descs[0]->GetRegistration();
  ASSERT_EQ(fake_reg.DebugString(), reg.DebugString()) << "Master got different registration";

  ASSERT_TRUE(mini_master_->master()->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));
  ASSERT_EQ(ts_desc, descs[0]);

  // If the tablet server somehow lost the response to its registration RPC, it would
  // attempt to register again. In that case, we shouldn't reject it -- we should
  // just respond the same.
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.mutable_registration()->CopyFrom(fake_reg);
    req.set_universe_uuid(universe_uuid);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
    ASSERT_TRUE(resp.has_tablet_report_limit());
  }

  // Now begin sending full tablet report
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.set_universe_uuid(universe_uuid);
    TabletReportPB* tr = req.mutable_tablet_report();
    tr->set_is_incremental(false);
    tr->set_sequence_number(0);
    tr->set_remaining_tablet_count(1);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_FALSE(resp.needs_full_tablet_report());
  }

  // ...and finish the full tablet report.
  {
    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(common);
    req.set_universe_uuid(universe_uuid);
    TabletReportPB* tr = req.mutable_tablet_report();
    tr->set_is_incremental(false);
    tr->set_sequence_number(0);
    tr->set_remaining_tablet_count(0);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_FALSE(resp.needs_reregister());
    ASSERT_FALSE(resp.needs_full_tablet_report());
  }

  descs.clear();
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(1, descs.size()) << "Should still only have one TS registered";

  ASSERT_TRUE(mini_master_->master()->ts_manager()->LookupTSByUUID(kTsUUID, &ts_desc));
  ASSERT_EQ(ts_desc, descs[0]);

  // Ensure that the ListTabletServers shows the faked server.
  {
    ListTabletServersRequestPB req;
    ListTabletServersResponsePB resp;
    ASSERT_OK(proxy_cluster_->ListTabletServers(req, &resp, ResetAndGetController()));
    LOG(INFO) << resp.DebugString();
    ASSERT_EQ(1, resp.servers_size());
    ASSERT_EQ("my-ts-uuid", resp.servers(0).instance_id().permanent_uuid());
    ASSERT_EQ(1, resp.servers(0).instance_id().instance_seqno());
  }
}

void MasterTest::TestRegisterDistBroadcastDupPrivate(
    string use_private_ip, bool only_check_used_host_port) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_private_ip) = use_private_ip;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_register_ts_check_desired_host_port) =
      only_check_used_host_port;
  const std::vector<std::string> tsUUIDs = { "ts1-uuid", "ts2-uuid", "ts3-uuid", "ts4-uuid" };
  std::vector<TSToMasterCommonPB> commons(tsUUIDs.size());

  // ts1 - ts2 => distinct cloud, region and zone.
  // ts1 - ts3 => same cloud, region with distinct zone.
  // ts1 - ts4 => same cloud with distinct region.
  std::vector<CloudInfoPB> cloud_infos(tsUUIDs.size());
  auto& cloud_info_ts1 = cloud_infos[0];
  *cloud_info_ts1.mutable_placement_cloud() = "cloud-xyz";
  *cloud_info_ts1.mutable_placement_region() = "region-xyz";
  *cloud_info_ts1.mutable_placement_zone() = "zone-xyz";
  auto& cloud_info_ts2 = cloud_infos[1];
  *cloud_info_ts2.mutable_placement_cloud() = "cloud-abc";
  *cloud_info_ts2.mutable_placement_region() = "region-abc";
  *cloud_info_ts2.mutable_placement_zone() = "zone-abc";
  auto& cloud_info_ts3 = cloud_infos[2];
  *cloud_info_ts3.mutable_placement_cloud() = "cloud-xyz";
  *cloud_info_ts3.mutable_placement_region() = "region-xyz";
  *cloud_info_ts3.mutable_placement_zone() = "zone-pqr";
  auto& cloud_info_ts4 = cloud_infos[3];
  *cloud_info_ts4.mutable_placement_cloud() = "cloud-xyz";
  *cloud_info_ts4.mutable_placement_region() = "region-pqr";
  *cloud_info_ts4.mutable_placement_zone() = "zone-anything";

  SysClusterConfigEntryPB config =
      ASSERT_RESULT(mini_master_->catalog_manager().GetClusterConfig());
  auto universe_uuid = config.universe_uuid();

  // Try heartbeat from all the tservers. The master hasn't heard of them, so should ask them to
  // re-register.
  for (size_t i = 0; i < tsUUIDs.size(); i++) {
    commons[i].mutable_ts_instance()->set_permanent_uuid(tsUUIDs[i]);
    commons[i].mutable_ts_instance()->set_instance_seqno(1);

    TSHeartbeatRequestPB req;
    TSHeartbeatResponsePB resp;
    req.mutable_common()->CopyFrom(commons[i]);
    req.set_universe_uuid(universe_uuid);
    ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));

    ASSERT_TRUE(resp.needs_reregister());
    ASSERT_TRUE(resp.needs_full_tablet_report());
  }

  vector<shared_ptr<TSDescriptor> > descs;
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(0, descs.size()) << "Should not have registered anything";

  shared_ptr<TSDescriptor> ts_desc;
  for (auto& uuid : tsUUIDs) {
    ASSERT_FALSE(mini_master_->master()->ts_manager()->LookupTSByUUID(uuid, &ts_desc));
  }

  // Each tserver will have a distinct broadcast address but the same private_rpc_address.
  std::vector<TSRegistrationPB> fake_regs(tsUUIDs.size());
  for (size_t i = 0; i < tsUUIDs.size(); i++) {
    MakeHostPortPB("localhost", 1000, fake_regs[i].mutable_common()->add_private_rpc_addresses());
    MakeHostPortPB(
        Format("111.111.111.11$0", i), 2000,
        fake_regs[i].mutable_common()->add_broadcast_addresses());
    MakeHostPortPB("localhost", 3000, fake_regs[i].mutable_common()->add_http_addresses());
    *fake_regs[i].mutable_common()->mutable_cloud_info() = cloud_infos[i];
  }

  std::set<string> expected_registration_uuids;
  if (only_check_used_host_port) {
    if (use_private_ip == "cloud") {
      // ts-3 and ts-4 will fail since they share cloud with ts-1.
      expected_registration_uuids = { tsUUIDs[0], tsUUIDs[1] };
    } else if (use_private_ip == "region") {
      // ts-3 fails since it shares region with ts-1.
      expected_registration_uuids = { tsUUIDs[0], tsUUIDs[1], tsUUIDs[3] };
    } else {
      expected_registration_uuids.insert(tsUUIDs.begin(), tsUUIDs.end());
    }
  } else {
    expected_registration_uuids = { tsUUIDs[0] };
  }

  // Register the fake TServers.
  {
    for (size_t i = 0; i < tsUUIDs.size(); i++) {
      TSHeartbeatRequestPB req;
      TSHeartbeatResponsePB resp;
      req.mutable_common()->CopyFrom(commons[i]);
      req.set_universe_uuid(universe_uuid);
      req.mutable_registration()->CopyFrom(fake_regs[i]);

      ASSERT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
      if (expected_registration_uuids.find(tsUUIDs[i]) == expected_registration_uuids.end()) {
        ASSERT_TRUE(resp.needs_reregister());
      } else {
        ASSERT_FALSE(resp.needs_reregister());
      }
    }
  }

  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(expected_registration_uuids.size(), descs.size())
      << "Should have registered the expected number of TServers";

  // Ensure that the ListTabletServers shows the faked servers.
  {
    ListTabletServersRequestPB req;
    ListTabletServersResponsePB resp;
    ASSERT_OK(proxy_cluster_->ListTabletServers(req, &resp, ResetAndGetController()));
    LOG(INFO) << resp.DebugString();
    ASSERT_EQ(expected_registration_uuids.size(), resp.servers_size());

    // Assert that expected UUIDs are present in the response.
    std::unordered_set<std::string> uuids_res;
    for (auto i = 0; i < resp.servers_size(); i++) {
      uuids_res.insert(resp.servers(i).instance_id().permanent_uuid());
    }
    for (auto& uuid : expected_registration_uuids) {
      ASSERT_TRUE(uuids_res.contains(uuid));
    }
  }
}

TEST_F(MasterTest, TestRegisterDistBroadcastDupPrivateUsePrivateIpNeverCheckUsed) {
  TestRegisterDistBroadcastDupPrivate(
      /*use_private_ip*/ "never", /*only_check_used_host_port*/ true);
}

TEST_F(MasterTest, TestRegisterDistBroadcastDupPrivateUsePrivateIpNeverCheckAll) {
  TestRegisterDistBroadcastDupPrivate(
      /*use_private_ip*/ "never", /*only_check_used_host_port*/ false);
}

TEST_F(MasterTest, TestRegisterDistBroadcastDupPrivateUsePrivateIpCloudCheckUsed) {
  TestRegisterDistBroadcastDupPrivate(
      /*use_private_ip*/ "cloud", /*only_check_used_host_port*/ true);
}

TEST_F(MasterTest, TestRegisterDistBroadcastDupPrivateUsePrivateIpCloudCheckAll) {
  TestRegisterDistBroadcastDupPrivate(
      /*use_private_ip*/ "cloud", /*only_check_used_host_port*/ false);
}

TEST_F(MasterTest, TestRegisterDistBroadcastDupPrivateUsePrivateIpRegionCheckUsed) {
  TestRegisterDistBroadcastDupPrivate(
      /*use_private_ip*/ "region", /*only_check_used_host_port*/ true);
}

TEST_F(MasterTest, TestRegisterDistBroadcastDupPrivateUsePrivateIpRegionCheckAll) {
  TestRegisterDistBroadcastDupPrivate(
      /*use_private_ip*/ "region", /*only_check_used_host_port*/ false);
}

TEST_F(MasterTest, TestReRegisterRemovedUUID) {
  // When a tserver's disk is wiped and the process restarted, the tserver comes back with a
  // different uuid. If a quorum is broken by a majority of tservers failing in this way, one
  // strategy to repair the quorum is to reset the wiped tservers with their original uuids.
  // However this requires the master to re-register a tserver with a uuid it has seen before and
  // rejected due to seeing a later sequence number from the same node. This test verifies the
  // master process can handle re-registering tservers with uuids it has previously removed.
  const std::string first_uuid = "uuid1";
  const std::string second_uuid = "uuid2";
  vector<shared_ptr<TSDescriptor>> descs;
  int seqno = 1;
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  EXPECT_EQ(descs.size(), 0);
  TSToMasterCommonPB original_common;
  TSRegistrationPB registration;
  original_common.mutable_ts_instance()->set_permanent_uuid(first_uuid);
  original_common.mutable_ts_instance()->set_instance_seqno(seqno++);
  MakeHostPortPB("localhost", 1000, registration.mutable_common()->add_private_rpc_addresses());
  MakeHostPortPB("localhost", 1000, registration.mutable_common()->add_broadcast_addresses());
  MakeHostPortPB("localhost", 2000, registration.mutable_common()->add_http_addresses());
  *registration.mutable_common()->mutable_cloud_info() = MakeCloudInfoPB("cloud", "region", "zone");
  auto resp = ASSERT_RESULT(SendHeartbeat(original_common, registration));
  EXPECT_FALSE(resp.needs_reregister());

  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(descs.size(), 1);
  auto original_desc = descs[0];

  auto new_common = original_common;
  new_common.mutable_ts_instance()->set_permanent_uuid(second_uuid);
  new_common.mutable_ts_instance()->set_instance_seqno(seqno++);
  resp = ASSERT_RESULT(SendHeartbeat(new_common, registration));
  EXPECT_FALSE(resp.needs_reregister());
  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  // This function filters out descriptors of removed tservers so we still expect just 1 descriptor.
  ASSERT_EQ(descs.size(), 1);
  auto new_desc = descs[0];
  EXPECT_EQ(new_desc->permanent_uuid(), second_uuid);
  EXPECT_TRUE(original_desc->IsRemoved());

  auto updated_original_common = original_common;
  updated_original_common.mutable_ts_instance()->set_instance_seqno(seqno++);
  resp = ASSERT_RESULT(SendHeartbeat(updated_original_common, registration));
  EXPECT_FALSE(resp.needs_reregister());

  mini_master_->master()->ts_manager()->GetAllDescriptors(&descs);
  ASSERT_EQ(descs.size(), 1);
  EXPECT_EQ(descs[0]->permanent_uuid(), first_uuid);
  EXPECT_TRUE(new_desc->IsRemoved());
}

TEST_F(MasterTest, TestListTablesWithoutMasterCrash) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_slow_table_create_secs) = 10;

  const char *kNamespaceName = "testnamespace";
  CreateNamespaceResponsePB resp;
  ASSERT_OK(CreateNamespace(kNamespaceName, YQLDatabase::YQL_DATABASE_CQL, &resp));

  auto task = [kNamespaceName, this]() {
    const char *kTableName = "testtable";
    const Schema kTableSchema({ ColumnSchema("key", DataType::INT32, ColumnKind::HASH) });
    shared_ptr<RpcController> controller;
    // Set an RPC timeout for the controllers.
    controller = make_shared<RpcController>();
    controller->set_timeout(MonoDelta::FromSeconds(FLAGS_TEST_simulate_slow_table_create_secs * 2));

    CreateTableRequestPB req;
    CreateTableResponsePB resp;

    req.set_name(kTableName);
    SchemaToPB(kTableSchema, req.mutable_schema());
    req.mutable_namespace_()->set_name(kNamespaceName);
    req.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(8);
    ASSERT_OK(this->proxy_ddl_->CreateTable(req, &resp, controller.get()));
    ASSERT_FALSE(resp.has_error());
    LOG(INFO) << "Done creating table";
  };

  std::thread t(task);

  // Delete the namespace (by NAME).
  {
    // Give the CreateTable request some time to start and find the namespace.
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_simulate_slow_table_create_secs / 2));
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(kNamespaceName);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  t.join();

  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_return_error_if_namespace_not_found) = true;
    ListTablesRequestPB req;
    ListTablesResponsePB resp;
    ASSERT_OK(proxy_ddl_->ListTables(req, &resp, ResetAndGetController()));
    LOG(INFO) << "Finished first ListTables request";
    ASSERT_TRUE(resp.has_error());
    string msg = resp.error().status().message();
    ASSERT_TRUE(msg.find("Keyspace identifier not found") != string::npos);

    // After turning off this flag, ListTables should skip the table with the error.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_return_error_if_namespace_not_found) = false;
    ASSERT_OK(proxy_ddl_->ListTables(req, &resp, ResetAndGetController()));
    LOG(INFO) << "Finished second ListTables request";
    ASSERT_FALSE(resp.has_error());
  }
}

TEST_F(MasterTest, TestCatalog) {
  const char *kTableName = "testtb";
  const char *kOtherTableName = "tbtest";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
      ColumnSchema("v1", DataType::UINT64),
      ColumnSchema("v2", DataType::STRING) });

  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  ListTablesResponsePB tables;
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table
  TableId id;
  ASSERT_OK(DeleteTableSync(default_namespace_name, kTableName, &id));

  // List tables, should show only system table
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Re-create the table
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  // Restart the master, verify the table still shows up.
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Test listing tables with a filter.
  ASSERT_OK(CreateTable(kOtherTableName, kTableSchema));

  {
    ListTablesRequestPB req;
    req.set_name_filter("test");
    DoListTables(req, &tables);
    ASSERT_EQ(2, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("tb");
    DoListTables(req, &tables);
    ASSERT_EQ(2, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter(kTableName);
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_EQ(kTableName, tables.tables(0).name());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("btes");
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_EQ(kOtherTableName, tables.tables(0).name());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("randomname");
    DoListTables(req, &tables);
    ASSERT_EQ(0, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.set_name_filter("peer");
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_EQ(kSystemPeersTableName, tables.tables(0).name());
  }

  {
    ListTablesRequestPB req;
    req.add_relation_type_filter(USER_TABLE_RELATION);
    DoListTables(req, &tables);
    ASSERT_EQ(2, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.add_relation_type_filter(INDEX_TABLE_RELATION);
    DoListTables(req, &tables);
    ASSERT_EQ(0, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.add_relation_type_filter(MATVIEW_TABLE_RELATION);
    DoListTables(req, &tables);
    ASSERT_EQ(0, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.add_relation_type_filter(COLOCATED_PARENT_TABLE_RELATION);
    DoListTables(req, &tables);
    ASSERT_EQ(0, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.add_relation_type_filter(SYSTEM_TABLE_RELATION);
    DoListTables(req, &tables);
    ASSERT_EQ(kNumSystemTables, tables.tables_size());
  }

  {
    ListTablesRequestPB req;
    req.add_relation_type_filter(SYSTEM_TABLE_RELATION);
    req.add_relation_type_filter(USER_TABLE_RELATION);
    DoListTables(req, &tables);
    ASSERT_EQ(kNumSystemTables + 2, tables.tables_size());
  }
}

TEST_F(MasterTest, TestListTablesIncludesIndexedTableId) {
  // Create a new PGSQL namespace.
  NamespaceName test_name = "test_pgsql";
  CreateNamespaceResponsePB resp;
  NamespaceId nsid;
  ASSERT_OK(CreateNamespaceAsync(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  nsid = resp.id();

  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
      ColumnSchema("v1", DataType::UINT64),
      ColumnSchema("v2", DataType::STRING) });
  const TableName kTableNamePgsql = "testtb_pgsql";
  ASSERT_OK(CreatePgsqlTable(nsid, kTableNamePgsql, kTableSchema));

  ListTablesResponsePB tables;
  TableId id;
  {
    ListTablesRequestPB req;
    req.set_name_filter("testtb_pgsql");
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    id = tables.tables(0).id();
  }

  master::CreateTableRequestPB req;
  IndexInfoPB index_info;
  index_info.set_indexed_table_id(id);
  index_info.set_hash_column_count(1);
  index_info.add_indexed_hash_column_ids(10);
  auto *col = index_info.add_columns();
  col->set_column_name("v1");
  col->set_indexed_column_id(10);
  req.mutable_index_info()->CopyFrom(index_info);
  req.set_indexed_table_id(id);
  const TableName kIndexNamePgsql = "testin_pgsql";
  const Schema kIndexSchema(
      {ColumnSchema("v1", DataType::UINT64, ColumnKind::RANGE_ASC_NULL_FIRST)});
  ASSERT_OK(CreatePgsqlTable(nsid, kIndexNamePgsql, kIndexSchema, &req));
  {
    ListTablesRequestPB req;
    req.set_name_filter("testin_pgsql");
    DoListTables(req, &tables);
    ASSERT_EQ(1, tables.tables_size());
    ASSERT_TRUE(tables.tables(0).has_indexed_table_id());
    ASSERT_EQ(id, tables.tables(0).indexed_table_id());
  }
}

TEST_F(MasterTest, TestParentBasedTableToTabletMappingFlag) {
  // This test is for the new parent table based mapping from tables to tablets. It verifies we only
  // use the new schema for user tables when the flag is set. In particular this test verifies:
  //   1. We never use the new parent table based schema for system tables.
  //   2. When the flag is set, we use the new parent table based schema for user tables.
  //   3. When the flag is not set we use the old mapping schema for user tables.
  const std::string kNewSchemaTableName = "newschema";
  const std::string kOldSchemaTableName = "oldschema";
  const Schema kTableSchema(
      {ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
       ColumnSchema("v1", DataType::UINT64),
       ColumnSchema("v2", DataType::STRING)});
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_parent_table_id_field) = true;
  ASSERT_OK(CreateTable(kNewSchemaTableName, kTableSchema));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_parent_table_id_field) = false;
  ASSERT_OK(CreateTable(kOldSchemaTableName, kTableSchema));

  auto tables = mini_master_->catalog_manager_impl().GetTables(GetTablesMode::kAll);
  for (const auto& table : tables) {
    for (const auto& tablet : table->GetTablets(IncludeInactive::kTrue)) {
      if (table->is_system() &&
          tablet->LockForRead().data().pb.hosted_tables_mapped_by_parent_id()) {
        FAIL() << Format(
            "System table $0 has tablet $1 using the new schema", table->name(), tablet->id());
      }
    }
  }

  auto find_table = [&tables](const std::string& name) -> Result<TableInfoPtr> {
    auto table_it = std::find_if(
        tables.begin(), tables.end(),
        [&name](const TableInfoPtr& table_info) { return table_info->name() == name; });
    if (table_it != tables.end()) {
      return *table_it;
    } else {
      return STATUS_FORMAT(NotFound, "Couldn't find table $0", name);
    }
  };

  auto tablet_uses_new_schema = [](const TabletInfoPtr& tablet_info) {
    return tablet_info->LockForRead().data().pb.hosted_tables_mapped_by_parent_id();
  };

  const auto new_schema_tablets =
      ASSERT_RESULT(find_table(kNewSchemaTableName))->GetTablets(IncludeInactive::kTrue);
  EXPECT_TRUE(
      std::all_of(new_schema_tablets.begin(), new_schema_tablets.end(), tablet_uses_new_schema));
  EXPECT_GT(new_schema_tablets.size(), 0);

  auto old_schema_tablets =
      ASSERT_RESULT(find_table(kOldSchemaTableName))->GetTablets(IncludeInactive::kTrue);
  EXPECT_TRUE(
      std::none_of(old_schema_tablets.begin(), old_schema_tablets.end(), tablet_uses_new_schema));
  EXPECT_GT(old_schema_tablets.size(), 0);
}

TEST_F(MasterTest, TestCatalogHasBlockCache) {
  // Restart mini_master
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  // Check prometheus metrics via webserver to verify block_cache metrics exist
  string addr = AsString(mini_master_->bound_http_addr());
  string url = strings::Substitute("http://$0/prometheus-metrics", AsString(addr));
  EasyCurl curl;
  faststring buf;

  ASSERT_OK(curl.FetchURL(url, &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "block_cache_misses");
  ASSERT_STR_CONTAINS(buf.ToString(), "block_cache_hits");

  // Check block cache metrics directly and verify
  // that the counters are greater than 0
  const auto metric_map = mini_master_->master()->metric_entity()->UnsafeMetricsMapForTests();

  scoped_refptr<Counter> cache_misses_counter = down_cast<Counter *>(
      FindOrDie(metric_map,
                &METRIC_block_cache_misses).get());
  scoped_refptr<Counter> cache_hits_counter = down_cast<Counter *>(
      FindOrDie(metric_map,
                &METRIC_block_cache_hits).get());

  ASSERT_GT(cache_misses_counter->value(), 0);
  ASSERT_GT(cache_hits_counter->value(), 0);
}

TEST_F(MasterTest, TestTablegroups) {
  TablegroupId kTablegroupId = GetPgsqlTablegroupId(12345, 67890);
  TableId      kTableId = GetPgsqlTableId(123455, 67891);
  const char*  kTableName = "test_table";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  const NamespaceName ns_name = "test_tablegroup_ns";

  // Create a new namespace.
  NamespaceId ns_id;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(ns_name, YQL_DATABASE_PGSQL, &resp));
    ns_id = resp.id();
  }

  {
    ListNamespacesResponsePB namespaces;
    ASSERT_NO_FATALS(DoListAllNamespaces(YQL_DATABASE_PGSQL, &namespaces));
    ASSERT_EQ(1, namespaces.namespaces_size());
    CheckNamespaces(
        {
            std::make_tuple(ns_name, ns_id)
        }, namespaces);
  }

  SetAtomicFlag(true, &FLAGS_TEST_tablegroup_master_only);
  // Create tablegroup and ensure it exists in catalog manager maps.
  ASSERT_OK(CreateTablegroup(kTablegroupId, ns_id, ns_name, "" /* tablespace_id */));
  SetAtomicFlag(false, &FLAGS_TEST_tablegroup_master_only);

  ListTablegroupsRequestPB req;
  req.set_namespace_id(ns_id);
  {
    ListTablegroupsResponsePB resp;
    ASSERT_NO_FATALS(DoListTablegroups(req, &resp));

    bool tablegroup_found = false;
    for (auto& tg : *resp.mutable_tablegroups()) {
      if (tg.id().compare(kTablegroupId) == 0) {
        tablegroup_found = true;
      }
    }
    ASSERT_TRUE(tablegroup_found);
  }

  // Restart the master, verify the tablegroup still shows up
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  {
    ListTablegroupsResponsePB resp;
    ASSERT_NO_FATALS(DoListTablegroups(req, &resp));

    bool tablegroup_found = false;
    for (auto& tg : *resp.mutable_tablegroups()) {
      if (tg.id().compare(kTablegroupId) == 0) {
        tablegroup_found = true;
      }
    }
    ASSERT_TRUE(tablegroup_found);
  }

  // Now ensure that a table can be created in the tablegroup.
  ASSERT_OK(CreateTablegroupTable(ns_id, kTableId, kTableName, kTablegroupId, kTableSchema));

  // Delete the table to clean up tablegroup.
  ASSERT_OK(DeleteTableById(kTableId));

  // Delete the tablegroup.
  ASSERT_OK(DeleteTablegroup(kTablegroupId));
}

// Regression test for KUDU-253/KUDU-592: crash if the schema passed to CreateTable
// is invalid.
TEST_F(MasterTest, TestCreateTableInvalidSchema) {
  CreateTableRequestPB req;
  CreateTableResponsePB resp;

  req.set_name("table");
  req.mutable_namespace_()->set_name(default_namespace_name);
  for (int i = 0; i < 2; i++) {
    ColumnSchemaPB* col = req.mutable_schema()->add_columns();
    col->set_name("col");
    QLType::Create(DataType::INT32)->ToQLTypePB(col->mutable_type());
    col->set_is_key(true);
  }

  ASSERT_OK(proxy_ddl_->CreateTable(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(AppStatusPB::INVALID_ARGUMENT, resp.error().status().code());
  ASSERT_EQ("Duplicate column name: col", resp.error().status().message());
}

// Regression test for KUDU-253/KUDU-592: crash if the GetTableLocations RPC call is
// invalid.
TEST_F(MasterTest, TestInvalidGetTableLocations) {
  const TableName kTableName = "test";
  Schema schema({ ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  ASSERT_OK(CreateTable(kTableName, schema));
  {
    GetTableLocationsRequestPB req;
    GetTableLocationsResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    // Set the "start" key greater than the "end" key.
    req.set_partition_key_start("zzzz");
    req.set_partition_key_end("aaaa");
    ASSERT_OK(proxy_client_->GetTableLocations(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(AppStatusPB::INVALID_ARGUMENT, resp.error().status().code());
    ASSERT_EQ("start partition key is greater than the end partition key",
              resp.error().status().message());
  }
}

// Test for DB-6087. Previously, GetTabletLocations was not looking at the tablespace overrides for
// number of replicas. This test follows some change in logic to make sure we are using checking
// the tablespace first before defaulting to cluster config.
TEST_F(MasterTest, GetNumTabletReplicasChecksTablespace) {
  const TableName kTableName = "test";
  Schema schema({ ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  ASSERT_OK(
      proxy_cluster_->GetMasterClusterConfig(config_req, &config_resp, ResetAndGetController()));
  ASSERT_FALSE(config_resp.has_error());
  ASSERT_TRUE(config_resp.has_cluster_config());
  auto cluster_config = config_resp.cluster_config();
  auto replication_info = cluster_config.mutable_replication_info();

  // update replication info
  int kNumClusterLiveReplicas = 5;
  auto* live_replicas = replication_info->mutable_live_replicas();
  live_replicas->set_num_replicas(kNumClusterLiveReplicas);
  UpdateMasterClusterConfig(&cluster_config);

  // set tablespace replication info to be different than cluster config
  int kNumTableLiveReplicas = 2;
  CreateTableRequestPB req;
  live_replicas = req.mutable_replication_info()->mutable_live_replicas();
  live_replicas->set_num_replicas(kNumTableLiveReplicas);
  ASSERT_OK(DoCreateTable(kTableName, schema, &req));

  TableId table_id;
  {
    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
    for (auto& t : *tables.mutable_tables()) {
      if (t.name().compare(kTableName) == 0) {
        table_id = t.id();
      }
    }
  }

  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  auto table = mini_master_->catalog_manager_impl().GetTableInfo(table_id);
  int num_live_replicas = 0, num_read_replicas = 0;
  mini_master_->catalog_manager_impl().GetExpectedNumberOfReplicasForTable(
      table, &num_live_replicas, &num_read_replicas);
  ASSERT_EQ(num_live_replicas + num_read_replicas, kNumTableLiveReplicas);

  for (auto& tablet : table->GetTablets()) {
    num_live_replicas = 0, num_read_replicas = 0;
    ASSERT_OK(mini_master_->catalog_manager_impl().GetExpectedNumberOfReplicasForTablet(
        tablet->id(), &num_live_replicas, &num_read_replicas));
    ASSERT_EQ(num_live_replicas + num_read_replicas, kNumTableLiveReplicas);
  }
}

// Test for DB-6087. This case ensures the tablespace RF defaults to cluster RF when there are no
// table level overrides.
TEST_F(MasterTest, GetNumTabletReplicasDefaultsToClusterConfig) {
  const TableName kTableName = "test";
  Schema schema({ ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  ASSERT_OK(
      proxy_cluster_->GetMasterClusterConfig(config_req, &config_resp, ResetAndGetController()));
  ASSERT_FALSE(config_resp.has_error());
  ASSERT_TRUE(config_resp.has_cluster_config());
  auto cluster_config = config_resp.cluster_config();
  auto replication_info = cluster_config.mutable_replication_info();

  // update replication info
  int kNumClusterLiveReplicas = 5;
  auto* live_replicas = replication_info->mutable_live_replicas();
  live_replicas->set_num_replicas(kNumClusterLiveReplicas);
  UpdateMasterClusterConfig(&cluster_config);

  CreateTableRequestPB req;
  ASSERT_OK(DoCreateTable(kTableName, schema, &req));

  TableId table_id;
  {
    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
    for (auto& t : *tables.mutable_tables()) {
      if (t.name().compare(kTableName) == 0) {
        table_id = t.id();
      }
    }
  }

  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  auto table = mini_master_->catalog_manager_impl().GetTableInfo(table_id);
  int num_live_replicas = 0, num_read_replicas = 0;
  mini_master_->catalog_manager_impl().GetExpectedNumberOfReplicasForTable(
      table, &num_live_replicas, &num_read_replicas);
  ASSERT_EQ(num_live_replicas + num_read_replicas, kNumClusterLiveReplicas);

  for (auto& tablet : table->GetTablets()) {
    num_live_replicas = 0, num_read_replicas = 0;
    ASSERT_OK(mini_master_->catalog_manager_impl().GetExpectedNumberOfReplicasForTablet(
        tablet->id(), &num_live_replicas, &num_read_replicas));
    ASSERT_EQ(num_live_replicas + num_read_replicas, kNumClusterLiveReplicas);
  }
}

TEST_F(MasterTest, TestInvalidPlacementInfo) {
  const TableName kTableName = "test";
  Schema schema({ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST)});
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  ASSERT_OK(proxy_cluster_->GetMasterClusterConfig(
      config_req, &config_resp, ResetAndGetController()));
  ASSERT_FALSE(config_resp.has_error());
  ASSERT_TRUE(config_resp.has_cluster_config());
  auto cluster_config = config_resp.cluster_config();

  CreateTableRequestPB req;

  // Fail due to not cloud_info.
  auto* live_replicas = cluster_config.mutable_replication_info()->mutable_live_replicas();
  live_replicas->set_num_replicas(5);
  auto* pb = live_replicas->add_placement_blocks();
  UpdateMasterClusterConfig(&cluster_config);
  Status s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());

  // Fail due to min_num_replicas being more than num_replicas.
  auto* cloud_info = pb->mutable_cloud_info();
  pb->set_min_num_replicas(live_replicas->num_replicas() + 1);
  UpdateMasterClusterConfig(&cluster_config);
  s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());

  // Fail because there are no TServers matching the given placement policy.
  pb->set_min_num_replicas(live_replicas->num_replicas());
  cloud_info->set_placement_cloud("fail");
  UpdateMasterClusterConfig(&cluster_config);
  s = DoCreateTable(kTableName, schema, &req);
  ASSERT_TRUE(s.IsInvalidArgument());
}

TEST_F(MasterTest, TestNamespaces) {
  ListNamespacesResponsePB namespaces;

  // Check default namespace.
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Try to create the existing namespace twice.
  {
    CreateNamespaceResponsePB resp;
    const Status s = CreateNamespace(other_ns_name, &resp);
    ASSERT_TRUE(s.IsAlreadyPresent()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        Substitute("Keyspace '$0' already exists", other_ns_name));
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Delete the namespace (by ID).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_id(other_ns_id);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Re-create the namespace once again.
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Try to create the 'default' namespace.
  {
    CreateNamespaceResponsePB resp;
    const Status s = CreateNamespace(default_namespace_name, &resp);
    ASSERT_TRUE(s.IsAlreadyPresent()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(),
        Substitute("Keyspace '$0' already exists", default_namespace_name));
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Try to delete a non-existing namespace - by NAME.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_name("nonexistingns");
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(), "YCQL keyspace name not found");
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestNamespaceSeparation) {
  ListNamespacesResponsePB namespaces;

  // Check default namespace.
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Create a new namespace for each of YCQL, YSQL and YEDIS database types.
  CreateNamespaceResponsePB resp;
  ASSERT_OK(CreateNamespace("test_cql", YQLDatabase::YQL_DATABASE_CQL, &resp));
  const NamespaceId cql_ns_id = resp.id();
  ASSERT_OK(CreateNamespace("test_pgsql", YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  const NamespaceId pgsql_ns_id = resp.id();
  ASSERT_OK(CreateNamespace("test_redis", YQLDatabase::YQL_DATABASE_REDIS, &resp));
  const NamespaceId redis_ns_id = resp.id();

  // List all namespaces and by each database type.
  ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
  ASSERT_EQ(4 + kNumSystemNamespaces, namespaces.namespaces_size());
  CheckNamespaces(
      {
        EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
        std::make_tuple("test_cql", cql_ns_id),
        std::make_tuple("test_pgsql", pgsql_ns_id),
        std::make_tuple("test_redis", redis_ns_id),
      }, namespaces);

  ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_CQL, &namespaces));
  ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
  CheckNamespaces(
      {
        // Defalt and system namespaces are created in YCQL.
        EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
        std::make_tuple("test_cql", cql_ns_id),
      }, namespaces);

  ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_PGSQL, &namespaces));
  ASSERT_EQ(1, namespaces.namespaces_size());
  CheckNamespaces(
      {
        std::make_tuple("test_pgsql", pgsql_ns_id),
      }, namespaces);

  ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_REDIS, &namespaces));
  ASSERT_EQ(1, namespaces.namespaces_size());
  CheckNamespaces(
      {
        std::make_tuple("test_redis", redis_ns_id),
      }, namespaces);
}

TEST_F(MasterTest, TestDeletingNonEmptyNamespace) {
  ListNamespacesResponsePB namespaces;

  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  const NamespaceName other_ns_pgsql_name = "testns_pgsql";
  NamespaceId other_ns_pgsql_id;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_pgsql_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
    other_ns_pgsql_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(3 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
            std::make_tuple(other_ns_pgsql_name, other_ns_pgsql_id)
        }, namespaces);
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(YQLDatabase::YQL_DATABASE_PGSQL, &namespaces));
    ASSERT_EQ(1, namespaces.namespaces_size());
    CheckNamespaces(
        {
            std::make_tuple(other_ns_pgsql_name, other_ns_pgsql_id)
        }, namespaces);
  }

  // Create a table.
  const TableName kTableName = "testtb";
  const TableName kTableNamePgsql = "testtb_pgsql";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });

  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));
  ASSERT_OK(CreatePgsqlTable(other_ns_pgsql_id, kTableNamePgsql + "_1", kTableSchema));
  ASSERT_OK(CreatePgsqlTable(other_ns_pgsql_id, kTableNamePgsql + "_2", kTableSchema));

  {
    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(3 + kNumSystemTables, tables.tables_size());
    CheckTables(
        {
            std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
            std::make_tuple(kTableNamePgsql + "_1", other_ns_pgsql_name, other_ns_pgsql_id,
                USER_TABLE_RELATION),
            std::make_tuple(kTableNamePgsql + "_2", other_ns_pgsql_name, other_ns_pgsql_id,
                USER_TABLE_RELATION),
            EXPECTED_SYSTEM_TABLES
        }, tables);
  }

  // You should be able to successfully delete a non-empty PGSQL Database - by ID only
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    req.mutable_namespace_()->set_id(other_ns_pgsql_id);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());

    // Must wait for IsDeleteNamespaceDone with PGSQL Namespaces.
    IsDeleteNamespaceDoneRequestPB del_req;
    del_req.mutable_namespace_()->set_id(other_ns_pgsql_id);
    del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    ASSERT_OK(DeleteNamespaceWait(del_req));
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id)
        }, namespaces);
  }
  {
    // verify that the table for that database also went away
    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
    CheckTables(
        {
            std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
            EXPECTED_SYSTEM_TABLES
        }, tables);
  }

  // Try to delete the non-empty namespace - by NAME.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Cannot delete keyspace which has table: " + kTableName);
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Try to delete the non-empty namespace - by ID.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;

    req.mutable_namespace_()->set_id(other_ns_id);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::INVALID_ARGUMENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        "Cannot delete keyspace which has table: " + kTableName);
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Delete the table.
  ASSERT_OK(DeleteTable(other_ns_name, kTableName));

  // List tables, should show only system table.
  {
    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(kNumSystemTables, tables.tables_size());
    CheckTables(
        {
            EXPECTED_SYSTEM_TABLES
        }, tables);
  }

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestTablesWithNamespace) {
  const TableName kTableName = "testtb";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  ListTablesResponsePB tables;

  // Create a table with default namespace.
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table.
  ASSERT_OK(DeleteTable(kTableName));

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Create a table with the default namespace.
  ASSERT_OK(CreateTable(default_namespace_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table.
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName));

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Try to create a table with an unknown namespace.
  {
    Status s = CreateTable("nonexistingns", kTableName, kTableSchema);
    ASSERT_TRUE(s.IsNotFound()) << s.ToString();
    ASSERT_STR_CONTAINS(s.ToString(), "YCQL keyspace name not found");
  }

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  const NamespaceName other_ns_name = "testns";

  // Create a new namespace.
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Create a table with the defined new namespace.
  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Alter table: try to change the table namespace name into an invalid one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name("nonexistingns");
    ASSERT_OK(proxy_ddl_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(), "YCQL keyspace name not found");
  }
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Alter table: try to change the table namespace id into an invalid one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_id("deadbeafdeadbeafdeadbeafdeadbeaf");
    ASSERT_OK(proxy_ddl_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::NAMESPACE_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(), "Keyspace identifier not found");
  }
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Alter table: change namespace name into the default one.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name(default_namespace_name);
    ASSERT_OK(proxy_ddl_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table.
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName));

  // List tables, should show 1 table.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestNamespaceCreateStates) {
  NamespaceName test_name = "test_pgsql";

  // Don't allow the BG thread to process namespaces.
  SetAtomicFlag(true, &FLAGS_TEST_hang_on_namespace_transition);

  // Create a new PGSQL namespace.
  CreateNamespaceResponsePB resp;
  NamespaceId nsid;
  ASSERT_OK(CreateNamespaceAsync(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  nsid = resp.id();

  // ListNamespaces should not yet show the Namespace, because it's in the PREPARING state.
  ListNamespacesResponsePB namespaces;
  ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
  ASSERT_FALSE(FindNamespace(std::make_tuple(test_name, nsid), namespaces));

  // Test that Basic Access is not allowed to a Namespace while INITIALIZING.
  // 1. CANNOT Create a Table on the namespace.
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  ASSERT_NOK(CreatePgsqlTable(nsid, "test_table", kTableSchema));
  // 2. CANNOT Alter the namespace.
  {
    AlterNamespaceResponsePB alter_resp;
    ASSERT_NOK(AlterNamespace(test_name, nsid, YQLDatabase::YQL_DATABASE_PGSQL,
                              "new_" + test_name, &alter_resp));
    ASSERT_TRUE(alter_resp.has_error());
    ASSERT_EQ(alter_resp.error().code(), MasterErrorPB::IN_TRANSITION_CAN_RETRY);
  }
  // 3. CANNOT Delete the namespace.
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(test_name);
    req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::IN_TRANSITION_CAN_RETRY);
  }

  // Finish Namespace create.
  SetAtomicFlag(false, &FLAGS_TEST_hang_on_namespace_transition);
  ASSERT_OK(CreateNamespaceWait(nsid, YQLDatabase::YQL_DATABASE_PGSQL));

  // Verify that Basic Access to a Namespace is now available.
  // 1. Create a Table within the Schema.
  ASSERT_OK(CreatePgsqlTable(nsid, "test_table", kTableSchema));
  // 2. Alter the namespace.
  {
    AlterNamespaceResponsePB alter_resp;
    ASSERT_OK(AlterNamespace(test_name, nsid, YQLDatabase::YQL_DATABASE_PGSQL,
                             "new_" + test_name, &alter_resp) );
    ASSERT_FALSE(alter_resp.has_error());
  }
  // 3. Delete the namespace.
  {
    SetAtomicFlag(true, &FLAGS_TEST_hang_on_namespace_transition);

    DeleteNamespaceRequestPB del_req;
    DeleteNamespaceResponsePB del_resp;
    del_req.mutable_namespace_()->set_name("new_" + test_name);
    del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(del_req, &del_resp, ResetAndGetController()));
    ASSERT_FALSE(del_resp.has_error());

    // ListNamespaces should not show the Namespace, because it's in the DELETING state.
    ListNamespacesResponsePB namespaces;
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_FALSE(FindNamespace(std::make_tuple("new_" + test_name, nsid), namespaces));

    // Resume finishing both [1] the delete and [2] the create.
    SetAtomicFlag(false, &FLAGS_TEST_hang_on_namespace_transition);

    // Verify the old namespace finishes deletion.
    IsDeleteNamespaceDoneRequestPB is_del_req;
    is_del_req.mutable_namespace_()->set_id(nsid);
    is_del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    ASSERT_OK(DeleteNamespaceWait(is_del_req));

    // We should be able to create a namespace with the same NAME at this time.
    ASSERT_OK(CreateNamespaceAsync("new_" + test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
    ASSERT_OK(CreateNamespaceWait(resp.id(), YQLDatabase::YQL_DATABASE_PGSQL));
  }
}

TEST_F(MasterTest, TestNamespaceCreateFailure) {
  NamespaceName test_name = "test_pgsql";

  // Don't allow the BG thread to process namespaces.
  SetAtomicFlag(true, &FLAGS_TEST_hang_on_namespace_transition);

  // Create a new PGSQL namespace.
  CreateNamespaceResponsePB resp;
  NamespaceId nsid;
  ASSERT_OK(CreateNamespaceAsync(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  nsid = resp.id();

  {
    // Public ListNamespaces should not show the Namespace, because it's in the PREPARING state.
    ListNamespacesResponsePB namespace_pb;
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespace_pb));
    ASSERT_FALSE(FindNamespace(std::make_tuple(test_name, nsid), namespace_pb));

    // Internal search of CatalogManager should reveal it's state (debug UI uses this function).
    std::vector<scoped_refptr<NamespaceInfo>> namespace_internal;
    mini_master_->catalog_manager().GetAllNamespaces(&namespace_internal, false);
    auto pos = std::find_if(namespace_internal.begin(), namespace_internal.end(),
                 [&nsid](const scoped_refptr<NamespaceInfo>& ns) {
                   return ns && ns->id() == nsid;
                 });
    ASSERT_NE(pos, namespace_internal.end());
    ASSERT_EQ((*pos)->state(), SysNamespaceEntryPB::PREPARING);
    // Namespace should be in namespace name map to prevent creation races using the same name.
    ASSERT_OK(FindNamespaceByName(YQLDatabase::YQL_DATABASE_PGSQL, test_name));
  }

  // Restart the master (Shutdown kills Namespace BG Thread).
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  {
    // ListNamespaces should not show the Namespace on restart because it didn't finish.
    ListNamespacesResponsePB namespaces;
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_FALSE(FindNamespace(std::make_tuple(test_name, nsid), namespaces));

    // Internal search of CatalogManager should reveal it's DELETING to cleanup any partial apply.
    std::vector<scoped_refptr<NamespaceInfo>> namespace_internal;
    mini_master_->catalog_manager().GetAllNamespaces(&namespace_internal, false);
    auto pos = std::find_if(namespace_internal.begin(), namespace_internal.end(),
        [&nsid](const scoped_refptr<NamespaceInfo>& ns) {
          return ns && ns->id() == nsid;
        });
    ASSERT_NE(pos, namespace_internal.end());
    ASSERT_EQ((*pos)->state(), SysNamespaceEntryPB::DELETING);
    // Namespace should not be in the by name map.
    ASSERT_NOK(FindNamespaceByName(YQLDatabase::YQL_DATABASE_PGSQL, test_name));
  }

  // Resume BG thread work and verify that the Namespace is eventually DELETED internally.
  SetAtomicFlag(false, &FLAGS_TEST_hang_on_namespace_transition);

  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    std::vector<scoped_refptr<NamespaceInfo>> namespace_internal;
    mini_master_->catalog_manager().GetAllNamespaces(&namespace_internal, false);
    auto pos = std::find_if(namespace_internal.begin(), namespace_internal.end(),
        [&nsid](const scoped_refptr<NamespaceInfo>& ns) {
          return ns && ns->id() == nsid;
        });
    return pos != namespace_internal.end() && (*pos)->state() == SysNamespaceEntryPB::DELETED;
  }, MonoDelta::FromSeconds(10), "Verify Namespace was DELETED"));

  // Restart the master #2, this round should completely remove the Namespace from memory.
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    std::vector<scoped_refptr<NamespaceInfo>> namespace_internal;
    mini_master_->catalog_manager().GetAllNamespaces(&namespace_internal, false);
    auto pos = std::find_if(namespace_internal.begin(), namespace_internal.end(),
        [&nsid](const scoped_refptr<NamespaceInfo>& ns) {
          return ns && ns->id() == nsid;
        });
    return pos == namespace_internal.end();
  }, MonoDelta::FromSeconds(10), "Verify Namespace was completely removed"));
}

TEST_F(MasterTest, TestMultipleNamespacesWithSameName) {
  NamespaceName test_name = "test_pgsql";
  SetAtomicFlag(true, &FLAGS_TEST_hang_on_namespace_transition);

  // Create a new PGSQL namespace.
  CreateNamespaceResponsePB resp;
  ASSERT_OK(CreateNamespaceAsync(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  NamespaceId failure_nsid = resp.id();

  // Restart the master to fail the creation of the first namespace.
  // The loader should enqueue an async deletion task.
  ASSERT_OK(mini_master_->Restart());
  ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  // Create the namespace again. The async deletion task for the first namespace shouldn't have run
  // yet due to the test flag.
  ASSERT_OK(CreateNamespaceAsync(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));
  NamespaceId success_nsid = resp.id();

  // There should now be two namespaces with the same name in the system, one in the FAILED state
  // and one in the PREPARING state. Allow the async work to run. The first namespace should be
  // cleaned up and the second namespace should be running.
  SetAtomicFlag(false, &FLAGS_TEST_hang_on_namespace_transition);
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    std::vector<scoped_refptr<NamespaceInfo>> namespace_internal;
    mini_master_->catalog_manager().GetAllNamespaces(&namespace_internal, false);
    bool failure_deleted = false;
    bool success_running = false;
    for (const auto& ns : namespace_internal) {
      if (ns->id() == failure_nsid) {
        failure_deleted = ns->state() == SysNamespaceEntryPB::DELETED;
      } else if (ns->id() == success_nsid) {
        success_running = ns->state() == SysNamespaceEntryPB::RUNNING;
      }
    }
    LOG(INFO) << Format(
        "First namespace is deleted: $0, second namespace is running: $1", failure_deleted,
        success_running);
    return failure_deleted && success_running;
  }, MonoDelta::FromSeconds(15), "Timed out waiting for namespaces to enter expected states."));

  // The by-name map should point to the successfully created namespace.
  auto ns_by_name = ASSERT_RESULT(FindNamespaceByName(YQLDatabase::YQL_DATABASE_PGSQL, test_name));
  ASSERT_EQ(ns_by_name->id(), success_nsid);
}

class LoopedMasterTest : public MasterTest, public testing::WithParamInterface<int> {};
INSTANTIATE_TEST_CASE_P(Loops, LoopedMasterTest, ::testing::Values(10));

TEST_P(LoopedMasterTest, TestNamespaceCreateSysCatalogFailure) {
  NamespaceName test_name = "test_pgsql"; // Reuse name to find errors with delete cleanup.
  CreateNamespaceResponsePB resp;
  DeleteNamespaceRequestPB del_req;
  del_req.mutable_namespace_()->set_name(test_name);
  del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  IsDeleteNamespaceDoneRequestPB is_del_req;
  is_del_req.mutable_namespace_()->set_name(test_name);
  is_del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);

  int failures = 0, created = 0, iter = 0;
  int loops = GetParam();
  LOG(INFO) << "Loops = " << loops;

  std::unordered_set<NamespaceId> previously_created_namespaces;
  // Loop this to cover a spread of random failure situations.
  while (failures < loops) {
    // Inject Frequent failures into sys catalog commit.
    // The below code should eventually succeed but require a lot of restarts.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sys_catalog_write_rejection_percentage) = 50;

    LOG(INFO) << "Iteration " << ++iter;

    // There are 4 possibilities for the CreateNamespace call below.

    // 1. CreateNamespace RPC returns not-OK status.
    //    We expect no in-memory state to be persisted.
    // 2. CreateNamespace RPC returns an OK status. Async namespace creation work fails.
    //    We expect the namespace to be present in memory but not in the by name map.
    //    Its status should be FAILED.
    // 3. CreateNamespace RPC returns an OK status. Async namespace creation work succeeds.
    //    We expect the namespace to be present in the maps with the state RUNNING.
    // 4. CreateNamespace RPC returns an OK status. Test times out waiting for async work.
    //    The test doesn't explicitly handle this case and would fail.
    //    There may be a bug in production code related to this case as well.
    //    If the async namespace creation does eventually succeed in CM after pgsql
    //    times out waiting for it, the namespace might not be visible to pgsql
    //    but it will exist in yb-master, preventing the creation of a database with that
    //    name. In this case users will need to delete the database with yb-admin.
    Status s = CreateNamespace(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp);
    if (!s.ok()) {
      LOG(INFO) << "CreateNamespace with injected failures: " << s;
      ++failures;
    }

    // Turn off random failures.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sys_catalog_write_rejection_percentage) = 0;

    // Internal search of CatalogManager should reveal whether it was partially created.
    std::vector<scoped_refptr<NamespaceInfo>> namespace_internal;
    mini_master_->catalog_manager().GetAllNamespaces(&namespace_internal, false);
    auto new_ns_it = std::find_if(
        namespace_internal.begin(), namespace_internal.end(),
        [&test_name, &previously_created_namespaces](const scoped_refptr<NamespaceInfo>& ns) {
          return ns && ns->name() == test_name && !previously_created_namespaces.contains(ns->id());
        });
    scoped_refptr<NamespaceInfo> new_ns;
    if (new_ns_it != namespace_internal.end()) {
      // Namespace created. We're in case 2, 3, or 4.
      new_ns = (*new_ns_it).get();
      previously_created_namespaces.insert(new_ns->id());
      ASSERT_TRUE(
          new_ns->state() == SysNamespaceEntryPB::RUNNING ||
          new_ns->state() == SysNamespaceEntryPB::PREPARING ||
          new_ns->state() == SysNamespaceEntryPB::FAILED)
          << Format(
                 "Expected RUNNING, PREPARING, or FAILED, got: $0",
                 SysNamespaceEntryPB::State_Name(new_ns->state()));
    }

    // A pgsql connection will only commit state for this new namespace to the pg catalog if
    // CreateNamespace and WaitForCreateNamespaceDone return OK statuses. Therefore if
    // there is a failure in the complete create namespace flow any subsequent DROP DATABASE
    // statement will fail because there is no metadata for the database in the pg catalogs.

    // Because we cannot expect clients to issue a DROP DATABASE in this failure case we must
    // ensure we can handle a subsequent CREATE DATABASE with the same name.
    if (s.ok()) {
      // Case 3.
      ++created;
      DeleteNamespaceResponsePB del_resp;
      ASSERT_OK(proxy_ddl_->DeleteNamespace(del_req, &del_resp, ResetAndGetController()));
      if (del_resp.has_error()) {
        LOG(INFO) << del_resp.error().DebugString();
      }
      ASSERT_FALSE(del_resp.has_error());
      ASSERT_OK(DeleteNamespaceWait(is_del_req));
    } else if (s.IsTimedOut()) {
      // Case 4.
      // The namespace state should be PREPARING, but the CM is responsible for transitioning it to
      // another state so asserting state is PREPARING here introduces a race. For now just fail the
      // test.
      FAIL() << "Timed out waiting for async namespace creation";
    } else {
      // Case 1 or 2.
      LOG(INFO) << "Namespace should not be in by name map, state: "
                << (new_ns ? SysNamespaceEntryPB::State_Name(new_ns->state()) : "n/a");
      ASSERT_NOK(FindNamespaceByName(YQLDatabase::YQL_DATABASE_PGSQL, "default_namespace"));
    }
  }
  ASSERT_EQ(failures, loops);
  LOG(INFO) << "created = " << created;
}

TEST_P(LoopedMasterTest, TestNamespaceDeleteSysCatalogFailure) {
  NamespaceName test_name = "test_pgsql";
  CreateNamespaceResponsePB resp;
  DeleteNamespaceRequestPB del_req;
  DeleteNamespaceResponsePB del_resp;
  del_req.mutable_namespace_()->set_name(test_name);
  del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  IsDeleteNamespaceDoneRequestPB is_del_req;
  is_del_req.mutable_namespace_()->set_name(test_name);
  is_del_req.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  int failures = 0, iter = 0;
  int loops = GetParam();
  LOG(INFO) << "Loops = " << loops;

  // Loop this to cover a spread of random failure situations.
  while (failures < loops) {
    // CreateNamespace to setup test
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(test_name, YQLDatabase::YQL_DATABASE_PGSQL, &resp));

    // The below code should eventually succeed but require a lot of restarts.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sys_catalog_write_rejection_percentage) = 50;

    // DeleteNamespace : Inject IO Errors.
    LOG(INFO) << "Iteration " << ++iter;
    bool delete_failed = false;

    ASSERT_OK(proxy_ddl_->DeleteNamespace(del_req, &del_resp, ResetAndGetController()));

    delete_failed = del_resp.has_error();
    if (del_resp.has_error()) {
      LOG(INFO) << "Expected failure: " << del_resp.error().DebugString();
    }

    if (!del_resp.has_error()) {
      Status s = DeleteNamespaceWait(is_del_req);
      ASSERT_FALSE(s.IsTimedOut()) << "Unexpected timeout: " << s;
      WARN_NOT_OK(s, "Expected failure");
      delete_failed = !s.ok();
    }

    // Turn off random failures.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sys_catalog_write_rejection_percentage) = 0;

    if (delete_failed) {
      ++failures;
      LOG(INFO) << "Next Delete should succeed";

      // If the namespace delete fails, Ensure that we can restart the delete and it succeeds.
      ASSERT_OK(proxy_ddl_->DeleteNamespace(del_req, &del_resp, ResetAndGetController()));
      ASSERT_FALSE(del_resp.has_error());
      ASSERT_OK(DeleteNamespaceWait(is_del_req));
    }
  }
  ASSERT_EQ(failures, loops);
}

TEST_F(MasterTest, TestFullTableName) {
  const TableName kTableName = "testtb";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  ListTablesResponsePB tables;

  // Create a table with the default namespace.
  ASSERT_OK(CreateTable(default_namespace_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  const NamespaceName other_ns_name = "testns";

  // Create a new namespace.
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id)
        }, namespaces);
  }

  // Create a table with the defined new namespace.
  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(2 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Test ListTables() for one particular namespace.
  // There are 2 tables now: 'default_namespace::testtb' and 'testns::testtb'.
  ASSERT_NO_FATALS(DoListAllTables(&tables, default_namespace_name));
  ASSERT_EQ(1, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
      }, tables);

  ASSERT_NO_FATALS(DoListAllTables(&tables, other_ns_name));
  ASSERT_EQ(1, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION)
      }, tables);

  // Try to alter table: change namespace name into the default one.
  // Try to change 'testns::testtb' into 'default_namespace::testtb', but the target table exists,
  // so it must fail.
  {
    AlterTableRequestPB req;
    AlterTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    req.mutable_new_namespace()->set_name(default_namespace_name);
    ASSERT_OK(proxy_ddl_->AlterTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::OBJECT_ALREADY_PRESENT);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::ALREADY_PRESENT);
    ASSERT_STR_CONTAINS(resp.error().status().ShortDebugString(),
        " already exists");
  }
  // Check that nothing's changed (still have 3 tables).
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(2 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the table in the namespace 'testns'.
  ASSERT_OK(DeleteTable(other_ns_name, kTableName));

  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, default_namespace_name, default_namespace_id,
              USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Try to delete the table from wrong namespace (table 'default_namespace::testtbl').
  {
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), MasterErrorPB::OBJECT_NOT_FOUND);
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_FOUND);
    auto status = StatusFromPB(resp.error().status());
    ASSERT_EQ(MasterError(status), MasterErrorPB::OBJECT_NOT_FOUND);
  }

  // Delete the table.
  ASSERT_OK(DeleteTable(default_namespace_name, kTableName));

  // List tables, should show only system tables.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestGetTableSchema) {
  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Create a table with the defined new namespace.
  const TableName kTableName = "testtb";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST) });
  ASSERT_OK(CreateTable(other_ns_name, kTableName, kTableSchema));

  ListTablesResponsePB tables;
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          std::make_tuple(kTableName, other_ns_name, other_ns_id, USER_TABLE_RELATION),
          EXPECTED_SYSTEM_TABLES
      }, tables);

  TableId table_id;
  for (int i = 0; i < tables.tables_size(); ++i) {
    if (tables.tables(i).name() == kTableName) {
        table_id = tables.tables(i).id();
        break;
    }
  }

  ASSERT_FALSE(table_id.empty()) << "Couldn't get table id for table " << kTableName;

  // Check GetTableSchema().
  {
    GetTableSchemaRequestPB req;
    GetTableSchemaResponsePB resp;
    req.mutable_table()->set_table_name(kTableName);
    req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);

    // Check the request.
    ASSERT_OK(proxy_ddl_->GetTableSchema(req, &resp, ResetAndGetController()));

    // Check the responsed data.
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_TRUE(resp.has_table_type());
    ASSERT_TRUE(resp.has_create_table_done());
    // SchemaPB schema.
    ASSERT_TRUE(resp.has_schema());
    ASSERT_EQ(1, resp.schema().columns_size());
    ASSERT_EQ(Schema::first_column_id(), resp.schema().columns(0).id());
    ASSERT_EQ("key", resp.schema().columns(0).name());
    ASSERT_EQ(INT32, resp.schema().columns(0).type().main());
    ASSERT_TRUE(resp.schema().columns(0).is_key());
    ASSERT_FALSE(resp.schema().columns(0).is_nullable());
    ASSERT_EQ(1, resp.schema().columns(0).sorting_type());
    // PartitionSchemaPB partition_schema.
    ASSERT_TRUE(resp.has_partition_schema());
    ASSERT_EQ(resp.partition_schema().hash_schema(), PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
    // TableIdentifierPB identifier.
    ASSERT_TRUE(resp.has_identifier());
    ASSERT_TRUE(resp.identifier().has_table_name());
    ASSERT_EQ(kTableName, resp.identifier().table_name());
    ASSERT_TRUE(resp.identifier().has_table_id());
    ASSERT_EQ(table_id, resp.identifier().table_id());
    ASSERT_TRUE(resp.identifier().has_namespace_());
    ASSERT_TRUE(resp.identifier().namespace_().has_name());
    ASSERT_EQ(other_ns_name, resp.identifier().namespace_().name());
    ASSERT_TRUE(resp.identifier().namespace_().has_id());
    ASSERT_EQ(other_ns_id, resp.identifier().namespace_().id());
  }

  // Delete the table in the namespace 'testns'.
  ASSERT_OK(DeleteTable(other_ns_name, kTableName));

  // List tables, should show only system tables.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(kNumSystemTables, tables.tables_size());
  CheckTables(
      {
          EXPECTED_SYSTEM_TABLES
      }, tables);

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }
}

TEST_F(MasterTest, TestFailedMasterRestart) {
  TearDown();

  mini_master_.reset(new MiniMaster(Env::Default(), GetTestPath("Master-test"),
                                    AllocateFreePort(), AllocateFreePort(), 0));
  ASSERT_NOK(mini_master_->Start(true));
  // Restart master should succeed.
  ASSERT_OK(mini_master_->Start());
}

TEST_F(MasterTest, TestNetworkErrorOnFirstRun) {
  TearDown();
  mini_master_.reset(new MiniMaster(Env::Default(), GetTestPath("Master-test"),
                                    AllocateFreePort(), AllocateFreePort(), 0));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_port_conflict_error) = true;
  ASSERT_NOK(mini_master_->Start());
  // Instance file should be properly initialized, but consensus metadata is not initialized.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_port_conflict_error) = false;
  // Restarting master should succeed.
  ASSERT_OK(mini_master_->Start());
}

TEST_F(MasterTest, TestMasterAddressInBroadcastAddress) {
  // Test the scenario where master_address exists in broadcast_addresses
  // but not in rpc_bind_addresses.
  std::vector<std::string> master_addresses = {"127.0.0.51"};
  std::vector<std::string> rpc_bind_addresses = {"127.0.0.52"};
  std::vector<std::string> broadcast_addresses = {"127.0.0.51"};

  TearDown();
  mini_master_.reset(new MiniMaster(Env::Default(), GetTestPath("Master-test"),
                                    AllocateFreePort(), AllocateFreePort(), 0));
  mini_master_->SetCustomAddresses(
      master_addresses, rpc_bind_addresses, broadcast_addresses);
  ASSERT_OK(mini_master_->Start());
}

TEST_F(MasterTest, TestMasterAddressNotInRpcAndBroadcastAddress) {
  // Test the scenario where master_address does not exist in either
  // broadcast_addresses or rpc_bind_addresses.
  std::vector<std::string> master_addresses = {"127.0.0.51"};
  std::vector<std::string> rpc_bind_addresses = {"127.0.0.52"};
  std::vector<std::string> broadcast_addresses = {"127.0.0.53"};

  TearDown();
  mini_master_.reset(new MiniMaster(Env::Default(), GetTestPath("Master-test"),
                                    AllocateFreePort(), AllocateFreePort(), 0));
  mini_master_->SetCustomAddresses(
      master_addresses, rpc_bind_addresses, broadcast_addresses);
  ASSERT_NOK(mini_master_->Start());
}

namespace {

void GetTableSchema(const char* table_name,
                    const char* namespace_name,
                    const Schema* kSchema,
                    const MasterDdlProxy& proxy,
                    CountDownLatch* started,
                    AtomicBool* done) {
  GetTableSchemaRequestPB req;
  GetTableSchemaResponsePB resp;
  req.mutable_table()->set_table_name(table_name);
  req.mutable_table()->mutable_namespace_()->set_name(namespace_name);

  started->CountDown();
  while (!done->Load()) {
    RpcController controller;

    CHECK_OK(proxy.GetTableSchema(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());

    // There are two possible outcomes:
    //
    // 1. GetTableSchema() happened before CreateTable(): we expect to see a
    //    TABLE_NOT_FOUND error.
    // 2. GetTableSchema() happened after CreateTable(): we expect to see the
    //    full table schema.
    //
    // Any other outcome is an error.
    if (resp.has_error()) {
      CHECK_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
    } else {
      Schema receivedSchema;
      CHECK_OK(SchemaFromPB(resp.schema(), &receivedSchema));
      CHECK(kSchema->Equals(receivedSchema)) <<
          strings::Substitute("$0 not equal to $1",
                              kSchema->ToString(), receivedSchema.ToString());
    }
  }
}

} // namespace

// The catalog manager had a bug wherein GetTableSchema() interleaved with
// CreateTable() could expose intermediate uncommitted state to clients. This
// test ensures that bug does not regress.
TEST_F(MasterTest, TestGetTableSchemaIsAtomicWithCreateTable) {
  const char *kTableName = "testtb";
  const Schema kTableSchema({
      ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
      ColumnSchema("v1", DataType::UINT64),
      ColumnSchema("v2", DataType::STRING) });

  CountDownLatch started(1);
  AtomicBool done(false);

  // Kick off a thread that calls GetTableSchema() in a loop.
  scoped_refptr<Thread> t;
  ASSERT_OK(Thread::Create("test", "test",
                           &GetTableSchema, kTableName, default_namespace_name.c_str(),
                           &kTableSchema, std::cref(*proxy_ddl_), &started, &done, &t));

  // Only create the table after the thread has started.
  started.Wait();
  ASSERT_OK(CreateTable(kTableName, kTableSchema));

  done.Store(true);
  t->Join();
}

class NamespaceTest : public MasterTest, public testing::WithParamInterface<YQLDatabase> {};

TEST_P(NamespaceTest, RenameNamespace) {
  ListNamespacesResponsePB namespaces;

  // Check default namespace.
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES
        }, namespaces);
  }

  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, GetParam() /* database_type */, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_name, other_ns_id),
        }, namespaces);
  }

  // Rename the namespace
  const NamespaceName other_ns_new_name = "testns_newname";
  {
    AlterNamespaceResponsePB resp;
    ASSERT_OK(AlterNamespace(other_ns_name,
                             other_ns_id,
                             boost::none /* database_type */,
                             other_ns_new_name,
                             &resp));
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Including system namespace.
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespaces(
        {
            EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES,
            std::make_tuple(other_ns_new_name, other_ns_id),
        }, namespaces);
  }
}

INSTANTIATE_TEST_CASE_P(
    DatabaseType, NamespaceTest,
    ::testing::Values(YQLDatabase::YQL_DATABASE_CQL, YQLDatabase::YQL_DATABASE_PGSQL));

Result<scoped_refptr<NamespaceInfo>> MasterTest::FindNamespaceByName(
    YQLDatabase db_type, const std::string& name) {
  NamespaceIdentifierPB ns_idpb;
  ns_idpb.set_name(name);
  ns_idpb.set_database_type(db_type);
  return mini_master_->catalog_manager().FindNamespace(ns_idpb);
}

class MasterStartUpTest : public YBTest {
 public:
  std::unique_ptr<MiniMaster> CreateMiniMaster(std::string fs_root) {
    return std::make_unique<MiniMaster>(
        Env::Default(), std::move(fs_root), AllocateFreePort(), AllocateFreePort(),
        /* index */ 0);
  }
};

TEST_F(MasterStartUpTest, JoinExistingClusterWithoutMasterAddresses) {
  // Confirm the master enters shell mode with:
  //     master_addresses.empty() == true
  //     master_join_existing_universe == true
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_join_existing_universe) = true;
  auto fs_root = GetTestPath("Master");
  auto mini_master = CreateMiniMaster(fs_root);
  mini_master->set_pass_master_addresses(false);
  ASSERT_OK(mini_master->Start());
  auto cleanup = ScopeExit([&mini_master] { mini_master->Shutdown(); });
  ASSERT_TRUE(mini_master->master()->IsShellMode());
}

TEST_F(MasterStartUpTest, JoinExistingClusterWithMasterAddresses) {
  // Confirm the master enters shell mode with:
  //     master_addresses.empty() == false
  //     master_join_existing_universe == true
  auto fs_root = GetTestPath("Master1");
  auto mini_master = CreateMiniMaster(fs_root);
  ASSERT_OK(mini_master->Start());
  auto cleanup = ScopeExit([&mini_master] { mini_master->Shutdown(); });
  // Confirm the master does not enter shell mode without the flag passed.
  // The MiniMaster class by default sets master_addresses for a new master.
  ASSERT_FALSE(mini_master->master()->IsShellMode());
  // Delete the system catalog tablet and restart the master so the master runs through the
  // initialization logic again.
  mini_master->Shutdown();
  ASSERT_OK(Env::Default()->DeleteRecursively(fs_root));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_join_existing_universe) = true;
  ASSERT_OK(mini_master->Start());
  ASSERT_TRUE(mini_master->master()->IsShellMode());
}

TEST_F(MasterStartUpTest, JoinExistingClusterUnsetWithoutMasterAddresses) {
  // Confirm the master enters shell mode with:
  //     master_addresses.empty() == true
  //     master_join_existing_universe == false
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_join_existing_universe) = false;
  auto fs_root = GetTestPath("Master");
  auto mini_master = CreateMiniMaster(fs_root);
  mini_master->set_pass_master_addresses(false);
  ASSERT_OK(mini_master->Start());
  auto cleanup = ScopeExit([&mini_master] { mini_master->Shutdown(); });
  ASSERT_TRUE(mini_master->master()->IsShellMode());
}

} // namespace master
} // namespace yb
