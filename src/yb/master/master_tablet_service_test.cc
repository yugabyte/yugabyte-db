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

#include <functional>

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_auth_catalog_read.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_tablet_service.h"
#include "yb/master/mini_master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tserver.messages.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/memory/arena.h"
#include "yb/util/test_util.h"

DECLARE_bool(ysql_enable_auth_catalog_follower_reads);

METRIC_DECLARE_counter(ysql_auth_catalog_follower_reads);
METRIC_DECLARE_counter(ysql_auth_catalog_leader_reads);

using namespace std::chrono_literals;

namespace yb::master {

TEST(AuthCatalogReadValidationTest, RelationsAndRequestShapes) {
  PgsqlReadRequestPB req;
  for (auto oid : {1260, 2676, 2677, 1262, 2671, 2672, 1261, 2694, 2695,
                   2964, 2965, 8010, 8012, 8073, 8075}) {
    req.set_table_id(GetPgsqlTableId(kTemplate1Oid, oid));
    ASSERT_TRUE(IsYsqlAuthCatalogRead(req)) << oid;
    req.set_table_id(GetPgsqlTableId(kPgPostgresDbOid, oid));
    ASSERT_FALSE(IsYsqlAuthCatalogRead(req)) << oid;
    req.set_table_id(GetPriorVersionYsqlCatalogTableId(kTemplate1Oid, oid));
    ASSERT_FALSE(IsYsqlAuthCatalogRead(req)) << oid;
  }
  req.set_table_id(GetPgsqlTableId(kTemplate1Oid, kPgClassTableOid));
  ASSERT_FALSE(IsYsqlAuthCatalogRead(req));
  req.set_table_id(GetPgsqlTableId(kTemplate1Oid, 1260));
  const std::vector<std::function<void(PgsqlReadRequestPB*)>> invalid_shapes = {
      [](auto* r) { r->set_row_mark_type(RowMarkType::ROW_MARK_EXCLUSIVE); },
      [](auto* r) { r->set_wait_policy(WaitPolicy::WAIT_BLOCK); },
      [](auto* r) { r->mutable_sampling_state(); },
      [](auto* r) { r->add_sample_blocks(); },
      [](auto* r) { r->set_is_for_backfill(true); },
      [](auto* r) { r->set_backfill_spec("backfill"); },
      [](auto* r) { r->mutable_vector_idx_options(); },
      [](auto* r) { r->mutable_get_tablet_key_ranges_request(); },
      [](auto* r) { r->set_ysql_catalog_version(1); },
      [](auto* r) { r->set_ysql_db_catalog_version(1); },
      [](auto* r) { r->set_ysql_db_oid(1); },
      [](auto* r) { r->set_skip_intents_read(true); },
      [](auto* r) { r->set_read_at_in_txn_limit(true); },
      [](auto* r) { r->set_is_aggregate(true); },
      [](auto* r) { r->add_batch_arguments(); },
      [](auto* r) { r->add_targets()->mutable_tscall(); },
      [](auto* r) { r->mutable_where_expr(); },
      [](auto* r) { r->set_client(YQL_CLIENT_CQL); },
      [](auto* r) { r->set_deprecated_max_partition_key("key"); },
  };
  for (const auto& invalidate : invalid_shapes) {
    auto invalid = req;
    invalidate(&invalid);
    ASSERT_FALSE(IsYsqlAuthCatalogRead(invalid)) << invalid.ShortDebugString();
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB lw_req(&arena);
    lw_req.CopyFrom(invalid);
    ASSERT_FALSE(IsYsqlAuthCatalogRead(lw_req));
  }
  for (const auto& [table_oid, index_oid] :
       {std::pair{1260, 2676}, {1262, 2671}, {1261, 2695},
        {2964, 2965}, {8010, 8012}, {8073, 8075}}) {
    req.set_table_id(GetPgsqlTableId(kTemplate1Oid, table_oid));
    auto* index = req.mutable_index_request();
    index->set_table_id(GetPgsqlTableId(kTemplate1Oid, index_oid));
    ASSERT_TRUE(IsYsqlAuthCatalogRead(req));
    index->set_ysql_db_catalog_version(1);
    ASSERT_FALSE(IsYsqlAuthCatalogRead(req));
    index->clear_ysql_db_catalog_version();
    index->mutable_index_request()->set_table_id(req.table_id());
    ASSERT_FALSE(IsYsqlAuthCatalogRead(req));
    index->clear_index_request();
    index->set_table_id(GetPgsqlTableId(kTemplate1Oid, table_oid));
    ASSERT_FALSE(IsYsqlAuthCatalogRead(req));
  }
  req.set_table_id(GetPgsqlTableId(kTemplate1Oid, 1260));
  req.mutable_index_request()->set_table_id(GetPgsqlTableId(kTemplate1Oid, 2671));
  ASSERT_FALSE(IsYsqlAuthCatalogRead(req));
  req.mutable_index_request()->set_table_id(GetPgsqlTableId(kTemplate1Oid, 2676));
  const auto read_time = HybridTime::FromMicros(1000000);
  auto* paging = req.mutable_index_request()->mutable_paging_state();
  ReadHybridTime::SingleTime(read_time).ToPB(paging->mutable_read_time());
  ASSERT_TRUE(YsqlAuthCatalogPagingMatchesReadTime(req, read_time));
  ASSERT_FALSE(YsqlAuthCatalogPagingMatchesReadTime(req, read_time.Incremented()));
  paging->set_next_partition_key("continuation");
  req.set_partition_key("continuation");
  ASSERT_TRUE(IsYsqlAuthCatalogRead(req));
  ThreadSafeArena arena;
  LWPgsqlReadRequestPB lw_req(&arena, req);
  ASSERT_TRUE(IsYsqlAuthCatalogRead(lw_req));
  ASSERT_TRUE(YsqlAuthCatalogPagingMatchesReadTime(lw_req, read_time));
  req.set_partition_key("unrelated");
  ASSERT_FALSE(IsYsqlAuthCatalogRead(req));
}

class MasterTabletServiceTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  virtual void SetUp() override;

