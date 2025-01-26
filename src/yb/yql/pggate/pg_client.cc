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

#include <functional>

#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client-internal.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/call_data.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/poller.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/pg_client.messages.h"
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
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/ybc_guc.h"
#include "yb/util/flags.h"

DECLARE_bool(use_node_hostname_for_local_tserver);
DECLARE_int32(backfill_index_client_rpc_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_uint32(ddl_verification_timeout_multiplier);

DEFINE_UNKNOWN_uint64(pg_client_heartbeat_interval_ms, 10000,
    "Pg client heartbeat interval in ms.");

DEFINE_NON_RUNTIME_int32(pg_client_extra_timeout_ms, 2000,
   "Adding this value to RPC call timeout, so postgres could detect timeout by it's own mechanism "
   "and report it.");

DECLARE_bool(TEST_index_read_multiple_partitions);
DECLARE_bool(TEST_ash_fetch_wait_states_for_raft_log);
DECLARE_bool(TEST_ash_fetch_wait_states_for_rocksdb_flush_and_compaction);
DECLARE_bool(TEST_export_wait_state_names);
DECLARE_bool(ysql_enable_db_catalog_version_mode);
DECLARE_int32(ysql_yb_ash_sample_size);

extern int yb_locks_min_txn_age;
extern int yb_locks_max_transactions;
extern int yb_locks_txn_locks_per_tablet;

using namespace std::literals;

namespace yb::pggate {

namespace {

using PerformCallback = std::function<void(const PerformResult&)>;

class FetchBigDataCallback {
 public:
  virtual void BigDataFetched(Result<rpc::CallData>* call_data) = 0;
  virtual ~FetchBigDataCallback() = default;
};

template <class Info>
Result<rpc::CallData> MakeFetchBigDataResult(const Info& info) {
  RETURN_NOT_OK(info.controller->status());
  RETURN_NOT_OK(ResponseStatus(*info.fetch_resp));
  auto sidecar = VERIFY_RESULT(info.controller->ExtractSidecar(info.fetch_resp->sidecar()));
  return rpc::CallData(std::move(sidecar));
}

class BigDataFetcher {
 public:
  virtual void FetchBigData(uint64_t data_id, FetchBigDataCallback* callback) = 0;
  virtual Slice FetchBigSharedMemory(uint64_t id, size_t size) = 0;
  virtual ~BigDataFetcher() = default;
};

template <class T>
struct ResponseReadyTraits;

template <>
struct ResponseReadyTraits<bool> {
  static bool AllowNotReady() {
    return true;
  }

  static bool NotReady() {
    return false;
  }

  static bool FromStatus(const Status& status) {
    return true;
  }

  static bool FromSlice(Slice slice) {
    return true;
  }

  static bool FromBigCallData(rpc::CallData* big_call_data) {
    return true;
  }
};

template <>
struct ResponseReadyTraits<Result<rpc::CallData>> {
  using ResultType = Result<rpc::CallData>;

  static bool AllowNotReady() {
    return false;
  }

  static ResultType NotReady() {
    CHECK(false);
  }

  static ResultType FromStatus(const Status& status) {
    return status;
  }

  static ResultType FromSlice(Slice slice) {
    rpc::CallData call_data(slice.size());
    slice.CopyTo(call_data.data());
    return call_data;
  }

  static ResultType FromBigCallData(rpc::CallData* big_call_data) {
    return std::move(*big_call_data);
  }
};

} // namespace

struct PerformData : public FetchBigDataCallback {
  PgsqlOps operations;
  tserver::LWPgPerformResponsePB resp;
  rpc::RpcController controller;

  tserver::SharedExchange* exchange = nullptr;
  CoarseTimePoint deadline;
  BigDataFetcher* big_data_fetcher;

  PerformCallback callback;

  std::mutex exchange_mutex;
  std::condition_variable exchange_cond;
  std::optional<Result<Slice>> exchange_result GUARDED_BY(exchange_mutex);
  bool fetching_big_data GUARDED_BY(exchange_mutex) = false;
  rpc::CallData big_call_data GUARDED_BY(exchange_mutex);

  PerformData(ThreadSafeArena* arena, PgsqlOps&& operations_, const PerformCallback& callback_)
      : operations(std::move(operations_)), resp(arena), callback(callback_) {
  }

  void SetupExchange(
      tserver::SharedExchange* exchange_, BigDataFetcher* big_data_fetcher_, MonoDelta timeout) {
    exchange = exchange_;
    big_data_fetcher = big_data_fetcher_;
    deadline = CoarseMonoClock::now() + timeout;
  }

  bool BigDataReady() const REQUIRES(exchange_mutex) {
    return !big_call_data.empty() || !exchange_result->ok();
  }

  template <class Res>
  Res FetchBigSharedMemory(size_t encoded_id_and_size) {
    if constexpr (std::is_same_v<Res, bool>) {
      return true;
    }
    using Traits = ResponseReadyTraits<Res>;
    auto id = encoded_id_and_size >> tserver::kBigSharedMemoryIdShift;
    auto size = encoded_id_and_size & ((1ULL << tserver::kBigSharedMemoryIdShift) - 1);
    auto slice = big_data_fetcher->FetchBigSharedMemory(id, size);
    auto& in_use = *pointer_cast<std::atomic<bool>*>(slice.mutable_data());
    auto result = Traits::FromSlice(slice.WithoutPrefix(sizeof(std::atomic<bool>)));
    in_use.store(false);
    return result;
  }

  template <class Res>
  Res ResponseReady() {
    using Traits = ResponseReadyTraits<Res>;
    UniqueLock lock(exchange_mutex);
    if (!exchange_result) {
      if (Traits::AllowNotReady() && !exchange->ResponseReady()) {
        return Traits::NotReady();
      }
      exchange_result = exchange->FetchResponse(deadline);
    }
    if (!exchange_result->ok()) {
      return Traits::FromStatus(exchange_result->status());
    }
    auto slice = **exchange_result;
    if (slice.data()) {
      return Traits::FromSlice(slice);
    }
    uint64_t data_id;
    if (fetching_big_data) {
      if (BigDataReady()) {
        if (!big_call_data.empty()) {
          return Traits::FromBigCallData(&big_call_data);
        } else {
          return Traits::FromStatus(exchange_result->status());
        }
      }
      if (Traits::AllowNotReady()) {
        return Traits::NotReady();
      }
      data_id = tserver::kTooBigResponseMask;
    } else {
      data_id = (**exchange_result).size() ^ tserver::kTooBigResponseMask;
      if (data_id & tserver::kBigSharedMemoryMask) {
        return FetchBigSharedMemory<Res>(data_id ^ tserver::kBigSharedMemoryMask);
      }
      fetching_big_data = true;
    }
    lock.unlock();
    if (data_id != tserver::kTooBigResponseMask) {
      big_data_fetcher->FetchBigData(data_id, this);
    }
    if (Traits::AllowNotReady()) {
      return Traits::NotReady();
    }
    lock.lock();
    WaitOnConditionVariable(&exchange_cond, &lock, [this]() NO_THREAD_SAFETY_ANALYSIS {
      return BigDataReady();
    });
    if (!big_call_data.empty()) {
      return Traits::FromBigCallData(&big_call_data);
    } else {
      return Traits::FromStatus(exchange_result->status());
    }
  }

  void BigDataFetched(Result<rpc::CallData>* call_data) {
    std::lock_guard lock(exchange_mutex);
    if (!call_data->ok()) {
      exchange_result = call_data->status();
    } else {
      big_call_data = std::move(**call_data);
    }
    exchange_cond.notify_all();
  }

  Result<rpc::CallResponsePtr> CompletePerform() {
    auto call_data = ResponseReady<Result<rpc::CallData>>();
    RETURN_NOT_OK(call_data);
    auto response = std::make_shared<rpc::CallResponse>();
    RETURN_NOT_OK(response->ParseFrom(&*call_data));
    RETURN_NOT_OK(resp.ParseFromSlice(response->serialized_response()));
    return response;
  }

  Status Process() {
    auto& responses = *resp.mutable_responses();
    SCHECK_EQ(implicit_cast<size_t>(responses.size()), operations.size(), RuntimeError,
              Format("Wrong number of responses: $0, while $1 expected",
                     responses.size(), operations.size()));
    uint32_t i = 0;
    for (auto& op_response : responses) {
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

namespace {

Status DoProcessPerformResponse(PerformData* data, PerformResult* result) {
  RETURN_NOT_OK(ResponseStatus(data->resp));
  RETURN_NOT_OK(data->Process());
  if (data->resp.has_catalog_read_time()) {
    result->catalog_read_time = ReadHybridTime::FromPB(data->resp.catalog_read_time());
  }
  result->used_in_txn_limit = HybridTime::FromPB(data->resp.used_in_txn_limit_ht());
  return Status::OK();
}

PerformResult MakePerformResult(
    PerformData* data, const Result<rpc::CallResponsePtr>& response) {
  PerformResult result;
  if (response.ok()) {
    result.response = *response;
    result.status = DoProcessPerformResponse(data, &result);
  } else {
    result.status = response.status();
  }
  return result;
}

void ProcessPerformResponse(
    PerformData* data, const Result<rpc::CallResponsePtr>& response) {
  data->callback(MakePerformResult(data, response));
}

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

client::VersionedTablePartitionList BuildTablePartitionList(
    const tserver::PgTablePartitionsPB& partitionsPB, const PgObjectId& table_id) {
  client::VersionedTablePartitionList partition_list;
  partition_list.version = partitionsPB.version();
  const auto& keys = partitionsPB.keys();
  if (PREDICT_FALSE(FLAGS_TEST_index_read_multiple_partitions && keys.size() > 1)) {
    // It is required to simulate tablet splitting. This is done by reducing number of partitions.
    // Only middle element is used to split table into 2 partitions.
    // DocDB partition schema like [, 12, 25, 37, 50, 62, 75, 87] will be interpret by YSQL
    // as [, 50].
    partition_list.keys = {PartitionKey(), keys[keys.size() / 2]};
    static auto key_printer = [](const auto& key) { return Slice(key).ToDebugHexString(); };
    LOG(INFO) << "Partitions for " << table_id << " are joined."
              << " source: " << yb::ToString(keys, key_printer)
              << " result: " << yb::ToString(partition_list.keys, key_printer);
  } else {
    partition_list.keys.assign(keys.begin(), keys.end());
  }
  return partition_list;
}

} // namespace

class PgClient::Impl : public BigDataFetcher {
 public:
  Impl(const YbcPgAshConfig& ash_config,
       std::reference_wrapper<const WaitEventWatcher> wait_event_watcher)
    : heartbeat_poller_(std::bind(&Impl::Heartbeat, this, false)),
      ash_config_(ash_config),
      wait_event_watcher_(wait_event_watcher) {
    tablet_server_count_cache_.fill(0);
  }

  ~Impl() {
    CHECK(!proxy_);
  }

  Status Start(rpc::ProxyCache* proxy_cache,
               rpc::Scheduler* scheduler,
               const tserver::TServerSharedObject& tserver_shared_object,
               std::optional<uint64_t> session_id) {
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
    local_cdc_service_proxy_ = std::make_unique<cdc::CDCServiceProxy>(
        proxy_cache, host_port, nullptr /* protocol */, resolve_cache_timeout);

    if (!session_id) {
      auto future = create_session_promise_.get_future();
      Heartbeat(true);
      session_id_ = VERIFY_RESULT(future.get());
    } else {
      session_id_ = *session_id;
    }
    LOG_WITH_PREFIX(INFO) << "Session id acquired. Postgres backend pid: " << getpid();
    heartbeat_poller_.Start(scheduler, FLAGS_pg_client_heartbeat_interval_ms * 1ms);

    memcpy(ash_config_.top_level_node_id, tserver_shared_data_.tserver_uuid(), 16);

    return Status::OK();
  }

  void Shutdown() {
    heartbeat_poller_.Shutdown();
    proxy_ = nullptr;
    local_cdc_service_proxy_ = nullptr;
  }

  uint64_t SessionID() { return session_id_; }

  void Heartbeat(bool create) {
    {
      bool expected = false;
      if (!heartbeat_running_.compare_exchange_strong(expected, true)) {
        LOG_WITH_PREFIX(DFATAL) << "Heartbeat did not complete yet";
        return;
      }
    }
    tserver::PgHeartbeatRequestPB req;
    if (create) {
      req.set_pid(getpid());
    } else {
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
          auto instance_id = heartbeat_resp_.instance_id();
          if (!instance_id.empty()) {
            auto exchange = tserver::SharedExchange::Make(
                instance_id, heartbeat_resp_.session_id(), tserver::Create::kFalse);
            if (exchange.ok()) {
              exchange_.emplace(std::move(*exchange));
            } else {
              LOG(DFATAL) << "Failed to create exchange: " << exchange.status();
            }
          }
          create_session_promise_.set_value(heartbeat_resp_.session_id());
        }
      }
      heartbeat_running_ = false;
      if (!status.ok()) {
        if (status.IsInvalidArgument()) {
          // Unknown session errors are handled as FATALs in the postgres layer. Since the error
          // response of heartbeat RPCs is not propagated to postgres, we handle the error here by
          // shutting down the heartbeat mechanism. Nothing further can be done in this session, and
          // the next user activity will trigger a FATAL anyway. This is done specifically to avoid
          // log spew of the warning message below in cases where the session is idle (ie. no other
          // RPCs are being sent to the tserver).
          LOG(ERROR) << "Heartbeat failed. Connection needs to be reset. "
                     << "Shutting down heartbeating mechanism due to unknown session "
                     << session_id_;
          heartbeat_poller_.Shutdown();
          return;
        }

        LOG_WITH_PREFIX(WARNING) << "Heartbeat failed: " << status;
      }
    });
  }

  void SetTimeout(MonoDelta timeout) {
    timeout_ = timeout + MonoDelta::FromMilliseconds(FLAGS_pg_client_extra_timeout_ms);
  }

  Result<PgTableDescPtr> OpenTable(
      const PgObjectId& table_id, bool reopen, CoarseTimePoint invalidate_cache_time,
      master::IncludeHidden include_hidden) {
    tserver::PgOpenTableRequestPB req;
    req.set_table_id(table_id.GetYbTableId());
    req.set_reopen(reopen);
    if (invalidate_cache_time != CoarseTimePoint()) {
      req.set_invalidate_cache_time_us(ToMicroseconds(invalidate_cache_time.time_since_epoch()));
    }
    req.set_include_hidden(include_hidden);
    tserver::PgOpenTableResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::OpenTable,
        req, resp, ash::PggateRPC::kOpenTable));
    RETURN_NOT_OK(ResponseStatus(resp));

    auto result = make_scoped_refptr<PgTableDesc>(
        table_id, resp.info(), BuildTablePartitionList(resp.partitions(), table_id));
    RETURN_NOT_OK(result->Init());
    return result;
  }

  Result<client::VersionedTablePartitionList> GetTablePartitionList(const PgObjectId& table_id) {
    tserver::PgGetTablePartitionListRequestPB req;
    req.set_table_id(table_id.GetYbTableId());

    tserver::PgGetTablePartitionListResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetTablePartitionList,
        req, resp, ash::PggateRPC::kGetTablePartitionList));
    RETURN_NOT_OK(ResponseStatus(resp));

    return BuildTablePartitionList(resp.partitions(), table_id);
  }

  Result<tserver::PgListClonesResponsePB> ListDatabaseClones() {
    tserver::PgListClonesRequestPB req;
    tserver::PgListClonesResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ListClones,
        req, resp, ash::PggateRPC::kListClones));
    return resp;
  }

  Status FinishTransaction(Commit commit, const std::optional<DdlMode>& ddl_mode) {
    tserver::PgFinishTransactionRequestPB req;
    req.set_session_id(session_id_);
    req.set_commit(commit);
    bool has_docdb_schema_changes = false;
    if (ddl_mode) {
      ddl_mode->ToPB(req.mutable_ddl_mode());
      has_docdb_schema_changes = ddl_mode->has_docdb_schema_changes;
    }
    tserver::PgFinishTransactionResponsePB resp;

    if (PREDICT_FALSE(yb_debug_log_docdb_requests)) {
      LOG_WITH_PREFIX(INFO) << Format("$0$1 transaction",
                                      (commit ? "Committing" : "Aborting"),
                                      (ddl_mode ? " DDL" : ""));
    }

    // If docdb schema changes are present, then this transaction had DDL changes that changed the
    // DocDB schema. In this case FinishTransaction has to wait for any post-processing for these
    // DDLs to complete. Some examples of such post-processing is rolling back any DocDB schema
    // changes in case this transaction was aborted (or) dropping a column/table marked for deletion
    // after commit. Increase the deadline in that case for this operation. FinishTransaction waits
    // for FLAGS_ddl_verification_timeout_multiplier times the normal timeout for this operation,
    // so we have to wait longer than that here.
    auto deadline = !has_docdb_schema_changes ? CoarseTimePoint() :
        CoarseMonoClock::Now() +
            MonoDelta::FromSeconds((FLAGS_ddl_verification_timeout_multiplier + 1) *
                                   FLAGS_yb_client_admin_operation_timeout_sec);
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::FinishTransaction,
        req, resp, ash::PggateRPC::kFinishTransaction, deadline));

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

  Status RollbackToSubTransaction(SubTransactionId id, tserver::PgPerformOptionsPB* options) {
    tserver::PgRollbackToSubTransactionRequestPB req;
    req.set_session_id(session_id_);
    if (options) {
      options->Swap(req.mutable_options());
    }
    req.set_sub_transaction_id(id);

    tserver::PgRollbackToSubTransactionResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::RollbackToSubTransaction,
        req, resp, ash::PggateRPC::kRollbackToSubTransaction));
    return ResponseStatus(resp);
  }

  Status InsertSequenceTuple(int64_t db_oid,
                             int64_t seq_oid,
                             uint64_t ysql_catalog_version,
                             bool is_db_catalog_version_mode,
                             int64_t last_val,
                             bool is_called) {
    tserver::PgInsertSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    if (is_db_catalog_version_mode) {
      DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
      req.set_ysql_db_catalog_version(ysql_catalog_version);
    } else {
      req.set_ysql_catalog_version(ysql_catalog_version);
    }
    req.set_last_val(last_val);
    req.set_is_called(is_called);

    tserver::PgInsertSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::InsertSequenceTuple,
        req, resp, ash::PggateRPC::kInsertSequenceTuple));
    return ResponseStatus(resp);
  }

  Result<bool> UpdateSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   bool is_db_catalog_version_mode,
                                   int64_t last_val,
                                   bool is_called,
                                   std::optional<int64_t> expected_last_val,
                                   std::optional<bool> expected_is_called) {
    tserver::PgUpdateSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    if (is_db_catalog_version_mode) {
      DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
      req.set_ysql_db_catalog_version(ysql_catalog_version);
    } else {
      req.set_ysql_catalog_version(ysql_catalog_version);
    }
    req.set_last_val(last_val);
    req.set_is_called(is_called);
    if (expected_last_val && expected_is_called) {
      req.set_has_expected(true);
      req.set_expected_last_val(*expected_last_val);
      req.set_expected_is_called(*expected_is_called);
    }

    tserver::PgUpdateSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::UpdateSequenceTuple,
        req, resp, ash::PggateRPC::kUpdateSequenceTuple));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.skipped();
  }

  Result<std::pair<int64_t, int64_t>> FetchSequenceTuple(int64_t db_oid,
                                                         int64_t seq_oid,
                                                         uint64_t ysql_catalog_version,
                                                         bool is_db_catalog_version_mode,
                                                         uint32_t fetch_count,
                                                         int64_t inc_by,
                                                         int64_t min_value,
                                                         int64_t max_value,
                                                         bool cycle) {
    tserver::PgFetchSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    if (is_db_catalog_version_mode) {
      DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
      req.set_ysql_db_catalog_version(ysql_catalog_version);
    } else {
      req.set_ysql_catalog_version(ysql_catalog_version);
    }
    req.set_fetch_count(fetch_count);
    req.set_inc_by(inc_by);
    req.set_min_value(min_value);
    req.set_max_value(max_value);
    req.set_cycle(cycle);

    tserver::PgFetchSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::FetchSequenceTuple,
        req, resp, ash::PggateRPC::kFetchSequenceTuple));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::make_pair(resp.first_value(), resp.last_value());
  }

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(
      int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version,
      bool is_db_catalog_version_mode, std::optional<uint64_t> read_time) {
    tserver::PgReadSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    if (is_db_catalog_version_mode) {
      DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
      req.set_ysql_db_catalog_version(ysql_catalog_version);
    } else {
      req.set_ysql_catalog_version(ysql_catalog_version);
    }

    if (read_time) {
      req.set_read_time(*read_time);
    }

    tserver::PgReadSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ReadSequenceTuple,
        req, resp, ash::PggateRPC::kReadSequenceTuple));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::make_pair(resp.last_val(), resp.is_called());
  }

  Status DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
    tserver::PgDeleteSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);

    tserver::PgDeleteSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::DeleteSequenceTuple,
        req, resp, ash::PggateRPC::kDeleteSequenceTuple));
    return ResponseStatus(resp);
  }

  Status DeleteDBSequences(int64_t db_oid) {
    tserver::PgDeleteDBSequencesRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);

    tserver::PgDeleteDBSequencesResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::DeleteDBSequences,
        req, resp, ash::PggateRPC::kDeleteDBSequences));
    return ResponseStatus(resp);
  }

  PerformResultFuture PerformAsync(tserver::PgPerformOptionsPB* options, PgsqlOps&& operations) {
    auto& arena = operations.front()->arena();
    tserver::LWPgPerformRequestPB req(&arena);
    AshMetadataToPB(*options);
    req.set_session_id(session_id_);
    *req.mutable_options() = std::move(*options);
    PrepareOperations(&req, operations);

    auto promise = std::make_shared<std::promise<PerformResult>>();
    auto callback = [promise](const PerformResult& result) {
      promise->set_value(result);
    };

    auto data = std::make_shared<PerformData>(&arena, std::move(operations), callback);
    if (exchange_ && exchange_->ReadyToSend()) {
      constexpr size_t kHeaderSize = sizeof(uint64_t);
      auto out = exchange_->Obtain(kHeaderSize + req.SerializedSize());
      if (out) {
        LittleEndian::Store64(out, timeout_.ToMilliseconds());
        out += sizeof(uint64_t);
        auto status = StartPerform(data.get(), req, out);
        if (!status.ok()) {
          ProcessPerformResponse(data.get(), status);
          return promise->get_future();
        }
        data->SetupExchange(&exchange_.value(), this, timeout_);
        return PerformExchangeFuture(std::move(data));
      }
    }
    data->controller.set_invoke_callback_mode(rpc::InvokeCallbackMode::kReactorThread);

    proxy_->PerformAsync(req, &data->resp, SetupController(&data->controller), [data] {
      ProcessPerformResponse(data.get(), data->controller.CheckedResponse());
    });
    return promise->get_future();
  }

  Status StartPerform(
      PerformData* data, const tserver::LWPgPerformRequestPB& req, std::byte* out) {
    auto size = req.SerializedSize();
    auto* end = pointer_cast<std::byte*>(req.SerializeToArray(pointer_cast<uint8_t*>(out)));
    SCHECK_EQ(end - out, size, InternalError, "Obtained size does not match serialized size");

    return exchange_->SendRequest();
  }

  void FetchBigData(uint64_t data_id, FetchBigDataCallback* callback) override {
    struct FetchBigDataInfo {
      FetchBigDataCallback* callback;
      tserver::LWPgFetchDataRequestPB* fetch_req;
      tserver::LWPgFetchDataResponsePB* fetch_resp;
      rpc::RpcController* controller;
    };
    auto arena = SharedArena();
    auto info = std::shared_ptr<FetchBigDataInfo>(arena, arena->NewObject<FetchBigDataInfo>());
    info->callback = callback;
    info->fetch_req = arena->NewArenaObject<tserver::LWPgFetchDataRequestPB>();
    info->fetch_req->set_session_id(session_id_);
    info->fetch_req->set_data_id(data_id);
    info->fetch_resp = arena->NewArenaObject<tserver::LWPgFetchDataResponsePB>();
    info->controller = arena->NewObject<rpc::RpcController>();
    proxy_->FetchDataAsync(
        *info->fetch_req, info->fetch_resp, SetupController(info->controller), [info]() {
      auto se = ScopeExit([&info] {
        info->controller->~RpcController();
      });
      Result<rpc::CallData> result = MakeFetchBigDataResult(*info);
      info->callback->BigDataFetched(&result);
    });
  }

  Slice FetchBigSharedMemory(uint64_t id, size_t size) override {
    if (id != big_shared_memory_id_) {
      big_mapped_region_ = {};
      big_shared_memory_object_ = {};
      big_shared_memory_object_ = boost::interprocess::shared_memory_object(
          boost::interprocess::open_only,
          tserver::MakeSharedMemoryBigSegmentName(exchange_->instance_id(), id).c_str(),
          boost::interprocess::read_write);
      big_mapped_region_ = boost::interprocess::mapped_region(
          big_shared_memory_object_, boost::interprocess::read_write);
    }
    return Slice(
        static_cast<const char*>(big_mapped_region_.get_address()),
        size + sizeof(std::atomic<bool>));
  }

  void PrepareOperations(tserver::LWPgPerformRequestPB* req, const PgsqlOps& operations) {
    auto& ops = *req->mutable_ops();
    for (const auto& op : operations) {
      auto& union_op = ops.emplace_back();
      if (op->is_read()) {
        auto& read_op = down_cast<PgsqlReadOp&>(*op);
        union_op.ref_read(&read_op.read_request());
      } else {
        auto& write_op = down_cast<PgsqlWriteOp&>(*op);
        if (write_op.write_time()) {
          req->set_write_time(write_op.write_time().ToUint64());
        }
        union_op.ref_write(&write_op.write_request());
      }
    }
  }

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count) {
    tserver::PgReserveOidsRequestPB req;
    req.set_database_oid(database_oid);
    req.set_next_oid(next_oid);
    req.set_count(count);

    tserver::PgReserveOidsResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ReserveOids,
        req, resp, ash::PggateRPC::kReserveOids));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::pair<PgOid, PgOid>(resp.begin_oid(), resp.end_oid());
  }

  Result<PgOid> GetNewObjectId(PgOid db_oid) {
    tserver::PgGetNewObjectIdRequestPB req;
    req.set_db_oid(db_oid);

    tserver::PgGetNewObjectIdResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetNewObjectId,
        req, resp, ash::PggateRPC::kGetNewObjectId));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.new_oid();
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
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::DropTable,
        *req, resp, ash::PggateRPC::kDropTable, deadline));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::YBTableName result;
    if (resp.has_indexed_table()) {
      result.GetFromTableIdentifierPB(resp.indexed_table());
    }
    return result;
  }

  Result<int> WaitForBackendsCatalogVersion(
      tserver::PgWaitForBackendsCatalogVersionRequestPB* req, CoarseTimePoint deadline) {
    tserver::PgWaitForBackendsCatalogVersionResponsePB resp;
    req->set_session_id(session_id_);
    RETURN_NOT_OK(DoSyncRPC(
        &tserver::PgClientServiceProxy::WaitForBackendsCatalogVersion,
        *req, resp, ash::PggateRPC::kWaitForBackendsCatalogVersion, deadline));
    RETURN_NOT_OK(ResponseStatus(resp));
    if (resp.num_lagging_backends() != -1) {
      return resp.num_lagging_backends();
    }
    return STATUS_FORMAT(
        TryAgain,
        "Counting backends in progress: database oid $0, catalog version $1",
        req->database_oid(), req->catalog_version());
  }

  Status BackfillIndex(
      tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline) {
    tserver::PgBackfillIndexResponsePB resp;
    req->set_session_id(session_id_);

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::BackfillIndex,
        *req, resp, ash::PggateRPC::kBackfillIndex, deadline));
    return ResponseStatus(resp);
  }

  Status GetIndexBackfillProgress(const std::vector<PgObjectId>& index_ids,
                                uint64_t** backfill_statuses) {
    tserver::PgGetIndexBackfillProgressRequestPB req;
    tserver::PgGetIndexBackfillProgressResponsePB resp;

    for (const auto& index_id : index_ids) {
      index_id.ToPB(req.add_index_ids());
    }

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetIndexBackfillProgress,
        req, resp, ash::PggateRPC::kGetIndexBackfillProgress));
    RETURN_NOT_OK(ResponseStatus(resp));
    uint64_t* backfill_status = *backfill_statuses;
    for (const auto entry : resp.rows_processed_entries()) {
      *backfill_status = entry;
      backfill_status++;
    }
    return Status::OK();
  }

  Result<yb::tserver::PgGetLockStatusResponsePB> GetLockStatusData(
      const std::string& table_id, const std::string& transaction_id) {
    tserver::PgGetLockStatusRequestPB req;
    tserver::PgGetLockStatusResponsePB resp;

    if (!table_id.empty()) {
      req.set_table_id(table_id);
    }
    if (!transaction_id.empty()) {
      req.set_transaction_id(transaction_id);
    }
    req.set_min_txn_age_ms(yb_locks_min_txn_age);
    req.set_max_num_txns(yb_locks_max_transactions);
    req.set_max_txn_locks_per_tablet(yb_locks_txn_locks_per_tablet);

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetLockStatus,
        req, resp, ash::PggateRPC::kGetLockStatus));
    RETURN_NOT_OK(ResponseStatus(resp));

    return resp;
  }

  Result<int32> TabletServerCount(bool primary_only) {
    if (tablet_server_count_cache_[primary_only] > 0) {
      return tablet_server_count_cache_[primary_only];
    }
    tserver::PgTabletServerCountRequestPB req;
    tserver::PgTabletServerCountResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::TabletServerCount,
        req, resp, ash::PggateRPC::kTabletServerCount));
    RETURN_NOT_OK(ResponseStatus(resp));
    tablet_server_count_cache_[primary_only] = resp.count();
    return resp.count();
  }

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only) {
    tserver::PgListLiveTabletServersRequestPB req;
    tserver::PgListLiveTabletServersResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ListLiveTabletServers,
        req, resp, ash::PggateRPC::kListLiveTabletServers));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::TabletServersInfo result;
    result.reserve(resp.servers().size());
    for (const auto& server : resp.servers()) {
      result.push_back(client::YBTabletServerPlacementInfo::FromPB(server));
    }
    return result;
  }

  Status ValidatePlacement(tserver::PgValidatePlacementRequestPB* req) {
    tserver::PgValidatePlacementResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ValidatePlacement,
        *req, resp, ash::PggateRPC::kValidatePlacement));
    return ResponseStatus(resp);
  }

  Result<client::TableSizeInfo> GetTableDiskSize(
      const PgObjectId& table_oid) {
    tserver::PgGetTableDiskSizeResponsePB resp;

    tserver::PgGetTableDiskSizeRequestPB req;
    table_oid.ToPB(req.mutable_table_id());

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetTableDiskSize,
        req, resp, ash::PggateRPC::kGetTableDiskSize));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }

    return client::TableSizeInfo{resp.size(), resp.num_missing_tablets()};
  }

  Result<bool> CheckIfPitrActive() {
    tserver::PgCheckIfPitrActiveRequestPB req;
    tserver::PgCheckIfPitrActiveResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::CheckIfPitrActive,
        req, resp, ash::PggateRPC::kCheckIfPitrActive));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp.is_pitr_active();
  }

  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id) {
    tserver::PgIsObjectPartOfXReplRequestPB req;
    tserver::PgIsObjectPartOfXReplResponsePB resp;
    table_id.ToPB(req.mutable_table_id());
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::IsObjectPartOfXRepl,
        req, resp, ash::PggateRPC::kIsObjectPartOfXRepl));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp.is_object_part_of_xrepl();
  }

  Status EnumerateActiveTransactions(
      const ActiveTransactionCallback& callback, bool for_current_session_only) {
    tserver::PgGetActiveTransactionListRequestPB req;
    tserver::PgGetActiveTransactionListResponsePB resp;
    if (for_current_session_only) {
      req.mutable_session_id()->set_value(session_id_);
    }

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetActiveTransactionList,
        req, resp, ash::PggateRPC::kGetActiveTransactionList));
    RETURN_NOT_OK(ResponseStatus(resp));

    const auto& entries = resp.entries();
    if (entries.empty()) {
      return Status::OK();
    }

    for (auto i = entries.begin(), end = entries.end();;) {
      const auto& entry = *i;
      const auto is_last = (++i == end);
      RETURN_NOT_OK(callback(entry, is_last));
      if (is_last) {
        break;
      }
    }

    return Status::OK();
  }

  Result<TableKeyRanges> GetTableKeyRanges(
      const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
      uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward,
      uint32_t max_key_length) {
    tserver::PgGetTableKeyRangesRequestPB req;
    tserver::PgGetTableKeyRangesResponsePB resp;
    req.set_session_id(session_id_);
    table_id.ToPB(req.mutable_table_id());
    if (!lower_bound_key.empty()) {
      req.mutable_lower_bound_key()->assign(lower_bound_key.cdata(), lower_bound_key.size());
    }
    if (!upper_bound_key.empty()) {
      req.mutable_upper_bound_key()->assign(upper_bound_key.cdata(), upper_bound_key.size());
    }
    req.set_max_num_ranges(max_num_ranges);
    req.set_range_size_bytes(range_size_bytes);
    req.set_is_forward(is_forward);
    req.set_max_key_length(max_key_length);

    auto* controller = PrepareController();

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetTableKeyRanges,
        req, resp, ash::PggateRPC::kGetTableKeyRanges, controller));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }

    TableKeyRanges result;
    result.reserve(controller->GetSidecarsCount());
    for (size_t i = 0; i < controller->GetSidecarsCount(); ++i) {
      result.push_back(VERIFY_RESULT(controller->ExtractSidecar(i)));
    }
    return result;
  }

  Result<tserver::PgGetTserverCatalogVersionInfoResponsePB> GetTserverCatalogVersionInfo(
      bool size_only, uint32_t db_oid) {
    tserver::PgGetTserverCatalogVersionInfoRequestPB req;
    tserver::PgGetTserverCatalogVersionInfoResponsePB resp;
    req.set_size_only(size_only);
    req.set_db_oid(db_oid);
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetTserverCatalogVersionInfo,
        req, resp, ash::PggateRPC::kGetTserverCatalogVersionInfo));
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
    auto status = DoSyncRPC(&tserver::PgClientServiceProxy::method, \
        *req, resp, BOOST_PP_CAT(ash::PggateRPC::k, method), deadline); \
    if (!status.ok()) { \
      if (status.IsTimedOut()) { \
        return STATUS_FORMAT(TimedOut, "Timed out waiting for $0", PrettyFunctionName(__func__)); \
      } \
      return status; \
    } \
    return ResponseStatus(resp); \
  }

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_IMPL, ~, YB_PG_CLIENT_SIMPLE_METHODS);

  Status CancelTransaction(const unsigned char* transaction_id) {
    tserver::PgCancelTransactionRequestPB req;
    req.set_transaction_id(transaction_id, kUuidSize);
    tserver::PgCancelTransactionResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::CancelTransaction,
        req, resp, ash::PggateRPC::kCancelTransaction));
    return ResponseStatus(resp);
  }

  Result<tserver::PgCreateReplicationSlotResponsePB> CreateReplicationSlot(
      tserver::PgCreateReplicationSlotRequestPB* req, CoarseTimePoint deadline) {
    tserver::PgCreateReplicationSlotResponsePB resp;
    req->set_session_id(session_id_);
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::CreateReplicationSlot,
        *req, resp, ash::PggateRPC::kCreateReplicationSlot, deadline));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgListReplicationSlotsResponsePB> ListReplicationSlots() {
    tserver::PgListReplicationSlotsRequestPB req;
    tserver::PgListReplicationSlotsResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ListReplicationSlots,
        req, resp, ash::PggateRPC::kListReplicationSlots));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgGetReplicationSlotResponsePB> GetReplicationSlot(
      const ReplicationSlotName& slot_name) {
    tserver::PgGetReplicationSlotRequestPB req;
    req.set_replication_slot_name(slot_name.ToString());

    tserver::PgGetReplicationSlotResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::GetReplicationSlot,
        req, resp, ash::PggateRPC::kGetReplicationSlot));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<std::string> ExportTxnSnapshot(tserver::PgExportTxnSnapshotRequestPB* req) {
    tserver::PgExportTxnSnapshotResponsePB resp;
    req->set_session_id(session_id_);
    RETURN_NOT_OK(DoSyncRPC(
        &tserver::PgClientServiceProxy::ExportTxnSnapshot, *req, resp,
        ash::PggateRPC::kExportTxnSnapshot));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.snapshot_id();
  }

  Result<tserver::PgImportTxnSnapshotResponsePB> ImportTxnSnapshot(
      std::string_view snapshot_id, tserver::PgPerformOptionsPB&& options) {
    tserver::PgImportTxnSnapshotRequestPB req;
    req.set_session_id(session_id_);
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.length());
    *req.mutable_options() = std::move(options);

    tserver::PgImportTxnSnapshotResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(
        &tserver::PgClientServiceProxy::ImportTxnSnapshot, req, resp,
        ash::PggateRPC::kImportTxnSnapshot));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgActiveSessionHistoryResponsePB> ActiveSessionHistory() {
    tserver::PgActiveSessionHistoryRequestPB req;
    req.set_fetch_tserver_states(true);
    req.set_fetch_raft_log_appender_states(FLAGS_TEST_ash_fetch_wait_states_for_raft_log);
    req.set_fetch_flush_and_compaction_states(
        FLAGS_TEST_ash_fetch_wait_states_for_rocksdb_flush_and_compaction);
    req.set_fetch_cql_states(true);
    req.set_ignore_ash_and_perform_calls(true);
    req.set_export_wait_state_code_as_string(FLAGS_TEST_export_wait_state_names);
    req.set_sample_size(FLAGS_ysql_yb_ash_sample_size);
    tserver::PgActiveSessionHistoryResponsePB resp;

    RETURN_NOT_OK(proxy_->ActiveSessionHistory(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgYCQLStatementStatsResponsePB> YCQLStatementStats() {
    tserver::PgYCQLStatementStatsRequestPB req;
    tserver::PgYCQLStatementStatsResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::YCQLStatementStats,
        req, resp, ash::PggateRPC::kYCQLStatementStats));
    return resp;
  }

  Result<cdc::InitVirtualWALForCDCResponsePB> InitVirtualWALForCDC(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
    cdc::InitVirtualWALForCDCRequestPB req;

    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);
    for (const auto& table_id : table_ids) {
      *req.add_table_id() = table_id.GetYbTableId();
    }

    cdc::InitVirtualWALForCDCResponsePB resp;
    RETURN_NOT_OK(local_cdc_service_proxy_->InitVirtualWALForCDC(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::UpdatePublicationTableListResponsePB> UpdatePublicationTableList(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
    cdc::UpdatePublicationTableListRequestPB req;
    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);
    for (const auto& table_id : table_ids) {
      *req.add_table_id() = table_id.GetYbTableId();
    }

    cdc::UpdatePublicationTableListResponsePB resp;
    RETURN_NOT_OK(
        local_cdc_service_proxy_->UpdatePublicationTableList(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::DestroyVirtualWALForCDCResponsePB> DestroyVirtualWALForCDC() {
    cdc::DestroyVirtualWALForCDCRequestPB req;
    req.set_session_id(session_id_);

    cdc::DestroyVirtualWALForCDCResponsePB resp;
    RETURN_NOT_OK(
        local_cdc_service_proxy_->DestroyVirtualWALForCDC(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::GetConsistentChangesResponsePB> GetConsistentChangesForCDC(
      const std::string& stream_id) {
    cdc::GetConsistentChangesRequestPB req;
    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);

    cdc::GetConsistentChangesResponsePB resp;
    RETURN_NOT_OK(local_cdc_service_proxy_->GetConsistentChanges(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::UpdateAndPersistLSNResponsePB> UpdateAndPersistLSN(
    const std::string& stream_id, YbcPgXLogRecPtr restart_lsn, YbcPgXLogRecPtr confirmed_flush) {
    cdc::UpdateAndPersistLSNRequestPB req;
    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);
    req.set_restart_lsn(restart_lsn);
    req.set_confirmed_flush_lsn(confirmed_flush);

    cdc::UpdateAndPersistLSNResponsePB resp;
    RETURN_NOT_OK(
        local_cdc_service_proxy_->UpdateAndPersistLSN(req, &resp, PrepareController()));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgTabletsMetadataResponsePB> TabletsMetadata() {
    tserver::PgTabletsMetadataRequestPB req;
    tserver::PgTabletsMetadataResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::TabletsMetadata,
        req, resp, ash::PggateRPC::kTabletsMetadata));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgServersMetricsResponsePB> ServersMetrics() {
    tserver::PgServersMetricsRequestPB req;
    tserver::PgServersMetricsResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::ServersMetrics,
        req, resp, ash::PggateRPC::kServersMetrics));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Status SetCronLastMinute(int64_t last_minute) {
    tserver::PgCronSetLastMinuteRequestPB req;
    req.set_last_minute(last_minute);
    tserver::PgCronSetLastMinuteResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::CronSetLastMinute,
        req, resp, ash::PggateRPC::kCronSetLastMinute));
    return ResponseStatus(resp);
  }

  Result<int64_t> GetCronLastMinute() {
    tserver::PgCronGetLastMinuteRequestPB req;
    tserver::PgCronGetLastMinuteResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&tserver::PgClientServiceProxy::CronGetLastMinute,
        req, resp, ash::PggateRPC::kCronGetLastMinute));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.last_minute();
  }

 private:
  std::string LogPrefix() const {
    return Format("Session id $0: ", session_id_);
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

  template <class RequestPB>
  void AshMetadataToPB(RequestPB& req) {
    if (!(*ash_config_.yb_enable_ash)) {
      return;
    }

    auto& ash_metadata = *req.mutable_ash_metadata();
    const auto& pg_metadata = *ash_config_.metadata;
    ash_metadata.set_top_level_node_id(ash_config_.top_level_node_id, 16);
    ash_metadata.set_root_request_id(pg_metadata.root_request_id, 16);
    ash_metadata.set_query_id(pg_metadata.query_id);
    ash_metadata.set_pid(pg_metadata.pid);
    ash_metadata.set_database_id(pg_metadata.database_id);

    const auto& addr_family = pg_metadata.addr_family;
    ash_metadata.set_addr_family(addr_family);
    // unix addresses are displayed as null, so we only send IPv4 and IPv6 addresses.
    if (addr_family == AF_INET || addr_family == AF_INET6) {
      ash_metadata.mutable_client_host_port()->set_host(ash_config_.host);
      ash_metadata.mutable_client_host_port()->set_port(pg_metadata.client_port);
    }
  }

  template <class Req, class Resp>
  using SyncRPCFunc = Status (tserver::PgClientServiceProxy::*)(
      const Req&, Resp*, rpc::RpcController*) const;

  template <class Req, class Resp>
  Status DoSyncRPC(
      SyncRPCFunc<Req, Resp> func, Req& req, Resp& resp,
      ash::PggateRPC rpc_enum, CoarseTimePoint deadline = CoarseTimePoint()) {
    return DoSyncRPC(func, req, resp, rpc_enum, PrepareController(deadline));
  }

  template <class Req, class Resp>
  Status DoSyncRPC(
      SyncRPCFunc<Req, Resp> func, Req& req, Resp& resp,
      ash::PggateRPC rpc_enum, rpc::RpcController* controller) {
    AshMetadataToPB(req);
    auto watcher = wait_event_watcher_(ash::WaitStateCode::kWaitingOnTServer, rpc_enum);
    return (proxy_.get()->*func)(req, &resp, controller);
  }

  std::unique_ptr<tserver::PgClientServiceProxy> proxy_;
  std::unique_ptr<cdc::CDCServiceProxy> local_cdc_service_proxy_;

  rpc::RpcController controller_;
  uint64_t session_id_ = 0;

  rpc::Poller heartbeat_poller_;
  std::atomic<bool> heartbeat_running_{false};
  rpc::RpcController heartbeat_controller_;
  tserver::PgHeartbeatResponsePB heartbeat_resp_;
  std::optional<tserver::SharedExchange> exchange_;
  std::promise<Result<uint64_t>> create_session_promise_;
  std::array<int, 2> tablet_server_count_cache_;
  MonoDelta timeout_ = FLAGS_yb_client_admin_operation_timeout_sec * 1s;

  YbcPgAshConfig ash_config_;
  const WaitEventWatcher& wait_event_watcher_;

  uint64_t big_shared_memory_id_;
  boost::interprocess::shared_memory_object big_shared_memory_object_;
  boost::interprocess::mapped_region big_mapped_region_;
};

std::string DdlMode::ToString() const {
  return YB_STRUCT_TO_STRING(has_docdb_schema_changes, silently_altered_db);
}

void DdlMode::ToPB(tserver::PgFinishTransactionRequestPB_DdlModePB* dest) const {
  dest->set_has_docdb_schema_changes(has_docdb_schema_changes);
  if (silently_altered_db) {
    dest->mutable_silently_altered_db()->set_value(*silently_altered_db);
  }
}

PgClient::PgClient(const YbcPgAshConfig& ash_config,
                   std::reference_wrapper<const WaitEventWatcher> wait_event_watcher)
    : impl_(new Impl(ash_config, wait_event_watcher)) {}

PgClient::~PgClient() = default;

Status PgClient::Start(
    rpc::ProxyCache* proxy_cache, rpc::Scheduler* scheduler,
    const tserver::TServerSharedObject& tserver_shared_object,
    std::optional<uint64_t> session_id) {
  return impl_->Start(proxy_cache, scheduler, tserver_shared_object, session_id);
}

void PgClient::Shutdown() {
  impl_->Shutdown();
}

void PgClient::SetTimeout(MonoDelta timeout) {
  impl_->SetTimeout(timeout);
}

uint64_t PgClient::SessionID() const { return impl_->SessionID(); }

Result<PgTableDescPtr> PgClient::OpenTable(
    const PgObjectId& table_id, bool reopen, CoarseTimePoint invalidate_cache_time,
    master::IncludeHidden include_hidden) {
  return impl_->OpenTable(table_id, reopen, invalidate_cache_time, include_hidden);
}

Result<client::VersionedTablePartitionList> PgClient::GetTablePartitionList(
    const PgObjectId& table_id) {
  return impl_->GetTablePartitionList(table_id);
}

Status PgClient::FinishTransaction(Commit commit, const std::optional<DdlMode>& ddl_mode) {
  return impl_->FinishTransaction(commit, ddl_mode);
}

Result<tserver::PgListClonesResponsePB> PgClient::ListDatabaseClones() {
  return impl_->ListDatabaseClones();
}

Result<master::GetNamespaceInfoResponsePB> PgClient::GetDatabaseInfo(uint32_t oid) {
  return impl_->GetDatabaseInfo(oid);
}

Result<std::pair<PgOid, PgOid>> PgClient::ReserveOids(
    PgOid database_oid, PgOid next_oid, uint32_t count) {
  return impl_->ReserveOids(database_oid, next_oid, count);
}

Result<PgOid> PgClient::GetNewObjectId(PgOid db_oid) {
  return impl_->GetNewObjectId(db_oid);
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

Result<int> PgClient::WaitForBackendsCatalogVersion(
    tserver::PgWaitForBackendsCatalogVersionRequestPB* req, CoarseTimePoint deadline) {
  return impl_->WaitForBackendsCatalogVersion(req, deadline);
}

Status PgClient::BackfillIndex(
    tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline) {
  return impl_->BackfillIndex(req, deadline);
}

Status PgClient::GetIndexBackfillProgress(
    const std::vector<PgObjectId>& index_ids,
    uint64_t** backfill_statuses) {
  return impl_->GetIndexBackfillProgress(index_ids, backfill_statuses);
}

Result<yb::tserver::PgGetLockStatusResponsePB> PgClient::GetLockStatusData(
    const std::string& table_id, const std::string& transaction_id) {
  return impl_->GetLockStatusData(table_id, transaction_id);
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

Status PgClient::RollbackToSubTransaction(
    SubTransactionId id, tserver::PgPerformOptionsPB* options) {
  return impl_->RollbackToSubTransaction(id, options);
}

Status PgClient::ValidatePlacement(tserver::PgValidatePlacementRequestPB* req) {
  return impl_->ValidatePlacement(req);
}

Result<client::TableSizeInfo> PgClient::GetTableDiskSize(
    const PgObjectId& table_oid) {
  return impl_->GetTableDiskSize(table_oid);
}

Status PgClient::InsertSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     bool is_db_catalog_version_mode,
                                     int64_t last_val,
                                     bool is_called) {
  return impl_->InsertSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called);
}

Result<bool> PgClient::UpdateSequenceTuple(int64_t db_oid,
                                           int64_t seq_oid,
                                           uint64_t ysql_catalog_version,
                                           bool is_db_catalog_version_mode,
                                           int64_t last_val,
                                           bool is_called,
                                           std::optional<int64_t> expected_last_val,
                                           std::optional<bool> expected_is_called) {
  return impl_->UpdateSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, last_val, is_called,
      expected_last_val, expected_is_called);
}

Result<std::pair<int64_t, int64_t>> PgClient::FetchSequenceTuple(int64_t db_oid,
                                                                 int64_t seq_oid,
                                                                 uint64_t ysql_catalog_version,
                                                                 bool is_db_catalog_version_mode,
                                                                 uint32_t fetch_count,
                                                                 int64_t inc_by,
                                                                 int64_t min_value,
                                                                 int64_t max_value,
                                                                 bool cycle) {
  return impl_->FetchSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, fetch_count, inc_by,
      min_value, max_value, cycle);
}

Result<std::pair<int64_t, bool>> PgClient::ReadSequenceTuple(
    int64_t db_oid, int64_t seq_oid, uint64_t ysql_catalog_version, bool is_db_catalog_version_mode,
    std::optional<uint64_t> read_time) {
  return impl_->ReadSequenceTuple(
      db_oid, seq_oid, ysql_catalog_version, is_db_catalog_version_mode, read_time);
}

Status PgClient::DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
  return impl_->DeleteSequenceTuple(db_oid, seq_oid);
}

Status PgClient::DeleteDBSequences(int64_t db_oid) {
  return impl_->DeleteDBSequences(db_oid);
}

PerformResultFuture PgClient::PerformAsync(
    tserver::PgPerformOptionsPB* options, PgsqlOps&& operations) {
  return impl_->PerformAsync(options, std::move(operations));
}

Result<bool> PgClient::CheckIfPitrActive() {
  return impl_->CheckIfPitrActive();
}

Result<bool> PgClient::IsObjectPartOfXRepl(const PgObjectId& table_id) {
  return impl_->IsObjectPartOfXRepl(table_id);
}

Result<TableKeyRanges> PgClient::GetTableKeyRanges(
    const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length) {
  return impl_->GetTableKeyRanges(
      table_id, lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes, is_forward,
      max_key_length);
}

Result<tserver::PgGetTserverCatalogVersionInfoResponsePB> PgClient::GetTserverCatalogVersionInfo(
    bool size_only, uint32_t db_oid) {
  return impl_->GetTserverCatalogVersionInfo(size_only, db_oid);
}

Status PgClient::EnumerateActiveTransactions(
    const ActiveTransactionCallback& callback, bool for_current_session_only) const {
  return impl_->EnumerateActiveTransactions(callback, for_current_session_only);
}

#define YB_PG_CLIENT_SIMPLE_METHOD_DEFINE(r, data, method) \
Status PgClient::method( \
    tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
    CoarseTimePoint deadline) { \
  return impl_->method(req, deadline); \
}

BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_DEFINE, ~, YB_PG_CLIENT_SIMPLE_METHODS);

