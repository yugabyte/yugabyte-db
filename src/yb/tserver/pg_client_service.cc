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

#include "yb/tserver/pg_client_service.h"

#include <sys/wait.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/stateful_services/pg_cron_leader_service_client.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"

#include "yb/common/pg_types.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/scheduler.h"

#include "yb/server/server_base.h"

#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/pg_response_cache.h"
#include "yb/tserver/pg_sequence_cache.h"
#include "yb/tserver/pg_shared_mem_pool.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/pg_txn_snapshot_manager.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"
#include "yb/util/yb_pg_errcodes.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_uint64(pg_client_session_expiration_ms, 60000,
                      "Pg client session expiration time in milliseconds.");

DECLARE_bool(pg_client_use_shared_memory);

DEFINE_RUNTIME_int32(get_locks_status_max_retry_attempts, 5,
                     "Maximum number of retries that will be performed for GetLockStatus "
                     "requests that fail in the validation phase due to unseen responses from "
                     "some of the involved tablets.");

DEFINE_RUNTIME_int32(pg_locks_max_tablet_lookup_retries, 5,
                     "Maximum number of retries when fetching tablet locations during "
                     "pg_locks queries. Retries occur when tablets have no elected leader "
                     "or when handling tablet splits.");

DEFINE_RUNTIME_int32(pg_locks_retry_delay_ms, 200,
                     "Delay in milliseconds between retries during pg_locks operations. "
                     "Applied when  retrying tablet location lookups and GetLockStatus requests");

DEFINE_test_flag(uint64, delay_before_get_old_transactions_heartbeat_intervals, 0,
                 "When non-zero, we sleep for set transaction heartbeat interval periods before "
                 "fetching old transactions. This delay is implemented to ensure that the "
                 "information returned for yb_lock_status is more up-to-date. Currently, the flag "
                 "is used in tests alone.");

DEFINE_test_flag(uint64, delay_before_get_locks_status_ms, 0,
                 "When non-zero, we sleep for set number of milliseconds after fetching involved "
                 "tablet locations and before invoking GetLockStatus RPC. Currently the flag is "
                 "being used to test pg_locks behavior when split happens after fetching involved "
                 "tablet(s) locations.");

DEFINE_RUNTIME_uint32(ysql_oid_cache_prefetch_size, 256,
    "How many new OIDs the YSQL OID allocator should prefetch at a time from YB-Master.");

DEFINE_test_flag(uint64, ysql_oid_prefetch_adjustment, 0,
                 "Amount to add when prefetch the next batch of OIDs. Never use this flag in "
                 "production environment. In unit test we use this flag to force allocation of "
                 "large Postgres OIDs.");

DEFINE_test_flag(uint64, delay_before_complete_expired_pg_sessions_shutdown_ms, 0,
                 "Inject delay before completing shutdown of expired PG sessions.");

DEFINE_RUNTIME_uint64(ysql_cdc_active_replication_slot_window_ms, 60000,
                      "Determines the window in milliseconds in which if a client has consumed the "
                      "changes of a ReplicationSlot across any tablet, then it is considered to be "
                      "actively used. ReplicationSlots which haven't been used in this interval are"
                      "considered to be inactive.");
TAG_FLAG(ysql_cdc_active_replication_slot_window_ms, advanced);

DEFINE_RUNTIME_int32(
    check_pg_object_id_allocators_interval_secs, 3600 * 3,
    "Interval at which pg object id allocators are checked for dropped databases.");
TAG_FLAG(check_pg_object_id_allocators_interval_secs, advanced);

DEFINE_NON_RUNTIME_int64(shmem_exchange_idle_timeout_ms, 2000 * yb::kTimeMultiplier,
    "Idle timeout interval in milliseconds used by shared memory exchange thread pool.");

DEFINE_test_flag(bool, pause_get_lock_status, false,
    "Whether tservers should pause before sending GetLockStatus requests.");

DECLARE_uint64(cdc_intent_retention_ms);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_bool(ysql_yb_enable_advisory_locks);
DECLARE_bool(enable_object_locking_for_table_locks);

namespace yb::tserver {

struct LockablePgClientSessionAccessorTag {};

namespace {

template <class Resp>
void Respond(const Status& status, Resp* resp, rpc::RpcContext* context) {
  if (!status.ok()) {
    StatusToPB(status, resp->mutable_status());
  }
  context->RespondSuccess();
}

class TxnAssignment {
 public:
  using Watcher = std::shared_ptr<client::YBTransactionPtr>;

  explicit TxnAssignment(rw_spinlock* mutex) : mutex_(*mutex) {}

  void Assign(const Watcher& watcher, IsDDL is_ddl) {
    std::lock_guard lock(mutex_);
    *(is_ddl ? &ddl_ : &plain_) = watcher;
  }

  client::YBTransactionPtr Get() {
    SharedLock lock(mutex_);
    // TODO(kramanathan): Return the DDL txn in preference to the plain txn until GHI #18451 is
    // resolved.
    auto ddl = ddl_.lock();
    if (ddl) {
      return *ddl;
    }

    auto plain = plain_.lock();
    if (plain) {
      return *plain;
    }

    return nullptr;
  }

 private:
  using WatcherWeak = Watcher::weak_type;

  rw_spinlock& mutex_;
  WatcherWeak plain_;
  WatcherWeak ddl_;
};

class PgClientSessionLocker;

class LockablePgClientSession {
 public:
  auto id() const { return session_.id(); }

  template <class... Args>
  explicit LockablePgClientSession(CoarseDuration lifetime, Args&&... args)
      : session_(std::forward<Args>(args)...),
        lifetime_(lifetime), expiration_(NewExpiration()) {
  }

  Status StartExchange(const std::string& instance_id, YBThreadPool& thread_pool) {
    shared_mem_manager_ = VERIFY_RESULT(PgSessionSharedMemoryManager::Make(
        instance_id, id(), Create::kTrue));
    session_.SetupSharedObjectLocking(shared_mem_manager_.object_locking_data());
    exchange_runnable_ = std::make_shared<SharedExchangeRunnable>(
        shared_mem_manager_.exchange(), shared_mem_manager_.session_id(), [this](size_t size) {
      Touch();
      std::unique_lock lock(mutex_);
      session_.ProcessSharedRequest(size, &exchange_runnable_->exchange());
    });
    auto status = exchange_runnable_->Start(thread_pool);
    if (!status.ok()) {
      exchange_runnable_.reset();
    }
    return status;
  }

  void StartShutdown(bool pg_service_shutting_down) {
    if (exchange_runnable_) {
      exchange_runnable_->StartShutdown();
    }
    session_.StartShutdown(pg_service_shutting_down);
  }

  bool ReadyToShutdown() const {
    return (!exchange_runnable_ || exchange_runnable_->ReadyToShutdown()) &&
           session_.ReadyToShutdown();
  }

  void CompleteShutdown() {
    if (exchange_runnable_) {
      exchange_runnable_->CompleteShutdown();
    }
    session_.CompleteShutdown();
  }

  CoarseTimePoint expiration() const {
    return expiration_.load(std::memory_order_acquire);
  }

  void SetExpiration(CoarseTimePoint value) {
    expiration_.store(value, std::memory_order_release);
  }

  void Touch() {
    auto new_expiration = NewExpiration();
    auto old_expiration = expiration_.load(std::memory_order_acquire);
    while (new_expiration > old_expiration) {
      if (expiration_.compare_exchange_weak(
          old_expiration, new_expiration, std::memory_order_acq_rel)) {
        break;
      }
    }
  }

  PgClientSession& session() { return session_; }

 private:
  friend class PgClientSessionLocker;

  CoarseTimePoint NewExpiration() const {
    return CoarseMonoClock::now() + lifetime_;
  }

  std::mutex mutex_;
  PgSessionSharedMemoryManager shared_mem_manager_;
  PgClientSession session_;
  std::shared_ptr<SharedExchangeRunnable> exchange_runnable_;
  const CoarseDuration lifetime_;
  std::atomic<CoarseTimePoint> expiration_;
};

using TransactionBuilder = std::function<
    client::YBTransactionPtr(
        TxnAssignment* dest, IsDDL, TransactionFullLocality, CoarseTimePoint,
        client::ForceCreateTransaction)>;

class SessionInfo {
 private:
  class PrivateTag {};

 public:
  SessionInfo(rw_spinlock& txn_assignment_mutex, PrivateTag)
      : txn_assignment_(&txn_assignment_mutex) {}

  LockablePgClientSession& session() { return *session_; }

  auto id() const { return session_->id(); }

  TxnAssignment& txn_assignment() { return txn_assignment_; }

  template <class... Args>
  static auto Make(
      rw_spinlock& txn_assignment_mutex, CoarseDuration lifetime, const TransactionBuilder& builder,
      Args&&... args) {
    auto session_info = std::make_shared<SessionInfo>(txn_assignment_mutex, PrivateTag{});
    session_info->session_.emplace(
        lifetime,
        [&builder, txn_assignment = &session_info->txn_assignment_](auto&&... builder_args) {
          return builder(txn_assignment, std::forward<decltype(builder_args)>(builder_args)...);
        },
        session_info,
        std::forward<Args>(args)...);
    return session_info;
  }

 private:
  TxnAssignment txn_assignment_;
  std::optional<LockablePgClientSession> session_;
};

void AddTransactionInfo(
    PgGetActiveTransactionListResponsePB* out, SessionInfo* src) {
  auto txn = src->txn_assignment().Get();
  if (txn) {
    auto& entry = *out->add_entries();
    entry.set_session_id(src->id());
    txn->id().AsSlice().CopyToBuffer(entry.mutable_txn_id());
  }
}

using LockablePgClientSessionPtr = std::shared_ptr<LockablePgClientSession>;

class PgClientSessionLocker {
 public:
  explicit PgClientSessionLocker(LockablePgClientSessionPtr lockable)
      : lockable_(std::move(lockable)), lock_(lockable_->mutex_) {
  }

  PgClientSession* operator->() const { return &lockable_->session(); }

 private:
  LockablePgClientSessionPtr lockable_;
  std::unique_lock<std::mutex> lock_;
};

using LockInfoWithCounter = std::pair<ObjectLockInfoPB, size_t>;
using SessionInfoPtr = std::shared_ptr<SessionInfo>;
using RemoteTabletServerPtr = std::shared_ptr<client::internal::RemoteTabletServer>;
using client::internal::RemoteTabletPtr;
using OldTxnsRespPtr = std::shared_ptr<tserver::GetOldTransactionsResponsePB>;
using OldSingleShardWaitersRespPtr = std::shared_ptr<tserver::GetOldSingleShardWaitersResponsePB>;
using OldTransactionMetadataPB = tserver::GetOldTransactionsResponsePB::OldTransactionMetadataPB;
using OldTransactionMetadataPBPtr = std::shared_ptr<OldTransactionMetadataPB>;
using OldSingleShardWaiterMetadataPB =
    tserver::GetOldSingleShardWaitersResponsePB::OldSingleShardWaiterMetadataPB;
using OldSingleShardWaiterMetadataPBPtr = std::shared_ptr<OldSingleShardWaiterMetadataPB>;
using OldTxnsRespPtrVariant = std::variant<OldSingleShardWaitersRespPtr, OldTxnsRespPtr>;
using OldTxnMetadataVariant =
    std::variant<OldSingleShardWaiterMetadataPB, OldTransactionMetadataPB>;
using OldTxnMetadataPtrVariant =
    std::variant<OldSingleShardWaiterMetadataPBPtr, OldTransactionMetadataPBPtr>;

void GetTablePartitionList(const client::YBTable& table, PgTablePartitionsPB* partition_list) {
  const auto table_partition_list = table.GetVersionedPartitions();
  const auto& partition_keys = partition_list->mutable_keys();
  partition_keys->Clear();
  partition_keys->Reserve(narrow_cast<int>(table_partition_list->keys.size()));
  for (const auto& key : table_partition_list->keys) {
    *partition_keys->Add() = key;
  }
  partition_list->set_version(table_partition_list->version);
}

struct OldTxnsRespInfo {
  const TabletId status_tablet_id;
  OldTxnsRespPtrVariant resp_ptr;
};

struct OldTxnMetadataPtrVariantVisitor {
  std::function<void(const OldSingleShardWaiterMetadataPBPtr&)> single_shard_visitor;
  std::function<void(const OldTransactionMetadataPBPtr&)> dist_txn_visitor;
  void operator()(const OldSingleShardWaiterMetadataPBPtr& arg) { single_shard_visitor(arg); }
  void operator()(const OldTransactionMetadataPBPtr& arg) { dist_txn_visitor(arg); }
  void operator()(auto&& arg) {
    LOG(DFATAL) << "Unsupported type passed for OldTxnMetadataPtrVariantVisitor";
  }
};

std::optional<OldTxnMetadataPtrVariant> MakeSharedOldTxnMetadataVariant(
    OldTxnMetadataVariant&& txn_meta_variant,
    const std::unordered_set<TransactionId>& object_lock_txn_ids) {
  OldTxnMetadataPtrVariant shared_txn_meta;
  if (auto txn_meta_pb_ptr = std::get_if<OldTransactionMetadataPB>(&txn_meta_variant)) {
    if (txn_meta_pb_ptr->tablets().empty()) {
      auto txn_id_or_status = FullyDecodeTransactionId(txn_meta_pb_ptr->transaction_id());
      if (!txn_id_or_status.ok()) {
        LOG(WARNING) << "Failed to decode txn id: " << txn_id_or_status.status();
        return std::nullopt;
      }
      if (object_lock_txn_ids.find(*txn_id_or_status) == object_lock_txn_ids.end()) {
        return std::nullopt;
      }
    }
    shared_txn_meta = std::make_shared<OldTransactionMetadataPB>(std::move(*txn_meta_pb_ptr));
  } else {
    auto meta_pb_ptr = std::get_if<OldSingleShardWaiterMetadataPB>(&txn_meta_variant);
    shared_txn_meta = std::make_shared<OldSingleShardWaiterMetadataPB>(std::move(*meta_pb_ptr));
  }
  return shared_txn_meta;
}

bool ShouldUseSecondarySpace(
    const TserverXClusterContextIf* xcluster_context, const NamespaceId& namespace_id) {
  // We use the secondary space when allocating on the target universe under xCluster automatic
  // mode.  We only care at the moment about allocations done by TServers on the behalf of Postgres
  // because the only allocations done by masters on behalf of Postgres processes are part of the
  // YSQL major upgrade, which currently does not allocate any OIDs of kinds that can cause
  // collisions for xCluster preserved OIDs.
  //
  // TODO(#26018): Make allocations done by master (that is, when this function is running in a
  // master process) also use the secondary space when allocating on the target universe under
  // xCluster automatic mode.
  return xcluster_context && xcluster_context->IsTargetAndInAutomaticMode(namespace_id);
}

template <class Extractor>
class ApplyToValue {
 public:
  using result_type = typename Extractor::result_type;

  template <class T>
  auto operator()(const T& t) const {
    return extractor_(t.value());
  }

