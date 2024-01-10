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

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"

#include "yb/common/partition.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_heartbeat.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/tasks_pool.h"

#include "yb/tserver/pg_client_session.h"
#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/pg_response_cache.h"
#include "yb/tserver/pg_sequence_cache.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
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

DEFINE_RUNTIME_bool(pg_client_use_shared_memory, false,
                    "Use shared memory for executing read and write pg client queries");

namespace yb {
namespace tserver {

DEFINE_test_flag(uint64, ysql_oid_prefetch_adjustment, 0,
                 "Amount to add when prefetch the next batch of OIDs. Never use this flag in "
                 "production environment. In unit test we use this flag to force allocation of "
                 "large Postgres OIDs.");

namespace {

template <class Resp>
void Respond(const Status& status, Resp* resp, rpc::RpcContext* context) {
  if (!status.ok()) {
    StatusToPB(status, resp->mutable_status());
  }
  context->RespondSuccess();
}

class PgClientSessionLocker;

class LockablePgClientSession : public PgClientSession {
 public:
  template <class... Args>
  explicit LockablePgClientSession(CoarseDuration lifetime, Args&&... args)
      : PgClientSession(std::forward<Args>(args)...),
        lifetime_(lifetime), expiration_(NewExpiration()) {
  }

  void StartExchange(const Uuid& instance_id) {
    exchange_.emplace(instance_id, id(), Create::kTrue, [this](size_t size) {
      Touch();
      std::shared_ptr<CountDownLatch> latch;
      {
        std::unique_lock<std::mutex> lock(mutex_);
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

class PgClientSessionLocker {
 public:
  using LockablePtr = std::shared_ptr<LockablePgClientSession>;

  explicit PgClientSessionLocker(const LockablePtr& lockable)
      : lockable_(lockable), lock_(lockable->mutex_) {
  }

  PgClientSession& operator*() const {
    return *lockable_;
  }

  PgClientSession* operator->() const {
    return lockable_.get();
  }

 private:
  LockablePtr lockable_;
  std::unique_lock<std::mutex> lock_;
};

using LockablePgClientSessionPtr = std::shared_ptr<LockablePgClientSession>;

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
      const scoped_refptr<ClockBase>& clock,
      TransactionPoolProvider transaction_pool_provider,
      rpc::Scheduler* scheduler,
      const std::optional<XClusterContext>& xcluster_context,
      PgMutationCounter* pg_node_level_mutation_counter,
      MetricEntity* metric_entity)
      : tablet_server_(tablet_server.get()),
        client_future_(client_future),
        clock_(clock),
        transaction_pool_provider_(std::move(transaction_pool_provider)),
        table_cache_(client_future),
        check_expired_sessions_(scheduler),
        xcluster_context_(xcluster_context),
        pg_node_level_mutation_counter_(pg_node_level_mutation_counter),
        response_cache_(metric_entity),
        instance_id_(Uuid::Generate()) {
    ScheduleCheckExpiredSessions(CoarseMonoClock::now());
  }

  ~Impl() {
    check_expired_sessions_.Shutdown();
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
    auto session = std::make_shared<LockablePgClientSession>(
        FLAGS_pg_client_session_expiration_ms * 1ms, session_id, &client(), clock_,
        transaction_pool_provider_, &table_cache_, xcluster_context_,
        pg_node_level_mutation_counter_, &response_cache_, &sequence_cache_);
    resp->set_session_id(session_id);
    if (FLAGS_pg_client_use_shared_memory) {
      resp->set_instance_id(instance_id_.data(), instance_id_.size());
      session->StartExchange(instance_id_);
    }

    std::lock_guard<rw_spinlock> lock(mutex_);
    auto it = sessions_.insert(std::move(session)).first;
    session_expiration_queue_.push({(**it).expiration(), session_id});
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

  void CheckObjectIdAllocators(const std::unordered_set<uint32_t>& db_oids) {
    std::lock_guard lock(mutex_);
    for (auto it = reserved_oids_map_.begin(); it != reserved_oids_map_.end();) {
      if (db_oids.count(it->first) == 0) {
        LOG(INFO) << "Erase PG object id allocator of database: " << it->first;
        it = reserved_oids_map_.erase(it);
      } else {
        it++;
      }
    }
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

  Status GetLockStatus(
      const PgGetLockStatusRequestPB& req, PgGetLockStatusResponsePB* resp,
      rpc::RpcContext* context) {
    std::vector<master::TSInformationPB> live_tservers;
    RETURN_NOT_OK(tablet_server_.GetLiveTServers(&live_tservers));

    // TODO(pglocks): Make use of req.table_id()
    // TODO(pglocks): parallelize RPCs
    rpc::RpcController controller;
    for (const auto& live_ts : live_tservers) {
      const auto& permanent_uuid = live_ts.tserver_instance().permanent_uuid();
      auto remote_tserver = VERIFY_RESULT(client().GetRemoteTabletServer(permanent_uuid));
      auto proxy = remote_tserver->proxy();
      GetLockStatusRequestPB node_req;
      // GetLockStatusRequestPB supports providing multiple transaction ids, but postgres sends
      // only one transaction id in PgGetLockStatusRequestPB for now.
      node_req.add_transaction_ids(req.transaction_id());
      GetLockStatusResponsePB node_resp;
      controller.Reset();
      RETURN_NOT_OK(proxy->GetLockStatus(node_req, &node_resp, &controller));

      auto* node_locks = resp->add_node_locks();
      node_locks->set_permanent_uuid(permanent_uuid);
      node_locks->mutable_tablet_lock_infos()->Swap(node_resp.mutable_tablet_lock_infos());
    }

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

  void Perform(PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext* context) {
    auto status = DoPerform(req, resp, context);
    if (!status.ok()) {
      Respond(status, resp, context);
    }
  }

  void InvalidateTableCache() {
    table_cache_.InvalidateAll(CoarseMonoClock::Now());
  }

  #define PG_CLIENT_SESSION_METHOD_FORWARD(r, data, method) \
  Status method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext* context) { \
    return VERIFY_RESULT(GetSession(req))->method(req, resp, context); \
  }

  BOOST_PP_SEQ_FOR_EACH(PG_CLIENT_SESSION_METHOD_FORWARD, ~, PG_CLIENT_SESSION_METHODS);

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

  Result<LockablePgClientSessionPtr> DoGetSession(uint64_t session_id) {
    SharedLock<rw_spinlock> lock(mutex_);
    DCHECK_NE(session_id, 0);
    auto it = sessions_.find(session_id);
    if (PREDICT_FALSE(it == sessions_.end())) {
      return STATUS(InvalidArgument,
          Format("Connection terminated unexpectedly due to unknown session $0", session_id),
          Slice(),
          PgsqlError(YBPgErrorCode::YB_PG_CONNECTION_DOES_NOT_EXIST));
    }
    (**it).Touch();
    return *it;
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
    std::lock_guard<rw_spinlock> lock(mutex_);
    while (!session_expiration_queue_.empty()) {
      auto& top = session_expiration_queue_.top();
      if (top.first > now) {
        break;
      }
      auto id = top.second;
      session_expiration_queue_.pop();
      auto it = sessions_.find(id);
      if (it != sessions_.end()) {
        auto current_expiration = (**it).expiration();
        if (current_expiration > now) {
          session_expiration_queue_.push({current_expiration, id});
        } else {
          sessions_.erase(it);
        }
      }
    }
    ScheduleCheckExpiredSessions(now);
  }

  Status DoPerform(PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext* context) {
    return VERIFY_RESULT(GetSession(*req))->Perform(req, resp, context);
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

  boost::multi_index_container<
      LockablePgClientSessionPtr,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::const_mem_fun<PgClientSession, uint64_t, &PgClientSession::id>
          >
      >
  > sessions_ GUARDED_BY(mutex_);

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

  const std::optional<XClusterContext> xcluster_context_;

  PgMutationCounter* pg_node_level_mutation_counter_;

  PgResponseCache response_cache_;

  PgSequenceCache sequence_cache_;

  const Uuid instance_id_;
};

PgClientServiceImpl::PgClientServiceImpl(
    std::reference_wrapper<const TabletServerIf> tablet_server,
    const std::shared_future<client::YBClient*>& client_future,
    const scoped_refptr<ClockBase>& clock,
    TransactionPoolProvider transaction_pool_provider,
    const scoped_refptr<MetricEntity>& entity,
    rpc::Scheduler* scheduler,
    const std::optional<XClusterContext>& xcluster_context,
    PgMutationCounter* pg_node_level_mutation_counter)
    : PgClientServiceIf(entity),
      impl_(new Impl(
          tablet_server, client_future, clock, std::move(transaction_pool_provider), scheduler,
          xcluster_context, pg_node_level_mutation_counter, entity.get())) {}

PgClientServiceImpl::~PgClientServiceImpl() = default;

void PgClientServiceImpl::Perform(
    const PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext context) {
  impl_->Perform(const_cast<PgPerformRequestPB*>(req), resp, &context);
}

void PgClientServiceImpl::InvalidateTableCache() {
  impl_->InvalidateTableCache();
}

void PgClientServiceImpl::CheckObjectIdAllocators(const std::unordered_set<uint32_t>& db_oids) {
  impl_->CheckObjectIdAllocators(db_oids);
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

BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DEFINE, ~, YB_PG_CLIENT_METHODS);

}  // namespace tserver
}  // namespace yb
