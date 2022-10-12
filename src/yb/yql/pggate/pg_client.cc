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

#include "yb/gutil/casts.h"

#include "yb/rpc/poller.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_tabledesc.h"

DECLARE_bool(use_node_hostname_for_local_tserver);
DECLARE_int32(backfill_index_client_rpc_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

DEFINE_uint64(pg_client_heartbeat_interval_ms, 10000, "Pg client heartbeat interval in ms.");

DECLARE_bool(TEST_index_read_multiple_partitions);

using namespace std::literals;

namespace yb {
namespace pggate {

namespace {

// Adding this value to RPC call timeout, so postgres could detect timeout by it's own mechanism
// and report it.
const auto kExtraTimeout = 2s;

struct PerformData {
  PgsqlOps operations;
  tserver::LWPgPerformResponsePB resp;
  rpc::RpcController controller;
  PerformCallback callback;

  explicit PerformData(Arena* arena) : resp(arena) {
  }

  Status Process() {
    auto& responses = *resp.mutable_responses();
    SCHECK_EQ(implicit_cast<size_t>(responses.size()), operations.size(), RuntimeError,
              Format("Wrong number of responses: $0, while $1 expected",
                     responses.size(), operations.size()));
    uint32_t i = 0;
    for (auto& op_response : responses) {
      // TODO(LW_PERFORM)
      auto& arena = operations[i]->arena();
      if (&arena != &operations.front()->arena()) {
        operations[i]->set_response(arena.NewObject<LWPgsqlResponsePB>(&arena, op_response));
      } else {
        operations[i]->set_response(&op_response);
      }
      ++i;
    }
    return Status::OK();
  }
};

std::string PrettyFunctionName(const char* name) {
  std::string result;
  for (const char* ch = name; *ch; ++ch) {
    if (!result.empty() && std::isupper(*ch)) {
      result += ' ';
    }
    result += *ch;
  }
  return result;
}

} // namespace

class PgClient::Impl {
 public:
  Impl() : heartbeat_poller_(std::bind(&Impl::Heartbeat, this, false)) {
    tablet_server_count_cache_.fill(0);
  }

  ~Impl() {
    CHECK(!proxy_);
  }

  Status Start(rpc::ProxyCache* proxy_cache,
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
    LOG_WITH_PREFIX(INFO) << "Session id acquired";
    heartbeat_poller_.Start(scheduler, FLAGS_pg_client_heartbeat_interval_ms * 1ms);
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
        LOG_WITH_PREFIX(DFATAL) << "Heartbeat did not complete yet";
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
        LOG_WITH_PREFIX(WARNING) << "Heartbeat failed: " << status;
      }
    });
  }

  void SetTimeout(MonoDelta timeout) {
    timeout_ = timeout + kExtraTimeout;
  }

  Result<PgTableDescPtr> OpenTable(
      const PgObjectId& table_id, bool reopen, CoarseTimePoint invalidate_cache_time) {
    tserver::PgOpenTableRequestPB req;
    req.set_table_id(table_id.GetYbTableId());
    req.set_reopen(reopen);
    if (invalidate_cache_time != CoarseTimePoint()) {
      req.set_invalidate_cache_time_us(ToMicroseconds(invalidate_cache_time.time_since_epoch()));
    }
    tserver::PgOpenTableResponsePB resp;

    RETURN_NOT_OK(proxy_->OpenTable(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));

    auto partitions = std::make_shared<client::VersionedTablePartitionList>();
    partitions->version = resp.partitions().version();
    const auto& keys = resp.partitions().keys();
    if (PREDICT_FALSE(FLAGS_TEST_index_read_multiple_partitions && keys.size() > 1)) {
      // It is required to simulate tablet splitting. This is done by reducing number of partitions.
      // Only middle element is used to split table into 2 partitions.
      // DocDB partition schema like [, 12, 25, 37, 50, 62, 75, 87] will be interpret by YSQL
      // as [, 50].
      partitions->keys = {PartitionKey(), keys[keys.size() / 2]};
      static auto key_printer = [](const auto& key) { return Slice(key).ToDebugHexString(); };
      LOG(INFO) << "Partitions for " << table_id << " are joined."
                << " source: " << ToString(keys, key_printer)
                << " result: " << ToString(partitions->keys, key_printer);
    } else {
      partitions->keys.assign(keys.begin(), keys.end());
    }

    auto result = make_scoped_refptr<PgTableDesc>(
        table_id, resp.info(), std::move(partitions));
    RETURN_NOT_OK(result->Init());
    return result;
  }

  Status FinishTransaction(Commit commit, DdlMode ddl_mode) {
    tserver::PgFinishTransactionRequestPB req;
    req.set_session_id(session_id_);
    req.set_commit(commit);
    req.set_ddl_mode(ddl_mode);
    tserver::PgFinishTransactionResponsePB resp;

    RETURN_NOT_OK(proxy_->FinishTransaction(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Result<master::GetNamespaceInfoResponsePB> GetDatabaseInfo(uint32_t oid) {
    tserver::PgGetDatabaseInfoRequestPB req;
    req.set_oid(oid);

    tserver::PgGetDatabaseInfoResponsePB resp;

    RETURN_NOT_OK(proxy_->GetDatabaseInfo(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.info();
  }

  Status SetActiveSubTransaction(
      SubTransactionId id, tserver::PgPerformOptionsPB* options) {
    tserver::PgSetActiveSubTransactionRequestPB req;
    req.set_session_id(session_id_);
    if (options) {
      options->Swap(req.mutable_options());
    }
    req.set_sub_transaction_id(id);

    tserver::PgSetActiveSubTransactionResponsePB resp;

    RETURN_NOT_OK(proxy_->SetActiveSubTransaction(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Status RollbackToSubTransaction(SubTransactionId id) {
    tserver::PgRollbackToSubTransactionRequestPB req;
    req.set_session_id(session_id_);
    req.set_sub_transaction_id(id);

    tserver::PgRollbackToSubTransactionResponsePB resp;

    RETURN_NOT_OK(proxy_->RollbackToSubTransaction(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Status InsertSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     int64_t last_val,
                                     bool is_called) {
    tserver::PgInsertSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    req.set_ysql_catalog_version(ysql_catalog_version);
    req.set_last_val(last_val);
    req.set_is_called(is_called);

    tserver::PgInsertSequenceTupleResponsePB resp;

    RETURN_NOT_OK(proxy_->InsertSequenceTuple(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Result<bool> UpdateSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   int64_t last_val,
                                   bool is_called,
                                   boost::optional<int64_t> expected_last_val,
                                   boost::optional<bool> expected_is_called) {
    tserver::PgUpdateSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    req.set_ysql_catalog_version(ysql_catalog_version);
    req.set_last_val(last_val);
    req.set_is_called(is_called);
    if (expected_last_val && expected_is_called) {
      req.set_has_expected(true);
      req.set_expected_last_val(*expected_last_val);
      req.set_expected_is_called(*expected_is_called);
    }

    tserver::PgUpdateSequenceTupleResponsePB resp;

    RETURN_NOT_OK(proxy_->UpdateSequenceTuple(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.skipped();
  }

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(int64_t db_oid,
                                                     int64_t seq_oid,
                                                     uint64_t ysql_catalog_version) {
    tserver::PgReadSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    req.set_ysql_catalog_version(ysql_catalog_version);

    tserver::PgReadSequenceTupleResponsePB resp;

    RETURN_NOT_OK(proxy_->ReadSequenceTuple(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::make_pair(resp.last_val(), resp.is_called());
  }

  Status DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
    tserver::PgDeleteSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);

    tserver::PgDeleteSequenceTupleResponsePB resp;

    RETURN_NOT_OK(proxy_->DeleteSequenceTuple(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Status DeleteDBSequences(int64_t db_oid) {
    tserver::PgDeleteDBSequencesRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);

    tserver::PgDeleteDBSequencesResponsePB resp;

    RETURN_NOT_OK(proxy_->DeleteDBSequences(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  void PerformAsync(
      tserver::PgPerformOptionsPB* options,
      PgsqlOps* operations,
      const PerformCallback& callback) {
    auto& arena = operations->front()->arena();
    tserver::LWPgPerformRequestPB req(&arena);
    req.set_session_id(session_id_);
    *req.mutable_options() = std::move(*options);
    PrepareOperations(&req, operations);

    auto data = std::make_shared<PerformData>(&arena);
    data->operations = std::move(*operations);
    data->callback = callback;
    data->controller.set_invoke_callback_mode(rpc::InvokeCallbackMode::kReactorThread);

    proxy_->PerformAsync(req, &data->resp, SetupController(&data->controller), [data] {
      PerformResult result;
      result.status = data->controller.status();
      result.response = data->controller.response();
      if (result.status.ok()) {
        result.status = ResponseStatus(data->resp);
      }
      if (result.status.ok()) {
        result.status = data->Process();
      }
      if (result.status.ok() && data->resp.has_catalog_read_time()) {
        result.catalog_read_time = ReadHybridTime::FromPB(data->resp.catalog_read_time());
      }
      data->callback(result);
    });
  }

  void PrepareOperations(tserver::LWPgPerformRequestPB* req, PgsqlOps* operations) {
    auto& ops = *req->mutable_ops();
    for (auto& op : *operations) {
      auto& union_op = ops.emplace_back();
      if (op->is_read()) {
        auto& read_op = down_cast<PgsqlReadOp&>(*op);
        union_op.ref_read(&read_op.read_request());
        if (read_op.read_from_followers()) {
          union_op.set_read_from_followers(true);
        }
      } else {
        auto& write_op = down_cast<PgsqlWriteOp&>(*op);
        if (write_op.write_time()) {
          req->set_write_time(write_op.write_time().ToUint64());
        }
        union_op.ref_write(&write_op.write_request());
      }
      if (op->read_time()) {
        op->read_time().AddToPB(req->mutable_options());
      }
    }
  }

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count) {
    tserver::PgReserveOidsRequestPB req;
    req.set_database_oid(database_oid);
    req.set_next_oid(next_oid);
    req.set_count(count);

    tserver::PgReserveOidsResponsePB resp;

    RETURN_NOT_OK(proxy_->ReserveOids(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::pair<PgOid, PgOid>(resp.begin_oid(), resp.end_oid());
  }

  Result<bool> IsInitDbDone() {
    tserver::PgIsInitDbDoneRequestPB req;
    tserver::PgIsInitDbDoneResponsePB resp;

    RETURN_NOT_OK(proxy_->IsInitDbDone(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.done();
  }

  Result<uint64_t> GetCatalogMasterVersion() {
    tserver::PgGetCatalogMasterVersionRequestPB req;
    tserver::PgGetCatalogMasterVersionResponsePB resp;

    RETURN_NOT_OK(proxy_->GetCatalogMasterVersion(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.version();
  }

  Status CreateSequencesDataTable() {
    tserver::PgCreateSequencesDataTableRequestPB req;
    tserver::PgCreateSequencesDataTableResponsePB resp;

    RETURN_NOT_OK(proxy_->CreateSequencesDataTable(req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Result<client::YBTableName> DropTable(
      tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline) {
    req->set_session_id(session_id_);
    tserver::PgDropTableResponsePB resp;
    RETURN_NOT_OK(proxy_->DropTable(*req, &resp, PrepareController(deadline)));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::YBTableName result;
    if (resp.has_indexed_table()) {
      result.GetFromTableIdentifierPB(resp.indexed_table());
    }
    return result;
  }

  Status BackfillIndex(
      tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline) {
    tserver::PgBackfillIndexResponsePB resp;
    req->set_session_id(session_id_);

    RETURN_NOT_OK(proxy_->BackfillIndex(*req, &resp, PrepareController(deadline)));
    return ResponseStatus(resp);
  }

  Result<int32> TabletServerCount(bool primary_only) {
    if (tablet_server_count_cache_[primary_only] > 0) {
      return tablet_server_count_cache_[primary_only];
    }
    tserver::PgTabletServerCountRequestPB req;
    tserver::PgTabletServerCountResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(proxy_->TabletServerCount(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    tablet_server_count_cache_[primary_only] = resp.count();
    return resp.count();
  }

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only) {
    tserver::PgListLiveTabletServersRequestPB req;
    tserver::PgListLiveTabletServersResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(proxy_->ListLiveTabletServers(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::TabletServersInfo result;
    result.reserve(resp.servers().size());
    for (const auto& server : resp.servers()) {
      result.push_back(client::YBTabletServerPlacementInfo::FromPB(server));
    }
    return result;
  }

  Status ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req) {
    tserver::PgValidatePlacementResponsePB resp;
    RETURN_NOT_OK(proxy_->ValidatePlacement(*req, &resp, PrepareController()));
    return ResponseStatus(resp);
  }

  Result<client::TableSizeInfo> GetTableDiskSize(
      const PgObjectId& table_oid) {
    tserver::PgGetTableDiskSizeResponsePB resp;

    tserver::PgGetTableDiskSizeRequestPB req;
    table_oid.ToPB(req.mutable_table_id());

    RETURN_NOT_OK(proxy_->GetTableDiskSize(req, &resp, PrepareController()));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }

    return client::TableSizeInfo{resp.size(), resp.num_missing_tablets()};
  }

  Result<bool> CheckIfPitrActive() {
    tserver::PgCheckIfPitrActiveRequestPB req;
    tserver::PgCheckIfPitrActiveResponsePB resp;
    RETURN_NOT_OK(proxy_->CheckIfPitrActive(req, &resp, PrepareController()));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp.is_pitr_active();
  }

  Result<tserver::PgGetTserverCatalogVersionInfoResponsePB> GetTserverCatalogVersionInfo() {
    tserver::PgGetTserverCatalogVersionInfoRequestPB req;
    tserver::PgGetTserverCatalogVersionInfoResponsePB resp;
    RETURN_NOT_OK(proxy_->GetTserverCatalogVersionInfo(req, &resp, PrepareController()));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp;
  }

  #define YB_PG_CLIENT_SIMPLE_METHOD_IMPL(r, data, method) \
  Status method( \
      tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      CoarseTimePoint deadline) { \
    tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB) resp; \
    req->set_session_id(session_id_); \
    auto status = proxy_->method(*req, &resp, PrepareController(deadline)); \
    if (!status.ok()) { \
      if (status.IsTimedOut()) { \
        return STATUS_FORMAT(TimedOut, "Timed out waiting for $0", PrettyFunctionName(__func__)); \
      } \
      return status; \
    } \
    return ResponseStatus(resp); \
  }

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_IMPL, ~, YB_PG_CLIENT_SIMPLE_METHODS);

 private:
  std::string LogPrefix() const {
    return Format("S $0: ", session_id_);
  }

  rpc::RpcController* SetupController(
      rpc::RpcController* controller, CoarseTimePoint deadline = CoarseTimePoint()) {
    if (deadline != CoarseTimePoint()) {
      controller->set_deadline(deadline);
    } else {
      controller->set_timeout(timeout_);
    }
    return controller;
  }

  rpc::RpcController* PrepareController(CoarseTimePoint deadline = CoarseTimePoint()) {
    controller_.Reset();
    return SetupController(&controller_, deadline);
  }

  rpc::RpcController* PrepareHeartbeatController() {
    heartbeat_controller_.Reset();
    heartbeat_controller_.set_timeout(FLAGS_pg_client_heartbeat_interval_ms * 1ms - 1s);
    return &heartbeat_controller_;
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
  MonoDelta timeout_ = FLAGS_yb_client_admin_operation_timeout_sec * 1s;
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

void PgClient::SetTimeout(MonoDelta timeout) {
  impl_->SetTimeout(timeout);
}

Result<PgTableDescPtr> PgClient::OpenTable(
    const PgObjectId& table_id, bool reopen, CoarseTimePoint invalidate_cache_time) {
  return impl_->OpenTable(table_id, reopen, invalidate_cache_time);
}

Status PgClient::FinishTransaction(Commit commit, DdlMode ddl_mode) {
  return impl_->FinishTransaction(commit, ddl_mode);
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

Status PgClient::SetActiveSubTransaction(
    SubTransactionId id, tserver::PgPerformOptionsPB* options) {
  return impl_->SetActiveSubTransaction(id, options);
}

Status PgClient::RollbackToSubTransaction(SubTransactionId id) {
  return impl_->RollbackToSubTransaction(id);
}

Status PgClient::ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req) {
  return impl_->ValidatePlacement(req);
}

Result<client::TableSizeInfo> PgClient::GetTableDiskSize(
    const PgObjectId& table_oid) {
  return impl_->GetTableDiskSize(table_oid);
}

Status PgClient::InsertSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     int64_t last_val,
                                     bool is_called) {
  return impl_->InsertSequenceTuple(db_oid, seq_oid, ysql_catalog_version, last_val, is_called);
}

Result<bool> PgClient::UpdateSequenceTuple(int64_t db_oid,
                                           int64_t seq_oid,
                                           uint64_t ysql_catalog_version,
                                           int64_t last_val,
                                           bool is_called,
                                           boost::optional<int64_t> expected_last_val,
                                           boost::optional<bool> expected_is_called) {
  return impl_->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, last_val, is_called, expected_last_val,
      expected_is_called);
}

Result<std::pair<int64_t, bool>> PgClient::ReadSequenceTuple(int64_t db_oid,
                                                             int64_t seq_oid,
                                                             uint64_t ysql_catalog_version) {
  return impl_->ReadSequenceTuple(db_oid, seq_oid, ysql_catalog_version);
}

Status PgClient::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  return impl_->DeleteSequenceTuple(db_oid, seq_oid);
}

Status PgClient::DeleteDBSequences(int64_t db_oid) {
  return impl_->DeleteDBSequences(db_oid);
}

void PgClient::PerformAsync(
    tserver::PgPerformOptionsPB* options,
    PgsqlOps* operations,
    const PerformCallback& callback) {
  impl_->PerformAsync(options, operations, callback);
}

Result<bool> PgClient::CheckIfPitrActive() {
  return impl_->CheckIfPitrActive();
}

Result<tserver::PgGetTserverCatalogVersionInfoResponsePB> PgClient::GetTserverCatalogVersionInfo() {
  return impl_->GetTserverCatalogVersionInfo();
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
