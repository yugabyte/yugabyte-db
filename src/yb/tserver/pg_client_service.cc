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

#include "yb/tserver/pg_client_service.h"

#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/server/async_client_initializer.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"

#include "yb/dockv/partition.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/rocksdb/db/db_impl.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/tasks_pool.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/server/server_base.h"

#include "yb/tserver/pg_client_session.h"
#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/pg_response_cache.h"
#include "yb/tserver/pg_sequence_cache.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/shared_lock.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/yb_pg_errcodes.h"

using namespace std::literals;

DEFINE_UNKNOWN_uint64(pg_client_session_expiration_ms, 60000,
                      "Pg client session expiration time in milliseconds.");

DEFINE_RUNTIME_bool(pg_client_use_shared_memory, yb::IsDebug(),
                    "Use shared memory for executing read and write pg client queries");

DEFINE_RUNTIME_int32(get_locks_status_max_retry_attempts, 2,
                     "Maximum number of retries that will be performed for GetLockStatus "
                     "requests that fail in the validation phase due to unseen responses from "
                     "some of the involved tablets.");

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

DEFINE_test_flag(uint64, ysql_oid_prefetch_adjustment, 0,
                 "Amount to add when prefetch the next batch of OIDs. Never use this flag in "
                 "production environment. In unit test we use this flag to force allocation of "
                 "large Postgres OIDs.");

DEFINE_RUNTIME_uint64(ysql_cdc_active_replication_slot_window_ms, 60000,
                      "Determines the window in milliseconds in which if a client has consumed the "
                      "changes of a ReplicationSlot across any tablet, then it is considered to be "
                      "actively used. ReplicationSlots which haven't been used in this interval are"
                      "considered to be inactive.");
TAG_FLAG(ysql_cdc_active_replication_slot_window_ms, advanced);

DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_int32(cdc_read_rpc_timeout_ms);

DEFINE_RUNTIME_int32(
    check_pg_object_id_allocators_interval_secs, 3600 * 3,
    "Interval at which pg object id allocators are checked for dropped databases.");
TAG_FLAG(check_pg_object_id_allocators_interval_secs, advanced);


namespace yb::tserver {
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

class LockablePgClientSession : public PgClientSession {
 public:
  template <class... Args>
  explicit LockablePgClientSession(CoarseDuration lifetime, Args&&... args)
      : PgClientSession(std::forward<Args>(args)...),
        lifetime_(lifetime), expiration_(NewExpiration()) {
  }

  void StartExchange(const std::string& instance_id) {
    exchange_.emplace(instance_id, id(), Create::kTrue, [this](size_t size) {
      Touch();
      std::shared_ptr<CountDownLatch> latch;
      {
        std::unique_lock lock(mutex_);
        latch = ProcessSharedRequest(size, &exchange_->exchange());
      }
      if (latch) {
        latch->Wait();
      }
    });
  }

  CoarseTimePoint expiration() const {
    return expiration_.load(std::memory_order_acquire);
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

 private:
  friend class PgClientSessionLocker;

  CoarseTimePoint NewExpiration() const {
    return CoarseMonoClock::now() + lifetime_;
  }

  std::mutex mutex_;
  std::optional<SharedExchangeThread> exchange_;
  const CoarseDuration lifetime_;
  std::atomic<CoarseTimePoint> expiration_;
};

template <class T>
class DeferredConstructible {
 public:
  DeferredConstructible() = default;

  ~DeferredConstructible() {
    get().~T();
  }

  T& get() const {
#ifndef NDEBUG
    DCHECK(initialized_);
#endif
    return *pointer_cast<T*>(holder_);
  }

  template <class... Args>
  void Init(Args&&... args) {
#ifndef NDEBUG
    DCHECK(!initialized_);
    initialized_ = true;
#endif
    new (holder_) T(std::forward<Args>(args)...);
  }

 private:
  mutable char holder_[sizeof(T)];
#ifndef NDEBUG
  bool initialized_{false};
#endif

  DISALLOW_COPY_AND_ASSIGN(DeferredConstructible);
};

using TransactionBuilder = std::function<
    client::YBTransactionPtr(
        TxnAssignment* dest, IsDDL, client::ForceGlobalTransaction, CoarseTimePoint)>;

class SessionInfo {
 public:
  LockablePgClientSession& session() { return session_.get(); }

  uint64_t id() const { return session_.get().id(); }

  TxnAssignment& txn_assignment() { return txn_assignment_; }