 private:
  Extractor extractor_;
};

class SessionProvider {
 public:
  virtual ~SessionProvider() = default;
  virtual Result<PgClientSessionLocker> GetSession(uint64_t session_id) = 0;
  virtual rpc::Messenger& Messenger() = 0;
};

class PerformQuery : public std::enable_shared_from_this<PerformQuery>, public rpc::ThreadPoolTask,
                     public PgTablesQueryListener {
 public:
  using ContextHolder = rpc::TypedPBRpcContextHolder<PgPerformRequestPB, PgPerformResponsePB>;

  PerformQuery(
      SessionProvider& provider, ContextHolder&& context)
      : provider_(provider), context_(std::move(context)), tid_(Thread::UniqueThreadId()),
        wait_state_ptr_(ash::WaitStateInfo::CurrentWaitState()) {
  }

  void Ready(const PgTablesQueryResult& tables) override {
    tables_ = tables;
    if (Thread::UniqueThreadId() == tid_) {
      Run();
    } else {
      retained_self_ = shared_from_this();
      provider_.Messenger().ThreadPool().Enqueue(this);
    }
  }

 private:
  PgPerformRequestPB& req() {
    return context_.req();
  }

  PgPerformResponsePB& resp() {
    return context_.resp();
  }

  void Run() override {
    ADOPT_WAIT_STATE(wait_state_ptr_);
    SCOPED_WAIT_STATUS(OnCpu_Active);
    auto& context = context_.context();
    auto session = provider_.GetSession(req().session_id());
    if (!session.ok()) {
      Respond(session.status(), &resp(), &context);
      return;
    }
    (*session)->Perform(req(), resp(), std::move(context), *tables_);
  }

  void Done(const Status& status) override {
    retained_self_ = nullptr;
  }

  SessionProvider& provider_;
  rpc::TypedPBRpcContextHolder<PgPerformRequestPB, PgPerformResponsePB> context_;
  const int64_t tid_;
  std::optional<PgTablesQueryResult> tables_;
  std::shared_ptr<PerformQuery> retained_self_;

  // kept here in case the task is scheduled in another thread.
  const ash::WaitStateInfoPtr wait_state_ptr_;
};

class OpenTableQuery : public PgTablesQueryListener {
 public:
  using ContextHolder = rpc::TypedPBRpcContextHolder<PgOpenTableRequestPB, PgOpenTableResponsePB>;

  explicit OpenTableQuery(ContextHolder&& context) : context_(std::move(context)) {
  }

  void Ready(const PgTablesQueryResult& tables) override {
    auto res = tables.GetInfo(context_.req().table_id());
    auto& resp = context_.resp();
    if (!res.ok()) {
      Respond(res.status(), &resp, &context_.context());
      return;
    }
    *resp.mutable_info() = *res->schema;
    GetTablePartitionList(*res->table, resp.mutable_partitions());
    context_->RespondSuccess();
  }

 private:
  ContextHolder context_;
};

}  // namespace

class PgClientServiceImpl::Impl : public SessionProvider {
 public:
  explicit Impl(
      std::reference_wrapper<const TabletServerIf> tablet_server,
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock,
      TransactionManagerProvider transaction_manager_provider,
      TransactionPoolProvider transaction_pool_provider,
      rpc::Messenger* messenger, const TserverXClusterContextIf* xcluster_context,
      PgMutationCounter* pg_node_level_mutation_counter, MetricEntity* metric_entity,
      const MemTrackerPtr& parent_mem_tracker, const std::string& permanent_uuid,
      const server::ServerBaseOptions& tablet_server_opts)
      : tablet_server_(tablet_server.get()),
        client_future_(client_future),
        clock_(clock),
        transaction_manager_provider_(std::move(transaction_manager_provider)),
        transaction_pool_provider_(std::move(transaction_pool_provider)),
        messenger_(*messenger),
        table_cache_(client_future_),
        check_expired_sessions_("check_expired_sessions", &messenger->scheduler()),
        check_object_id_allocators_("check_object_id_allocators", &messenger->scheduler()),
        response_cache_(parent_mem_tracker, metric_entity),
        instance_id_(permanent_uuid),
        shared_mem_pool_(parent_mem_tracker, instance_id_),
        transaction_builder_([this](auto&&... args) {
          return BuildTransaction(std::forward<decltype(args)>(args)...);
        }),
        advisory_locks_table_(client_future_),
        session_context_{
            .xcluster_context = xcluster_context,
            .advisory_locks_table = advisory_locks_table_,
            .pg_node_level_mutation_counter = pg_node_level_mutation_counter,
            .clock = clock_,
            .table_cache = table_cache_,
            .response_cache = response_cache_,
            .sequence_cache = sequence_cache_,
            .shared_mem_pool = shared_mem_pool_,
            .metrics = PgClientSessionMetrics{metric_entity},
            .instance_uuid = instance_id_,
            .lock_owner_registry =
                tablet_server_.ObjectLockSharedStateManager()
                    ? &tablet_server_.ObjectLockSharedStateManager()->registry()
                    : nullptr,
            .transaction_manager_provider = transaction_manager_provider_},
        cdc_state_table_(client_future_),
        txn_snapshot_manager_(
            instance_id_,
            [this](const auto& ts_uuid) -> Result<std::shared_ptr<TabletServerServiceProxy>> {
              auto servers = VERIFY_RESULT(tablet_server_.GetRemoteTabletServers({ts_uuid}));
              SCHECK_EQ(servers.size(), 1, NotFound, "Failed to find single ts");
              auto& ts = *servers.front();
              RETURN_NOT_OK(ts.InitProxy(&client()));
              return ts.proxy();
            }) {
    DCHECK(!permanent_uuid.empty());
    ScheduleCheckExpiredSessions(CoarseMonoClock::now());
    ScheduleCheckObjectIdAllocators();
    if (FLAGS_pg_client_use_shared_memory) {
      WARN_NOT_OK(PgSessionSharedMemoryManager::Cleanup(instance_id_),
                  "Cleanup shared memory failed");
    }
    shared_mem_pool_.Start(messenger->scheduler());
  }

  void Shutdown() {
    std::vector<SessionInfoPtr> sessions;
    {
      std::lock_guard lock(mutex_);
      if (shutting_down_) {
        return;
      }
      shutting_down_ = true;
      sessions.reserve(sessions_.size());
      for (const auto& session : sessions_) {
        sessions.push_back(session);
      }
    }
    cdc_state_table_.reset();
    for (const auto& session : sessions) {
      session->session().StartShutdown(/* pg_service_shutting_down */ true);
    }
    for (const auto& session : sessions) {
      session->session().CompleteShutdown();
    }
    sessions.clear();
    auto schedulers = {
        std::reference_wrapper(check_expired_sessions_),
        std::reference_wrapper(check_object_id_allocators_),
        std::reference_wrapper(check_ysql_lease_)};
    for (auto& task : schedulers) {
      task.get().StartShutdown();
    }
    for (auto& task : schedulers) {
      task.get().CompleteShutdown();
    }
    if (exchange_thread_pool_) {
      exchange_thread_pool_->Shutdown();
    }
  }

  ~Impl() override {
    Shutdown();
  }

  uint64_t lease_epoch() {
    auto maybe_lease_info = tablet_server_.GetYSQLLeaseInfo();
    if (maybe_lease_info.ok()) {
      return maybe_lease_info->lease_epoch;
    }
    return 0;
  }

  Status Heartbeat(
      const PgHeartbeatRequestPB& req, PgHeartbeatResponsePB* resp, rpc::RpcContext* context) {
    if (req.session_id() == std::numeric_limits<uint64_t>::max()) {
      return Status::OK();
    }

    if (req.session_id()) {
      return ResultToStatus(DoGetSession(req.session_id()));
    }

    auto session_id = ++session_serial_no_;
    auto session_info = SessionInfo::Make(
        txns_assignment_mutexes_[session_id % txns_assignment_mutexes_.size()],
        FLAGS_pg_client_session_expiration_ms * 1ms, transaction_builder_, client(),
        session_context_, session_id, req.pid(), lease_epoch(),
        tablet_server_.ts_local_lock_manager(), messenger_.scheduler());
    resp->set_session_id(session_id);
    if (FLAGS_pg_client_use_shared_memory) {
      std::call_once(exchange_thread_pool_once_flag_, [this] {
        exchange_thread_pool_ = std::make_unique<YBThreadPool>(ThreadPoolOptions {
          .name = "shmem_exchange",
          .max_workers = ThreadPoolOptions::kUnlimitedWorkersWithoutQueue,
          .min_workers = 1,
          .idle_timeout = MonoDelta::FromMilliseconds(FLAGS_shmem_exchange_idle_timeout_ms),
        });
      });
      auto status = session_info->session().StartExchange(instance_id_, *exchange_thread_pool_);
      if (status.ok()) {
        resp->set_instance_id(instance_id_);
      } else {
        LOG(WARNING) << "Failed to start exchange for " << session_id << ": " << status;
      }
    }

    context->ListenConnectionShutdown([this, session_id, pid = req.pid()]() {
#if defined(__APPLE__)
      auto delay = 250ms;
#else
      auto delay = RegularBuildVsSanitizers(50ms, 1000ms);
#endif
      messenger_.scheduler().Schedule([this, session_id, pid](const Status& status) {
        if (!status.ok()) {
          // Task was aborted.
          return;
        }
        CheckSessionShutdown(pid, session_id);
        // Give some time for process to exit after connection shutdown.
      }, delay);
    });

    std::lock_guard lock(mutex_);
    if (shutting_down_) {
      return STATUS(ShutdownInProgress, "PG client service shutting down");
    }
    auto it = sessions_.insert(std::move(session_info)).first;
    session_expiration_queue_.emplace((**it).session().expiration(), session_id);
    return Status::OK();
  }

  void CheckSessionShutdown(pid_t pid, int64_t session_id) {
    auto sid = getsid(pid);
    if (sid != -1 || errno != ESRCH) {
      return;
    }
    auto now = CoarseMonoClock::now();
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      return;
    }
    VLOG(2) << "Requesting session expiry for session " << session_id << " with pid " << pid;
    (**it).session().SetExpiration(now);
    session_expiration_queue_.emplace(now, session_id);
    ScheduleCheckExpiredSessions(now);
  }

  void OpenTable(
      const PgOpenTableRequestPB& req, PgOpenTableResponsePB* resp, rpc::RpcContext context) {
    PgTableCacheGetOptions options = {
      .reopen = req.reopen(),
      .min_ysql_catalog_version = req.ysql_catalog_version(),
      .include_hidden = master::IncludeHidden(req.include_hidden()),
    };
    auto query = std::make_shared<OpenTableQuery>(
        MakeTypedPBRpcContextHolder(req, resp, std::move(context)));
    table_cache_.GetTables(std::span(&req.table_id(), 1), query, options);
  }

  Status GetTablePartitionList(
      const PgGetTablePartitionListRequestPB& req,
      PgGetTablePartitionListResponsePB* resp,
      rpc::RpcContext* context) {
    const auto table = VERIFY_RESULT(table_cache_.Get(req.table_id()));
    tserver::GetTablePartitionList(*table, resp->mutable_partitions());
    return Status::OK();
  }

  Status GetDatabaseInfo(
      const PgGetDatabaseInfoRequestPB& req, PgGetDatabaseInfoResponsePB* resp,
      rpc::RpcContext* context) {
    return client().GetNamespaceInfo(GetPgsqlNamespaceId(req.oid()), resp->mutable_info());
  }

  Result<PgPollVectorIndexReadyResponsePB> PollVectorIndexReady(
      const PgPollVectorIndexReadyRequestPB& req, CoarseTimePoint deadline) {
    // TODO(vector_index) Move method implementation to the place where actual polling could be
    // implemented. So method could wait until deadline of index is ready.
    auto& client = this->client();
    bool ready = false;
    for (;;) {
      auto table = VERIFY_RESULT(client.OpenTable(req.table_id()));
      auto tablets = VERIFY_RESULT(client.LookupAllTabletsFuture(table, deadline).get());
      ready = true;
      for (const auto& tablet : tablets) {
        auto* leader = tablet->LeaderTServer();
        if (!leader) {
          VLOG_WITH_FUNC(4) << "No leader for " << tablet->tablet_id();
          ready = false;
          break;
        }
        auto proxy = leader->ObtainProxy(client);
        tserver::GetTabletStatusRequestPB status_req;
        tserver::GetTabletStatusResponsePB status_resp;
        status_req.set_tablet_id(tablet->tablet_id());
        rpc::RpcController controller;
        controller.set_deadline(deadline);
        auto status = leader->proxy()->GetTabletStatus(status_req, &status_resp, &controller);
        if (!status.ok()) {
          LOG_WITH_FUNC(INFO) << "Failed to query tablet " << tablet->tablet_id() << ": " << status;
          ready = false;
          break;
        }
        bool tablet_ready = false;
        VLOG_WITH_FUNC(4)
            << "Finished on " << tablet->tablet_id() << ": "
            << AsString(status_resp.tablet_status().vector_index_finished_backfills());
        for (const auto& table_id : status_resp.tablet_status().vector_index_finished_backfills()) {
          if (table_id == req.table_id()) {
            tablet_ready = true;
            break;
          }
        }
        if (!tablet_ready) {
          ready = false;
          break;
        }
      }
      if (ready) {
        break;
      }
      auto wait_time = 100ms;
      if (CoarseMonoClock::Now() + wait_time * 2 > deadline) {
        break;
      }
      std::this_thread::sleep_for(wait_time);
    }
    PgPollVectorIndexReadyResponsePB result;
    result.set_ready(ready);
    return result;
  }

  Status IsInitDbDone(
      const PgIsInitDbDoneRequestPB& req, PgIsInitDbDoneResponsePB* resp,
      rpc::RpcContext* context) {
    HostPort master_leader_host_port = client().GetMasterLeaderAddress();
    auto proxy = std::make_shared<master::MasterAdminProxy>(
        &client().proxy_cache(), master_leader_host_port);
    rpc::RpcController rpc;
    master::IsInitDbDoneRequestPB master_req;
    master::IsInitDbDoneResponsePB master_resp;
    RETURN_NOT_OK(proxy->IsInitDbDone(master_req, &master_resp, &rpc));
    if (master_resp.has_error()) {
      return STATUS_FORMAT(
          RuntimeError,
          "IsInitDbDone RPC response hit error: $0",
          master_resp.error().ShortDebugString());
    }
    if (master_resp.done() && master_resp.has_initdb_error() &&
        !master_resp.initdb_error().empty()) {
      return STATUS_FORMAT(RuntimeError, "initdb failed: $0", master_resp.initdb_error());
    }
    VLOG(1) << "IsInitDbDone response: " << master_resp.ShortDebugString();
    // We return true if initdb finished running, as well as if we know that it created the first
    // table (pg_proc) to make initdb idempotent on upgrades.
    resp->set_done(master_resp.done() || master_resp.pg_proc_exists());
    return Status::OK();
  }