  virtual MiniClusterOptions GetMiniClusterOptions();

 protected:
  Result<std::shared_ptr<tablet::AbstractTablet>> LookupReadTablet(
      MiniMaster* mini_master, TabletIdView tablet_id, YBConsistencyLevel consistency_level,
      tablet::TabletPeerPtr peer = nullptr) {
    auto* master = mini_master->master();
    MasterTabletServiceImpl service(master->tablet_server(), master);
    tserver::ReadTabletProvider& provider = service;
    ThreadSafeArena arena;
    tserver::ReadResponseMsg resp(&arena);
    return provider.GetTabletForRead(
        tablet_id, std::move(peer), consistency_level, tserver::AllowSplitTablet::kFalse, &resp);
  }
};

class MasterTabletServiceMultiMasterTest : public MasterTabletServiceTest {
 public:
  MiniClusterOptions GetMiniClusterOptions() override {
    auto options = MasterTabletServiceTest::GetMiniClusterOptions();
    options.num_masters = 3;
    return options;
  }

 protected:
  Result<MiniMaster*> RestartFollower() {
    auto* leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      auto* follower = cluster_->mini_master(i);
      if (follower != leader) {
        RETURN_NOT_OK(follower->Restart());
        return follower;
      }
    }
    return STATUS(NotFound, "No master follower");
  }

  tserver::ReadRequestPB AuthCatalogReadRequest(HybridTime read_time) {
    tserver::ReadRequestPB req;
    req.set_tablet_id(kSysCatalogTabletId);
    req.set_ysql_auth_catalog_read(true);
    req.set_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
    auto* pg = req.add_pgsql_batch();
    pg->set_table_id(GetPgsqlTableId(kTemplate1Oid, 1260));
    pg->set_client(YQL_CLIENT_PGSQL);
    pg->set_schema_version(0);
    pg->add_targets()->set_column_id(10);
    pg->add_col_refs()->set_column_id(10);
    ReadHybridTime::SingleTime(read_time).AddToPB(&req);
    return req;
  }

  Result<tserver::ReadResponsePB> ReadAuthCatalog(
      MiniMaster* master, const tserver::ReadRequestPB& req) {
    tserver::TabletServerServiceProxy proxy(&cluster_->proxy_cache(), master->bound_rpc_addr());
    tserver::ReadResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(10s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.Read(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp;
  }

  Status CreateAuthCatalogTable() {
    client::YBClientBuilder builder;
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      builder.add_master_server_addr(cluster_->mini_master(i)->bound_rpc_addr_str());
    }
    auth_client_ = VERIFY_RESULT(builder.Build());
    RETURN_NOT_OK(auth_client_->CreateNamespaceIfNotExists(
        "template1", YQL_DATABASE_PGSQL, "", GetPgsqlNamespaceId(kTemplate1Oid)));
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterDdlProxy>());
    CreateTableRequestPB req;
    req.set_name("auth_read_test");
    req.set_table_id(GetPgsqlTableId(kTemplate1Oid, 1260));
    req.set_table_type(PGSQL_TABLE_TYPE);
    req.set_is_pg_catalog_table(true);
    req.set_is_pg_shared_table(true);
    req.mutable_namespace_()->set_id(GetPgsqlNamespaceId(kTemplate1Oid));
    auto* column = req.mutable_schema()->add_columns();
    column->set_id(10);
    column->set_name("k");
    column->mutable_type()->set_main(INT32);
    column->set_is_key(true);
    column->set_pg_type_oid(23);
    CreateTableResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.CreateTable(req, &resp, &rpc));
    return resp.has_error() ? StatusFromPB(resp.error().status()) : Status::OK();
  }

  std::unique_ptr<client::YBClient> auth_client_;
};