  template <class... Args>
  static auto Make(rw_spinlock* txn_assignment_mutex,
                   CoarseDuration lifetime,
                   const TransactionBuilder& builder,
                   Args&&... args) {
    struct ConstructorAccessor : public SessionInfo {
      explicit ConstructorAccessor(rw_spinlock* txn_assignment_mutex)
          : SessionInfo(txn_assignment_mutex) {}
    };
    auto accessor = std::make_shared<ConstructorAccessor>(txn_assignment_mutex);
    SessionInfo* session_info = accessor.get();
    session_info->session_.Init(
        lifetime,
        [&builder, txn_assignment = &session_info->txn_assignment_](auto&&... builder_args) {
          return builder(txn_assignment, std::forward<decltype(builder_args)>(builder_args)...);
        },
        accessor,
        std::forward<Args>(args)...);
    return std::shared_ptr<SessionInfo>(std::move(accessor), session_info);
  }

 private:
  explicit SessionInfo(rw_spinlock* txn_assignment_mutex)
      : txn_assignment_(txn_assignment_mutex) {}

  TxnAssignment txn_assignment_;
  DeferredConstructible<LockablePgClientSession> session_;
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

  PgClientSession* operator->() const { return lockable_.get(); }

 private:
  LockablePgClientSessionPtr lockable_;
  std::unique_lock<std::mutex> lock_;
};

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


void GetTablePartitionList(const client::YBTablePtr& table, PgTablePartitionsPB* partition_list) {
  const auto table_partition_list = table->GetVersionedPartitions();
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

auto MakeSharedOldTxnMetadataVariant(OldTxnMetadataVariant&& txn_meta_variant) {
  OldTxnMetadataPtrVariant shared_txn_meta;
  if (auto txn_meta_pb_ptr = std::get_if<OldTransactionMetadataPB>(&txn_meta_variant)) {
    shared_txn_meta = std::make_shared<OldTransactionMetadataPB>(std::move(*txn_meta_pb_ptr));
  } else {
    auto meta_pb_ptr = std::get_if<OldSingleShardWaiterMetadataPB>(&txn_meta_variant);
    shared_txn_meta = std::make_shared<OldSingleShardWaiterMetadataPB>(std::move(*meta_pb_ptr));
  }
  return shared_txn_meta;
}

} // namespace

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

class PgClientServiceImpl::Impl {
 public:
  explicit Impl(
      std::reference_wrapper<const TabletServerIf> tablet_server,
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock, TransactionPoolProvider transaction_pool_provider,
      rpc::Messenger* messenger, const TserverXClusterContextIf* xcluster_context,
      PgMutationCounter* pg_node_level_mutation_counter, MetricEntity* metric_entity,
      const std::shared_ptr<MemTracker>& parent_mem_tracker, const std::string& permanent_uuid,
      const server::ServerBaseOptions* tablet_server_opts)
      : tablet_server_(tablet_server.get()),
        client_future_(client_future),
        clock_(clock),
        transaction_pool_provider_(std::move(transaction_pool_provider)),
        table_cache_(client_future),
        check_expired_sessions_(&messenger->scheduler()),
        check_object_id_allocators_(&messenger->scheduler()),
        xcluster_context_(xcluster_context),
        pg_node_level_mutation_counter_(pg_node_level_mutation_counter),
        response_cache_(parent_mem_tracker, metric_entity),
        instance_id_(permanent_uuid),
        transaction_builder_([this](auto&&... args) {
          return BuildTransaction(std::forward<decltype(args)>(args)...);
        }) {
    DCHECK(!permanent_uuid.empty());
    ScheduleCheckExpiredSessions(CoarseMonoClock::now());
    cdc_state_client_init_ = std::make_unique<client::AsyncClientInitializer>(
        "cdc_state_client", std::chrono::milliseconds(FLAGS_cdc_read_rpc_timeout_ms),
        permanent_uuid, tablet_server_opts, metric_entity, parent_mem_tracker, messenger);
    cdc_state_client_init_->Start();
    cdc_state_table_ =
        std::make_shared<cdc::CDCStateTable>(cdc_state_client_init_->get_client_future());
    if (FLAGS_pg_client_use_shared_memory) {
      WARN_NOT_OK(SharedExchange::Cleanup(instance_id_), "Cleanup shared memory failed");
    }
  }

  ~Impl() {
    cdc_state_table_.reset();
    cdc_state_client_init_->Shutdown();
    check_expired_sessions_.Shutdown();
    check_object_id_allocators_.Shutdown();
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
        &txns_assignment_mutexes_[session_id % txns_assignment_mutexes_.size()],
        FLAGS_pg_client_session_expiration_ms * 1ms,
        transaction_builder_, session_id, &client(), clock_, &table_cache_, xcluster_context_,
        pg_node_level_mutation_counter_, &response_cache_, &sequence_cache_);
    resp->set_session_id(session_id);
    if (FLAGS_pg_client_use_shared_memory) {
      resp->set_instance_id(instance_id_);
      session_info->session().StartExchange(instance_id_);
    }

    std::lock_guard lock(mutex_);
    auto it = sessions_.insert(std::move(session_info)).first;
    session_expiration_queue_.push({(**it).session().expiration(), session_id});
    return Status::OK();
  }