  Status ReserveOids(
      const PgReserveOidsRequestPB& req, PgReserveOidsResponsePB* resp, rpc::RpcContext* context) {
    // This function is only called in initdb mode or when
    // --ysql_enable_pg_per_database_oid_allocator is false.

    auto db_oid = req.database_oid();
    auto namespace_id = GetPgsqlNamespaceId(db_oid);
    bool need_secondary_space =
        ShouldUseSecondarySpace(session_context_.xcluster_context, namespace_id);
    SCHECK(
        !need_secondary_space, NotSupported,
        "--ysql_enable_pg_per_database_oid_allocator cannot be false in universes that are the "
        "target of xCluster automatic-mode replication");

    uint32_t begin_oid, end_oid;
    RETURN_NOT_OK(client().ReservePgsqlOids(
        namespace_id, req.next_oid(), req.count(), /*use_secondary_space=*/false, &begin_oid,
        &end_oid));
    resp->set_begin_oid(begin_oid);
    resp->set_end_oid(end_oid);

    return Status::OK();
  }

  Status GetNewObjectId(
      const PgGetNewObjectIdRequestPB& req, PgGetNewObjectIdResponsePB* resp,
      rpc::RpcContext* context) {
    auto db_oid = req.db_oid();
    auto namespace_id = GetPgsqlNamespaceId(db_oid);
    bool use_secondary_space =
        ShouldUseSecondarySpace(session_context_.xcluster_context, namespace_id);
    std::lock_guard lock(mutex_);
    auto& oid_chunk = reserved_oids_map_[db_oid];
    if (oid_chunk.allocated_from_secondary_space != use_secondary_space) {
      // Flush any previous OIDs we have cached when we switch spaces.
      oid_chunk.oid_count = 0;
      oid_chunk.next_oid =
          use_secondary_space ? kPgFirstSecondarySpaceObjectId : kPgFirstNormalObjectId;
      oid_chunk.allocated_from_secondary_space = use_secondary_space;
      oid_chunk.oid_cache_invalidations_count = 0;
    }
    uint32_t highest_received_invalidations_count =
        tablet_server_.get_oid_cache_invalidations_count();
    while (oid_chunk.oid_count == 0 ||
           oid_chunk.oid_cache_invalidations_count < highest_received_invalidations_count) {
      // We don't have any valid OIDs left so fetch more.
      const uint32_t next_oid =
          oid_chunk.next_oid + static_cast<uint32_t>(FLAGS_TEST_ysql_oid_prefetch_adjustment);
      uint32_t begin_oid, end_oid, oid_cache_invalidations_count;
      RETURN_NOT_OK(client().ReservePgsqlOids(
          namespace_id, next_oid, FLAGS_ysql_oid_cache_prefetch_size, use_secondary_space,
          &begin_oid, &end_oid, &oid_cache_invalidations_count));
      oid_chunk.next_oid = begin_oid;
      oid_chunk.oid_count = end_oid - begin_oid;
      oid_chunk.oid_cache_invalidations_count = oid_cache_invalidations_count;
      VLOG(1) << "Reserved oids in database: " << db_oid << ", next_oid: " << next_oid
              << ", begin_oid: " << begin_oid << ", end_oid: " << end_oid
              << ", invalidation count: " << oid_cache_invalidations_count;
    }
    uint32_t new_oid = oid_chunk.next_oid;
    oid_chunk.next_oid++;
    oid_chunk.oid_count--;
    resp->set_new_oid(new_oid);
    return Status::OK();
  }

  Status GetCatalogMasterVersion(
      const PgGetCatalogMasterVersionRequestPB& req,
      PgGetCatalogMasterVersionResponsePB* resp,
      rpc::RpcContext* context) {
    uint64_t version;
    RETURN_NOT_OK(client().DEPRECATED_GetYsqlCatalogMasterVersion(&version));
    resp->set_version(version);
    return Status::OK();
  }

  Status GetXClusterRole(
      const PgGetXClusterRoleRequestPB& req, PgGetXClusterRoleResponsePB* resp,
      rpc::RpcContext* context) {
    NamespaceId namespace_id = GetPgsqlNamespaceId(req.db_oid());
    const auto* xcluster_context = session_context_.xcluster_context;
    int32_t role = XClusterNamespaceInfoPB_XClusterRole_UNAVAILABLE;
    if (xcluster_context) {
      role = xcluster_context->GetXClusterRole(namespace_id);
    }
    resp->set_xcluster_role(role);
    return Status::OK();
  }

  Status CreateSequencesDataTable(
      const PgCreateSequencesDataTableRequestPB& req,
      PgCreateSequencesDataTableResponsePB* resp,
      rpc::RpcContext* context) {
    return tserver::CreateSequencesDataTable(&client(), context->GetClientDeadline());
  }

  std::future<Result<OldTxnsRespInfo>> DoGetOldTransactionsForTablet(
      const uint32_t min_txn_age_ms, const uint32_t max_num_txns,
      const RemoteTabletServerPtr& remote_ts, const TabletId& tablet_id) {
    auto req = std::make_shared<tserver::GetOldTransactionsRequestPB>();
    req->set_tablet_id(tablet_id);
    req->set_min_txn_age_ms(min_txn_age_ms);
    req->set_max_num_txns(max_num_txns);

    return MakeFuture<Result<OldTxnsRespInfo>>([&](auto callback) {
      auto resp = std::make_shared<GetOldTransactionsResponsePB>();
      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      remote_ts->proxy()->GetOldTransactionsAsync(
          *req.get(), resp.get(), controller.get(),
          [req, callback, controller, resp, remote_ts] {
        auto s = controller->status();
        if (!s.ok()) {
          s = s.CloneAndPrepend(Format(
              "GetOldTransactions request for tablet $0 to tserver $1 failed: ",
              req->tablet_id(), remote_ts->permanent_uuid()));
          return callback(s);
        }
        callback(OldTxnsRespInfo {
          .status_tablet_id = req->tablet_id(),
          .resp_ptr = std::move(resp),
        });
      });
    });
  }

  std::future<Result<OldTxnsRespInfo>> DoGetOldSingleShardWaiters(
      const uint32_t min_txn_age_ms, const uint32_t max_num_txns,
      const RemoteTabletServerPtr& remote_ts) {
    auto req = std::make_shared<tserver::GetOldSingleShardWaitersRequestPB>();
    req->set_min_txn_age_ms(min_txn_age_ms);
    req->set_max_num_txns(max_num_txns);

    return MakeFuture<Result<OldTxnsRespInfo>>([&](auto callback) {
      auto resp = std::make_shared<GetOldSingleShardWaitersResponsePB>();
      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      remote_ts->proxy()->GetOldSingleShardWaitersAsync(
          *req.get(), resp.get(), controller.get(),
          [req, callback, controller, resp, remote_ts] {
        auto s = controller->status();
        if (!s.ok()) {
          s = s.CloneAndPrepend(Format(
              "GetOldSingleShardWaiters request to tserver $0 failed: ",
              remote_ts->permanent_uuid()));
          return callback(s);
        }
        callback(OldTxnsRespInfo {
          .status_tablet_id = "",
          .resp_ptr = std::move(resp),
        });
      });
    });
  }

  // Comparator used for maintaining a max heap of old transactions based on their start times.
  struct OldTxnMetadataVariantComparator {
    bool operator()(
        const OldTxnMetadataPtrVariant& lhs, const OldTxnMetadataPtrVariant& rhs) const {
      // Order is reversed so that we pop newer transactions first.
      auto lhs_start_time = get_start_time(lhs);
      auto rhs_start_time = get_start_time(rhs);
      if (lhs_start_time != rhs_start_time) {
        return lhs_start_time < rhs_start_time;
      }
      return get_raw_ptr(lhs) > get_raw_ptr(rhs);
    }

   private:
    void* get_raw_ptr(const OldTxnMetadataPtrVariant& old_txn_meta_ptr_variant) const {
      return std::visit([&](auto&& old_txn_meta_pb_ptr) -> void* {
        return old_txn_meta_pb_ptr.get();
      }, old_txn_meta_ptr_variant);
    }

    uint64_t get_start_time(const OldTxnMetadataPtrVariant& old_txn_meta_ptr_variant) const {
      return std::visit([&](auto&& old_txn_meta_pb_ptr) {
        return old_txn_meta_pb_ptr->start_time();
      }, old_txn_meta_ptr_variant);
    }
  };

  // Fetches location info of involved tablets. On seeing tablet split errors in the response,
  // retries the operation with split child tablet ids. On encountering further tablet split
  // errors, returns a bad status.
  Result<std::vector<RemoteTabletServerPtr>> ReplaceSplitTabletsAndGetLocations(
      GetLockStatusRequestPB* req, int retry_attempt = 0) {
    std::vector<TabletId> tablet_ids;
    tablet_ids.reserve(req->transactions_by_tablet().size());
    for (const auto& [tablet_id, _] : req->transactions_by_tablet()) {
      tablet_ids.push_back(tablet_id);
    }

    auto resp = VERIFY_RESULT(client().GetTabletLocations(tablet_ids));
    auto& txns_by_tablet = *req->mutable_transactions_by_tablet();
    Status combined_status;
    for (const auto& error : resp.errors()) {
      const auto& tablet_id = error.tablet_id();
      auto status = StatusFromPB(error.status());
      auto split_child_ids = SplitChildTabletIdsData(status).value();
      // Return bad status on observing any error types other than tablet split.
      if (split_child_ids.empty()) {
        return status.CloneAndPrepend(Format("GetLocations for tablet $0 failed: ", tablet_id));
      }
      // Replace the split parent tablet entry in GetLockStatusRequestPB with the child tablets ids.
      auto& split_parent_entry = txns_by_tablet[tablet_id];
      const auto& first_child_id = split_child_ids[0];
      txns_by_tablet[first_child_id].mutable_transactions()->
                                     Swap(split_parent_entry.mutable_transactions());
      for (size_t i = 1 ; i < split_child_ids.size() ; i++) {
        txns_by_tablet[split_child_ids[i]].mutable_transactions()->
                                           CopyFrom(txns_by_tablet[first_child_id].transactions());
      }
      txns_by_tablet.erase(tablet_id);
      combined_status = status.CloneAndAppend(combined_status.message());
    }
    if (!resp.errors().empty()) {
      // Re-request location info of updated tablet set if not already in the retry context.
      if (retry_attempt > FLAGS_pg_locks_max_tablet_lookup_retries) {
        return combined_status;
      }
      VLOG(1) << "Retrying tablet location lookup after handling splits "
              << "(attempt " << retry_attempt + 1 << ")";
      AtomicFlagSleepMs(&FLAGS_pg_locks_retry_delay_ms);
      return ReplaceSplitTabletsAndGetLocations(req, ++retry_attempt);
    }

    std::unordered_set<std::string> tserver_uuids;
    for (const auto& tablet_location_pb : resp.tablet_locations()) {
      bool has_leader = false;
      for (const auto& replica : tablet_location_pb.replicas()) {
        if (replica.role() == PeerRole::LEADER) {
          tserver_uuids.insert(replica.ts_info().permanent_uuid());
          has_leader = true;
          break;
        }
      }
      if (!has_leader) {
        if (retry_attempt > FLAGS_pg_locks_max_tablet_lookup_retries) {
          return STATUS(IllegalState, Format(
                        "No leader found for tablet $0 after $1 attempts",
                        tablet_location_pb.tablet_id(), retry_attempt));
        }
        LOG_WITH_FUNC(INFO) << "Tablet " << tablet_location_pb.tablet_id()
                            << " have no leader, retrying (attempt " << retry_attempt + 1 << ")";
        AtomicFlagSleepMs(&FLAGS_pg_locks_retry_delay_ms);
        return ReplaceSplitTabletsAndGetLocations(req, ++retry_attempt);
      }
    }
    return tablet_server_.GetRemoteTabletServers(tserver_uuids);
  }

