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

#include "yb/yql/pggate/pg_client.h"

#include "yb/ash/rpc_wait_state.h"

#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client-internal.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/docdb/object_lock_shared_state.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/call_data.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/poller.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/pg_client.messages.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/flag_validators.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_shared_mem.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/ybc_guc.h"
#include "yb/yql/pggate/ybc_pggate.h"

DECLARE_bool(enable_object_lock_fastpath);
DECLARE_bool(use_node_hostname_for_local_tserver);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_uint32(ddl_verification_timeout_multiplier);

DEFINE_UNKNOWN_uint64(pg_client_heartbeat_interval_ms, 10000,
    "Pg client heartbeat interval in ms. Needs to be greater than 1000ms.");
DEFINE_validator(pg_client_heartbeat_interval_ms, FLAG_GT_VALUE_VALIDATOR(1000));

DEFINE_NON_RUNTIME_int32(pg_client_extra_timeout_ms, 2000,
   "Adding this value to RPC call timeout, so postgres could detect timeout by it's own mechanism "
   "and report it.");

DEFINE_test_flag(bool, emulate_op_lost_on_write, false,
    "Emulate lost of operation with serial_no by incrementing counter twice.");

DECLARE_bool(TEST_index_read_multiple_partitions);
DECLARE_bool(TEST_ash_fetch_wait_states_for_raft_log);
DECLARE_bool(TEST_ash_fetch_wait_states_for_rocksdb_flush_and_compaction);
DECLARE_bool(TEST_export_wait_state_names);
DECLARE_bool(ysql_enable_db_catalog_version_mode);
DECLARE_int32(ysql_yb_ash_sample_size);
DECLARE_bool(ysql_yb_enable_consistent_replication_from_hash_range);
DECLARE_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication);
DECLARE_bool(TEST_enable_table_rewrite_for_cdcsdk_table);

extern int yb_locks_min_txn_age;
extern int yb_locks_max_transactions;
extern int yb_locks_txn_locks_per_tablet;

using namespace std::literals;
using yb::tserver::PgClientServiceProxy;
using yb::cdc::CDCServiceProxy;
using yb::ash::PggateRPC;

namespace yb::pggate {

namespace {
// The following two terms are used to express how long an operation can take to complete:
// - Timeout: the maximum duration of time that an operation is allowed to run for.
//            For example: '30s'. Timeouts are relative.
// - Deadline: the fixed point in time by which an operation must complete.
//            For example: '13450s since epoch'. Deadlines are absolute.
//
// Pggate encounters the following classes of timeouts/deadlines:
// 1. Global timeout/deadline (from postgres): GUCs such as 'statement_timeout',
//    'transaction_timeout' (PG 17+) represent a timeout for a group of RPCs. These timeouts are
//    expressed as deadlines in pggate. These deadlines are enforced by postgres' timer mechanism
//    since pggate does not know how many RPCs the given statement/query will generate.
// 2. RPC timeout (from pggate): This represents how long a specific RPC is allowed to take. If the
//    deadline for the RPC exceeds the global deadline, the RPC timeout is clamped to the global
//    deadline.
// 3. Operation timeout (from postgres): GUCs such as 'lock_timeout', 'deadlock_timeout' represent
//    timeouts for specific operations. Each RPC may be composed of multiple operations. While
//    global and RPC timeouts are client-side timeouts (ie. enforced by pggate::PgClient), operation
//    timeouts must be implemented on the server side (ie. enforced by the tservers). Consequently,
//    operation timeouts must be communicated to the server as part of the RPC.
//
// There exists another class of timeouts, which does not affect pggate:
// 4. Idle timeout (from postgres): GUCs such as 'idle_in_transaction_session_timeout',
//    'idle_session_timeout' represent how long a session can be idle. These timeouts are enforced
//    in postgres and are not propagated to pggate.
class PgTimeout {
 public:
  void SetPGTimeoutAsGlobalDeadline(int timeout_ms) {
    const MonoDelta adjusted_timeout =
        MonoDelta::FromMilliseconds(timeout_ms) +
        MonoDelta::FromMilliseconds(FLAGS_pg_client_extra_timeout_ms);

    global_deadline_ = CoarseMonoClock::now() + adjusted_timeout;
  }

  void ClearGlobalDeadline() {
    global_deadline_ = CoarseTimePoint();
  }

  void SetLockTimeout(int timeout_ms) {
    if (timeout_ms <= 0) {
      // TODO(kramanathan): When disabled by PG, evaluate whether the lock timeout should be set to
      // a default value.
      lock_timeout_ = GetDefaultRpcTimeout();
      return;
    }

    lock_timeout_ =
        MonoDelta::FromMilliseconds(timeout_ms) +
        MonoDelta::FromMilliseconds(FLAGS_pg_client_extra_timeout_ms);
  }

  // TODO: Lock timeouts are the only operation timeouts currently supported and are implemented as
  // both client-side (RPC timeout) and server-side timeouts (tserver RPC timeout). They instead
  // need to be a part of the RPC message (since each RPC may have multiple operations, each of
  // which may have its own timeout).
  template<typename T>
  std::pair<CoarseTimePoint, MonoDelta> GetDeadlineAndTimeoutForRPC(
      CoarseTimePoint rpc_deadline = CoarseTimePoint()) const {

    const CoarseTimePoint now = CoarseMonoClock::now();
    const MonoDelta operation_timeout = GetOperationTimeout<T>();

    // Prioritize the minimum of RPC and global deadline, if supplied.
    CoarseTimePoint deadline = MinDeadline(rpc_deadline, global_deadline_);

    // If an RPC is being scheduled while we're in the grace period, it is likely that the
    // statement timer has already fired in postgres. Therefore check for interrupts.
    if (PREDICT_FALSE(
        global_deadline_ != CoarseTimePoint() &&
        global_deadline_ - MonoDelta::FromMilliseconds(FLAGS_pg_client_extra_timeout_ms) < now)) {
      YBCCheckForInterrupts();
    }

    // Assert that the global deadline is not in the past.
    DCHECK(deadline == CoarseTimePoint() || now < deadline);

    // If an operation timeout is applicable, apply it to further clamp the deadline.
    if (operation_timeout.Initialized()) {
      deadline = MinDeadline(deadline, now + operation_timeout);
    }

    // If none of the timeouts/deadlines are applicable, set it to a default value.
    if (deadline == CoarseTimePoint()) {
      deadline = now + GetDefaultRpcTimeout();
    }

    return std::make_pair(deadline, deadline - now);
  }

 private:
  template<typename T>
  MonoDelta GetOperationTimeout() const {
    if constexpr (std::is_same_v<T, tserver::PgAcquireAdvisoryLockRequestPB> ||
                  std::is_same_v<T, tserver::LWPgAcquireObjectLockRequestPB>) {
      return lock_timeout_;
    }

    return MonoDelta();
  }

  static MonoDelta GetDefaultRpcTimeout() {
    return MonoDelta::FromMilliseconds(client::YsqlClientReadWriteTimeoutMs()) +
        MonoDelta::FromMilliseconds(FLAGS_pg_client_extra_timeout_ms);
  }

