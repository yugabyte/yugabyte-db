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

#include "yb/yql/pggate/pg_client.h"

#include "yb/client/client-internal.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"

#include "yb/rpc/poller.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/result.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_tabledesc.h"

DECLARE_bool(use_node_hostname_for_local_tserver);
DECLARE_int32(backfill_index_client_rpc_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

DEFINE_uint64(pg_client_heartbeat_interval_ms, 10000, "Pg client heartbeat interval in ms.");

using namespace std::literals;

namespace yb {
namespace pggate {

class PgClient::Impl {
 public:
  Impl() : heartbeat_poller_(std::bind(&Impl::Heartbeat, this, false)) {
    tablet_server_count_cache_.fill(0);
  }

  ~Impl() {
    CHECK(!proxy_);
  }

  CHECKED_STATUS Start(rpc::ProxyCache* proxy_cache,
                       rpc::Scheduler* scheduler,
                       const tserver::TServerSharedObject& tserver_shared_object) {
    CHECK_NOTNULL(&tserver_shared_object);
    MonoDelta resolve_cache_timeout;
    const auto& tserver_shared_data_ = *tserver_shared_object;
    HostPort host_port(tserver_shared_data_.endpoint());
    if (FLAGS_use_node_hostname_for_local_tserver) {
      host_port = HostPort(tserver_shared_data_.host().ToBuffer(),
                           tserver_shared_data_.endpoint().port());
      resolve_cache_timeout = MonoDelta::kMax;
    }
    LOG(INFO) << "Using TServer host_port: " << host_port;
    proxy_ = std::make_unique<tserver::PgClientServiceProxy>(
        proxy_cache, host_port, nullptr /* protocol */, resolve_cache_timeout);

    auto future = create_session_promise_.get_future();
    Heartbeat(true);
    session_id_ = VERIFY_RESULT(future.get());
    heartbeat_poller_.Start(scheduler, FLAGS_pg_client_heartbeat_interval_ms * 1ms - 1s);
    return Status::OK();
  }

  void Shutdown() {
    heartbeat_poller_.Shutdown();
    proxy_ = nullptr;
  }

  void Heartbeat(bool create) {
    {
      bool expected = false;
      if (!heartbeat_running_.compare_exchange_strong(expected, true)) {
        LOG(DFATAL) << "Heartbeat did not complete yet";
        return;
      }
    }
    tserver::PgHeartbeatRequestPB req;
    if (!create) {
      req.set_session_id(session_id_);
    }
    proxy_->HeartbeatAsync(
        req, &heartbeat_resp_, PrepareHeartbeatController(),
        [this, create] {
      auto status = ResponseStatus(heartbeat_resp_);
      if (create) {
        if (!status.ok()) {
          create_session_promise_.set_value(status);
        } else {
          create_session_promise_.set_value(heartbeat_resp_.session_id());
        }
      }
      heartbeat_running_ = false;
      if (!status.ok()) {
        LOG(WARNING) << "Heartbeat failed: " << status;
      }
    });
  }

  Result<PgTableDescPtr> OpenTable(const PgObjectId& table_id) {
    tserver::PgOpenTableRequestPB req;
    req.set_table_id(table_id.GetYBTableId());
    tserver::PgOpenTableResponsePB resp;

    RETURN_NOT_OK(proxy_->OpenTable(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));

    client::YBTableInfo info;
    RETURN_NOT_OK(client::CreateTableInfoFromTableSchemaResp(resp.info(), &info));

    auto partitions = std::make_shared<client::VersionedTablePartitionList>();
    partitions->version = resp.partitions().version();
    partitions->keys.assign(resp.partitions().keys().begin(), resp.partitions().keys().end());

    return make_scoped_refptr<PgTableDesc>(std::make_shared<client::YBTable>(
        info, std::move(partitions)));
  }

  Result<master::GetNamespaceInfoResponsePB> GetDatabaseInfo(uint32_t oid) {
    tserver::PgGetDatabaseInfoRequestPB req;
    req.set_oid(oid);

    tserver::PgGetDatabaseInfoResponsePB resp;

    RETURN_NOT_OK(proxy_->GetDatabaseInfo(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.info();
  }

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count) {
    tserver::PgReserveOidsRequestPB req;
    req.set_database_oid(database_oid);
    req.set_next_oid(next_oid);
    req.set_count(count);

    tserver::PgReserveOidsResponsePB resp;

    RETURN_NOT_OK(proxy_->ReserveOids(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::pair<PgOid, PgOid>(resp.begin_oid(), resp.end_oid());
  }

  Result<bool> IsInitDbDone() {
    tserver::PgIsInitDbDoneRequestPB req;
    tserver::PgIsInitDbDoneResponsePB resp;

    RETURN_NOT_OK(proxy_->IsInitDbDone(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.done();
  }

  Result<uint64_t> GetCatalogMasterVersion() {
    tserver::PgGetCatalogMasterVersionRequestPB req;
    tserver::PgGetCatalogMasterVersionResponsePB resp;

    RETURN_NOT_OK(proxy_->GetCatalogMasterVersion(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.version();
  }

  CHECKED_STATUS CreateSequencesDataTable() {
    tserver::PgCreateSequencesDataTableRequestPB req;
    tserver::PgCreateSequencesDataTableResponsePB resp;

    RETURN_NOT_OK(proxy_->CreateSequencesDataTable(req, &resp, PrepareAdminController()));
    return ResponseStatus(resp);
  }

  Result<client::YBTableName> DropTable(
      tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline) {
    req->set_session_id(session_id_);
    tserver::PgDropTableResponsePB resp;
    RETURN_NOT_OK(proxy_->DropTable(*req, &resp, PrepareAdminController(deadline)));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::YBTableName result;
    if (resp.has_indexed_table()) {
      result.GetFromTableIdentifierPB(resp.indexed_table());
    }
    return result;
  }

  CHECKED_STATUS BackfillIndex(
      tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline) {
    tserver::PgBackfillIndexResponsePB resp;
    req->set_session_id(session_id_);

    // Use backfill_index_client_rpc_timeout_ms rather than yb_client_admin_operation_timeout_sec.
    controller_.Reset();
    DCHECK(deadline == CoarseTimePoint());
    controller_.set_timeout(FLAGS_backfill_index_client_rpc_timeout_ms * 1ms);

    RETURN_NOT_OK(proxy_->BackfillIndex(*req, &resp, &controller_));
    return ResponseStatus(resp);
  }

  Result<int32> TabletServerCount(bool primary_only) {
    if (tablet_server_count_cache_[primary_only] > 0) {
      return tablet_server_count_cache_[primary_only];
    }
    tserver::PgTabletServerCountRequestPB req;
    tserver::PgTabletServerCountResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(proxy_->TabletServerCount(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    tablet_server_count_cache_[primary_only] = resp.count();
    return resp.count();
  }

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only) {
    tserver::PgListLiveTabletServersRequestPB req;
    tserver::PgListLiveTabletServersResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(proxy_->ListLiveTabletServers(req, &resp, PrepareAdminController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::TabletServersInfo result;
    result.reserve(resp.servers().size());
    for (const auto& server : resp.servers()) {
      result.push_back(client::YBTabletServerPlacementInfo::FromPB(server));
    }
    return result;
  }

  CHECKED_STATUS ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req) {
    tserver::PgValidatePlacementResponsePB resp;
    RETURN_NOT_OK(proxy_->ValidatePlacement(*req, &resp, PrepareAdminController()));
    return ResponseStatus(resp);
  }

  #define YB_PG_CLIENT_SIMPLE_METHOD_IMPL(r, data, method) \
  CHECKED_STATUS method( \
      tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      CoarseTimePoint deadline) { \
    tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB) resp; \
    req->set_session_id(session_id_); \
    \
    RETURN_NOT_OK(proxy_->method(*req, &resp, PrepareAdminController(deadline))); \
    return ResponseStatus(resp); \
  }

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_IMPL, ~, YB_PG_CLIENT_SIMPLE_METHODS);

 private:
  static rpc::RpcController* SetupAdminController(
      rpc::RpcController* controller, CoarseTimePoint deadline = CoarseTimePoint()) {
    if (deadline != CoarseTimePoint()) {
      controller->set_deadline(deadline);
    } else {
      controller->set_timeout(FLAGS_yb_client_admin_operation_timeout_sec * 1s);
    }
    return controller;
  }

  rpc::RpcController* PrepareAdminController(CoarseTimePoint deadline = CoarseTimePoint()) {
    controller_.Reset();
    return SetupAdminController(&controller_, deadline);
  }

  rpc::RpcController* PrepareHeartbeatController() {
    heartbeat_controller_.Reset();
    return SetupAdminController(
        &heartbeat_controller_,
        CoarseMonoClock::now() + FLAGS_pg_client_heartbeat_interval_ms * 1ms);
  }

  std::unique_ptr<tserver::PgClientServiceProxy> proxy_;
  rpc::RpcController controller_;
  uint64_t session_id_ = 0;

  rpc::Poller heartbeat_poller_;
  std::atomic<bool> heartbeat_running_{false};
  rpc::RpcController heartbeat_controller_;
  tserver::PgHeartbeatResponsePB heartbeat_resp_;
  std::promise<Result<uint64_t>> create_session_promise_;
  std::array<int, 2> tablet_server_count_cache_;
};

PgClient::PgClient() : impl_(new Impl) {
}

PgClient::~PgClient() {
}

Status PgClient::Start(
    rpc::ProxyCache* proxy_cache, rpc::Scheduler* scheduler,
    const tserver::TServerSharedObject& tserver_shared_object) {
  return impl_->Start(proxy_cache, scheduler, tserver_shared_object);
}

void PgClient::Shutdown() {
  impl_->Shutdown();
}

Result<PgTableDescPtr> PgClient::OpenTable(const PgObjectId& table_id) {
  return impl_->OpenTable(table_id);
}

Result<master::GetNamespaceInfoResponsePB> PgClient::GetDatabaseInfo(uint32_t oid) {
  return impl_->GetDatabaseInfo(oid);
}

Result<std::pair<PgOid, PgOid>> PgClient::ReserveOids(
    PgOid database_oid, PgOid next_oid, uint32_t count) {
  return impl_->ReserveOids(database_oid, next_oid, count);
}

Result<bool> PgClient::IsInitDbDone() {
  return impl_->IsInitDbDone();
}

Result<uint64_t> PgClient::GetCatalogMasterVersion() {
  return impl_->GetCatalogMasterVersion();
}

Status PgClient::CreateSequencesDataTable() {
  return impl_->CreateSequencesDataTable();
}

Result<client::YBTableName> PgClient::DropTable(
    tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline) {
  return impl_->DropTable(req, deadline);
}

Status PgClient::BackfillIndex(
    tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline) {
  return impl_->BackfillIndex(req, deadline);
}

Result<int32> PgClient::TabletServerCount(bool primary_only) {
  return impl_->TabletServerCount(primary_only);
}

Result<client::TabletServersInfo> PgClient::ListLiveTabletServers(bool primary_only) {
  return impl_->ListLiveTabletServers(primary_only);
}

Status PgClient::ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req) {
  return impl_->ValidatePlacement(req);
}

#define YB_PG_CLIENT_SIMPLE_METHOD_DEFINE(r, data, method) \
Status PgClient::method( \
    tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
    CoarseTimePoint deadline) { \
  return impl_->method(req, deadline); \
}

BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_DEFINE, ~, YB_PG_CLIENT_SIMPLE_METHODS);

}  // namespace pggate
}  // namespace yb