  Status GetLockStatus(
      const PgGetLockStatusRequestPB& req, PgGetLockStatusResponsePB* resp,
      rpc::RpcContext* context) {
    auto remote_tservers = VERIFY_RESULT(tablet_server_.GetRemoteTabletServers());
    GetLockStatusRequestPB lock_status_req;
    lock_status_req.set_max_txn_locks_per_tablet(req.max_txn_locks_per_tablet());
    if (!req.transaction_id().empty()) {
      // TODO(pglocks): Forward the request to tservers hosting the involved tablets of the txn,
      // as opposed to broadcasting the request to all live tservers.
      // https://github.com/yugabyte/yugabyte-db/issues/17886.
      //
      // GetLockStatusRequestPB supports providing multiple transaction ids, but postgres sends
      // only one transaction id in PgGetLockStatusRequestPB for now.
      // TODO(pglocks): Once we call GetTransactionStatus for involved tablets, ensure we populate
      // aborted_subtxn_set in the GetLockStatusRequests that we send to involved tablets as well.
      //
      // TODO: Support specific transaction id filtering for getting object lock status
      // https://github.com/yugabyte/yugabyte-db/issues/27331
      lock_status_req.add_transaction_ids(req.transaction_id());
      return DoGetLockStatus(&lock_status_req, resp, context, remote_tservers);
    }

    std::unordered_map<ObjectLockContext, LockInfoWithCounter> object_lock_info_map;
    RETURN_NOT_OK(GetObjectLockStatus(remote_tservers, resp, &object_lock_info_map));
    // Create a map to filter transactions from transaction coordinator for object locks
    // The bool value indicates whether this txn_id can be exposed to pg_locks.
    std::unordered_set<TransactionId> object_lock_txn_ids;
    for (const auto& [context, _] : object_lock_info_map) {
       object_lock_txn_ids.insert(context.txn_id);
    }

    const auto& min_txn_age_ms = req.min_txn_age_ms();
    const auto& max_num_txns = req.max_num_txns();
    RSTATUS_DCHECK(max_num_txns > 0, InvalidArgument,
                   "Request must contain max_num_txns > 0, got $0", max_num_txns);
    // Sleep before fetching old transactions and their involved tablets. This is necessary for
    // yb_lock_status tests that expect to see complete lock info of respective transaction(s).
    // Else, the coordinator might not return updated involved tablet(s) and we could end up
    // returning incomplete lock info for a given transaction.
    if (PREDICT_FALSE(FLAGS_TEST_delay_before_get_old_transactions_heartbeat_intervals > 0)) {
      auto delay_usec = FLAGS_TEST_delay_before_get_old_transactions_heartbeat_intervals
                        * FLAGS_transaction_heartbeat_usec;
      SleepFor(MonoDelta::FromMicroseconds(delay_usec));
    }

    std::vector<std::future<Result<OldTxnsRespInfo>>> res_futures;
    std::unordered_set<TabletId> status_tablet_ids;
    for (const auto& remote_tserver : remote_tservers) {
      auto txn_status_tablets = VERIFY_RESULT(
            client().GetTransactionStatusTablets(remote_tserver->cloud_info_pb()));

      RETURN_NOT_OK(remote_tserver->InitProxy(&client()));
      auto proxy = remote_tserver->proxy();
      for (const auto& tablet : txn_status_tablets.global_tablets) {
        res_futures.push_back(
            DoGetOldTransactionsForTablet(min_txn_age_ms, max_num_txns, remote_tserver, tablet));
        status_tablet_ids.insert(tablet);
      }
      for (const auto& tablet : txn_status_tablets.region_local_tablets) {
        res_futures.push_back(
            DoGetOldTransactionsForTablet(min_txn_age_ms, max_num_txns, remote_tserver, tablet));
        status_tablet_ids.insert(tablet);
      }
      // Query for oldest single shard waiting transactions as well.
      res_futures.push_back(
          DoGetOldSingleShardWaiters(min_txn_age_ms, max_num_txns, remote_tserver));
    }
    // Limit num transactions to max_num_txns for which lock status is being queried.
    //
    // TODO(pglocks): We could end up storing duplicate records for the same transaction in the
    // priority queue, and end up reporting locks of #transaction < max_num_txns. This will be
    // fixed once https://github.com/yugabyte/yugabyte-db/issues/18140 is addressed.
    std::priority_queue<OldTxnMetadataPtrVariant,
                        std::vector<OldTxnMetadataPtrVariant>,
                        OldTxnMetadataVariantComparator> old_txns_pq;
    StatusToPB(Status::OK(), resp->mutable_status());
    for (auto it = res_futures.begin();
         it != res_futures.end() && resp->status().code() == AppStatusPB::OK; ++it) {
      auto res = it->get();
      if (!res.ok()) {
        // A node could be unavailable. We need not fail the pg_locks query if we see at least one
        // response for all of the status tablets.
        LOG(INFO) << res.status();
        continue;
      }

      std::visit([&](auto&& old_txns_resp) {
        if (old_txns_resp->has_error()) {
          // Ignore leadership and NOT_FOUND errors as we broadcast the request to all tservers.
          if (old_txns_resp->error().code() == TabletServerErrorPB::NOT_THE_LEADER ||
              old_txns_resp->error().code() == TabletServerErrorPB::TABLET_NOT_FOUND) {
            return;
          }
          const auto& s = StatusFromPB(old_txns_resp->error().status());
          StatusToPB(s, resp->mutable_status());
          return;
        }

        status_tablet_ids.erase(res->status_tablet_id);
        for (auto& old_txn : old_txns_resp->txn()) {
          auto old_txn_ptr =
              MakeSharedOldTxnMetadataVariant(std::move(old_txn), object_lock_txn_ids);
          if (!old_txn_ptr) {
            continue;
          }
          old_txns_pq.push(std::move(*old_txn_ptr));
          while (old_txns_pq.size() > max_num_txns) {
            VLOG(4) << "Dropping old transaction with metadata "
                    << std::visit([](auto&& arg) {
                         return arg->ShortDebugString();
                       }, old_txns_pq.top());
            old_txns_pq.pop();
          }
        }
      }, res->resp_ptr);
    }
    if (resp->status().code() != AppStatusPB::OK) {
      return Status::OK();
    }
    // Set status and return if we don't get a valid resp for all status tablets at least once.
    // It's ok if we get more than one resp for a status tablet, as we accumulate received
    // transactions and their involved tablets.
    if(!status_tablet_ids.empty()) {
      StatusToPB(
          STATUS_FORMAT(IllegalState,
                        "Couldn't fetch old transactions for the following status tablets: $0",
                        status_tablet_ids),
          resp->mutable_status());
      return Status::OK();
    }

    uint64_t max_single_shard_waiter_start_time = 0;
    bool include_single_shard_waiters = false;
    std::unordered_set<TransactionId> allowed_txn_ids;
    while (!old_txns_pq.empty()) {
      auto& old_txn = old_txns_pq.top();
      std::visit(OldTxnMetadataPtrVariantVisitor {
        [&](const OldSingleShardWaiterMetadataPBPtr& arg) {
          include_single_shard_waiters = true;
          if (max_single_shard_waiter_start_time == 0) {
            max_single_shard_waiter_start_time = arg->start_time();
          }
          (*lock_status_req.mutable_transactions_by_tablet())[arg->tablet()];
        },
        [&](const OldTransactionMetadataPBPtr& arg) {
          if (max_single_shard_waiter_start_time == 0) {
            max_single_shard_waiter_start_time = arg->start_time();
          }
          auto& txn_id = arg->transaction_id();
          auto& node_entry = (*resp->mutable_transactions_by_node())[arg->host_node_uuid()];
          node_entry.add_transaction_ids(txn_id);
          auto& involved_tablets = arg->tablets();
          for (const auto& tablet_id : involved_tablets) {
            // DDL statements might have master tablet as one of their involved tablets, skip it.
            if (tablet_id == master::kSysCatalogTabletId) {
              continue;
            }
            auto& tablet_entry = (*lock_status_req.mutable_transactions_by_tablet())[tablet_id];
            auto* transaction = tablet_entry.add_transactions();
            transaction->set_id(txn_id);
            transaction->mutable_aborted()->Swap(arg->mutable_aborted_subtxn_set());
          }
          auto decoded_txn_id_or_status = FullyDecodeTransactionId(txn_id);
          if (!decoded_txn_id_or_status.ok()) {
            LOG(WARNING) << "Failed to decode txn id: " << decoded_txn_id_or_status.status();
            return;
          }
          allowed_txn_ids.insert(*decoded_txn_id_or_status);
        }
      }, old_txn);
      old_txns_pq.pop();
    }

    // Populate the response with consolidated object locks info.
    for (auto& [context, object_lock_info] : object_lock_info_map) {
      if (allowed_txn_ids.contains(context.txn_id)) {
        resp->add_object_lock_infos()->Swap(&object_lock_info.first);
      }
    }
    VLOG(4) << "Added " << resp->object_lock_infos_size() << " object locks to response";

    if (include_single_shard_waiters) {
      lock_status_req.set_max_single_shard_waiter_start_time_us(max_single_shard_waiter_start_time);
    }
    TEST_PAUSE_IF_FLAG(TEST_pause_get_lock_status);
    auto remote_tservers_with_locks = VERIFY_RESULT(
        ReplaceSplitTabletsAndGetLocations(&lock_status_req));
    return DoGetLockStatus(&lock_status_req, resp, context, remote_tservers_with_locks);
  }

  // Merges the src PgGetLockStatusResponsePB into dest, while preserving existing entries in dest.
  Status MergeLockStatusResponse(PgGetLockStatusResponsePB* dest,
                                 PgGetLockStatusResponsePB* src) {
    if (src->status().code() != AppStatusPB::OK) {
      return StatusFromPB(src->status());
    }
    dest->add_node_locks()->Swap(src->mutable_node_locks(0));
    for (auto i = 0 ; i < src->node_locks_size() ; i++) {
      dest->add_node_locks()->Swap(src->mutable_node_locks(i));
    }
    return Status::OK();
  }

  // Get object lock status from all tablet servers and the master.
  //
  // The function first collects object lock status from all tablet servers.
  // Then, to accurately determine the state of global locks (those that span across
  // the master and all tablet servers), it collects lock status from the master.
  // A global lock is considered GRANTED only if all participants(the master and
  // every tablet server) report it as GRANTED. If any participant reports it as WAITING,
  // or if any participant is missing, the global lock is treated as WAITING.
  Status GetObjectLockStatus(
      const std::vector<RemoteTabletServerPtr>& remote_tservers,
      PgGetLockStatusResponsePB* resp,
      std::unordered_map<ObjectLockContext, LockInfoWithCounter>* object_lock_info_map) {
    if (!FLAGS_enable_object_locking_for_table_locks) {
      return Status::OK();
    }

    // Collect object lock infos from all tservers.
    GetObjectLockStatusRequestPB req;
    std::vector<std::future<Status>> status_futures;
    status_futures.reserve(remote_tservers.size());
    std::vector<std::shared_ptr<GetObjectLockStatusResponsePB>> node_responses;
    node_responses.reserve(remote_tservers.size());
    for (const auto& remote_tserver : remote_tservers) {
      RETURN_NOT_OK(remote_tserver->InitProxy(&client()));
      auto proxy = remote_tserver->proxy();
      auto status_promise = std::make_shared<std::promise<Status>>();
      status_futures.push_back(status_promise->get_future());
      auto node_resp = std::make_shared<GetObjectLockStatusResponsePB>();
      node_responses.push_back(node_resp);
      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      proxy->GetObjectLockStatusAsync(
          req, node_resp.get(), controller.get(), [controller, status_promise] {
            status_promise->set_value(controller->status());
          });
    }

    // Collect global object lock infos from master.
    // There may be some global locks , waiting on the master, are not yet be visible to TServers.
    // Also ensures accurate wait_start timestamps, since master observes the earliest wait times.
    auto master_proxy = std::make_shared<master::MasterDdlProxy>(
        &client().proxy_cache(), client().GetMasterLeaderAddress());
    master::GetObjectLockStatusRequestPB master_req;
    master::GetObjectLockStatusResponsePB master_resp;
    auto master_controller = std::make_shared<rpc::RpcController>();
    auto master_promise = std::make_shared<std::promise<Status>>();
    master_proxy->GetObjectLockStatusAsync(
        master_req, &master_resp, master_controller.get(),
        [master_controller, master_promise] {
          master_promise->set_value(master_controller->status());
        });

    // Process responses from tservers
    for (size_t i = 0; i < status_futures.size(); i++) {
      auto& node_resp = node_responses[i];
      auto s = status_futures[i].get();
      if (!s.ok()) {
        return s;
      }
      if (node_resp->has_error()) {
        *resp->mutable_status() = node_resp->error().status();
        return Status::OK();
      }
      VLOG(4) << "Processing GetObjectLockStatusResponsePB from tserver: "
              << node_resp->ShortDebugString();
      for (int j = 0; j < node_resp->object_lock_infos_size(); j++) {
        auto* lock_infos = node_resp->mutable_object_lock_infos(j);
        auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(lock_infos->transaction_id()));
        auto [it, inserted] = object_lock_info_map->try_emplace(
          ObjectLockContext{
            txn_id,
            lock_infos->subtransaction_id(),
            lock_infos->database_oid(),
            lock_infos->relation_oid(),
            lock_infos->object_oid(),
            lock_infos->object_sub_oid(),
            lock_infos->mode()
          },
          LockInfoWithCounter{ObjectLockInfoPB(), 1});
        auto& existing_lock_info = it->second.first;
        if (inserted) {
          // First time seeing this lock
          existing_lock_info.Swap(lock_infos);
          continue;
        }
        // We've seen this lock before, increment counter
        it->second.second++;
        // If existing lock is GRANTED but current one is WAITING, replace with WAITING
        // (A globally acquired lock is only truly granted if all servers grant it)
        if (existing_lock_info.lock_state() == ObjectLockState::GRANTED &&
            lock_infos->lock_state() == ObjectLockState::WAITING) {
          VLOG(4) << "Replacing GRANTED lock with WAITING lock for txn_id: " << txn_id
                  << ", subtxn_id: " << lock_infos->subtransaction_id()
                  << ", object_oid: " << lock_infos->object_oid()
                  << ", database_oid: " << lock_infos->database_oid()
                  << ", lock_type: " << TableLockType_Name(lock_infos->mode());
          existing_lock_info.Swap(lock_infos);
        }
      }
    }