  const CoarseTimePoint& MinDeadline(const CoarseTimePoint& a, const CoarseTimePoint& b) const {
    if (a == CoarseTimePoint()) {
      return b;
    }
    if (b == CoarseTimePoint()) {
      return a;
    }

    return a < b ? a : b;
  }

  // The deadline representing statement timeout in postgres.
  // Unset if statement_timeout is set to 0 (ie. no timeout).
  // This deadline is not applicable to heartbeats.
  CoarseTimePoint global_deadline_;
  // The timeout representing lock_timeout in postgres.
  MonoDelta lock_timeout_ = FLAGS_yb_client_admin_operation_timeout_sec * 1s;
};
} // namespace

namespace {

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
  virtual Result<Slice> FetchBigSharedMemory(uint64_t id, size_t size) = 0;
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

template <class Req>
std::string PrettyRequest(const char* name, const Req& req) {
  return PrettyFunctionName(name);
}

std::string PrettyRequest(const char* name, const tserver::PgCreateTableRequestPB& req) {
  return Format("'$0' table creation", req.table_name());
}

} // namespace

namespace pg_client::internal {

template <class Data>
ExchangeFuture<Data>::ExchangeFuture() = default;

template <class Data>
ExchangeFuture<Data>::ExchangeFuture(std::shared_ptr<Data> data) : data_(std::move(data)) {}

template <class Data>
ExchangeFuture<Data>::ExchangeFuture(ExchangeFuture&& rhs) noexcept : data_(std::move(rhs.data_)) {}

template <class Data>
ExchangeFuture<Data>& ExchangeFuture<Data>::operator=(ExchangeFuture&& rhs) noexcept {
  data_ = std::move(rhs.data_);
  return *this;
}

template <class Data>
bool ExchangeFuture<Data>::valid() const {
  return data_ != nullptr;
}

template <class Data>
void ExchangeFuture<Data>::wait() const {
  if (!value_) {
    value_ = MakeExchangeResult(*data_, data_->Complete());
  }
}

template <class Data>
bool ExchangeFuture<Data>::ready() const {
  return data_->template ResponseReady<bool>();
}

template <class Data>
ExchangeFuture<Data>::Result ExchangeFuture<Data>::get() {
  wait();
  data_.reset();
  return *value_;
}

template <class LWReqPB, class LWRespPB, class ResTp, tserver::PgSharedExchangeReqType ShExcReqType>
struct PgClientData : public FetchBigDataCallback {
  using RequestType = LWReqPB;
  using ResultType = ResTp;
  static constexpr tserver::PgSharedExchangeReqType kSharedExchangeRequestType = ShExcReqType;

  const LWReqPB& req;
  LWRespPB resp;
  rpc::RpcController controller;

  tserver::SharedExchange* exchange = nullptr;
  CoarseTimePoint deadline;
  BigDataFetcher* big_data_fetcher;

  std::promise<ResTp> promise;

  std::mutex exchange_mutex;
  std::condition_variable exchange_cond;
  std::optional<Result<Slice>> exchange_result GUARDED_BY(exchange_mutex);
  bool fetching_big_data GUARDED_BY(exchange_mutex) = false;
  rpc::CallData big_call_data GUARDED_BY(exchange_mutex);

  PgClientData(const LWReqPB& req_, ThreadSafeArena* arena_) : req(req_), resp(arena_) {}

  void SetupExchange(
      tserver::SharedExchange* exchange_, BigDataFetcher* big_data_fetcher_,
      CoarseTimePoint deadline_) {
    exchange = exchange_;
    big_data_fetcher = big_data_fetcher_;
    deadline = deadline_;
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
    auto slice_res = big_data_fetcher->FetchBigSharedMemory(id, size);
    if (!slice_res) {
      if constexpr (std::is_same_v<Res, bool>) {
        return true;
      } else {
        return slice_res.status();
      }
    }
    auto slice = *slice_res;
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

  void BigDataFetched(Result<rpc::CallData>* call_data) override {
    std::lock_guard lock(exchange_mutex);
    if (!call_data->ok()) {
      exchange_result = call_data->status();
    } else {
      big_call_data = std::move(**call_data);
    }
    exchange_cond.notify_all();
  }

  Result<rpc::CallResponsePtr> Complete() {
    auto call_data = ResponseReady<Result<rpc::CallData>>();
    RETURN_NOT_OK(call_data);
    auto response = std::make_shared<rpc::CallResponse>();
    RETURN_NOT_OK(response->ParseFrom(&*call_data));
    RETURN_NOT_OK(resp.ParseFromSlice(response->serialized_response()));
    return response;
  }
};

struct PerformData : public PgClientData<tserver::LWPgPerformRequestPB,
                                         tserver::LWPgPerformResponsePB,
                                         PerformResult,
                                         tserver::PgSharedExchangeReqType::PERFORM> {
  PgsqlOps operations;
  PgDocMetrics& metrics;

  PerformData(
      const tserver::LWPgPerformRequestPB& req_, ThreadSafeArena* arena, PgsqlOps&& operations_,
      PgDocMetrics& metrics_)
      : PgClientData(req_, arena), operations(std::move(operations_)), metrics(metrics_) {}

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
      metrics.RecordRequestMetrics(op_response.metrics(), operations[i]->is_read());
      ++i;
    }
    return Status::OK();
  }
};

static_assert(
    std::is_same_v<
        typename ResultTypeResolver<PerformData>::ResultType,
        typename PerformData::ResultType>);

struct AcquireObjectLockResult {
  Status status;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(status);
  }
};

struct AcquireObjectLockData : public PgClientData<
    tserver::LWPgAcquireObjectLockRequestPB,
    tserver::LWPgAcquireObjectLockResponsePB,
    AcquireObjectLockResult,
    tserver::PgSharedExchangeReqType::ACQUIRE_OBJECT_LOCK> {
  AcquireObjectLockData(
      const tserver::LWPgAcquireObjectLockRequestPB& req_, ThreadSafeArena* arena_)
      : PgClientData(req_, arena_) {}
};

} // namespace pg_client::internal

using pg_client::internal::PerformData;
using pg_client::internal::AcquireObjectLockResult;
using pg_client::internal::AcquireObjectLockData;
using pg_client::internal::ResultFuture;
using pg_client::internal::ExchangeFuture;