Status PgClient::CancelTransaction(const unsigned char* transaction_id) {
  return impl_->CancelTransaction(transaction_id);
}

Result<tserver::PgCreateReplicationSlotResponsePB> PgClient::CreateReplicationSlot(
    tserver::PgCreateReplicationSlotRequestPB* req, CoarseTimePoint deadline) {
  return impl_->CreateReplicationSlot(req, deadline);
}

Result<tserver::PgListReplicationSlotsResponsePB> PgClient::ListReplicationSlots() {
  return impl_->ListReplicationSlots();
}

Result<tserver::PgGetReplicationSlotResponsePB> PgClient::GetReplicationSlot(
    const ReplicationSlotName& slot_name) {
  return impl_->GetReplicationSlot(slot_name);
}

Result<std::string> PgClient::ExportTxnSnapshot(tserver::PgExportTxnSnapshotRequestPB* req) {
  return impl_->ExportTxnSnapshot(req);
}

Result<tserver::PgImportTxnSnapshotResponsePB> PgClient::ImportTxnSnapshot(
    std::string_view snapshot_id, tserver::PgPerformOptionsPB&& options) {
  return impl_->ImportTxnSnapshot(snapshot_id, std::move(options));
}

Result<tserver::PgActiveSessionHistoryResponsePB> PgClient::ActiveSessionHistory() {
  return impl_->ActiveSessionHistory();
}