    // Process responses from master
    // A global lock is treated GRANTED only if it's granted on the master AND all tablet servers.
    auto s = master_promise->get_future().get();
    if (!s.ok()) {
      return s;
    }
    if (master_resp.has_error()) {
      *resp->mutable_status() = master_resp.error().status();
      return Status::OK();
    }
    VLOG(4) << "Processing GetObjectLockStatusResponsePB from master: "
            << master_resp.ShortDebugString();
    for (int i = 0; i < master_resp.object_lock_infos_size(); i++) {
      auto* lock_infos = master_resp.mutable_object_lock_infos(i);
      auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(lock_infos->transaction_id()));
      auto [it, inserted] = object_lock_info_map->try_emplace(
        ObjectLockContext{
          txn_id,
          lock_infos->subtransaction_id(),
          lock_infos->database_oid(),
          lock_infos->relation_oid(),
          lock_infos->object_oid(),
          lock_infos->object_sub_oid(),
          lock_infos->mode()
        },
        LockInfoWithCounter{ObjectLockInfoPB(), 1});
      auto& existing_lock_info = it->second.first;
      auto tserver_count = it->second.second;
      if (!inserted && tserver_count == remote_tservers.size() &&
          existing_lock_info.lock_state() == ObjectLockState::GRANTED) {
        continue;
      }
      // If the lock was not reported by all tablet servers as GRANTED, treat the
      // global lock as WAITING.
      // Note: The wait_start timestamp will be from the master(first lock acquisition)
      lock_infos->set_lock_state(ObjectLockState::WAITING);
      existing_lock_info.Swap(lock_infos);
    }

    return Status::OK();
  }

  Status DoGetLockStatus(
      GetLockStatusRequestPB* req, PgGetLockStatusResponsePB* resp,
      rpc::RpcContext* context, const std::vector<RemoteTabletServerPtr>& remote_tservers,
      int retry_attempt = 0) {
    if (PREDICT_FALSE(FLAGS_TEST_delay_before_get_locks_status_ms > 0)) {
      AtomicFlagSleepMs(&FLAGS_TEST_delay_before_get_locks_status_ms);
    }

    VLOG(4) << "Request to DoGetLockStatus: " << req->ShortDebugString()
            << ", with existing response state " << resp->ShortDebugString();
    if (req->transactions_by_tablet().empty() && req->transaction_ids().empty()) {
      StatusToPB(Status::OK(), resp->mutable_status());
      return Status::OK();
    }

    std::vector<std::future<Status>> status_futures;
    status_futures.reserve(remote_tservers.size());
    std::vector<std::shared_ptr<GetLockStatusResponsePB>> node_responses;
    node_responses.reserve(remote_tservers.size());
    for (const auto& remote_tserver : remote_tservers) {
      RETURN_NOT_OK(remote_tserver->InitProxy(&client()));
      auto proxy = remote_tserver->proxy();
      auto status_promise = std::make_shared<std::promise<Status>>();
      status_futures.push_back(status_promise->get_future());
      auto node_resp = std::make_shared<GetLockStatusResponsePB>();
      node_responses.push_back(node_resp);
      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      proxy->GetLockStatusAsync(
          *req, node_resp.get(), controller.get(), [controller, status_promise] {
            status_promise->set_value(controller->status());
          });
    }

    for (size_t i = 0; i < status_futures.size(); i++) {
      auto& node_resp = node_responses[i];
      auto s = status_futures[i].get();
      if (!s.ok()) {
        resp->Clear();
        return s;
      }
      if (node_resp->has_error()) {
        resp->Clear();
        *resp->mutable_status() = node_resp->error().status();
        return Status::OK();
      }
      auto* node_locks = resp->add_node_locks();
      node_locks->set_permanent_uuid(remote_tservers[i]->permanent_uuid());
      node_locks->mutable_tablet_lock_infos()->Swap(node_resp->mutable_tablet_lock_infos());
      VLOG(4) << "Adding node locks to PgGetLockStatusResponsePB: "
              << node_locks->ShortDebugString();
    }

    RETURN_NOT_OK_PREPEND(RefineAccumulatedLockStatusResp(req, resp),
                          "Error refining accumulated LockStatus responses.");
    if (!req->transactions_by_tablet().empty()) {
      // We haven't heard back from all involved tablets, retry GetLockStatusRequest on the
      // tablets missing in the response if we haven't maxed out on the retry attempts.
      if (retry_attempt > FLAGS_get_locks_status_max_retry_attempts) {
        return STATUS_FORMAT(
            IllegalState, "Expected to see involved tablet(s) $0 in PgGetLockStatusResponsePB",
            req->ShortDebugString());
      }
      LOG_WITH_FUNC(INFO) << "Retrying DoGetLockStatus for remaining tablets "
                          << "(attempt " << retry_attempt + 1 << "). Due to missing tablets: "
                          << req->ShortDebugString();
      AtomicFlagSleepMs(&FLAGS_pg_locks_retry_delay_ms);
      PgGetLockStatusResponsePB sub_resp;
      for (const auto& node_txn_pair : resp->transactions_by_node()) {
        sub_resp.mutable_transactions_by_node()->insert(node_txn_pair);
      }
      RETURN_NOT_OK(DoGetLockStatus(
          req, &sub_resp, context, VERIFY_RESULT(ReplaceSplitTabletsAndGetLocations(req)),
          ++retry_attempt));
      RETURN_NOT_OK(MergeLockStatusResponse(resp, &sub_resp));
    }
    StatusToPB(Status::OK(), resp->mutable_status());
    return Status::OK();
  }

  // Refines PgGetLockStatusResponsePB by dropping duplicate lock responses for a given tablet.
  Status RefineAccumulatedLockStatusResp(GetLockStatusRequestPB* req,
                                         PgGetLockStatusResponsePB* resp) {
    // Track the highest seen term for each tablet id.
    std::map<TabletId, uint64_t> peer_term;
    for (const auto& node_lock : resp->node_locks()) {
      for (const auto& tablet_lock_info : node_lock.tablet_lock_infos()) {
        const auto& tablet_id = tablet_lock_info.tablet_id();
        peer_term[tablet_id] = std::max(peer_term[tablet_id], tablet_lock_info.term());
      }
    }

    std::set<TransactionId> seen_transactions;
    for (auto& node_lock : *resp->mutable_node_locks()) {
      auto* tablet_lock_infos = node_lock.mutable_tablet_lock_infos();
      for (auto lock_it = tablet_lock_infos->begin(); lock_it != tablet_lock_infos->end();) {
        const auto& tablet_id = lock_it->tablet_id();
        auto max_term_for_tablet = peer_term[tablet_id];
        if (lock_it->term() < max_term_for_tablet) {
          LOG(INFO) << "Dropping lock info from stale peer of tablet " << lock_it->tablet_id()
                    << " from node " << node_lock.permanent_uuid()
                    << " with term " << lock_it->term()
                    << " less than highest term seen " << max_term_for_tablet
                    << ". This should be rare but is not an error otherwise.";
          lock_it = node_lock.mutable_tablet_lock_infos()->erase(lock_it);
          continue;
        }
        // TODO(pglocks): We don't fetch involved tablets when the incoming PgGetLockStatusRequestPB
        // has transaction_id field set. Once https://github.com/yugabyte/yugabyte-db/issues/16913
        // is addressed remove !req->transaction_ids().empty() in the below check.
        RSTATUS_DCHECK(
            !req->transaction_ids().empty() || req->transactions_by_tablet().count(tablet_id) == 1,
            IllegalState, "Found tablet $0 more than once in PgGetLockStatusResponsePB", tablet_id);
        req->mutable_transactions_by_tablet()->erase(tablet_id);

        for (auto& txn : lock_it->transaction_locks()) {
          seen_transactions.insert(VERIFY_RESULT(FullyDecodeTransactionId(txn.id())));
        }
        lock_it++;
      }
    }

    // Ensure that the response contains host node uuid for all involved transactions.
    for (const auto& [_, txn_list] : resp->transactions_by_node()) {
      for (const auto& txn : txn_list.transaction_ids()) {
        seen_transactions.erase(VERIFY_RESULT(FullyDecodeTransactionId(txn)));
      }
    }
    // TODO(pglocks): We currently don't populate transaction's host node info when the incoming
    // PgGetLockStatusRequestPB has transaction_id field set. This shouldn't be the case once
    // https://github.com/yugabyte/yugabyte-db/issues/16913 is addressed. As part of the fix,
    // remove !req.transaction_ids().empty() in the below check.
    RSTATUS_DCHECK(
        seen_transactions.empty() || !req->transaction_ids().empty(), IllegalState,
        Format("Host node uuid not set for transactions: $0", yb::ToString(seen_transactions)));
    return Status::OK();
  }

  Status TabletServerCount(
      const PgTabletServerCountRequestPB& req, PgTabletServerCountResponsePB* resp,
      rpc::RpcContext* context) {
    int result = 0;
    RETURN_NOT_OK(client().TabletServerCount(&result, req.primary_only(), /* use_cache= */ true));
    resp->set_count(result);
    return Status::OK();
  }

  Status ListLiveTabletServers(
      const PgListLiveTabletServersRequestPB& req, PgListLiveTabletServersResponsePB* resp,
      rpc::RpcContext* context) {
    auto tablet_servers_info = VERIFY_RESULT(client().ListLiveTabletServers(req.primary_only()));
    for (const auto& server : tablet_servers_info.tablet_servers) {
      server.ToPB(resp->mutable_servers()->Add());
    }
    resp->set_universe_uuid(VERIFY_RESULT(tablet_server_.GetUniverseUuid()));
    return Status::OK();
  }

  Status ListReplicationSlots(
      const PgListReplicationSlotsRequestPB& req, PgListReplicationSlotsResponsePB* resp,
      rpc::RpcContext* context) {
    auto streams = VERIFY_RESULT(client().ListCDCSDKStreams());

    // Determine latest active time of each stream if there are any.
    std::unordered_map<xrepl::StreamId, uint64_t> stream_to_latest_active_time;
    // stream id -> ((confirmed_flush, restart_lsn), (xmin, record_id_commit_time))
    std::unordered_map<
        xrepl::StreamId, std::pair<std::pair<uint64_t, uint64_t>, std::pair<uint32_t, uint64_t>>>
        stream_to_metadata;
    // stream id -> last_pub_refresh_time
    std::unordered_map<xrepl::StreamId, uint64_t> stream_to_last_pub_refresh_time;
    std::unordered_map<xrepl::StreamId, uint64_t> stream_to_active_pid;

    if (!streams.empty()) {
      Status iteration_status;
      auto range_result = VERIFY_RESULT(cdc_state_table_->GetTableRange(
          cdc::CDCStateTableEntrySelector()
              .IncludeActiveTime()
              .IncludeConfirmedFlushLSN()
              .IncludeRestartLSN()
              .IncludeXmin()
              .IncludeRecordIdCommitTime()
              .IncludeLastPubRefreshTime()
              .IncludeActivePid(),
          &iteration_status));

      int cdc_state_table_result_count = 0;
      for (auto entry_result : range_result) {
        cdc_state_table_result_count++;
        RETURN_NOT_OK(entry_result);
        const auto& entry = *entry_result;

        VLOG_WITH_FUNC(4) << "Received entry from CDC state table for stream_id: "
                          << entry.key.stream_id << ", tablet_id: " << entry.key.tablet_id
                          << ", active_time: "
                          << (entry.active_time.has_value() ? *entry.active_time : 0);

        auto stream_id = entry.key.stream_id;
        auto active_time = entry.active_time;

        // The special entry storing the replication slot metadata set during the stream creation.
        if (entry.key.tablet_id == kCDCSDKSlotEntryTabletId) {
          DCHECK(!stream_to_metadata.contains(stream_id));
          DCHECK(entry.confirmed_flush_lsn.has_value());
          DCHECK(entry.restart_lsn.has_value());
          DCHECK(entry.xmin.has_value());
          DCHECK(entry.record_id_commit_time.has_value());
          DCHECK(entry.last_pub_refresh_time.has_value());

          stream_to_metadata[stream_id] = std::make_pair(
              std::make_pair(*entry.confirmed_flush_lsn, *entry.restart_lsn),
              std::make_pair(*entry.xmin, *entry.record_id_commit_time));
          stream_to_last_pub_refresh_time[stream_id] = *entry.last_pub_refresh_time;
          stream_to_active_pid[stream_id] = entry.active_pid.has_value() ? *entry.active_pid : 0;
          continue;
        }

        // If active_time isn't populated, then the (stream_id, tablet_id) pair hasn't been consumed
        // yet by the client. So treat it is as an inactive case.
        if (!active_time) {
          continue;
        }

        if (stream_to_latest_active_time.contains(stream_id)) {
          stream_to_latest_active_time[stream_id] =
              std::max(stream_to_latest_active_time[stream_id], *active_time);
        } else {
          stream_to_latest_active_time[stream_id] = *active_time;
        }
      }
      SCHECK(
          iteration_status.ok(), InternalError, "Unable to read the CDC state table",
          iteration_status);

      VLOG_WITH_FUNC(4) << "Received a total of " << cdc_state_table_result_count
                        << " entries from the CDC state table";
    }

    auto current_time = GetCurrentTimeMicros();
    for (const auto& stream : streams) {
      auto stream_id = xrepl::StreamId::FromString(stream.stream_id);
      RSTATUS_DCHECK(
          stream_id.ok(),
          IllegalState, "Received invalid stream_id: $0 from ListCDCSDKStreams", stream.stream_id);

      auto replication_slot = resp->mutable_replication_slots()->Add();
      replication_slot->set_yb_lsn_type(stream.replication_slot_lsn_type);
      stream.ToPB(replication_slot);
      auto it = stream_to_latest_active_time.find(*stream_id);
      auto last_active_time_micros = (it != stream_to_latest_active_time.end()) ? it->second : 0;
      auto is_stream_active =
          current_time - last_active_time_micros <=
          1000 * GetAtomicFlag(&FLAGS_ysql_cdc_active_replication_slot_window_ms);
      replication_slot->set_replication_slot_status(
          (is_stream_active) ? ReplicationSlotStatus::ACTIVE : ReplicationSlotStatus::INACTIVE);

      auto expiration_threshold_micros =
          static_cast<int64_t>(1000 * GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));
      int64_t idle_duration_micros;
      // If the active time has not been set yet implying no tables are present in the database
      // we use the consistent snapshot time to check if the slot/stream has expired or not
      if (!stream_to_latest_active_time.contains(*stream_id)) {
        idle_duration_micros =
            current_time -
            HybridTime(stream_to_metadata[*stream_id].second.second).GetPhysicalValueMicros();
      } else {
        idle_duration_micros = current_time - stream_to_latest_active_time[*stream_id];
      }

      auto is_stream_expired = idle_duration_micros > expiration_threshold_micros;
      replication_slot->set_expired(is_stream_expired);

      if (stream_to_metadata.contains(*stream_id)) {
        auto slot_metadata = stream_to_metadata[*stream_id];
        replication_slot->set_confirmed_flush_lsn(slot_metadata.first.first);
        replication_slot->set_restart_lsn(slot_metadata.first.second);
        replication_slot->set_xmin(slot_metadata.second.first);
        replication_slot->set_record_id_commit_time_ht(slot_metadata.second.second);
        replication_slot->set_last_pub_refresh_time(stream_to_last_pub_refresh_time[*stream_id]);
        replication_slot->set_active_pid(stream_to_active_pid[*stream_id]);
      } else {
        // TODO(#21780): This should never happen, so make this a DCHECK. We can do that after every
        // unit test that uses replication slots is updated to consistent snapshot stream.
        LOG(WARNING) << "The CDC state table metadata entry was not found for stream_id: "
                     << *stream_id << ", slot_name: " << replication_slot->slot_name();
      }
    }
    return Status::OK();
  }

  Status GetReplicationSlot(
      const PgGetReplicationSlotRequestPB& req, PgGetReplicationSlotResponsePB* resp,
      rpc::RpcContext* context) {
    std::unordered_map<uint32_t, PgReplicaIdentity> replica_identities;
    auto stream = VERIFY_RESULT(client().GetCDCStream(
        ReplicationSlotName(req.replication_slot_name()), &replica_identities));
    stream.ToPB(resp->mutable_replication_slot_info());

    auto m = resp->mutable_replication_slot_info()->mutable_replica_identity_map();
    for (const auto& replica_identity : replica_identities) {
      PgReplicaIdentityType replica_identity_value;
      switch (replica_identity.second) {
        case PgReplicaIdentity::DEFAULT:
          replica_identity_value = PgReplicaIdentityType::DEFAULT;
          break;
        case PgReplicaIdentity::FULL:
          replica_identity_value = PgReplicaIdentityType::FULL;
          break;
        case PgReplicaIdentity::NOTHING:
          replica_identity_value = PgReplicaIdentityType::NOTHING;
          break;
        case PgReplicaIdentity::CHANGE:
          replica_identity_value = PgReplicaIdentityType::CHANGE;
          break;
        default:
          RSTATUS_DCHECK(false, InternalError, "Invalid Replica Identity Type");
      }

      PgReplicaIdentityPB replica_identity_pb;
      replica_identity_pb.set_replica_identity(replica_identity_value);
      m->insert({replica_identity.first, std::move(replica_identity_pb)});
    }

    auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream.stream_id));
    bool is_slot_active;
    uint64_t confirmed_flush_lsn = 0;
    uint64_t restart_lsn = 0;
    uint32_t xmin = 0;
    uint64_t record_id_commit_time_ht;
    uint64_t last_pub_refresh_time = 0;
    uint64_t active_pid = 0;
    bool is_stream_expired;
    RETURN_NOT_OK(GetReplicationSlotInfoFromCDCState(
        stream_id, &is_slot_active, &confirmed_flush_lsn, &restart_lsn, &xmin,
        &record_id_commit_time_ht, &last_pub_refresh_time, &active_pid, &is_stream_expired));
    resp->mutable_replication_slot_info()->set_replication_slot_status(
        (is_slot_active) ? ReplicationSlotStatus::ACTIVE : ReplicationSlotStatus::INACTIVE);

    RSTATUS_DCHECK(
        confirmed_flush_lsn != 0 && restart_lsn != 0 && xmin != 0, InternalError,
        Format(
            "Unexpected value present in the CDC state table. confirmed_flush_lsn: $0, "
            "restart_lsn: $1, xmin: $2",
            confirmed_flush_lsn, restart_lsn, xmin));

    auto slot_info = resp->mutable_replication_slot_info();
    slot_info->set_confirmed_flush_lsn(confirmed_flush_lsn);
    slot_info->set_restart_lsn(restart_lsn);
    slot_info->set_xmin(xmin);
    slot_info->set_record_id_commit_time_ht(record_id_commit_time_ht);
    slot_info->set_last_pub_refresh_time(last_pub_refresh_time);
    slot_info->set_active_pid(active_pid);
    slot_info->set_expired(is_stream_expired);
    return Status::OK();
  }

  Status GetReplicationSlotInfoFromCDCState(
      const xrepl::StreamId& stream_id, bool* active, uint64_t* confirmed_flush_lsn,
      uint64_t* restart_lsn, uint32_t* xmin, uint64_t* record_id_commit_time_ht,
      uint64_t* last_pub_refresh_time, uint64_t* active_pid, bool* expired) {
    // TODO(#19850): Fetch only the entries belonging to the stream_id from the table.
    Status iteration_status;
    auto range_result = VERIFY_RESULT(cdc_state_table_->GetTableRange(
        cdc::CDCStateTableEntrySelector()
            .IncludeActiveTime()
            .IncludeConfirmedFlushLSN()
            .IncludeRestartLSN()
            .IncludeXmin()
            .IncludeRecordIdCommitTime()
            .IncludeLastPubRefreshTime()
            .IncludeActivePid(),
        &iteration_status));

    // Find the latest active time for the stream across all tablets.
    uint64_t last_activity_time_micros = 0;
    for (auto entry_result : range_result) {
      RETURN_NOT_OK(entry_result);
      const auto& entry = *entry_result;

      if (entry.key.stream_id != stream_id) {
        continue;
      }

      // The special entry storing the replication slot metadata set during the stream creation.
      if (entry.key.tablet_id == kCDCSDKSlotEntryTabletId) {
        DCHECK(entry.confirmed_flush_lsn.has_value());
        DCHECK(entry.restart_lsn.has_value());
        DCHECK(entry.xmin.has_value());
        DCHECK(entry.record_id_commit_time.has_value());
        DCHECK(entry.last_pub_refresh_time.has_value());

        *DCHECK_NOTNULL(confirmed_flush_lsn) = *entry.confirmed_flush_lsn;
        *DCHECK_NOTNULL(restart_lsn) = *entry.restart_lsn;
        *DCHECK_NOTNULL(xmin) = *entry.xmin;
        *DCHECK_NOTNULL(record_id_commit_time_ht) = *entry.record_id_commit_time;
        *DCHECK_NOTNULL(last_pub_refresh_time) = *entry.last_pub_refresh_time;
        *DCHECK_NOTNULL(active_pid) = entry.active_pid.has_value() ? *entry.active_pid : 0;
        continue;
      }

      auto active_time = entry.active_time;

      // If active_time isn't populated, then the (stream_id, tablet_id) pair hasn't been consumed
      // yet by the client. So treat it is as an inactive case.
      if (!active_time) {
        continue;
      }

      last_activity_time_micros = std::max(last_activity_time_micros, *active_time);
    }
    SCHECK(
        iteration_status.ok(), InternalError, "Unable to read the CDC state table",
        iteration_status);

    *DCHECK_NOTNULL(active) =
        GetCurrentTimeMicros() - last_activity_time_micros <=
        1000 * GetAtomicFlag(&FLAGS_ysql_cdc_active_replication_slot_window_ms);

    auto commit_idle_duration_micros =
        GetCurrentTimeMicros() - HybridTime(*record_id_commit_time_ht).GetPhysicalValueMicros();
    auto last_activity_idle_duration_micros = GetCurrentTimeMicros() - last_activity_time_micros;
    auto expiration_threshold_micros = 1000 * GetAtomicFlag(&FLAGS_cdc_intent_retention_ms);
    *DCHECK_NOTNULL(expired) =
        (last_activity_time_micros == 0)
            ? (commit_idle_duration_micros > expiration_threshold_micros)
            : (last_activity_idle_duration_micros > expiration_threshold_micros);
    return Status::OK();
  }

  Status ExportTxnSnapshot(
      const PgExportTxnSnapshotRequestPB& req, PgExportTxnSnapshotResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG(1) << "ExportTxnSnapshot from " << RequestorString(context) << ": " << req.DebugString();
    auto session = VERIFY_RESULT(GetSession(req.session_id()));
    const auto read_time = VERIFY_RESULT(session->GetTxnSnapshotReadTime(
        req.options(), context->GetClientDeadline()));
    resp->set_snapshot_id(VERIFY_RESULT(txn_snapshot_manager_.Register(
        session->id(), PgTxnSnapshot::Make(req.snapshot(), read_time))));
    return Status::OK();
  }

  Result<PgTxnSnapshot> GetLocalPgTxnSnapshot(const PgTxnSnapshotLocalId& snapshot_id) {
    return txn_snapshot_manager_.Get(snapshot_id);
  }

  Status ImportTxnSnapshot(
      const PgImportTxnSnapshotRequestPB& req, PgImportTxnSnapshotResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG(1) << "ImportTxnSnapshot from " << RequestorString(context) << ": " << req.DebugString();
    auto snapshot = VERIFY_RESULT(txn_snapshot_manager_.Get(req.snapshot_id()));
    auto options = req.options();
    snapshot.read_time.ToPB(options.mutable_read_time());
    RETURN_NOT_OK(VERIFY_RESULT(GetSession(req.session_id()))->SetTxnSnapshotReadTime(
        options, context->GetClientDeadline()));
    snapshot.ToPBNoReadTime(*resp->mutable_snapshot());
    return Status::OK();
  }

  Status ClearExportedTxnSnapshots(
      const PgClearExportedTxnSnapshotsRequestPB& req, PgClearExportedTxnSnapshotsResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG(1) << "ClearExportedTxnSnapshots from " << RequestorString(context) << ": "
            << req.DebugString();
    txn_snapshot_manager_.UnregisterAll(req.session_id());
    return Status::OK();
  }

  Status GetIndexBackfillProgress(
      const PgGetIndexBackfillProgressRequestPB& req, PgGetIndexBackfillProgressResponsePB* resp,
      rpc::RpcContext* context) {
    std::vector<TableId> index_ids;
    for (const auto& index_id : req.index_ids()) {
      index_ids.emplace_back(PgObjectId::GetYbTableIdFromPB(index_id));
    }
    return client().GetIndexBackfillProgress(index_ids,
                                             resp->mutable_num_rows_read_from_table_for_backfill(),
                                             resp->mutable_num_rows_backfilled_in_index());
  }

  Status ValidatePlacement(
      const PgValidatePlacementRequestPB& req, PgValidatePlacementResponsePB* resp,
      rpc::RpcContext* context) {
    return client().ValidateReplicationInfo(req.replication_info());
  }

  Status GetTableDiskSize(
      const PgGetTableDiskSizeRequestPB& req, PgGetTableDiskSizeResponsePB* resp,
      rpc::RpcContext* context) {
    auto result =
        client().GetTableDiskSize(PgObjectId::GetYbTableIdFromPB(req.table_id()));
    if (!result.ok()) {
      StatusToPB(result.status(), resp->mutable_status());
    } else {
      resp->set_size(result->table_size);
      resp->set_num_missing_tablets(result->num_missing_tablets);
    }
    return Status::OK();
  }

  Status CheckIfPitrActive(
      const PgCheckIfPitrActiveRequestPB& req, PgCheckIfPitrActiveResponsePB* resp,
      rpc::RpcContext* context) {
    auto res = client().CheckIfPitrActive();
    if (!res.ok()) {
      StatusToPB(res.status(), resp->mutable_status());
    } else {
      resp->set_is_pitr_active(*res);
    }
    return Status::OK();
  }

  bool ShouldIgnoreCall(
      const PgActiveSessionHistoryRequestPB& req, const rpc::RpcCallInProgressPB& call) {
    return (
        !call.has_wait_state() ||
        // Ignore log-appenders which are just Idle
        call.wait_state().wait_state_code() == std::to_underlying(ash::WaitStateCode::kIdle) ||
        // Ignore ActiveSessionHistory/Perform calls, if desired.
        (req.ignore_ash_and_perform_calls() && call.wait_state().has_aux_info() &&
         call.wait_state().aux_info().has_method() &&
         (call.wait_state().aux_info().method() == "ActiveSessionHistory" ||
          call.wait_state().aux_info().method() == "Perform" ||
          call.wait_state().aux_info().method() == "AcquireAdvisoryLock")));

  }

  void MaybeIncludeSample(
      tserver::WaitStatesPB* resp, const WaitStateInfoPB& wait_state_pb, int sample_size,
      int& samples_considered) {
    if (++samples_considered <= sample_size) {
      resp->add_wait_states()->CopyFrom(wait_state_pb);
    } else {
      int random_index = RandomUniformInt<int>(1, samples_considered);
      if (random_index <= sample_size) {
        resp->mutable_wait_states(random_index - 1)->CopyFrom(wait_state_pb);
      }
    }
  }

  void PopulateWaitStates(
      const PgActiveSessionHistoryRequestPB& req, const yb::rpc::RpcConnectionPB& conn,
      tserver::WaitStatesPB* resp, int sample_size, int& samples_considered) {
    for (const auto& call : conn.calls_in_flight()) {
      if (ShouldIgnoreCall(req, call)) {
        VLOG(3) << "Ignoring " << call.wait_state().DebugString();
        continue;
      }
      MaybeIncludeSample(resp, call.wait_state(), sample_size, samples_considered);
    }
  }

  void GetRpcsWaitStates(
      const PgActiveSessionHistoryRequestPB& req, ash::Component component,
      tserver::WaitStatesPB* resp, int sample_size, int& samples_considered) {
    auto* messenger = tablet_server_.GetMessenger(component);
    if (!messenger) {
      return;
    }

    resp->set_component(std::to_underlying(component));

    rpc::DumpRunningRpcsRequestPB dump_req;
    rpc::DumpRunningRpcsResponsePB dump_resp;
    dump_req.set_include_traces(false);
    dump_req.set_get_wait_state(true);
    dump_req.set_dump_timed_out(false);
    dump_req.set_get_local_calls(true);
    dump_req.set_export_wait_state_code_as_string(req.export_wait_state_code_as_string());

    WARN_NOT_OK(messenger->DumpRunningRpcs(dump_req, &dump_resp), "DumpRunningRpcs failed");

    for (const auto& conn : dump_resp.inbound_connections()) {
      PopulateWaitStates(req, conn, resp, sample_size, samples_considered);
    }

    if (dump_resp.has_local_calls()) {
      PopulateWaitStates(req, dump_resp.local_calls(), resp, sample_size, samples_considered);
    }

    VLOG(3) << __PRETTY_FUNCTION__ << " wait-states: " << yb::ToString(resp->wait_states());
  }

  void AddWaitStatesToResponse(
      const ash::WaitStateTracker& tracker, bool export_wait_state_names,
      tserver::WaitStatesPB* resp, int sample_size, int& samples_considered) {
    Result<Uuid> local_uuid = Uuid::FromHexStringBigEndian(instance_id_);
    DCHECK_OK(local_uuid);
    resp->set_component(std::to_underlying(ash::Component::kTServer));
    for (auto& wait_state_ptr : tracker.GetWaitStates()) {
      if (!wait_state_ptr) {
        continue;
      }
      WaitStateInfoPB wait_state_pb;
      wait_state_ptr->ToPB(&wait_state_pb, export_wait_state_names);
      if (wait_state_pb.wait_state_code() == std::to_underlying(ash::WaitStateCode::kIdle)) {
        continue;
      }
      if (local_uuid) {
        local_uuid->ToBytes(wait_state_pb.mutable_metadata()->mutable_top_level_node_id());
      }
      MaybeIncludeSample(resp, wait_state_pb, sample_size, samples_considered);
    }
    VLOG_IF(2, resp->wait_states_size() > 0) << "Tracker call sending " << resp->DebugString();
  }

  Status ActiveSessionHistory(
      const PgActiveSessionHistoryRequestPB& req, PgActiveSessionHistoryResponsePB* resp,
      rpc::RpcContext* context) {
    int tserver_samples_considered = 0;
    int cql_samples_considered = 0;
    int sample_size = req.sample_size();
    if (req.fetch_tserver_states()) {
      GetRpcsWaitStates(req, ash::Component::kTServer, resp->mutable_tserver_wait_states(),
          sample_size, tserver_samples_considered);
      AddWaitStatesToResponse(
          ash::SharedMemoryPgPerformTracker(), req.export_wait_state_code_as_string(),
          resp->mutable_tserver_wait_states(), sample_size, tserver_samples_considered);
    }
    if (req.fetch_flush_and_compaction_states()) {
      AddWaitStatesToResponse(
          ash::FlushAndCompactionWaitStatesTracker(), req.export_wait_state_code_as_string(),
          resp->mutable_tserver_wait_states(), sample_size, tserver_samples_considered);
    }
    if (req.fetch_raft_log_appender_states()) {
      AddWaitStatesToResponse(
          ash::RaftLogWaitStatesTracker(), req.export_wait_state_code_as_string(),
          resp->mutable_tserver_wait_states(), sample_size, tserver_samples_considered);
    }
    if (req.fetch_cql_states()) {
      GetRpcsWaitStates(req, ash::Component::kYCQL, resp->mutable_cql_wait_states(),
          sample_size, cql_samples_considered);
    }
    AddWaitStatesToResponse(
        ash::XClusterPollerTracker(), req.export_wait_state_code_as_string(),
        resp->mutable_tserver_wait_states(), sample_size, tserver_samples_considered);
    AddWaitStatesToResponse(
        ash::MinRunningHybridTimeTracker(), req.export_wait_state_code_as_string(),
        resp->mutable_tserver_wait_states(), sample_size, tserver_samples_considered);
    float tserver_sample_weight =
        std::max(tserver_samples_considered, sample_size) * 1.0 / sample_size;
    float cql_sample_weight = std::max(cql_samples_considered, sample_size) * 1.0 / sample_size;
    resp->mutable_tserver_wait_states()->set_sample_weight(tserver_sample_weight);
    resp->mutable_cql_wait_states()->set_sample_weight(cql_sample_weight);
    return Status::OK();
  }

  Status GetTserverCatalogVersionInfo(
      const PgGetTserverCatalogVersionInfoRequestPB& req,
      PgGetTserverCatalogVersionInfoResponsePB* resp,
      rpc::RpcContext* context) {
    GetTserverCatalogVersionInfoRequestPB request;
    GetTserverCatalogVersionInfoResponsePB info;
    const auto db_oid = req.db_oid();
    request.set_size_only(req.size_only());
    request.set_db_oid(db_oid);
    RETURN_NOT_OK(tablet_server_.get_ysql_db_oid_to_cat_version_info_map(
        request, &info));
    if (req.size_only()) {
      // We only ask for the size of catalog version map in tserver and should not need to
      // populate any entries.
      DCHECK_EQ(info.entries_size(), 0);
      resp->set_num_entries(info.num_entries());
      return Status::OK();
    }
    // If db_oid is kPgInvalidOid, we ask for the catalog version map of all databases.
    // Otherwise, we only ask for the catalog version info for the given database.
    if (db_oid != kPgInvalidOid) {
      // In a race condition it is possible that the database db_oid is already
      // dropped from another node even through we are still connecting to
      // db_oid. When that happens info.entries_size() can be 0.
      DCHECK_LE(info.entries_size(), 1);
    }
    resp->mutable_entries()->Swap(info.mutable_entries());
    return Status::OK();
  }

  Status GetTserverCatalogMessageLists(
      const PgGetTserverCatalogMessageListsRequestPB& req,
      PgGetTserverCatalogMessageListsResponsePB* resp,
      rpc::RpcContext* context) {
    GetTserverCatalogMessageListsRequestPB request;
    GetTserverCatalogMessageListsResponsePB response;
    const auto db_oid = req.db_oid();
    const auto ysql_catalog_version = req.ysql_catalog_version();
    const auto num_catalog_versions = req.num_catalog_versions();
    request.set_db_oid(db_oid);
    request.set_ysql_catalog_version(ysql_catalog_version);
    request.set_num_catalog_versions(num_catalog_versions);
    RETURN_NOT_OK(tablet_server_.GetTserverCatalogMessageLists(request, &response));
    resp->mutable_entries()->Swap(response.mutable_entries());
    return Status::OK();
  }

  Status SetTserverCatalogMessageList(
      const PgSetTserverCatalogMessageListRequestPB& req,
      PgSetTserverCatalogMessageListResponsePB* resp,
      rpc::RpcContext* context) {
    const auto db_oid = req.db_oid();
    const auto is_breaking_change = req.is_breaking_change();
    const auto new_catalog_version = req.new_catalog_version();
    std::optional<std::string> message_list;
    if (req.message_list().has_message_list()) {
      message_list = req.message_list().message_list();
    }
    return const_cast<TabletServerIf&>(tablet_server_).SetTserverCatalogMessageList(
        db_oid, is_breaking_change, new_catalog_version, message_list);
  }

  Status TriggerRelcacheInitConnection(
      const PgTriggerRelcacheInitConnectionRequestPB& req,
      PgTriggerRelcacheInitConnectionResponsePB* resp,
      rpc::RpcContext* context) {
    TriggerRelcacheInitConnectionRequestPB request;
    TriggerRelcacheInitConnectionResponsePB response;
    const auto& database_name = req.database_name();
    request.set_database_name(database_name);
    return const_cast<TabletServerIf&>(tablet_server_).TriggerRelcacheInitConnection(
        request, &response);
  }

  Status IsObjectPartOfXRepl(
    const PgIsObjectPartOfXReplRequestPB& req, PgIsObjectPartOfXReplResponsePB* resp,
    rpc::RpcContext* context) {
    auto res = client().IsObjectPartOfXRepl(PgObjectId::GetYbTableIdFromPB(req.table_id()));
    if (!res.ok()) {
      StatusToPB(res.status(), resp->mutable_status());
    } else {
      resp->set_is_object_part_of_xrepl(*res);
    }
    return Status::OK();
  }

  void Perform(PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext* context) {
    boost::container::small_vector<TableId, 4> table_ids;
    PreparePgTablesQuery(*req, table_ids);
    auto query = std::make_shared<PerformQuery>(
      *this, MakeTypedPBRpcContextHolder(*req, resp, std::move(*context)));
    table_cache_.GetTables(table_ids, query);
  }

  void InvalidateTableCache() {
    table_cache_.InvalidateAll(CoarseMonoClock::Now());
  }

  void InvalidateTableCache(
      const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
      const std::unordered_set<uint32_t>& db_oids_deleted) {
    table_cache_.InvalidateDbTables(db_oids_updated, db_oids_deleted);
  }

  void CleanupSessions(
      std::vector<SessionInfoPtr>&& expired_sessions, CoarseTimePoint time) {
    if (expired_sessions.empty()) {
      return;
    }
    std::vector<SessionInfoPtr> not_ready_sessions;
    for (const auto& session : expired_sessions) {
      VLOG(1) << "Starting shutdown for expired session ID: " << session->id();
      session->session().StartShutdown(/* pg_service_shutting_donw= */ false);
      txn_snapshot_manager_.UnregisterAll(session->id());
    }
    AtomicFlagSleepMs(&FLAGS_TEST_delay_before_complete_expired_pg_sessions_shutdown_ms);
    for (const auto& session : expired_sessions) {
      if (session->session().ReadyToShutdown()) {
        session->session().CompleteShutdown();
      } else {
        not_ready_sessions.push_back(session);
      }
    }
    {
      std::lock_guard lock(mutex_);
      stopping_sessions_.insert(
          stopping_sessions_.end(), not_ready_sessions.begin(), not_ready_sessions.end());
      ScheduleCheckExpiredSessions(time);
    }
    auto cdc_service = tablet_server_.GetCDCService();
    // We only want to call this on tablet servers. On master, cdc_service will be null.
    if (cdc_service) {
      std::vector<uint64_t> expired_session_ids;
      expired_session_ids.reserve(expired_sessions.size());
      for (auto& session : expired_sessions) {
        expired_session_ids.push_back(session->id());
      }
      expired_sessions.clear();

      cdc_service->DestroyVirtualWALBatchForCDC(expired_session_ids);
    }
  }

  // Return the TabletServer hosting the specified status tablet.
  std::future<Result<RemoteTabletServerPtr>> GetTServerHostingStatusTablet(
      const TabletId& status_tablet_id, CoarseTimePoint deadline) {
    return MakeFuture<Result<RemoteTabletServerPtr>>([&](auto callback) {
      client().LookupTabletById(
          status_tablet_id, /* table =*/ nullptr, master::IncludeHidden::kFalse,
          master::IncludeDeleted::kFalse, deadline,
          [&, status_tablet_id, callback] (const auto& lookup_result) {
            if (!lookup_result.ok()) {
              return callback(lookup_result.status());
            }

            auto& remote_tablet = *lookup_result;
            if (!remote_tablet) {
              return callback(STATUS_FORMAT(
                  InvalidArgument,
                  Format("Status tablet with id: $0 not found", status_tablet_id)));
            }

            if (!remote_tablet->LeaderTServer()) {
              return callback(STATUS_FORMAT(
                  TryAgain, Format("Leader not found for tablet $0", status_tablet_id)));
            }
            const auto& permanent_uuid = remote_tablet->LeaderTServer()->permanent_uuid();
            auto remote_ts_or_status = tablet_server_.GetRemoteTabletServers({permanent_uuid});
            if (!remote_ts_or_status.ok()) {
              return callback(remote_ts_or_status.status());
            }
            callback((*remote_ts_or_status)[0]);
          },
          // Force a client cache refresh so as to not hit NOT_LEADER error.
          client::UseCache::kFalse);
    });
  }

  Status GetActiveTransactionList(
      const PgGetActiveTransactionListRequestPB& req, PgGetActiveTransactionListResponsePB* resp,
      rpc::RpcContext* context) {
    if (req.has_session_id()) {
      AddTransactionInfo(resp, VERIFY_RESULT(GetSessionInfo(req.session_id().value())).get());
      return Status::OK();
    }

    decltype(sessions_) sessions_snapshot;
    {
      std::lock_guard lock(mutex_);
      sessions_snapshot = sessions_;
    }

    for (const auto& session : sessions_snapshot) {
      AddTransactionInfo(resp, session.get());
    }

    return Status::OK();
  }

  Status CancelTransaction(const PgCancelTransactionRequestPB& req,
                           PgCancelTransactionResponsePB* resp,
                           rpc::RpcContext* context) {
    if (req.transaction_id().empty()) {
      return STATUS_FORMAT(IllegalState,
                           "Transaction Id not provided in PgCancelTransactionRequestPB");
    }
    tserver::CancelTransactionRequestPB node_req;
    node_req.set_transaction_id(req.transaction_id());

    std::vector<RemoteTabletServerPtr> remote_tservers;
    if (req.status_tablet_id().empty()) {
      remote_tservers = VERIFY_RESULT(tablet_server_.GetRemoteTabletServers());
    } else {
      const auto& remote_ts = VERIFY_RESULT(GetTServerHostingStatusTablet(
          req.status_tablet_id(), context->GetClientDeadline()).get());
      remote_tservers.push_back(remote_ts);
      node_req.set_status_tablet_id(req.status_tablet_id());
    }

    std::vector<std::future<Status>> status_future;
    std::vector<tserver::CancelTransactionResponsePB> node_resp(remote_tservers.size());
    for (size_t i = 0 ; i < remote_tservers.size() ; i++) {
      RETURN_NOT_OK(remote_tservers[i]->InitProxy(&client()));
      const auto& proxy = remote_tservers[i]->proxy();
      auto controller = std::make_shared<rpc::RpcController>();
      status_future.push_back(
          MakeFuture<Status>([&, controller](auto callback) {
            proxy->CancelTransactionAsync(
                node_req, &node_resp[i], controller.get(), [callback, controller] {
              callback(controller->status());
            });
          }));
    }

    auto status = STATUS_FORMAT(NotFound, "Transaction not found.");
    resp->Clear();
    for (size_t i = 0 ; i < status_future.size() ; i++) {
      const auto& s = status_future[i].get();
      if (!s.ok()) {
        LOG(WARNING) << "CancelTransaction request to TS failed with status: " << s;
        continue;
      }

      if (node_resp[i].has_error()) {
        // Errors take precedence over TransactionStatus::ABORTED statuses. This needs to be done to
        // correctly handle cancelation requests of promoted txns. Ignore all NotFound statuses as
        // we collate them, collect all other error types.
        const auto& status_from_pb = StatusFromPB(node_resp[i].error().status());
        if (status_from_pb.IsNotFound()) {
          continue;
        }
        status = status_from_pb.CloneAndAppend("\n").CloneAndAppend(status.message());
      }

      // One of the TServers reported successfull cancelation of the transaction. Reset the status
      // if we haven't seen any errors other than NOT_FOUND from the remaining TServers.
      if (status.IsNotFound()) {
        status = Status::OK();
      }
    }

    StatusToPB(status, resp->mutable_status());
    return Status::OK();
  }

  Status YCQLStatementStats(const PgYCQLStatementStatsRequestPB& req,
      PgYCQLStatementStatsResponsePB* resp,
      rpc::RpcContext* context) {
    RETURN_NOT_OK(tablet_server_.YCQLStatementStats(req, resp));
    return Status::OK();
  }

  Status TabletsMetadata(
      const PgTabletsMetadataRequestPB& req, PgTabletsMetadataResponsePB* resp,
      rpc::RpcContext* context) {
    if (req.local_only()) {
      const auto& result = VERIFY_RESULT(tablet_server_.GetLocalTabletsMetadata());
      *resp->mutable_tablets() = {result.begin(), result.end()};
    } else {
      auto tablet_metadatas = VERIFY_RESULT(client().GetTabletsMetadata());
      *resp->mutable_tablets() = {tablet_metadatas.begin(), tablet_metadatas.end()};
    }
    return Status::OK();
  }

  Status ServersMetrics(
      const PgServersMetricsRequestPB& req, PgServersMetricsResponsePB* resp,
      rpc::RpcContext* context) {

    std::vector<tserver::PgServerMetricsInfoPB> result;
    std::vector<std::future<Status>> status_futures;
    std::vector<std::shared_ptr<GetMetricsResponsePB>> node_responses;

    GetMetricsRequestPB metrics_req;
    const auto remote_tservers = VERIFY_RESULT(tablet_server_.GetRemoteTabletServers());
    status_futures.reserve(remote_tservers.size());
    node_responses.reserve(remote_tservers.size());

    for (const auto& remote_tserver : remote_tservers) {
      RETURN_NOT_OK(remote_tserver->InitProxy(&client()));
      auto proxy = remote_tserver->proxy();
      auto status_promise = std::make_shared<std::promise<Status>>();
      status_futures.push_back(status_promise->get_future());
      auto node_resp = std::make_shared<GetMetricsResponsePB>();
      node_responses.push_back(node_resp);

      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      controller->set_timeout(MonoDelta::FromMilliseconds(5000));

      proxy->GetMetricsAsync(metrics_req, node_resp.get(), controller.get(),
      [controller, status_promise] {
        status_promise->set_value(controller->status());
      });
    }
    for (size_t i = 0; i < status_futures.size(); ++i) {
      auto& node_resp = node_responses[i];
      auto s = status_futures[i].get();
      tserver::PgServerMetricsInfoPB server_metrics;
      server_metrics.set_uuid(remote_tservers[i]->permanent_uuid());
      if (!s.ok()) {
        server_metrics.set_status(tserver::PgMetricsInfoStatus::ERROR);
        server_metrics.set_error(s.ToUserMessage());
      } else if (node_resp->has_error()) {
        server_metrics.set_status(tserver::PgMetricsInfoStatus::ERROR);
        server_metrics.set_error(node_resp->error().status().message());
      } else {
        server_metrics.mutable_metrics()->Swap(node_resp->mutable_metrics());
        server_metrics.set_status(tserver::PgMetricsInfoStatus::OK);
        server_metrics.set_error("");
      }
      result.emplace_back(std::move(server_metrics));
    }

    *resp->mutable_servers_metrics() = {result.begin(), result.end()};
    return Status::OK();
  }

  Status CronSetLastMinute(
      const PgCronSetLastMinuteRequestPB& req, PgCronSetLastMinuteResponsePB* resp,
      rpc::RpcContext* context) {
    auto controller = std::make_shared<rpc::RpcController>();
    controller->set_deadline(context->GetClientDeadline());

    stateful_service::PgCronSetLastMinuteRequestPB stateful_service_req;
    stateful_service_req.set_last_minute(req.last_minute());
    RETURN_NOT_OK(client::PgCronLeaderServiceClient(client()).PgCronSetLastMinute(
        stateful_service_req, context->GetClientDeadline()));

    return Status::OK();
  }

  Status CronGetLastMinute(
      const PgCronGetLastMinuteRequestPB& req, PgCronGetLastMinuteResponsePB* resp,
      rpc::RpcContext* context) {
    stateful_service::PgCronGetLastMinuteRequestPB stateful_service_req;
    auto stateful_service_resp =
        VERIFY_RESULT(client::PgCronLeaderServiceClient(client()).PgCronGetLastMinute(
            stateful_service_req, context->GetClientDeadline()));

    resp->set_last_minute(stateful_service_resp.last_minute());
    return Status::OK();
  }

  Status ListClones(
      const PgListClonesRequestPB& req, PgListClonesResponsePB* resp, rpc::RpcContext* context) {
    master::ListClonesResponsePB master_resp;
    RETURN_NOT_OK(client().ListClones(&master_resp));
    if (master_resp.has_error()) {
      return StatusFromPB(master_resp.error().status());
    }
    for (const auto& clone_state : master_resp.entries()) {
      if (clone_state.database_type() == YQL_DATABASE_PGSQL) {
        auto pg_clone_entry = resp->add_database_clones();
        if (clone_state.has_target_namespace_id()) {
          pg_clone_entry->set_db_id(
              VERIFY_RESULT(GetPgsqlDatabaseOid(clone_state.target_namespace_id())));
        }
        pg_clone_entry->set_db_name(clone_state.target_namespace_name());
        pg_clone_entry->set_parent_db_id(
            VERIFY_RESULT(GetPgsqlDatabaseOid(clone_state.source_namespace_id())));
        pg_clone_entry->set_parent_db_name(clone_state.source_namespace_name());
        pg_clone_entry->set_state(
            master::SysCloneStatePB_State_Name(clone_state.aggregate_state()));
        pg_clone_entry->set_as_of_time(clone_state.restore_time());
        if (clone_state.has_abort_message()) {
          pg_clone_entry->set_failure_reason(clone_state.abort_message());
        }
      }
    }
    return Status::OK();
  }

  #define PG_CLIENT_SESSION_METHOD_FORWARD(r, data, method) \
  Status method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext* context) { \
    return VERIFY_RESULT(GetSession(req))->method(req, resp, context); \
  }

  #define PG_CLIENT_SESSION_ASYNC_METHOD_FORWARD(r, data, method) \
  void method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext context) { \
    const auto session = GetSession(req); \
    if (!session.ok()) { \
      Respond(session.status(), resp, &context); \
      return; \
    } \
    (*session)->method(req, resp, std::move(context)); \
  }

  BOOST_PP_SEQ_FOR_EACH(PG_CLIENT_SESSION_METHOD_FORWARD, ~, PG_CLIENT_SESSION_METHODS);
  BOOST_PP_SEQ_FOR_EACH(PG_CLIENT_SESSION_ASYNC_METHOD_FORWARD, ~, PG_CLIENT_SESSION_ASYNC_METHODS);

  size_t TEST_SessionsCount() {
    SharedLock lock(mutex_);
    return sessions_.size();
  }

 private:
  client::YBClient& client() { return *client_future_.get(); }

  template <class Req>
  Result<PgClientSessionLocker> GetSession(const Req& req) {
    return GetSession(req.session_id());
  }

  Result<SessionInfoPtr> GetSessionInfo(uint64_t session_id) {
    RSTATUS_DCHECK_NE(session_id, static_cast<uint64_t>(0), InvalidArgument, "Bad session id");
    SharedLock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (PREDICT_FALSE(it == sessions_.end())) {
      return STATUS(InvalidArgument,
          Format("Connection terminated unexpectedly due to unknown session $0", session_id),
          Slice(),
          PgsqlError(YBPgErrorCode::YB_PG_CONNECTION_DOES_NOT_EXIST));
    }
    return *it;
  }

  Result<LockablePgClientSessionPtr> DoGetSession(uint64_t session_id) {
    auto session_info = VERIFY_RESULT(GetSessionInfo(session_id));
    LockablePgClientSessionPtr result(session_info, &session_info->session());
    result->Touch();
    return result;
  }

  Result<PgClientSessionLocker> GetSession(uint64_t session_id) override {
    return PgClientSessionLocker(VERIFY_RESULT(DoGetSession(session_id)));
  }

  rpc::Messenger& Messenger() override {
    return messenger_;
  }

  void ScheduleCheckExpiredSessions(CoarseTimePoint now) REQUIRES(mutex_) {
    auto time = session_expiration_queue_.empty()
        ? CoarseTimePoint(now + FLAGS_pg_client_session_expiration_ms * 1ms)
        : session_expiration_queue_.top().first + 100ms;
    if (!stopping_sessions_.empty()) {
      time = std::min(time, now + 1s);
    }
    if (check_expired_sessions_time_ != CoarseTimePoint() && check_expired_sessions_time_ < time) {
      return;
    }
    check_expired_sessions_time_ = time;
    check_expired_sessions_.Schedule([this](const Status& status) {
      if (!status.ok()) {
        return;
      }
      this->CheckExpiredSessions();
    }, time - now);
  }

  void CheckExpiredSessions() {
    auto now = CoarseMonoClock::now();
    std::vector<SessionInfoPtr> expired_sessions;
    std::vector<SessionInfoPtr> ready_sessions;
    {
      std::lock_guard lock(mutex_);
      check_expired_sessions_time_ = CoarseTimePoint();
      while (!session_expiration_queue_.empty()) {
        auto& top = session_expiration_queue_.top();
        if (top.first > now) {
          break;
        }
        auto id = top.second;
        session_expiration_queue_.pop();
        auto it = sessions_.find(id);
        if (it != sessions_.end()) {
          auto current_expiration = (**it).session().expiration();
          if (current_expiration > now) {
            session_expiration_queue_.emplace(current_expiration, id);
          } else {
            expired_sessions.push_back(*it);
            sessions_.erase(it);
          }
        }
      }
      auto filter = [&ready_sessions](const auto& session) {
        if (session->session().ReadyToShutdown()) {
          ready_sessions.push_back(session);
          return true;
        }
        return false;
      };
      stopping_sessions_.erase(
          std::remove_if(stopping_sessions_.begin(), stopping_sessions_.end(), filter),
          stopping_sessions_.end());
      if (expired_sessions.empty()) {
        ScheduleCheckExpiredSessions(now);
      }
    }
    for (const auto& session : ready_sessions) {
      session->session().CompleteShutdown();
    }
    CleanupSessions(std::move(expired_sessions), now);
  }

  [[nodiscard]] client::YBTransactionPtr BuildTransaction(
      TxnAssignment* dest, IsDDL is_ddl, TransactionFullLocality locality,
      CoarseTimePoint deadline, client::ForceCreateTransaction force_create_txn) {
    auto watcher = std::make_shared<client::YBTransactionPtr>(
        transaction_pool_provider_().Take(locality, deadline, force_create_txn));
    dest->Assign(watcher, is_ddl);
    auto* txn = &**watcher;
    return {std::move(watcher), txn};
  }

  Result<std::unordered_set<uint32_t>> GetPgDatabaseOids() {
    LOG(INFO) << "Fetching set of database oids";
    auto namespaces = VERIFY_RESULT(
        client().ListNamespaces(client::IncludeNonrunningNamespaces::kFalse, YQL_DATABASE_PGSQL));
    std::unordered_set<uint32_t> result;
    for (const auto& ns : namespaces) {
      result.insert(VERIFY_RESULT(GetPgsqlDatabaseOid(ns.id.id())));
    }
    LOG(INFO) << "Successfully fetched " << result.size() << " database oids";
    return result;
  }

  void CheckObjectIdAllocators(const std::unordered_set<uint32_t>& db_oids) {
    std::lock_guard lock(mutex_);
    std::erase_if(
        reserved_oids_map_,
        [&db_oids](const auto& item) {
          const auto& [db_oid, _] = item;
          if (!db_oids.contains(db_oid)) {
            LOG(INFO) << "Erase PG object id allocator of database: " << db_oid;
            return true;
          }
          return false;
        });
  }

  void ScheduleCheckObjectIdAllocators() {
    LOG(INFO) << "ScheduleCheckObjectIdAllocators";
    check_object_id_allocators_.Schedule(
      [this](const Status& status) {
        if (!status.ok()) {
          LOG(INFO) << status;
          return;
        }
        auto db_oids = GetPgDatabaseOids();
        if (db_oids.ok()) {
          CheckObjectIdAllocators(*db_oids);
        } else {
          LOG(WARNING) << "Could not get the set of database oids: " << ResultToStatus(db_oids);
        }
        ScheduleCheckObjectIdAllocators();
      },
      std::chrono::seconds(FLAGS_check_pg_object_id_allocators_interval_secs));
  }

  const TabletServerIf& tablet_server_;
  std::shared_future<client::YBClient*> client_future_;
  const scoped_refptr<ClockBase> clock_;
  TransactionManagerProvider transaction_manager_provider_;
  TransactionPoolProvider transaction_pool_provider_;
  rpc::Messenger& messenger_;
  PgTableCache table_cache_;
  rw_spinlock mutex_;

  struct OidPrefetchChunk {
    uint32_t next_oid = kPgFirstNormalObjectId;
    uint32_t oid_count = 0;
    bool allocated_from_secondary_space = false;
    uint32_t oid_cache_invalidations_count = 0;
  };
  // Domain here is db_oid of namespace we are allocating OIDs for.
  std::unordered_map<uint32_t, OidPrefetchChunk> reserved_oids_map_ GUARDED_BY(mutex_);

  using ExpirationEntry = std::pair<CoarseTimePoint, uint64_t>;

  struct CompareExpiration {
    bool operator()(const ExpirationEntry& lhs, const ExpirationEntry& rhs) const {
      // Order is reversed, because std::priority_queue keeps track of the largest element.
      // This comparator is important for the cleanup logic.
      return rhs.first < lhs.first;
    }
  };

  std::priority_queue<ExpirationEntry,
                      std::vector<ExpirationEntry>,
                      CompareExpiration> session_expiration_queue_;

  std::atomic<int64_t> session_serial_no_{0};

  rpc::ScheduledTaskTracker check_expired_sessions_;
  CoarseTimePoint check_expired_sessions_time_ GUARDED_BY(mutex_);
  rpc::ScheduledTaskTracker check_object_id_allocators_;
  rpc::ScheduledTaskTracker check_ysql_lease_;

  PgResponseCache response_cache_;

  PgSequenceCache sequence_cache_;

  const std::string instance_id_;
  std::once_flag exchange_thread_pool_once_flag_;
  std::unique_ptr<YBThreadPool> exchange_thread_pool_;

  PgSharedMemoryPool shared_mem_pool_;

  std::array<rw_spinlock, 8> txns_assignment_mutexes_;
  TransactionBuilder transaction_builder_;
  YsqlAdvisoryLocksTable advisory_locks_table_;

  PgClientSessionContext session_context_;

  boost::multi_index_container<
      SessionInfoPtr,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::const_mem_fun<SessionInfo, uint64_t, &SessionInfo::id>
          >
      >
  > sessions_ GUARDED_BY(mutex_);

  std::vector<SessionInfoPtr> stopping_sessions_ GUARDED_BY(mutex_);

  std::optional<cdc::CDCStateTable> cdc_state_table_;
  PgTxnSnapshotManager txn_snapshot_manager_;

  bool shutting_down_ GUARDED_BY(mutex_) = false;
};