namespace {

Status DoProcessResponse(
    PerformData& data, PerformResult& result, const rpc::CallResponsePtr& response) {
  result.response = response;
  RETURN_NOT_OK(ResponseStatus(data.resp));
  RETURN_NOT_OK(data.Process());
  if (data.resp.has_catalog_read_time()) {
    VLOG(2) << "Got catalog_read_time: " << data.resp.catalog_read_time().ShortDebugString();
    result.catalog_read_time = ReadHybridTime::FromPB(data.resp.catalog_read_time());
  }
  result.used_in_txn_limit = HybridTime::FromPB(data.resp.used_in_txn_limit_ht());
  return Status::OK();
}

Status DoProcessResponse(
    AcquireObjectLockData& data, AcquireObjectLockResult& result,
    const Result<rpc::CallResponsePtr>& response) {
  return ResponseStatus(data.resp);
}

template <class Data>
auto MakeExchangeResult(
    Data& data, const Result<rpc::CallResponsePtr>& response) {
  typename Data::ResultType result;
  result.status = response.ok() ? DoProcessResponse(data, result, *response) : response.status();
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

// List of additional RPCs that are logged by
// yb_debug_log_docdb_requests
static PggateRPC kDebugLogRPCs[] = {
  PggateRPC::kOpenTable,
  PggateRPC::kAlterTable,
  PggateRPC::kCreateDatabase,
  PggateRPC::kBackfillIndex,
  PggateRPC::kAlterDatabase,
  PggateRPC::kCreateTable,
  PggateRPC::kCreateTablegroup,
  PggateRPC::kDropDatabase,
  PggateRPC::kDropTable,
  PggateRPC::kDropTablegroup,
  PggateRPC::kAcquireAdvisoryLock,
  PggateRPC::kReleaseAdvisoryLock,
  PggateRPC::kAcquireObjectLock,
  PggateRPC::kTruncateTable
};

class PgClient::Impl : public BigDataFetcher {
 public:
  Impl(
      std::reference_wrapper<const WaitEventWatcher> wait_event_watcher,
      std::atomic<uint64_t>& next_perform_op_serial_no)
    : heartbeat_poller_(std::bind(&Impl::Heartbeat, this, false)),
      wait_event_watcher_(wait_event_watcher),
      next_perform_op_serial_no_(next_perform_op_serial_no) {
    tablet_server_count_cache_.fill(0);
  }

  ~Impl() {
    CHECK(!proxy_);
  }

  Status Start(rpc::ProxyCache* proxy_cache,
               rpc::Scheduler* scheduler,
               const tserver::TServerSharedData& tserver_shared_data,
               std::optional<uint64_t> session_id) {
    MonoDelta resolve_cache_timeout;
    HostPort host_port(tserver_shared_data.endpoint());
    if (FLAGS_use_node_hostname_for_local_tserver) {
      host_port = HostPort(tserver_shared_data.host().ToBuffer(),
                           tserver_shared_data.endpoint().port());
      resolve_cache_timeout = MonoDelta::kMax;
    }
    VLOG(1) << "Using TServer host_port: " << host_port;
    proxy_ = std::make_unique<PgClientServiceProxy>(
        proxy_cache, host_port, nullptr /* protocol */, resolve_cache_timeout);
    local_cdc_service_proxy_ = std::make_unique<CDCServiceProxy>(
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
    return Status::OK();
  }

  void Interrupt() {
    if (session_shared_mem_) {
      session_shared_mem_->exchange().SignalStop();
    }
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
      auto status = heartbeat_controller_.status();
      if (status.ok()) {
        status = ResponseStatus(heartbeat_resp_);;
      }
      if (create) {
        if (!status.ok()) {
          create_session_promise_.set_value(status);
        } else {
          auto instance_id = heartbeat_resp_.instance_id();
          if (!instance_id.empty()) {
            auto session_shared_mem = tserver::PgSessionSharedMemoryManager::Make(
                instance_id, heartbeat_resp_.session_id(), tserver::Create::kFalse);
            if (session_shared_mem.ok()) {
              session_shared_mem_.emplace(std::move(*session_shared_mem));
            } else {
              LOG(DFATAL) << "Failed to create shared memory: " << session_shared_mem.status();
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
          LOG(DFATAL) << "Heartbeat failed. Connection needs to be reset. "
                      << "Shutting down heartbeating mechanism due to unknown session "
                      << session_id_;
          heartbeat_poller_.Shutdown();
          return;
        }

        LOG_WITH_PREFIX(WARNING) << "Heartbeat failed: " << status;
      }
    });
  }

  void SetTimeout(int timeout_ms) {
    timeouts_.SetPGTimeoutAsGlobalDeadline(timeout_ms);
  }

  void ClearTimeout() {
    timeouts_.ClearGlobalDeadline();
  }

  void SetLockTimeout(int timeout_ms) {
    timeouts_.SetLockTimeout(timeout_ms);
  }

  Result<PgTableDescPtr> OpenTable(
      const PgObjectId& table_id, bool reopen, uint64_t min_ysql_catalog_version,
      master::IncludeHidden include_hidden) {
    tserver::PgOpenTableRequestPB req;
    req.set_table_id(table_id.GetYbTableId());
    req.set_reopen(reopen);
    if (min_ysql_catalog_version > 0) {
      req.set_ysql_catalog_version(min_ysql_catalog_version);
    }
    req.set_include_hidden(include_hidden);
    tserver::PgOpenTableResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::OpenTable,
        req, resp, PggateRPC::kOpenTable));
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
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetTablePartitionList,
        req, resp, PggateRPC::kGetTablePartitionList));
    RETURN_NOT_OK(ResponseStatus(resp));