  Status OpenTable(
      const PgOpenTableRequestPB& req, PgOpenTableResponsePB* resp, rpc::RpcContext* context) {
    if (req.invalidate_cache_time_us()) {
      table_cache_.InvalidateAll(CoarseTimePoint() + req.invalidate_cache_time_us() * 1us);
    }
    if (req.reopen()) {
      table_cache_.Invalidate(req.table_id());
    }

    client::YBTablePtr table;
    RETURN_NOT_OK(table_cache_.GetInfo(req.table_id(), &table, resp->mutable_info()));
    tserver::GetTablePartitionList(table, resp->mutable_partitions());
    return Status::OK();
  }

  Status GetTablePartitionList(
      const PgGetTablePartitionListRequestPB& req,
      PgGetTablePartitionListResponsePB* resp,
      rpc::RpcContext* context) {
    const auto table = VERIFY_RESULT(table_cache_.Get(req.table_id()));
    tserver::GetTablePartitionList(table, resp->mutable_partitions());
    return Status::OK();
  }

  Status GetDatabaseInfo(
      const PgGetDatabaseInfoRequestPB& req, PgGetDatabaseInfoResponsePB* resp,
      rpc::RpcContext* context) {
    return client().GetNamespaceInfo(
        GetPgsqlNamespaceId(req.oid()), "" /* namespace_name */, YQL_DATABASE_PGSQL,
        resp->mutable_info());
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
    uint32_t begin_oid, end_oid;
    RETURN_NOT_OK(client().ReservePgsqlOids(
        GetPgsqlNamespaceId(req.database_oid()), req.next_oid(), req.count(), &begin_oid,
        &end_oid));
    resp->set_begin_oid(begin_oid);
    resp->set_end_oid(end_oid);

    return Status::OK();
  }

