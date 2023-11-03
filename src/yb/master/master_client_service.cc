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

#include "yb/master/catalog_manager.h"
#include "yb/master/master_client.service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"

#include "yb/util/flags.h"

DEFINE_UNKNOWN_int32(master_inject_latency_on_tablet_lookups_ms, 0,
             "Number of milliseconds that the master will sleep before responding to "
             "requests for tablet locations.");
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, unsafe);
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, hidden);

DEFINE_test_flag(bool, master_fail_transactional_tablet_lookups, false,
                 "Whether to fail all lookup requests to transactional table.");

namespace yb {
namespace master {

namespace {

class MasterClientServiceImpl : public MasterServiceBase, public MasterClientIf {
 public:
  explicit MasterClientServiceImpl(Master* master)
      : MasterServiceBase(master), MasterClientIf(master->metric_entity()) {}

  void GetTabletLocations(const GetTabletLocationsRequestPB* req,
                          GetTabletLocationsResponsePB* resp,
                          rpc::RpcContext rpc) override {
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      return;
    }

    if (PREDICT_FALSE(FLAGS_master_inject_latency_on_tablet_lookups_ms > 0)) {
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_master_inject_latency_on_tablet_lookups_ms));
    }
    if (PREDICT_FALSE(FLAGS_TEST_master_fail_transactional_tablet_lookups)) {
      auto tables = server_->catalog_manager_impl()->GetTables(GetTablesMode::kAll);
      const auto& tablet_id = req->tablet_ids(0);
      for (const auto& table : tables) {
        TabletInfos tablets = table->GetTablets();
        for (const auto& tablet : tablets) {
          if (tablet->tablet_id() == tablet_id) {
            TableType table_type;
            {
              auto lock = table->LockForRead();
              table_type = table->metadata().state().table_type();
            }
            if (table_type == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
              rpc.RespondFailure(STATUS(InvalidCommand, "TEST: Artificial failure"));
              return;
            }
            break;
          }
        }
      }
    }

    if (req->has_table_id()) {
      const auto table_info = server_->catalog_manager_impl()->GetTableInfo(req->table_id());
      if (table_info) {
        const auto table_lock = table_info->LockForRead();
        resp->set_partition_list_version(table_lock->pb.partition_list_version());
      }
    }

    IncludeInactive include_inactive(req->has_include_inactive() && req->include_inactive());

    for (const TabletId& tablet_id : req->tablet_ids()) {
      int expected_live_replicas = 0, expected_read_replicas = 0;
      auto s = server_->catalog_manager_impl()->GetExpectedNumberOfReplicasForTablet(
          tablet_id, &expected_live_replicas, &expected_read_replicas);
      if (!s.ok()) {
        GetTabletLocationsResponsePB::Error* err = resp->add_errors();
        err->set_tablet_id(tablet_id);
        StatusToPB(s, err->mutable_status());
        continue;
      }

      // TODO: once we have catalog data. ACL checks would also go here, probably.
      TabletLocationsPB* locs_pb = resp->add_tablet_locations();
      locs_pb->set_expected_live_replicas(expected_live_replicas);
      locs_pb->set_expected_read_replicas(expected_read_replicas);
      s = server_->catalog_manager_impl()->GetTabletLocations(
          tablet_id, locs_pb, include_inactive);
      if (!s.ok()) {
        resp->mutable_tablet_locations()->RemoveLast();

        GetTabletLocationsResponsePB::Error* err = resp->add_errors();
        err->set_tablet_id(tablet_id);
        StatusToPB(s, err->mutable_status());
      }
    }

    rpc.RespondSuccess();
  }

  void GetTableLocations(const GetTableLocationsRequestPB* req,
                         GetTableLocationsResponsePB* resp,
                         rpc::RpcContext rpc) override {
    // We can't use the HANDLE_ON_LEADER_WITH_LOCK macro here because we have to inject latency
    // before acquiring the leader lock.
    HandleOnLeader(resp, &rpc, [&]() -> Status {
      if (PREDICT_FALSE(FLAGS_master_inject_latency_on_tablet_lookups_ms > 0)) {
        SleepFor(MonoDelta::FromMilliseconds(FLAGS_master_inject_latency_on_tablet_lookups_ms));
      }
      return server_->catalog_manager_impl()->GetTableLocations(req, resp);
    }, __FILE__, __LINE__, __func__, HoldCatalogLock::kTrue);
  }

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    CatalogManager,
    (GetYsqlCatalogConfig)
    (RedisConfigSet)
    (RedisConfigGet)
    (ReservePgsqlOids)
    (GetIndexBackfillProgress)
    (GetStatefulServiceLocation)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITHOUT_LOCK(
    CatalogManager,
    (GetTransactionStatusTablets)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterClientService(Master* master) {
  return std::make_unique<MasterClientServiceImpl>(master);
}

} // namespace master
} // namespace yb