    return BuildTablePartitionList(resp.partitions(), table_id);
  }

  Result<tserver::PgListClonesResponsePB> ListDatabaseClones() {
    tserver::PgListClonesRequestPB req;
    tserver::PgListClonesResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ListClones,
        req, resp, PggateRPC::kListClones));
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

    auto wait_event = commit ?
        ash::WaitStateCode::kTransactionCommit :
        ash::WaitStateCode::kTransactionTerminate;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::FinishTransaction,
        req, resp, wait_event, deadline));

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

  Result<bool> PollVectorIndexReady(const PgObjectId& table_id) {
    tserver::PgPollVectorIndexReadyRequestPB req;
    req.set_table_id(table_id.GetYbTableId());

    tserver::PgPollVectorIndexReadyResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::PollVectorIndexReady,
        req, resp, PggateRPC::kPollVectorIndexReady));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.ready();
  }

  Status RollbackToSubTransaction(SubTransactionId id, tserver::PgPerformOptionsPB* options) {
    tserver::PgRollbackToSubTransactionRequestPB req;
    req.set_session_id(session_id_);
    if (options) {
      options->Swap(req.mutable_options());
    }
    req.set_sub_transaction_id(id);

    tserver::PgRollbackToSubTransactionResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::RollbackToSubTransaction,
        req, resp, ash::WaitStateCode::kTransactionRollbackToSavepoint));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::InsertSequenceTuple,
        req, resp, PggateRPC::kInsertSequenceTuple));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::UpdateSequenceTuple,
        req, resp, PggateRPC::kUpdateSequenceTuple));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::FetchSequenceTuple,
        req, resp, PggateRPC::kFetchSequenceTuple));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::make_pair(resp.first_value(), resp.last_value());
  }

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(
      int64_t db_oid, int64_t seq_oid, std::optional<uint64_t> ysql_catalog_version,
      bool is_db_catalog_version_mode, std::optional<uint64_t> read_time) {
    tserver::PgReadSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);
    if (ysql_catalog_version) {
      if (is_db_catalog_version_mode) {
        DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
        req.set_ysql_db_catalog_version(*ysql_catalog_version);
      } else {
        req.set_ysql_catalog_version(*ysql_catalog_version);
      }
    }

    if (read_time) {
      req.set_read_time(*read_time);
    }

    tserver::PgReadSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ReadSequenceTuple,
        req, resp, PggateRPC::kReadSequenceTuple));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::make_pair(resp.last_val(), resp.is_called());
  }

  Status DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid) {
    tserver::PgDeleteSequenceTupleRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);
    req.set_seq_oid(seq_oid);

    tserver::PgDeleteSequenceTupleResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::DeleteSequenceTuple,
        req, resp, PggateRPC::kDeleteSequenceTuple));
    return ResponseStatus(resp);
  }

  Status DeleteDBSequences(int64_t db_oid) {
    tserver::PgDeleteDBSequencesRequestPB req;
    req.set_session_id(session_id_);
    req.set_db_oid(db_oid);

    tserver::PgDeleteDBSequencesResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::DeleteDBSequences,
        req, resp, PggateRPC::kDeleteDBSequences));
    return ResponseStatus(resp);
  }

  template <class Data, class Method, class... Args>
  ResultFuture<Data> PrepareAndSend(Method method, Args&&... args) {
    auto data = std::make_shared<Data>(std::forward<Args>(args)...);
    if (session_shared_mem_ && session_shared_mem_->exchange().ReadyToSend()) {
      ash::MetadataSerializer metadata(rpc::MetadataSerializationMode::kWriteOnZero);
      constexpr size_t kHeaderSize = sizeof(uint8_t) + sizeof(uint64_t);
      const size_t kMetadataSize = metadata.SerializedSize();
      auto& exchange = session_shared_mem_->exchange();
      auto out = exchange.Obtain(kHeaderSize + kMetadataSize + data->req.SerializedSize());
      if (out) {
        const auto [rpc_deadline, rpc_timeout] =
            timeouts_.GetDeadlineAndTimeoutForRPC<typename Data::RequestType>();
        *reinterpret_cast<uint8_t *>(out) = Data::kSharedExchangeRequestType;
        out += sizeof(uint8_t);
        LittleEndian::Store64(out, rpc_timeout.ToMilliseconds());
        out += sizeof(uint64_t);
        out = pointer_cast<std::byte*>(metadata.SerializeToArray(to_uchar_ptr(out)));
        const auto size = data->req.SerializedSize();
        auto* end = pointer_cast<std::byte*>(
            data->req.SerializeToArray(pointer_cast<uint8_t*>(out)));
        Status status;
        if ((size_t)(end - out) != size) {
          status = STATUS(InternalError, "Obtained size does not match serialized size");
        }
        if (status.ok()) {
          status = exchange.SendRequest();
        }
        if (!status.ok()) {
          data->promise.set_value(MakeExchangeResult(*data, status));
          return data->promise.get_future();
        }
        data->SetupExchange(&exchange, this, rpc_deadline);
        return ExchangeFuture<Data>(std::move(data));
      }
    }
    data->controller.set_invoke_callback_mode(rpc::InvokeCallbackMode::kReactorThread);
    method(
        proxy_.get(), data->req, &data->resp,
        SetupController<typename Data::RequestType>(&data->controller),
        [data] {
          data->promise.set_value(MakeExchangeResult(*data, data->controller.CheckedResponse()));
        });
    return data->promise.get_future();
  }

  PerformResultFuture PerformAsync(
      tserver::PgPerformOptionsPB* options, PgsqlOps&& operations, PgDocMetrics& metrics) {
    auto get_next_serial_no =
        [this] { return next_perform_op_serial_no_.fetch_add(1, std::memory_order_acq_rel); };

    if (PREDICT_FALSE(
        FLAGS_TEST_emulate_op_lost_on_write &&
        !options->ddl_mode() &&
        operations.size() == 1 &&
        operations.front()->is_write())) {
      const auto serial_no = get_next_serial_no();
      LOG(INFO) << "Emulating lost of operation with serial_no=" << serial_no;
    }

    auto& arena = operations.front()->arena();
    tserver::LWPgPerformRequestPB req(&arena);
    req.set_session_id(session_id_);
    *req.mutable_options() = std::move(*options);
    req.set_serial_no(get_next_serial_no());
    PrepareOperations(&req, operations);
    auto method = [](auto* proxy, const auto& req, auto* resp, auto* controller, auto callback) {
      proxy->PerformAsync(req, resp, controller, std::move(callback));
    };
    return PrepareAndSend<PerformData>(method, req, &arena, std::move(operations), metrics);
  }

  Status AcquireObjectLock(
      tserver::PgPerformOptionsPB* options, const YbcObjectLockId& lock_id,
      YbcObjectLockMode mode, std::optional<PgTablespaceOid> tablespace_oid) {
    object_locks_arena_.Reset(ResetMode::kKeepLast);
    tserver::LWPgAcquireObjectLockRequestPB req(&object_locks_arena_);
    req.set_session_id(session_id_);
    *req.mutable_options() = std::move(*options);
    auto& lock_oid = *req.mutable_lock_oid();
    lock_oid.set_database_oid(lock_id.db_oid);
    lock_oid.set_relation_oid(lock_id.relation_oid);
    lock_oid.set_object_oid(lock_id.object_oid);
    lock_oid.set_object_sub_oid(lock_id.object_sub_oid);
    if (tablespace_oid) {
      lock_oid.set_tablespace_oid(*tablespace_oid);
    }
    req.set_lock_type(static_cast<tserver::ObjectLockMode>(mode));
    auto method = [](auto* proxy, const auto& req, auto* resp, auto* controller, auto callback) {
      proxy->AcquireObjectLockAsync(req, resp, controller, std::move(callback));
    };
    return PrepareAndSend<AcquireObjectLockData>(method, req, &object_locks_arena_).Get().status;
  }

  bool TryAcquireObjectLockInSharedMemory(
      SubTransactionId subtxn_id, const YbcObjectLockId& lock_id,
      docdb::ObjectLockFastpathLockType lock_type) {
    if (!FLAGS_enable_object_lock_fastpath) {
      return false;
    }

    auto* lock_shared = PgSharedMemoryManager().SharedData()->object_lock_state();
    if (!lock_shared || !session_shared_mem_) {
      LOG(WARNING) << "Not using object locking fastpath: shared memory not ready";
      return false;
    }
    return lock_shared->Lock({
        .owner = SHARED_MEMORY_LOAD(session_shared_mem_->object_locking_data()),
        .subtxn_id = subtxn_id,
        .database_oid = lock_id.db_oid,
        .relation_oid = lock_id.relation_oid,
        .object_oid = lock_id.object_oid,
        .object_sub_oid = lock_id.object_sub_oid,
        .lock_type = lock_type});
  }

  void FetchBigData(uint64_t data_id, FetchBigDataCallback* callback) override {
    struct FetchBigDataInfo {
      FetchBigDataCallback* callback;
      tserver::LWPgFetchDataRequestPB* fetch_req;
      tserver::LWPgFetchDataResponsePB* fetch_resp;
      rpc::RpcController* controller;
    };
    auto arena = SharedThreadSafeArena();
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

  Result<Slice> FetchBigSharedMemory(uint64_t id, size_t size) override {
    if (id != big_shared_memory_id_) {
      big_mapped_region_ = {};
      big_shared_memory_object_ = {};
      big_shared_memory_object_ = VERIFY_RESULT(InterprocessSharedMemoryObject::Open(
          tserver::MakeSharedMemoryBigSegmentName(session_shared_mem_->instance_id(), id)));
      big_mapped_region_ = VERIFY_RESULT(big_shared_memory_object_.Map());
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ReserveOids,
        req, resp, PggateRPC::kReserveOids));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::pair<PgOid, PgOid>(resp.begin_oid(), resp.end_oid());
  }

  Result<PgOid> GetNewObjectId(PgOid db_oid) {
    tserver::PgGetNewObjectIdRequestPB req;
    req.set_db_oid(db_oid);

    tserver::PgGetNewObjectIdResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetNewObjectId,
        req, resp, PggateRPC::kGetNewObjectId));
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

  // Assert to make sure YbcXClusterReplicationRole is updated when new roles are added.
  static_assert(XClusterNamespaceInfoPB::XClusterRole_ARRAYSIZE == 5, "array length mismatch");
  Result<uint32_t> GetXClusterRole(uint32_t db_oid) {
    tserver::PgGetXClusterRoleRequestPB req;
    tserver::PgGetXClusterRoleResponsePB resp;

    req.set_db_oid(db_oid);
    RETURN_NOT_OK(DoSyncRPC(
        &PgClientServiceProxy::GetXClusterRole, req, resp,
        PggateRPC::kGetXClusterRole));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.xcluster_role();
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
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::DropTable,
        *req, resp, PggateRPC::kDropTable, deadline));
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
        &PgClientServiceProxy::WaitForBackendsCatalogVersion,
        *req, resp, PggateRPC::kWaitForBackendsCatalogVersion, deadline));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::BackfillIndex,
        *req, resp, PggateRPC::kBackfillIndex, deadline));
    return ResponseStatus(resp);
  }

  Status GetIndexBackfillProgress(const std::vector<PgObjectId>& index_ids,
                                  uint64_t* num_rows_read_from_table,
                                  double* num_rows_backfilled) {
    tserver::PgGetIndexBackfillProgressRequestPB req;
    tserver::PgGetIndexBackfillProgressResponsePB resp;

    for (const auto& index_id : index_ids) {
      index_id.ToPB(req.add_index_ids());
    }

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetIndexBackfillProgress,
        req, resp, PggateRPC::kGetIndexBackfillProgress));
    RETURN_NOT_OK(ResponseStatus(resp));
    for (int i = 0; i < resp.num_rows_read_from_table_for_backfill_size(); ++i) {
      num_rows_read_from_table[i] = resp.num_rows_read_from_table_for_backfill(i);
      if (num_rows_backfilled) {
        num_rows_backfilled[i] = resp.num_rows_backfilled_in_index(i);
      }
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetLockStatus,
        req, resp, PggateRPC::kGetLockStatus));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::TabletServerCount,
        req, resp, PggateRPC::kTabletServerCount));
    RETURN_NOT_OK(ResponseStatus(resp));
    tablet_server_count_cache_[primary_only] = resp.count();
    return resp.count();
  }

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only) {
    tserver::PgListLiveTabletServersRequestPB req;
    tserver::PgListLiveTabletServersResponsePB resp;
    req.set_primary_only(primary_only);

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ListLiveTabletServers,
        req, resp, PggateRPC::kListLiveTabletServers));
    RETURN_NOT_OK(ResponseStatus(resp));
    client::TabletServersInfo result;
    result.tablet_servers.reserve(resp.servers().size());
    for (const auto& server : resp.servers()) {
      result.tablet_servers.push_back(client::YBTabletServerPlacementInfo::FromPB(server));
    }
    result.universe_uuid = resp.universe_uuid();
    return result;
  }

  Status ValidatePlacement(tserver::PgValidatePlacementRequestPB* req) {
    tserver::PgValidatePlacementResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ValidatePlacement,
        *req, resp, PggateRPC::kValidatePlacement));
    return ResponseStatus(resp);
  }

  Result<client::TableSizeInfo> GetTableDiskSize(
      const PgObjectId& table_oid) {
    tserver::PgGetTableDiskSizeResponsePB resp;

    tserver::PgGetTableDiskSizeRequestPB req;
    table_oid.ToPB(req.mutable_table_id());

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetTableDiskSize,
        req, resp, PggateRPC::kGetTableDiskSize));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }

    return client::TableSizeInfo{resp.size(), resp.num_missing_tablets()};
  }

  Result<bool> CheckIfPitrActive() {
    tserver::PgCheckIfPitrActiveRequestPB req;
    tserver::PgCheckIfPitrActiveResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::CheckIfPitrActive,
        req, resp, PggateRPC::kCheckIfPitrActive));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp.is_pitr_active();
  }

  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id) {
    tserver::PgIsObjectPartOfXReplRequestPB req;
    tserver::PgIsObjectPartOfXReplResponsePB resp;
    table_id.ToPB(req.mutable_table_id());
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::IsObjectPartOfXRepl,
        req, resp, PggateRPC::kIsObjectPartOfXRepl));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetActiveTransactionList,
        req, resp, PggateRPC::kGetActiveTransactionList));
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

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetTableKeyRanges,
        req, resp, PggateRPC::kGetTableKeyRanges, controller));
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
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetTserverCatalogVersionInfo,
        req, resp, PggateRPC::kGetTserverCatalogVersionInfo));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp;
  }

  Result<tserver::PgGetTserverCatalogMessageListsResponsePB> GetTserverCatalogMessageLists(
      uint32_t db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions) {
    tserver::PgGetTserverCatalogMessageListsRequestPB req;
    tserver::PgGetTserverCatalogMessageListsResponsePB resp;
    req.set_db_oid(db_oid);
    req.set_ysql_catalog_version(ysql_catalog_version);
    req.set_num_catalog_versions(num_catalog_versions);
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetTserverCatalogMessageLists,
        req, resp, PggateRPC::kGetTserverCatalogMessageLists));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp;
  }

  Result<tserver::PgSetTserverCatalogMessageListResponsePB> SetTserverCatalogMessageList(
      uint32_t db_oid, bool is_breaking_change, uint64_t new_catalog_version,
      const std::optional<std::string>& message_list) {
    tserver::PgSetTserverCatalogMessageListRequestPB req;
    tserver::PgSetTserverCatalogMessageListResponsePB resp;
    req.set_db_oid(db_oid);
    req.set_is_breaking_change(is_breaking_change);
    req.set_new_catalog_version(new_catalog_version);
    if (message_list.has_value()) {
      req.mutable_message_list()->set_message_list(message_list.value());
    }
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::SetTserverCatalogMessageList,
        req, resp, PggateRPC::kSetTserverCatalogMessageList));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    return resp;
  }

  Status TriggerRelcacheInitConnection(std::string dbname) {
    tserver::PgTriggerRelcacheInitConnectionRequestPB req;
    tserver::PgTriggerRelcacheInitConnectionResponsePB resp;
    req.set_database_name(dbname);
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::TriggerRelcacheInitConnection,
        req, resp, PggateRPC::kTriggerRelcacheInitConnection));
    return ResponseStatus(resp);
  }

  #define YB_PG_CLIENT_SIMPLE_METHOD_IMPL(r, data, method) \
  Status method( \
      tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      CoarseTimePoint deadline) { \
    tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB) resp; \
    req->set_session_id(session_id_); \
    auto status = DoSyncRPC(&PgClientServiceProxy::method, \
        *req, resp, BOOST_PP_CAT(PggateRPC::k, method), deadline); \
    if (!status.ok()) { \
      if (status.IsTimedOut()) { \
        return STATUS_FORMAT(TimedOut, "Timed out waiting for $0", PrettyRequest(__func__, *req)); \
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
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::CancelTransaction,
        req, resp, ash::WaitStateCode::kTransactionCancel));
    return ResponseStatus(resp);
  }

  Result<tserver::PgCreateReplicationSlotResponsePB> CreateReplicationSlot(
      tserver::PgCreateReplicationSlotRequestPB* req, CoarseTimePoint deadline) {
    tserver::PgCreateReplicationSlotResponsePB resp;
    req->set_session_id(session_id_);
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::CreateReplicationSlot,
        *req, resp, PggateRPC::kCreateReplicationSlot, deadline));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgListReplicationSlotsResponsePB> ListReplicationSlots() {
    tserver::PgListReplicationSlotsRequestPB req;
    tserver::PgListReplicationSlotsResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ListReplicationSlots,
        req, resp, PggateRPC::kListReplicationSlots));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgGetReplicationSlotResponsePB> GetReplicationSlot(
      const ReplicationSlotName& slot_name) {
    tserver::PgGetReplicationSlotRequestPB req;
    req.set_replication_slot_name(slot_name.ToString());

    tserver::PgGetReplicationSlotResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::GetReplicationSlot,
        req, resp, PggateRPC::kGetReplicationSlot));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<std::string> ExportTxnSnapshot(tserver::PgExportTxnSnapshotRequestPB* req) {
    tserver::PgExportTxnSnapshotResponsePB resp;
    req->set_session_id(session_id_);
    RETURN_NOT_OK(DoSyncRPC(
        &PgClientServiceProxy::ExportTxnSnapshot, *req, resp,
        PggateRPC::kExportTxnSnapshot));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.snapshot_id();
  }

  Result<PgTxnSnapshotPB> ImportTxnSnapshot(
      std::string_view snapshot_id, tserver::PgPerformOptionsPB&& options) {
    tserver::PgImportTxnSnapshotRequestPB req;
    tserver::PgImportTxnSnapshotResponsePB resp;
    req.set_session_id(session_id_);
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    *req.mutable_options() = std::move(options);
    RETURN_NOT_OK(DoSyncRPC(
        &PgClientServiceProxy::ImportTxnSnapshot, req, resp,
        PggateRPC::kImportTxnSnapshot));
    RETURN_NOT_OK(ResponseStatus(resp));
    return std::move(resp.snapshot());
  }

  Status ClearExportedTxnSnapshots() {
    tserver::PgClearExportedTxnSnapshotsRequestPB req;
    tserver::PgClearExportedTxnSnapshotsResponsePB resp;
    req.set_session_id(session_id_);
    RETURN_NOT_OK(DoSyncRPC(
        &PgClientServiceProxy::ClearExportedTxnSnapshots, req, resp,
        PggateRPC::kClearExportedTxnSnapshots));
    return ResponseStatus(resp);
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
    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::YCQLStatementStats,
        req, resp, PggateRPC::kYCQLStatementStats));
    return resp;
  }

  Result<cdc::InitVirtualWALForCDCResponsePB> InitVirtualWALForCDC(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
      const std::unordered_map<uint32_t, uint32_t>& oid_to_relfilenode,
      const YbcReplicationSlotHashRange* slot_hash_range, uint64_t active_pid,
      const std::vector<PgOid>& publication_oids, bool pub_all_tables) {
    cdc::InitVirtualWALForCDCRequestPB req;

    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);
    req.set_active_pid(active_pid);
    for (const auto& table_id : table_ids) {
      *req.add_table_id() = table_id.GetYbTableId();
    }

    if (FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) {
      for (const auto& e : oid_to_relfilenode) {
        req.mutable_oid_to_relfilenode()->emplace(e.first, e.second);
      }
    }

    if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
      for (const auto& publication_oid : publication_oids) {
        req.add_publication_oid(publication_oid);
      }
      req.set_pub_all_tables(pub_all_tables);
    }

    if (FLAGS_ysql_yb_enable_consistent_replication_from_hash_range) {
      if (slot_hash_range != nullptr) {
        VLOG(1) << "Setting hash ranges in InitVirtualVWAL request - start_range: "
                << slot_hash_range->start_range << ", end_range: " << slot_hash_range->end_range;
        auto req_slot_range = req.mutable_slot_hash_range();
        req_slot_range->set_start_range(slot_hash_range->start_range);
        req_slot_range->set_end_range(slot_hash_range->end_range);
      } else {
        VLOG(1) << "No hash range constraints to be set in InitVirtualVWAL request";
      }
    }

    cdc::InitVirtualWALForCDCResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&CDCServiceProxy::InitVirtualWALForCDC,
        req, resp, PggateRPC::kInitVirtualWALForCDC));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::GetLagMetricsResponsePB> GetLagMetrics(
      const std::string& stream_id, int64_t* lag_metric) {
    cdc::GetLagMetricsRequestPB req;
    req.set_stream_id(stream_id);
    cdc::GetLagMetricsResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&CDCServiceProxy::GetLagMetrics,
        req, resp, PggateRPC::kGetLagMetrics));
    RETURN_NOT_OK(ResponseStatus(resp));
    *lag_metric = resp.lag_metric();
    return resp;
  }

  Result<cdc::UpdatePublicationTableListResponsePB> UpdatePublicationTableList(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
      const std::unordered_map<uint32_t, uint32_t>& oid_to_relfilenode) {
    cdc::UpdatePublicationTableListRequestPB req;
    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);
    for (const auto& table_id : table_ids) {
      *req.add_table_id() = table_id.GetYbTableId();
    }

    if (FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) {
      for (const auto& e : oid_to_relfilenode) {
        req.mutable_oid_to_relfilenode()->emplace(e.first, e.second);
      }
    }

    cdc::UpdatePublicationTableListResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&CDCServiceProxy::UpdatePublicationTableList,
        req, resp, PggateRPC::kUpdatePublicationTableList));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::DestroyVirtualWALForCDCResponsePB> DestroyVirtualWALForCDC() {
    cdc::DestroyVirtualWALForCDCRequestPB req;
    req.set_session_id(session_id_);

    cdc::DestroyVirtualWALForCDCResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&CDCServiceProxy::DestroyVirtualWALForCDC,
        req, resp, PggateRPC::kDestroyVirtualWALForCDC));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<cdc::GetConsistentChangesResponsePB> GetConsistentChangesForCDC(
      const std::string& stream_id) {
    cdc::GetConsistentChangesRequestPB req;
    req.set_session_id(session_id_);
    req.set_stream_id(stream_id);

    cdc::GetConsistentChangesResponsePB resp;
    RETURN_NOT_OK(DoSyncRPC(&CDCServiceProxy::GetConsistentChanges,
        req, resp, PggateRPC::kGetConsistentChanges));
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
    RETURN_NOT_OK(DoSyncRPC(&CDCServiceProxy::UpdateAndPersistLSN,
        req, resp, PggateRPC::kUpdateAndPersistLSN));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgTabletsMetadataResponsePB> TabletsMetadata(bool local_only) {
    tserver::PgTabletsMetadataRequestPB req;
    req.set_local_only(local_only);
    tserver::PgTabletsMetadataResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::TabletsMetadata,
        req, resp, PggateRPC::kTabletsMetadata));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Result<tserver::PgServersMetricsResponsePB> ServersMetrics() {
    tserver::PgServersMetricsRequestPB req;
    tserver::PgServersMetricsResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::ServersMetrics,
        req, resp, PggateRPC::kServersMetrics));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp;
  }

  Status SetCronLastMinute(int64_t last_minute) {
    tserver::PgCronSetLastMinuteRequestPB req;
    req.set_last_minute(last_minute);
    tserver::PgCronSetLastMinuteResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::CronSetLastMinute,
        req, resp, PggateRPC::kCronSetLastMinute));
    return ResponseStatus(resp);
  }

  Result<int64_t> GetCronLastMinute() {
    tserver::PgCronGetLastMinuteRequestPB req;
    tserver::PgCronGetLastMinuteResponsePB resp;

    RETURN_NOT_OK(DoSyncRPC(&PgClientServiceProxy::CronGetLastMinute,
        req, resp, PggateRPC::kCronGetLastMinute));
    RETURN_NOT_OK(ResponseStatus(resp));
    return resp.last_minute();
  }

  Status GetYbSystemTableInfo(
      PgOid namespace_oid, std::string_view table_name, PgOid* oid, PgOid* relfilenode) {
    tserver::PgGetYbSystemTableInfoRequestPB req;
    tserver::PgGetYbSystemTableInfoResponsePB resp;
    req.set_namespace_oid(namespace_oid);
    req.set_table_name(table_name.data(), table_name.size());
    RETURN_NOT_OK(DoSyncRPC(
        &PgClientServiceProxy::GetYbSystemTableInfo, req, resp, PggateRPC::kGetYbSystemTableInfo));
    RETURN_NOT_OK(ResponseStatus(resp));
    *oid = resp.table_oid();
    *relfilenode = resp.relfilenode();
    return Status::OK();
  }

 private:
  std::string LogPrefix() const {
    return Format("Session id $0: ", session_id_);
  }

  template <typename T = void>
  rpc::RpcController* SetupController(
      rpc::RpcController* controller,
      CoarseTimePoint rpc_deadline = CoarseTimePoint()) {
    const auto [_, timeout] = timeouts_.GetDeadlineAndTimeoutForRPC<T>(rpc_deadline);
    controller->set_timeout(timeout);
    return controller;
  }

  template <typename T = void>
  rpc::RpcController* PrepareController(CoarseTimePoint rpc_deadline = CoarseTimePoint()) {
    controller_.Reset();
    return SetupController<T>(&controller_, rpc_deadline);
  }

  template <typename T = void>
  rpc::RpcController* PrepareController(rpc::RpcController* controller) {
    return controller;
  }

  rpc::RpcController* PrepareHeartbeatController() {
    heartbeat_controller_.Reset();
    // Should update the flag validator if the 1s grace period is changed.
    heartbeat_controller_.set_timeout(FLAGS_pg_client_heartbeat_interval_ms * 1ms - 1s);
    return &heartbeat_controller_;
  }

  template <class Proxy, class Req, class Resp>
  using SyncRPCFunc = Status (Proxy::*)(
      const Req&, Resp*, rpc::RpcController*) const;

  template <class Proxy, class Req, class Resp>
  Status DoSyncRPCImpl(
      Proxy& proxy, SyncRPCFunc<Proxy, Req, Resp> func, Req& req, Resp& resp,
      PggateRPC rpc_enum, rpc::RpcController* controller, ash::WaitStateCode wait_event) {

    const auto log_detail =
        yb_debug_log_docdb_requests &&
        std::ranges::any_of(kDebugLogRPCs, [rpc_enum](auto value) { return value == rpc_enum; });

    if (log_detail) {
      LOG(INFO) << "DoSyncRPC " << GetTypeName<Req>() << ":\n " << req.ShortDebugString();
    }

    auto watcher = wait_event_watcher_(wait_event, rpc_enum);
    const auto s = (proxy.*func)(req, &resp, controller);

    if (log_detail) {
      LOG(INFO) << "DoSyncRPC " << GetTypeName<Resp>() << " response:\n"
                << "status " << s << "\n" << resp.ShortDebugString();
    }

    // Check for interrupts are executing sync RPC.
    YBCCheckForInterrupts();

    return s;
  }

  auto* GetProxy(PgClientServiceProxy*) const { return proxy_.get(); }
  auto* GetProxy(CDCServiceProxy*) const { return local_cdc_service_proxy_.get(); }

  template <class Proxy, class Req, class Resp, class... Args>
  Status DoSyncRPC(
      SyncRPCFunc<Proxy, Req, Resp> func,
      Req& req, Resp& resp, PggateRPC rpc_enum, Args&&... args) {
    return DoSyncRPCImpl(
        *DCHECK_NOTNULL(GetProxy(static_cast<Proxy*>(nullptr))), func, req, resp, rpc_enum,
        PrepareController<Req>(std::forward<Args>(args)...), ash::WaitStateCode::kWaitingOnTServer);
  }

  // Overload for when wait_event is specific enough and RPC name is not needed.
  template <class Proxy, class Req, class Resp, class... Args>
  Status DoSyncRPC(
      SyncRPCFunc<Proxy, Req, Resp> func,
      Req& req, Resp& resp, ash::WaitStateCode wait_event, Args&&... args) {
    return DoSyncRPCImpl(
        *DCHECK_NOTNULL(GetProxy(static_cast<Proxy*>(nullptr))), func, req, resp, PggateRPC::kNoRPC,
        PrepareController<Req>(std::forward<Args>(args)...), wait_event);
  }

  std::unique_ptr<PgClientServiceProxy> proxy_;
  std::unique_ptr<CDCServiceProxy> local_cdc_service_proxy_;

  rpc::RpcController controller_;
  uint64_t session_id_ = 0;

  rpc::Poller heartbeat_poller_;
  std::atomic<bool> heartbeat_running_{false};
  rpc::RpcController heartbeat_controller_;
  tserver::PgHeartbeatResponsePB heartbeat_resp_;
  std::optional<tserver::PgSessionSharedMemoryManager> session_shared_mem_;
  std::promise<Result<uint64_t>> create_session_promise_;
  std::array<int, 2> tablet_server_count_cache_;
  PgTimeout timeouts_;

  const WaitEventWatcher& wait_event_watcher_;

  uint64_t big_shared_memory_id_;
  InterprocessSharedMemoryObject big_shared_memory_object_;
  InterprocessMappedRegion big_mapped_region_;
  ThreadSafeArena object_locks_arena_;
  std::atomic<uint64_t>& next_perform_op_serial_no_;
};