PgClientServiceImpl::PgClientServiceImpl(
    std::reference_wrapper<const TabletServerIf> tablet_server,
    const std::shared_future<client::YBClient*>& client_future,
    const scoped_refptr<ClockBase>& clock, TransactionManagerProvider transaction_manager_provider,
    TransactionPoolProvider transaction_pool_provider,
    const std::shared_ptr<MemTracker>& parent_mem_tracker,
    const scoped_refptr<MetricEntity>& entity, rpc::Messenger* messenger,
    const std::string& permanent_uuid, const server::ServerBaseOptions& tablet_server_opts,
    const TserverXClusterContextIf* xcluster_context,
    PgMutationCounter* pg_node_level_mutation_counter)
    : PgClientServiceIf(entity),
      impl_(new Impl(
          tablet_server, client_future, clock, std::move(transaction_manager_provider),
          std::move(transaction_pool_provider), messenger,
          xcluster_context, pg_node_level_mutation_counter, entity.get(), parent_mem_tracker,
          permanent_uuid, tablet_server_opts)) {}

PgClientServiceImpl::~PgClientServiceImpl() = default;

void PgClientServiceImpl::Perform(
    const PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext context) {
  impl_->Perform(const_cast<PgPerformRequestPB*>(req), resp, &context);
}