Result<tserver::PgYCQLStatementStatsResponsePB> PgClient::YCQLStatementStats() {
  return impl_->YCQLStatementStats();
}

Result<cdc::InitVirtualWALForCDCResponsePB> PgClient::InitVirtualWALForCDC(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
  return impl_->InitVirtualWALForCDC(stream_id, table_ids);
}

Result<cdc::UpdatePublicationTableListResponsePB> PgClient::UpdatePublicationTableList(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids) {
  return impl_->UpdatePublicationTableList(stream_id, table_ids);
}

Result<cdc::DestroyVirtualWALForCDCResponsePB> PgClient::DestroyVirtualWALForCDC() {
  return impl_->DestroyVirtualWALForCDC();
}

Result<cdc::GetConsistentChangesResponsePB> PgClient::GetConsistentChangesForCDC(
    const std::string& stream_id) {
  return impl_->GetConsistentChangesForCDC(stream_id);
}

Result<cdc::UpdateAndPersistLSNResponsePB> PgClient::UpdateAndPersistLSN(
    const std::string& stream_id, YbcPgXLogRecPtr restart_lsn, YbcPgXLogRecPtr confirmed_flush) {
  return impl_->UpdateAndPersistLSN(stream_id, restart_lsn, confirmed_flush);
}