std::string DdlMode::ToString() const {
  return YB_STRUCT_TO_STRING(
      has_docdb_schema_changes, silently_altered_db, use_regular_transaction_block);
}

void DdlMode::ToPB(tserver::PgFinishTransactionRequestPB_DdlModePB* dest) const {
  dest->set_has_docdb_schema_changes(has_docdb_schema_changes);
  if (silently_altered_db) {
    dest->mutable_silently_altered_db()->set_value(*silently_altered_db);
  }
  dest->set_use_regular_transaction_block(use_regular_transaction_block);
}

PgClient::PgClient(
    std::reference_wrapper<const WaitEventWatcher> wait_event_watcher,
    std::atomic<uint64_t>& seq_number)
    : impl_(new Impl(wait_event_watcher, seq_number)) {}

PgClient::~PgClient() = default;

Status PgClient::Start(
    rpc::ProxyCache* proxy_cache, rpc::Scheduler* scheduler,
    const tserver::TServerSharedData& tserver_shared_object,
    std::optional<uint64_t> session_id) {
  return impl_->Start(proxy_cache, scheduler, tserver_shared_object, session_id);
}

void PgClient::Interrupt() {
  impl_->Interrupt();
}

void PgClient::Shutdown() {
  impl_->Shutdown();
}

void PgClient::SetTimeout(int timeout_ms) {
  impl_->SetTimeout(timeout_ms);
}