void PgClientServiceImpl::InvalidateTableCache() {
  impl_->InvalidateTableCache();
}

void PgClientServiceImpl::InvalidateTableCache(
    const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
    const std::unordered_set<uint32_t>& db_oids_deleted) {
  impl_->InvalidateTableCache(db_oids_updated, db_oids_deleted);
}

Result<PgTxnSnapshot> PgClientServiceImpl::GetLocalPgTxnSnapshot(
    const PgTxnSnapshotLocalId& snapshot_id) {
  return impl_->GetLocalPgTxnSnapshot(snapshot_id);
}

size_t PgClientServiceImpl::TEST_SessionsCount() { return impl_->TEST_SessionsCount(); }

void PgClientServiceImpl::Shutdown() { impl_->Shutdown(); }

#define YB_PG_CLIENT_METHOD_DEFINE(r, data, method) \
void PgClientServiceImpl::method( \
    const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
    BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
    rpc::RpcContext context) { \
  Respond(impl_->method(*req, resp, &context), resp, &context); \
}

#define YB_PG_CLIENT_ASYNC_METHOD_DEFINE(r, data, method) \
void PgClientServiceImpl::method( \
    const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
    BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
    rpc::RpcContext context) { \
  impl_->method(*req, resp, std::move(context)); \
}