  Status GetNewObjectId(
      const PgGetNewObjectIdRequestPB& req,
      PgGetNewObjectIdResponsePB* resp,
      rpc::RpcContext* context) {
    // Number of OIDs to prefetch (preallocate) in YugabyteDB setup.
    // Given there are multiple Postgres nodes, each node should prefetch
    // in smaller chunks.
    constexpr int32_t kYbOidPrefetch = 256;
    auto db_oid = req.db_oid();
    std::lock_guard lock(mutex_);
    auto& oid_chunk = reserved_oids_map_[db_oid];
    if (oid_chunk.oid_count == 0) {
      const uint32_t next_oid = oid_chunk.next_oid +
          static_cast<uint32_t>(FLAGS_TEST_ysql_oid_prefetch_adjustment);
      uint32_t begin_oid, end_oid;
      RETURN_NOT_OK(client().ReservePgsqlOids(
          GetPgsqlNamespaceId(db_oid), next_oid, kYbOidPrefetch, &begin_oid, &end_oid));
      oid_chunk.next_oid = begin_oid;
      oid_chunk.oid_count = end_oid - begin_oid;
      VLOG(1) << "Reserved oids in database: " << db_oid << ", next_oid: " << next_oid
              << ", begin_oid: " << begin_oid << ", end_oid: " << end_oid;
    }
    uint32 new_oid = oid_chunk.next_oid;
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
    RETURN_NOT_OK(client().GetYsqlCatalogMasterVersion(&version));
    resp->set_version(version);
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
      const std::shared_ptr<TabletServerServiceProxy>& proxy, const TabletId& tablet_id) {
    auto req = std::make_shared<tserver::GetOldTransactionsRequestPB>();
    req->set_tablet_id(tablet_id);
    req->set_min_txn_age_ms(min_txn_age_ms);
    req->set_max_num_txns(max_num_txns);

    return MakeFuture<Result<OldTxnsRespInfo>>([&](auto callback) {
      auto resp = std::make_shared<GetOldTransactionsResponsePB>();
      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      proxy->GetOldTransactionsAsync(
          *req.get(), resp.get(), controller.get(),
          [req, callback, controller, resp] {
        auto s = controller->status();
        if (!s.ok()) {
          s = s.CloneAndPrepend(
              Format("GetOldTransactions request for tablet $0 failed: ", req->tablet_id()));
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
      const std::shared_ptr<TabletServerServiceProxy>& proxy) {
    auto req = std::make_shared<tserver::GetOldSingleShardWaitersRequestPB>();
    req->set_min_txn_age_ms(min_txn_age_ms);
    req->set_max_num_txns(max_num_txns);

    return MakeFuture<Result<OldTxnsRespInfo>>([&](auto callback) {
      auto resp = std::make_shared<GetOldSingleShardWaitersResponsePB>();
      std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
      proxy->GetOldSingleShardWaitersAsync(
          *req.get(), resp.get(), controller.get(),
          [req, callback, controller, resp] {
        auto s = controller->status();
        if (!s.ok()) {
          s = s.CloneAndPrepend("GetOldSingleShardWaiters request failed: ");
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
      GetLockStatusRequestPB* req, bool is_within_retry = false) {
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
      return is_within_retry ? combined_status : ReplaceSplitTabletsAndGetLocations(req, true);
    }

    std::set<std::string> tserver_uuids;
    for (const auto& tablet_location_pb : resp.tablet_locations()) {
      for (const auto& replica : tablet_location_pb.replicas()) {
        if (replica.role() == PeerRole::LEADER) {
          tserver_uuids.insert(replica.ts_info().permanent_uuid());
        }
      }
    }

    std::vector<RemoteTabletServerPtr> remote_tservers;
    remote_tservers.reserve(tserver_uuids.size());
    for(const auto& ts_uuid : tserver_uuids) {
      remote_tservers.push_back(VERIFY_RESULT(client().GetRemoteTabletServer(ts_uuid)));
    }
    return remote_tservers;
  }

  Status GetLockStatus(
      const PgGetLockStatusRequestPB& req, PgGetLockStatusResponsePB* resp,
      rpc::RpcContext* context) {
    std::vector<master::TSInformationPB> live_tservers;
    RETURN_NOT_OK(tablet_server_.GetLiveTServers(&live_tservers));
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
      lock_status_req.add_transaction_ids(req.transaction_id());
      std::vector<RemoteTabletServerPtr> remote_tservers;
      remote_tservers.reserve(live_tservers.size());
      for (const auto& live_ts : live_tservers) {
        const auto& permanent_uuid = live_ts.tserver_instance().permanent_uuid();
        remote_tservers.push_back(VERIFY_RESULT(client().GetRemoteTabletServer(permanent_uuid)));
      }
      return DoGetLockStatus(&lock_status_req, resp, context, remote_tservers);
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
    for (const auto& live_ts : live_tservers) {
      const auto& permanent_uuid = live_ts.tserver_instance().permanent_uuid();
      auto remote_tserver = VERIFY_RESULT(client().GetRemoteTabletServer(permanent_uuid));
      auto txn_status_tablets = VERIFY_RESULT(
            client().GetTransactionStatusTablets(remote_tserver->cloud_info_pb()));

      auto proxy = remote_tserver->proxy();
      for (const auto& tablet : txn_status_tablets.global_tablets) {
        res_futures.push_back(
            DoGetOldTransactionsForTablet(min_txn_age_ms, max_num_txns, proxy, tablet));
        status_tablet_ids.insert(tablet);
      }
      for (const auto& tablet : txn_status_tablets.placement_local_tablets) {
        res_futures.push_back(
            DoGetOldTransactionsForTablet(min_txn_age_ms, max_num_txns, proxy, tablet));
        status_tablet_ids.insert(tablet);
      }
      // Query for oldest single shard waiting transactions as well.
      res_futures.push_back(
          DoGetOldSingleShardWaiters(min_txn_age_ms, max_num_txns, proxy));
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
         it != res_futures.end() && resp->status().code() == AppStatusPB::OK; ) {
      auto res = it->get();
      if (!res.ok()) {
        return res.status();
      }

      std::visit([&](auto&& old_txns_resp) {
        if (old_txns_resp->has_error()) {
          // Ignore leadership errors as we broadcast the request to all tservers.
          if (old_txns_resp->error().code() == TabletServerErrorPB::NOT_THE_LEADER) {
            it = res_futures.erase(it);
            return;
          }
          const auto& s = StatusFromPB(old_txns_resp->error().status());
          StatusToPB(s, resp->mutable_status());
          return;
        }

        status_tablet_ids.erase(res->status_tablet_id);
        for (auto& old_txn : old_txns_resp->txn()) {
          auto old_txn_ptr = MakeSharedOldTxnMetadataVariant(std::move(old_txn));
          old_txns_pq.push(std::move(old_txn_ptr));
          while (old_txns_pq.size() > max_num_txns) {
            VLOG(4) << "Dropping old transaction with metadata "
                    << std::visit([](auto&& arg) {
                         return arg->ShortDebugString();
                       }, old_txns_pq.top());
            old_txns_pq.pop();
          }
        }
        it++;
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
        }
      }, old_txn);
      old_txns_pq.pop();
    }
    if (include_single_shard_waiters) {
      lock_status_req.set_max_single_shard_waiter_start_time_us(max_single_shard_waiter_start_time);
    }
    auto remote_tservers = VERIFY_RESULT(ReplaceSplitTabletsAndGetLocations(&lock_status_req));
    return DoGetLockStatus(&lock_status_req, resp, context, remote_tservers);
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

  Status DoGetLockStatus(
      GetLockStatusRequestPB* req, PgGetLockStatusResponsePB* resp,
      rpc::RpcContext* context, const std::vector<RemoteTabletServerPtr>& remote_tservers,
      int retry_attempt = 0) {
    if (PREDICT_FALSE(FLAGS_TEST_delay_before_get_locks_status_ms > 0)) {
      AtomicFlagSleepMs(&FLAGS_TEST_delay_before_get_locks_status_ms);
    }

    VLOG(4) << "Request to DoGetLockStatus: " << req->ShortDebugString();
    if (req->transactions_by_tablet().empty() && req->transaction_ids().empty()) {
      return Status::OK();
    }

    std::vector<std::future<Status>> status_futures;
    status_futures.reserve(remote_tservers.size());
    std::vector<std::shared_ptr<GetLockStatusResponsePB>> node_responses;
    node_responses.reserve(remote_tservers.size());
    for (const auto& remote_tserver : remote_tservers) {
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

    auto s = RefineAccumulatedLockStatusResp(req, resp);
    if (!s.ok()) {
      s = s.CloneAndPrepend("Error refining accumulated LockStatus responses.");
    } else if (!req->transactions_by_tablet().empty()) {
      // We haven't heard back from all involved tablets, retry GetLockStatusRequest on the
      // tablets missing in the response if we haven't maxed out on the retry attempts.
      if (retry_attempt > FLAGS_get_locks_status_max_retry_attempts) {
        s = STATUS_FORMAT(IllegalState,
                          "Expected to see involved tablet(s) $0 in PgGetLockStatusResponsePB",
                          req->ShortDebugString());
      } else {
        PgGetLockStatusResponsePB sub_resp;
        for (const auto& node_txn_pair : resp->transactions_by_node()) {
          sub_resp.mutable_transactions_by_node()->insert(node_txn_pair);
        }
        RETURN_NOT_OK(DoGetLockStatus(
            req, &sub_resp, context, VERIFY_RESULT(ReplaceSplitTabletsAndGetLocations(req)),
            ++retry_attempt));
        s = MergeLockStatusResponse(resp, &sub_resp);
      }
    }
    if (!s.ok()) {
      resp->Clear();
    }
    StatusToPB(s, resp->mutable_status());
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
    RSTATUS_DCHECK(seen_transactions.empty() || !req->transaction_ids().empty(), IllegalState,
           "Host node uuid not set for all involved transactions");
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
    auto tablet_servers = VERIFY_RESULT(client().ListLiveTabletServers(req.primary_only()));
    for (const auto& server : tablet_servers) {
      server.ToPB(resp->mutable_servers()->Add());
    }
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

    if (!streams.empty()) {
      Status iteration_status;
      auto range_result = VERIFY_RESULT(cdc_state_table_->GetTableRange(
          cdc::CDCStateTableEntrySelector()
              .IncludeActiveTime()
              .IncludeConfirmedFlushLSN()
              .IncludeRestartLSN()
              .IncludeXmin()
              .IncludeRecordIdCommitTime()
              .IncludeLastPubRefreshTime(),
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
      stream.ToPB(replication_slot);
      auto is_stream_active =
          current_time - stream_to_latest_active_time[*stream_id] <=
          1000 * GetAtomicFlag(&FLAGS_ysql_cdc_active_replication_slot_window_ms);
      replication_slot->set_replication_slot_status(
          (is_stream_active) ? ReplicationSlotStatus::ACTIVE : ReplicationSlotStatus::INACTIVE);

      if (stream_to_metadata.contains(*stream_id)) {
        auto slot_metadata = stream_to_metadata[*stream_id];
        replication_slot->set_confirmed_flush_lsn(slot_metadata.first.first);
        replication_slot->set_restart_lsn(slot_metadata.first.second);
        replication_slot->set_xmin(slot_metadata.second.first);
        replication_slot->set_record_id_commit_time_ht(slot_metadata.second.second);
        replication_slot->set_last_pub_refresh_time(stream_to_last_pub_refresh_time[*stream_id]);
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
    RETURN_NOT_OK(GetReplicationSlotInfoFromCDCState(
        stream_id, &is_slot_active, &confirmed_flush_lsn, &restart_lsn, &xmin,
        &record_id_commit_time_ht, &last_pub_refresh_time));
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
    return Status::OK();
  }

  // DEPRECATED: GetReplicationSlot RPC is a superset of this GetReplicationSlotStatus.
  // So GetReplicationSlot should be used everywhere.
  Status GetReplicationSlotStatus(
      const PgGetReplicationSlotStatusRequestPB& req, PgGetReplicationSlotStatusResponsePB* resp,
      rpc::RpcContext* context) {
    // Get the stream_id for the replication slot.
    auto stream = VERIFY_RESULT(client().GetCDCStream(
        ReplicationSlotName(req.replication_slot_name()), /* replica_identities */ nullptr));
    auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream.stream_id));

    bool is_slot_active;
    uint64_t confirmed_flush_lsn;
    uint64_t restart_lsn;
    uint32_t xmin;
    uint64_t record_id_commit_time_ht;
    uint64_t last_pub_refresh_time;
    RETURN_NOT_OK(GetReplicationSlotInfoFromCDCState(
        stream_id, &is_slot_active, &confirmed_flush_lsn, &restart_lsn, &xmin,
        &record_id_commit_time_ht, &last_pub_refresh_time));
    resp->set_replication_slot_status(
        (is_slot_active) ? ReplicationSlotStatus::ACTIVE : ReplicationSlotStatus::INACTIVE);
    return Status::OK();
  }

  Status GetReplicationSlotInfoFromCDCState(
      const xrepl::StreamId& stream_id, bool* active, uint64_t* confirmed_flush_lsn,
      uint64_t* restart_lsn, uint32_t* xmin, uint64_t* record_id_commit_time_ht,
      uint64_t* last_pub_refresh_time) {
    // TODO(#19850): Fetch only the entries belonging to the stream_id from the table.
    Status iteration_status;
    auto range_result = VERIFY_RESULT(cdc_state_table_->GetTableRange(
        cdc::CDCStateTableEntrySelector()
            .IncludeActiveTime()
            .IncludeConfirmedFlushLSN()
            .IncludeRestartLSN()
            .IncludeXmin()
            .IncludeRecordIdCommitTime()
            .IncludeLastPubRefreshTime(),
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
    return Status::OK();
  }

  Status GetIndexBackfillProgress(
      const PgGetIndexBackfillProgressRequestPB& req, PgGetIndexBackfillProgressResponsePB* resp,
      rpc::RpcContext* context) {
    std::vector<TableId> index_ids;
    for (const auto& index_id : req.index_ids()) {
      index_ids.emplace_back(PgObjectId::GetYbTableIdFromPB(index_id));
    }
    return client().GetIndexBackfillProgress(index_ids, resp->mutable_rows_processed_entries());
  }

  Status ValidatePlacement(
      const PgValidatePlacementRequestPB& req, PgValidatePlacementResponsePB* resp,
      rpc::RpcContext* context) {
    master::ReplicationInfoPB replication_info;
    master::PlacementInfoPB* live_replicas = replication_info.mutable_live_replicas();

    for (const auto& block : req.placement_infos()) {
      auto pb = live_replicas->add_placement_blocks();
      pb->mutable_cloud_info()->set_placement_cloud(block.cloud());
      pb->mutable_cloud_info()->set_placement_region(block.region());
      pb->mutable_cloud_info()->set_placement_zone(block.zone());
      pb->set_min_num_replicas(block.min_num_replicas());

      if (block.leader_preference() < 0) {
        return STATUS(InvalidArgument, "leader_preference cannot be negative");
      } else if (block.leader_preference() > req.placement_infos_size()) {
        return STATUS(
            InvalidArgument,
            "Priority value cannot be more than the number of zones in the preferred list since "
            "each priority should be associated with at least one zone from the list");
      } else if (block.leader_preference() > 0) {
        while (replication_info.multi_affinitized_leaders_size() < block.leader_preference()) {
          replication_info.add_multi_affinitized_leaders();
        }

        auto zone_set =
            replication_info.mutable_multi_affinitized_leaders(block.leader_preference() - 1);
        auto ci = zone_set->add_zones();
        ci->set_placement_cloud(block.cloud());
        ci->set_placement_region(block.region());
        ci->set_placement_zone(block.zone());
      }
    }
    live_replicas->set_num_replicas(req.num_replicas());

    return client().ValidateReplicationInfo(replication_info);
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
        call.wait_state().wait_status_code() == yb::to_underlying(ash::WaitStateCode::kIdle) ||
        // Ignore ActiveSessionHistory/Perform calls, if desired.
        (req.ignore_ash_and_perform_calls() && call.wait_state().has_aux_info() &&
         call.wait_state().aux_info().has_method() &&
         (call.wait_state().aux_info().method() == "ActiveSessionHistory" ||
          call.wait_state().aux_info().method() == "Perform")));
  }

  void GetRpcsWaitStates(
      const PgActiveSessionHistoryRequestPB& req, ash::Component component,
      tserver::WaitStatesPB* resp) {
    auto* messenger = tablet_server_.GetMessenger(component);
    if (!messenger) {
      LOG_WITH_FUNC(ERROR) << "got no messenger for " << yb::ToString(component);
      return;
    }

    resp->set_component(yb::to_underlying(component));

    rpc::DumpRunningRpcsRequestPB dump_req;
    rpc::DumpRunningRpcsResponsePB dump_resp;
    dump_req.set_include_traces(false);
    dump_req.set_get_wait_state(true);
    dump_req.set_dump_timed_out(false);
    dump_req.set_get_local_calls(true);

    WARN_NOT_OK(messenger->DumpRunningRpcs(dump_req, &dump_resp), "DumpRunningRpcs failed");

    size_t ignored_calls = 0;
    size_t ignored_calls_no_wait_state = 0;
    for (const auto& conns : dump_resp.inbound_connections()) {
      for (const auto& call : conns.calls_in_flight()) {
        if (ShouldIgnoreCall(req, call)) {
          ignored_calls++;
          if (!call.has_wait_state()) {
            ignored_calls_no_wait_state++;
          }
          continue;
        }
        resp->add_wait_states()->CopyFrom(call.wait_state());
      }
    }
    if (dump_resp.has_local_calls()) {
      for (const auto& call : dump_resp.local_calls().calls_in_flight()) {
        if (ShouldIgnoreCall(req, call)) {
          ignored_calls++;
          if (!call.has_wait_state()) {
            ignored_calls_no_wait_state++;
          }
          continue;
        }
        resp->add_wait_states()->CopyFrom(call.wait_state());
      }
    }
    LOG_IF(INFO, VLOG_IS_ON(1) || ignored_calls_no_wait_state > 0)
        << "Ignored " << ignored_calls << " calls. " << ignored_calls_no_wait_state
        << " without wait state";
    VLOG(2) << __PRETTY_FUNCTION__
            << " wait-states: " << yb::ToString(resp->wait_states());
  }

  void AddWaitStatesToResponse(const ash::WaitStateTracker& tracker, tserver::WaitStatesPB* resp) {
    Result<Uuid> local_uuid = Uuid::FromHexString(instance_id_);
    DCHECK_OK(local_uuid);
    resp->set_component(yb::to_underlying(ash::Component::kTServer));
    for (auto& wait_state_ptr : tracker.GetWaitStates()) {
      if (wait_state_ptr && wait_state_ptr->code() != ash::WaitStateCode::kIdle) {
        if (local_uuid) {
          wait_state_ptr->set_yql_endpoint_tserver_uuid(*local_uuid);
        }
        wait_state_ptr->ToPB(resp->add_wait_states());
      }
    }
  }

  Status ActiveSessionHistory(
      const PgActiveSessionHistoryRequestPB& req, PgActiveSessionHistoryResponsePB* resp,
      rpc::RpcContext* context) {
    if (req.fetch_tserver_states()) {
      GetRpcsWaitStates(req, ash::Component::kTServer, resp->mutable_tserver_wait_states());
      AddWaitStatesToResponse(
          ash::SharedMemoryPgPerformTracker(), resp->mutable_tserver_wait_states());
    }
    if (req.fetch_flush_and_compaction_states()) {
      AddWaitStatesToResponse(
          ash::FlushAndCompactionWaitStatesTracker(),
          resp->mutable_flush_and_compaction_wait_states());
    }
    if (req.fetch_raft_log_appender_states()) {
      AddWaitStatesToResponse(
          ash::RaftLogAppenderWaitStatesTracker(), resp->mutable_raft_log_appender_wait_states());
    }
    if (req.fetch_cql_states()) {
      GetRpcsWaitStates(req, ash::Component::kYCQL, resp->mutable_cql_wait_states());
    }
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
    auto status = DoPerform(req, resp, context);
    if (!status.ok()) {
      Respond(status, resp, context);
    }
  }

  void InvalidateTableCache() {
    table_cache_.InvalidateAll(CoarseMonoClock::Now());
  }

  // Return the TabletServer hosting the specified status tablet.
  std::future<Result<RemoteTabletServerPtr>> GetTServerHostingStatusTablet(
      const TabletId& status_tablet_id, CoarseTimePoint deadline) {

    return MakeFuture<Result<RemoteTabletServerPtr>>([&](auto callback) {
      client().LookupTabletById(
          status_tablet_id, /* table =*/ nullptr, master::IncludeInactive::kFalse,
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
            callback(client().GetRemoteTabletServer(permanent_uuid));
          },
          // Force a client cache refresh so as to not hit NOT_LEADER error.
          client::UseCache::kFalse);
    });
  }

  Result<std::vector<RemoteTabletServerPtr>> GetAllLiveTservers() {
    std::vector<RemoteTabletServerPtr> remote_tservers;
    std::vector<master::TSInformationPB> live_tservers;
    RETURN_NOT_OK(tablet_server_.GetLiveTServers(&live_tservers));
    for (const auto& live_ts : live_tservers) {
      const auto& permanent_uuid = live_ts.tserver_instance().permanent_uuid();
      remote_tservers.push_back(VERIFY_RESULT(client().GetRemoteTabletServer(permanent_uuid)));
    }
    return remote_tservers;
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
      remote_tservers = VERIFY_RESULT(GetAllLiveTservers());
    } else {
      const auto& remote_ts = VERIFY_RESULT(GetTServerHostingStatusTablet(
          req.status_tablet_id(), context->GetClientDeadline()).get());
      remote_tservers.push_back(remote_ts);
      node_req.set_status_tablet_id(req.status_tablet_id());
    }

    std::vector<std::future<Status>> status_future;
    std::vector<tserver::CancelTransactionResponsePB> node_resp(remote_tservers.size());
    for (size_t i = 0 ; i < remote_tservers.size() ; i++) {
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
    const auto& result = VERIFY_RESULT(tablet_server_.GetLocalTabletsMetadata());
    *resp->mutable_tablets() = {result.begin(), result.end()};
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
    DCHECK_NE(session_id, 0);
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

  Result<PgClientSessionLocker> GetSession(uint64_t session_id) {
    return PgClientSessionLocker(VERIFY_RESULT(DoGetSession(session_id)));
  }

  void ScheduleCheckExpiredSessions(CoarseTimePoint now) REQUIRES(mutex_) {
    auto time = session_expiration_queue_.empty()
        ? CoarseTimePoint(now + FLAGS_pg_client_session_expiration_ms * 1ms)
        : session_expiration_queue_.top().first + 1s;
    check_expired_sessions_.Schedule([this](const Status& status) {
      if (!status.ok()) {
        return;
      }
      this->CheckExpiredSessions();
    }, time - now);
  }

  void CheckExpiredSessions() {
    auto now = CoarseMonoClock::now();
    std::vector<uint64_t> expired_sessions;
    std::lock_guard lock(mutex_);
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
          session_expiration_queue_.push({current_expiration, id});
        } else {
          expired_sessions.push_back(id);
          sessions_.erase(it);
        }
      }
    }
    auto cdc_service = tablet_server_.GetCDCService();
    // We only want to call this on tablet servers. On master, cdc_service will be null.
    if (cdc_service) {
      cdc_service->DestroyVirtualWALBatchForCDC(expired_sessions);
    }
    ScheduleCheckExpiredSessions(now);
  }

  Status DoPerform(PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext* context) {
    return VERIFY_RESULT(GetSession(*req))->Perform(req, resp, context);
  }

  [[nodiscard]] client::YBTransactionPtr BuildTransaction(
      TxnAssignment* dest, IsDDL is_ddl, client::ForceGlobalTransaction force_global,
      CoarseTimePoint deadline) {
    auto watcher = std::make_shared<client::YBTransactionPtr>(
        transaction_pool_provider_().Take(force_global, deadline));
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
  scoped_refptr<ClockBase> clock_;
  TransactionPoolProvider transaction_pool_provider_;
  PgTableCache table_cache_;
  rw_spinlock mutex_;

  struct OidPrefetchChunk {
    uint32_t next_oid = kPgFirstNormalObjectId;
    uint32_t oid_count = 0;
  };
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
  rpc::ScheduledTaskTracker check_object_id_allocators_;

  std::unique_ptr<yb::client::AsyncClientInitializer> cdc_state_client_init_;
  std::shared_ptr<cdc::CDCStateTable> cdc_state_table_;

  const TserverXClusterContextIf* xcluster_context_;

  PgMutationCounter* pg_node_level_mutation_counter_;

  PgResponseCache response_cache_;

  PgSequenceCache sequence_cache_;

  const std::string instance_id_;

  std::array<rw_spinlock, 8> txns_assignment_mutexes_;
  TransactionBuilder transaction_builder_;

  boost::multi_index_container<
      SessionInfoPtr,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::const_mem_fun<SessionInfo, uint64_t, &SessionInfo::id>
          >
      >
  > sessions_ GUARDED_BY(mutex_);
};

PgClientServiceImpl::PgClientServiceImpl(
    std::reference_wrapper<const TabletServerIf> tablet_server,
    const std::shared_future<client::YBClient*>& client_future,
    const scoped_refptr<ClockBase>& clock, TransactionPoolProvider transaction_pool_provider,
    const std::shared_ptr<MemTracker>& parent_mem_tracker,
    const scoped_refptr<MetricEntity>& entity, rpc::Messenger* messenger,
    const std::string& permanent_uuid, const server::ServerBaseOptions* tablet_server_opts,
    const TserverXClusterContextIf* xcluster_context,
    PgMutationCounter* pg_node_level_mutation_counter)
    : PgClientServiceIf(entity),
      impl_(new Impl(
          tablet_server, client_future, clock, std::move(transaction_pool_provider), messenger,
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

size_t PgClientServiceImpl::TEST_SessionsCount() {
  return impl_->TEST_SessionsCount();
}

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

BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DEFINE, ~, YB_PG_CLIENT_METHODS);
BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_ASYNC_METHOD_DEFINE, ~, YB_PG_CLIENT_ASYNC_METHODS);

}  // namespace yb::tserver