TEST_F(MasterTabletServiceTest, ListMasterServers) {
  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<tserver::TabletServerServiceProxy>());
  tserver::ListMasterServersRequestPB req;
  tserver::ListMasterServersResponsePB resp;
  rpc::RpcController rpc;
  auto status = proxy.ListMasterServers(req, &resp, &rpc);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Not implemented");
}

TEST_F(MasterTabletServiceTest, SysCatalogTabletLookup) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto peer = leader->tablet_peer();
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(
                leader, kSysCatalogTabletId, YBConsistencyLevel::STRONG)), tablet);
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(
                leader, kSysCatalogTabletId, YBConsistencyLevel::STRONG, peer)), tablet);
}

TEST_F(MasterTabletServiceTest, VirtualTabletLookup) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto& catalog_manager = leader->catalog_manager_impl();
  SCOPED_LEADER_SHARED_LOCK(lock, &catalog_manager);
  ASSERT_OK(lock.first_failed_status());
  auto table = catalog_manager.GetTableInfoFromNamespaceNameAndTableName(
      YQL_DATABASE_CQL, "system", "peers");
  ASSERT_TRUE(table);
  auto tablets = ASSERT_RESULT(table->GetTablets());
  ASSERT_EQ(tablets.size(), 1);
  const auto& tablet_id = tablets.front()->tablet_id();
  ASSERT_NOK(catalog_manager.GetServingTablet(tablet_id));
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(leader, tablet_id, YBConsistencyLevel::STRONG)),
            ASSERT_RESULT(catalog_manager.GetSystemTablet(tablet_id)));
}

TEST_F(MasterTabletServiceMultiMasterTest, FollowerSysCatalogTabletLookup) {
  auto* follower = ASSERT_RESULT(RestartFollower());
  ASSERT_LT(follower->catalog_manager().leader_ready_term(), 0);
  auto peer = follower->tablet_peer();
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_RESULT(tablet->SafeTime(
      tablet::RequireLease::kFalse, leader->Now(),
      CoarseMonoClock::Now() + MonoDelta::FromSeconds(30)));
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(
                follower, kSysCatalogTabletId, YBConsistencyLevel::CONSISTENT_PREFIX)), tablet);
  ASSERT_EQ(
      ASSERT_RESULT(LookupReadTablet(
          follower, kSysCatalogTabletId, YBConsistencyLevel::CONSISTENT_PREFIX, peer)), tablet);
  auto strong_read = LookupReadTablet(follower, kSysCatalogTabletId, YBConsistencyLevel::STRONG);
  ASSERT_NOK(strong_read);
  ASSERT_TRUE(tserver::IsErrorCodeNotTheLeader(strong_read.status()));
}

TEST_F(MasterTabletServiceMultiMasterTest, FollowerReadRpcRemainsLeaderOnly) {
  auto* follower = ASSERT_RESULT(RestartFollower());
  tserver::TabletServerServiceProxy proxy(&cluster_->proxy_cache(), follower->bound_rpc_addr());
  for (auto consistency : {YBConsistencyLevel::STRONG, YBConsistencyLevel::CONSISTENT_PREFIX}) {
    tserver::ReadRequestPB req;
    req.set_tablet_id(kSysCatalogTabletId);
    req.set_consistency_level(consistency);
    tserver::ReadResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(10));
    ASSERT_OK(proxy.Read(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), tserver::TabletServerErrorPB::NOT_THE_LEADER);
  }
}