void PgClient::ClearTimeout() {
  impl_->ClearTimeout();
}

void PgClient::SetLockTimeout(int lock_timeout_ms) {
  impl_->SetLockTimeout(lock_timeout_ms);
}

uint64_t PgClient::SessionID() const { return impl_->SessionID(); }

Result<PgTableDescPtr> PgClient::OpenTable(
    const PgObjectId& table_id, bool reopen, uint64_t min_ysql_catalog_version,
    master::IncludeHidden include_hidden) {
  return impl_->OpenTable(table_id, reopen, min_ysql_catalog_version, include_hidden);
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

Result<bool> PgClient::PollVectorIndexReady(const PgObjectId& table_id) {
  return impl_->PollVectorIndexReady(table_id);
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

Result<uint32_t> PgClient::GetXClusterRole(uint32_t db_oid) {
  return impl_->GetXClusterRole(db_oid);
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
    uint64_t* num_rows_read_from_table,
    double* num_rows_backfilled) {
  return impl_->GetIndexBackfillProgress(index_ids,
                                         num_rows_read_from_table,
                                         num_rows_backfilled);
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
    int64_t db_oid, int64_t seq_oid, std::optional<uint64_t> ysql_catalog_version,
    bool is_db_catalog_version_mode, std::optional<uint64_t> read_time) {
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
    tserver::PgPerformOptionsPB* options, PgsqlOps&& operations, PgDocMetrics& metrics) {
  return impl_->PerformAsync(options, std::move(operations), metrics);
}

bool PgClient::TryAcquireObjectLockInSharedMemory(
    SubTransactionId subtxn_id, const YbcObjectLockId& pg_lock_id,
    docdb::ObjectLockFastpathLockType lock_type) {
  return impl_->TryAcquireObjectLockInSharedMemory(subtxn_id, pg_lock_id, lock_type);
}

Status PgClient::AcquireObjectLock(
    tserver::PgPerformOptionsPB* options, const YbcObjectLockId& lock_id, YbcObjectLockMode mode,
    std::optional<PgTablespaceOid> tablespace_oid) {
  return impl_->AcquireObjectLock(options, lock_id, mode, tablespace_oid);
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

Result<tserver::PgGetTserverCatalogMessageListsResponsePB> PgClient::GetTserverCatalogMessageLists(
    uint32_t db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions) {
  return impl_->GetTserverCatalogMessageLists(db_oid, ysql_catalog_version, num_catalog_versions);
}

Result<tserver::PgSetTserverCatalogMessageListResponsePB> PgClient::SetTserverCatalogMessageList(
    uint32_t db_oid, bool is_breaking_change,
    uint64_t new_catalog_version, const std::optional<std::string>& message_list) {
  return impl_->SetTserverCatalogMessageList(db_oid, is_breaking_change,
                                             new_catalog_version, message_list);
}

Status PgClient::TriggerRelcacheInitConnection(const std::string& dbname) {
  return impl_->TriggerRelcacheInitConnection(dbname);
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

Result<PgTxnSnapshotPB> PgClient::ImportTxnSnapshot(
    std::string_view snapshot_id, tserver::PgPerformOptionsPB&& options) {
  return impl_->ImportTxnSnapshot(snapshot_id, std::move(options));
}

Status PgClient::ClearExportedTxnSnapshots() { return impl_->ClearExportedTxnSnapshots(); }

Result<tserver::PgActiveSessionHistoryResponsePB> PgClient::ActiveSessionHistory() {
  return impl_->ActiveSessionHistory();
}

Result<tserver::PgYCQLStatementStatsResponsePB> PgClient::YCQLStatementStats() {
  return impl_->YCQLStatementStats();
}

Result<cdc::InitVirtualWALForCDCResponsePB> PgClient::InitVirtualWALForCDC(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
    const std::unordered_map<uint32_t, uint32_t>& oid_to_relfilenode,
    const YbcReplicationSlotHashRange* slot_hash_range, uint64_t active_pid,
    const std::vector<PgOid>& publication_oids, bool pub_all_tables) {
  return impl_->InitVirtualWALForCDC(
      stream_id, table_ids, oid_to_relfilenode, slot_hash_range, active_pid, publication_oids,
      pub_all_tables);
}

Result<cdc::GetLagMetricsResponsePB> PgClient::GetLagMetrics(
    const std::string& stream_id, int64_t* lag_metric) {
  return impl_->GetLagMetrics(stream_id, lag_metric);
}

Result<cdc::UpdatePublicationTableListResponsePB> PgClient::UpdatePublicationTableList(
    const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
    const std::unordered_map<uint32_t, uint32_t>& oid_to_relfilenode) {
  return impl_->UpdatePublicationTableList(stream_id, table_ids, oid_to_relfilenode);
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

Result<tserver::PgTabletsMetadataResponsePB> PgClient::TabletsMetadata(bool local_only) {
  return impl_->TabletsMetadata(local_only);
}

Result<tserver::PgServersMetricsResponsePB> PgClient::ServersMetrics() {
  return impl_->ServersMetrics();
}

Status PgClient::SetCronLastMinute(int64_t last_minute) {
  return impl_->SetCronLastMinute(last_minute);
}

Result<int64_t> PgClient::GetCronLastMinute() { return impl_->GetCronLastMinute(); }

Status PgClient::GetYbSystemTableInfo(
    PgOid namespace_oid, std::string_view table_name, PgOid* oid, PgOid* relfilenode) {
  return impl_->GetYbSystemTableInfo(namespace_oid, table_name, oid, relfilenode);
}

template class pg_client::internal::ExchangeFuture<pg_client::internal::PerformData>;
template class pg_client::internal::ResultFuture<pg_client::internal::PerformData>;

}  // namespace yb::pggate
