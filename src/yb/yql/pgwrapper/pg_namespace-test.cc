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

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"

#include "yb/consensus/raft_consensus.h"
#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/flags.h"
#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;
using yb::master::CreateNamespaceRequestPB;
using yb::master::CreateNamespaceResponsePB;
using yb::master::MasterDdlProxy;

DECLARE_bool(TEST_pause_before_upsert_ysql_sys_table);
DECLARE_bool(batch_ysql_system_tables_metadata);
DECLARE_int32(master_leader_rpc_timeout_ms);

namespace yb {

namespace integration_tests {

class PgNamespaceTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    pgwrapper::PgMiniTestBase::SetUp();

    // TODO(asrivastava): should we create these in PgMiniTestBase?
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
    proxy_ddl_ = std::make_unique<MasterDdlProxy>(
        proxy_cache_.get(), ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->bound_rpc_addr());
  }

  virtual size_t NumMasters() override {
    return 3;
  }

  virtual size_t NumTabletServers() override {
    return 3;
  }

  Status CreateNamespace(const CreateNamespaceRequestPB& req,
                         CreateNamespaceResponsePB* resp) {
    RETURN_NOT_OK(CreateNamespaceAsync(req, resp));
    return CreateNamespaceWait(resp->id());
  }

  Status CreateNamespaceAsync(const CreateNamespaceRequestPB& req,
                              CreateNamespaceResponsePB* resp) {
    rpc::RpcController controller;
    controller.set_timeout(10s * kTimeMultiplier);
    RETURN_NOT_OK(proxy_ddl_->CreateNamespace(req, resp, &controller));
    if (resp->has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp->error().status()));
    }
    return Status::OK();
  }

  Status CreateNamespaceWait(const NamespaceId& ns_id) {
    Status status;

    master::IsCreateNamespaceDoneRequestPB is_req;
    is_req.mutable_namespace_()->set_id(ns_id);

    return LoggedWaitFor([&]() -> Result<bool> {
      master::IsCreateNamespaceDoneResponsePB is_resp;
      rpc::RpcController controller;
      controller.set_timeout(10s * kTimeMultiplier);
      status = proxy_ddl_->IsCreateNamespaceDone(is_req, &is_resp, &controller);
      RETURN_NOT_OK_PREPEND(status, "IsCreateNamespaceDone returned unexpected error");
      if (is_resp.has_done() && is_resp.done()) {
        if (is_resp.has_error()) {
          return StatusFromPB(is_resp.error().status());
        }
        return true;
      }
      return false;
    }, 60s, "Wait for create namespace to finish async setup tasks.");
  }

 protected:
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<MasterDdlProxy> proxy_ddl_;
};

TEST_F(PgNamespaceTest, CreateNamespaceFromTemplateLeaderFailover) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_batch_ysql_system_tables_metadata) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_upsert_ysql_sys_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_leader_rpc_timeout_ms) = 10000;
  // Need to set namespace id to a valid oid. Normally this is done in Postgres, but we can't use
  // Postgres code in this test.
  auto ns_id = "00004000000030008000000000000000";

  {
    // Create a new PGSQL namespace.
    CreateNamespaceRequestPB req;
    req.set_name("test");
    req.set_database_type(YQL_DATABASE_PGSQL);
    req.set_namespace_id(ns_id);
    req.set_source_namespace_id(GetPgsqlNamespaceId(kTemplate1Oid));
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespaceAsync(req, &resp));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    // Give ProcessPendingNamespace a chance to hit the test flag.
    SleepFor(2s);
  }

  ssize_t old_leader_idx = cluster_->LeaderMasterIdx();
  ASSERT_NE(old_leader_idx, -1);
  auto new_leader_idx = (old_leader_idx + 1) % NumMasters();
  auto old_leader = cluster_->mini_master(old_leader_idx);
  auto new_leader = cluster_->mini_master(new_leader_idx);

  // Step down the leader.
  ASSERT_OK(StepDown(old_leader->tablet_peer(),
      new_leader->permanent_uuid(),
      ForceStepDown::kFalse));
  // This waits for the new leader to be elected.
  ASSERT_EQ(new_leader_idx, cluster_->LeaderMasterIdx());
  // The new master's catalog loader should enqueue a DeleteYsqlDatabaseAsync task which will mark
  // the namespace as DELETED.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto result = VERIFY_RESULT(new_leader->catalog_manager().FindNamespaceById(ns_id));
    return result->state() == master::SysNamespaceEntryPB_State_DELETED;
  }, 10s * kTimeMultiplier, "Wait for namespace to be marked as DELETED"));

  // Step down the new leader to the original leader.
  ASSERT_OK(StepDown(new_leader->tablet_peer(),
      old_leader->permanent_uuid(),
      ForceStepDown::kFalse));

  // Wait for the sys catalog load to start. It should get blocked on the leader_lock_ that is held
  // by ProcessPendingNamespace.
  SleepFor(2s);
  ASSERT_OK(SET_FLAG(TEST_pause_before_upsert_ysql_sys_table, false));

  // The new master's catalog loader should enqueue a DeleteYsqlDatabaseAsync task which will
  // physically delete the now DELETED namespace and remove it from the namespace map.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto result = old_leader->catalog_manager().FindNamespaceById(ns_id);
    return !result.ok() && result.status().IsNotFound();
  }, 10s * kTimeMultiplier, "Wait for namespace to be removed from namespace map"));
}

}  // namespace integration_tests

}  // namespace yb