TEST_F(MasterTabletServiceMultiMasterTest, AuthFollowerServingAndFixedTimeLeaderFallback) {
  ASSERT_OK(CreateAuthCatalogTable());
  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, true));
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  const auto read_time = ASSERT_RESULT(auth_client_->GetYsqlAuthCatalogReadTime(
      CoarseMonoClock::Now() + 30s * kTimeMultiplier));
  ASSERT_GE(auth_client_->GetLatestObservedHybridTime(), read_time.ToUint64());
  auto req = AuthCatalogReadRequest(read_time);
  auto* follower = ASSERT_RESULT(RestartFollower());
  auto tablet = ASSERT_RESULT(follower->tablet_peer()->shared_tablet());
  ASSERT_RESULT(tablet->SafeTime(
      tablet::RequireLease::kFalse, read_time, CoarseMonoClock::Now() + 30s * kTimeMultiplier));
  const auto response = ASSERT_RESULT(ReadAuthCatalog(follower, req));
  ASSERT_FALSE(response.has_restart_read_time());
  ASSERT_EQ(response.local_limit_ht(), read_time.ToUint64());
  ASSERT_EQ(response.pgsql_batch_size(), 1);
  ASSERT_EQ(response.pgsql_batch(0).status(), PgsqlResponsePB::PGSQL_STATUS_OK);
  ASSERT_EQ(METRIC_ysql_auth_catalog_follower_reads.Instantiate(
      follower->master()->metric_entity())->value(), 1);
  ASSERT_EQ(METRIC_ysql_auth_catalog_leader_reads.Instantiate(
      follower->master()->metric_entity())->value(), 0);

  const std::vector<std::function<void(tserver::ReadRequestPB*)>> invalid_envelopes = {
      [](auto* r) { r->clear_read_time(); },
      [](auto* r) { r->mutable_read_time()->set_global_limit_ht(HybridTime::kMax.ToUint64()); },
      [](auto* r) { r->mutable_read_time()->set_read_ht(HybridTime::kMin.ToUint64()); },
      [](auto* r) { r->mutable_transaction(); },
      [](auto* r) { r->mutable_subtransaction(); },
      [](auto* r) { r->set_use_async_write(true); },
      [](auto* r) { r->mutable_pending_async_write_op_id(); },
      [](auto* r) { r->add_ql_batch(); },
      [](auto* r) { r->add_redis_batch(); },
      [](auto* r) { r->clear_pgsql_batch(); },
      [](auto* r) { r->set_tablet_id("virtual_tablet"); },
      [](auto* r) { r->mutable_pgsql_batch(0)->set_ysql_catalog_version(1); },
      [](auto* r) {
        r->mutable_pgsql_batch(0)->mutable_index_request()->set_table_id(
            GetPgsqlTableId(kTemplate1Oid, 2671));
      },
  };
  for (const auto& invalidate : invalid_envelopes) {
    auto invalid = req;
    invalidate(&invalid);
    const auto result = ReadAuthCatalog(follower, invalid);
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsInvalidArgument()) << result.status();
  }

  auto future_req = req;
  ReadHybridTime::SingleTime(leader->Now().AddSeconds(60)).AddToPB(&future_req);
  const auto start = CoarseMonoClock::Now();
  const auto lagging = ReadAuthCatalog(follower, future_req);
  ASSERT_NOK(lagging);
  ASSERT_TRUE(lagging.status().IsIllegalState()) << lagging.status();
  ASSERT_LT(CoarseMonoClock::Now() - start, 5s * kTimeMultiplier);
  ASSERT_EQ(METRIC_ysql_auth_catalog_follower_reads.Instantiate(
      follower->master()->metric_entity())->value(), 1);

  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, false));
  ASSERT_NOK_STR_CONTAINS(ReadAuthCatalog(follower, req), "disabled");
  req.set_consistency_level(YBConsistencyLevel::STRONG);
  ASSERT_NOK(ReadAuthCatalog(follower, req));
  const auto fallback = ASSERT_RESULT(ReadAuthCatalog(leader, req));
  ASSERT_FALSE(fallback.has_restart_read_time());
  ASSERT_EQ(fallback.local_limit_ht(), read_time.ToUint64());
  ASSERT_EQ(fallback.pgsql_batch_size(), 1);
  ASSERT_EQ(fallback.pgsql_batch(0).status(), PgsqlResponsePB::PGSQL_STATUS_OK);
  ASSERT_EQ(ReadHybridTime::FromReadTimePB(req), ReadHybridTime::SingleTime(read_time));
  ASSERT_EQ(METRIC_ysql_auth_catalog_leader_reads.Instantiate(
      leader->master()->metric_entity())->value(), 1);
}

void MasterTabletServiceTest::SetUp() {
  YBMiniClusterTestBase::SetUp();
  cluster_ = std::make_unique<MiniCluster>(GetMiniClusterOptions());
  ASSERT_OK(cluster_->Start());
}

MiniClusterOptions MasterTabletServiceTest::GetMiniClusterOptions() { return MiniClusterOptions(); }

}  // namespace yb::master