Result<tserver::PgTabletsMetadataResponsePB> PgClient::TabletsMetadata() {
  return impl_->TabletsMetadata();
}

Result<tserver::PgServersMetricsResponsePB> PgClient::ServersMetrics() {
  return impl_->ServersMetrics();
}

Status PgClient::SetCronLastMinute(int64_t last_minute) {
  return impl_->SetCronLastMinute(last_minute);
}

Result<int64_t> PgClient::GetCronLastMinute() { return impl_->GetCronLastMinute(); }

void PerformExchangeFuture::wait() const {
  if (!value_) {
    value_ = MakePerformResult(data_.get(), data_->CompletePerform());
  }
}

bool PerformExchangeFuture::ready() const {
  return data_->ResponseReady<bool>();
}

PerformResult PerformExchangeFuture::get() {
  wait();
  data_.reset();
  return *value_;
}

void Wait(const PerformResultFuture& future) {
  std::visit([](const auto& future) {
    future.wait();
  }, future);
}

bool Ready(const std::future<PerformResult>& future) {
  return future.wait_for(std::chrono::microseconds(0)) == std::future_status::ready;
}

bool Ready(const PerformExchangeFuture& future) {
  return future.ready();
}

bool Ready(const PerformResultFuture& future) {
  return std::visit([](const auto& future) {
    return Ready(future);
  }, future);
}

bool Valid(const PerformResultFuture& future) {
  return std::visit([](const auto& future) {
    return future.valid();
  }, future);
}

PerformResult Get(PerformResultFuture* future) {
  return std::visit([](auto& future) {
    return future.get();
  }, *future);
}

}  // namespace yb::pggate