#define YB_PG_CLIENT_TRIVIAL_METHOD_DEFINE(r, data, method) \
Result<BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)> PgClientServiceImpl::method( \
    const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
    CoarseTimePoint deadline) { \
  return impl_->method(req, deadline); \
}

BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DEFINE, ~, YB_PG_CLIENT_METHODS);
BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_ASYNC_METHOD_DEFINE, ~, YB_PG_CLIENT_ASYNC_METHODS);
BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_TRIVIAL_METHOD_DEFINE, ~, YB_PG_CLIENT_TRIVIAL_METHODS);

PgClientServiceMockImpl::PgClientServiceMockImpl(
    const scoped_refptr<MetricEntity>& entity, PgClientServiceIf* impl)
    : PgClientServiceIf(entity), impl_(impl) {}

PgClientServiceMockImpl::Handle PgClientServiceMockImpl::SetMock(
    const std::string& method, SharedFunctor&& mock) {
  {
    std::lock_guard lock(mutex_);
    mocks_[method] = mock;
  }

  return Handle{std::move(mock)};
}

void PgClientServiceMockImpl::UnsetMock(const std::string& method) {
  std::lock_guard lock(mutex_);
  auto it = mocks_.find(method);
  if (it == mocks_.end()) {
    LOG(WARNING) << "No mock found for method: " << method;
    return;
  }

  LOG(INFO) << "Resetting mock for method: " << method;
  mocks_.erase(it);
}

Result<bool> PgClientServiceMockImpl::DispatchMock(
    const std::string& method, const void* req, void* resp, rpc::RpcContext* context) {
  SharedFunctor mock;
  {
    SharedLock lock(mutex_);
    auto it = mocks_.find(method);
    if (it != mocks_.end()) {
      mock = it->second.lock();
    }
  }

  if (!mock) {
    return false;
  }
  RETURN_NOT_OK((*mock)(req, resp, context));
  return true;
}

#define YB_PG_CLIENT_MOCK_METHOD_DEFINE(r, data, method) \
  void PgClientServiceMockImpl::method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB) * req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB) * resp, rpc::RpcContext context) { \
    auto result = DispatchMock(BOOST_PP_STRINGIZE(method), req, resp, &context); \
    if (!result.ok() || *result) { \
      Respond(ResultToStatus(result), resp, &context); \
      return; \
    } \
    impl_->method(req, resp, std::move(context)); \
  }

template <class Req, class Resp>
auto MakeSharedFunctor(const std::function<Status(const Req*, Resp*, rpc::RpcContext*)>& func) {
  return std::make_shared<PgClientServiceMockImpl::Functor>(
      [func](const void* req, void* resp, rpc::RpcContext* context) {
        return func(pointer_cast<const Req*>(req), pointer_cast<Resp*>(resp), context);
      });
}

#define YB_PG_CLIENT_MOCK_METHOD_SETTER_DEFINE(r, data, method) \
  PgClientServiceMockImpl::Handle BOOST_PP_CAT(PgClientServiceMockImpl::Mock, method)( \
      const std::function<Status( \
          const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)*, \
          BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)*, rpc::RpcContext*)>& mock) { \
    return SetMock(BOOST_PP_STRINGIZE(method), MakeSharedFunctor(mock)); \
  }

BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_MOCK_METHOD_DEFINE, ~, YB_PG_CLIENT_MOCKABLE_METHODS);
BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_MOCK_METHOD_SETTER_DEFINE, ~, YB_PG_CLIENT_MOCKABLE_METHODS);

}  // namespace yb::tserver
