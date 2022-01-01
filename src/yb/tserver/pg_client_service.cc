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

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/tablet_server.h"

#include "yb/common/partition.h"
#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_admin.proxy.h"

#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/pg_client_session.h"

#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"

using namespace std::literals;

DEFINE_uint64(pg_client_session_expiration_ms, 60000,
              "Pg client session expiration time in milliseconds.");

namespace yb {
namespace tserver {

namespace {
//--------------------------------------------------------------------------------------------------
// Constants used for the sequences data table.
//--------------------------------------------------------------------------------------------------
static constexpr const char* const kPgSequencesNamespaceName = "system_postgres";
static constexpr const char* const kPgSequencesDataTableName = "sequences_data";

// Columns names and ids.
static constexpr const char* const kPgSequenceDbOidColName = "db_oid";

static constexpr const char* const kPgSequenceSeqOidColName = "seq_oid";

static constexpr const char* const kPgSequenceLastValueColName = "last_value";

static constexpr const char* const kPgSequenceIsCalledColName = "is_called";

} // namespace

template <class T>
class Expirable {
 public:
  template <class... Args>
  explicit Expirable(CoarseDuration lifetime, Args&&... args)
      : lifetime_(lifetime), value_(std::forward<Args>(args)...) {
    Touch();
  }

  CoarseTimePoint expiration() const {
    return expiration_;
  }

  void Touch() {
    expiration_ = CoarseMonoClock::now() + lifetime_;
  }

  const T& value() const {
    return value_;
  }

 private:
  CoarseTimePoint expiration_;
  CoarseDuration lifetime_;
  T value_;
};

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
      const std::shared_future<client::YBClient*>& client_future,
      TransactionPoolProvider transaction_pool_provider,
      rpc::Scheduler* scheduler)
      : client_future_(client_future),
        transaction_pool_provider_(std::move(transaction_pool_provider)),
        check_expired_sessions_(scheduler) {
    ScheduleCheckExpiredSessions(CoarseMonoClock::now());
  }

  ~Impl() {
    check_expired_sessions_.Shutdown();
  }

  CHECKED_STATUS Heartbeat(
      const PgHeartbeatRequestPB& req, PgHeartbeatResponsePB* resp, rpc::RpcContext* context) {
    if (req.session_id()) {
      return ResultToStatus(DoGetSession(req.session_id()));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto session_id = ++session_serial_no_;
    resp->set_session_id(session_id);
    sessions_.emplace(
        FLAGS_pg_client_session_expiration_ms * 1ms,
        std::make_shared<PgClientSession>(&client(), session_id));
    return Status::OK();
  }

  CHECKED_STATUS OpenTable(
      const PgOpenTableRequestPB& req, PgOpenTableResponsePB* resp, rpc::RpcContext* context) {
    client::YBTablePtr table;
    RETURN_NOT_OK(client().OpenTable(req.table_id(), &table, resp->mutable_info()));
    RSTATUS_DCHECK(
        table->table_type() == client::YBTableType::PGSQL_TABLE_TYPE, RuntimeError,
        "Wrong table type");

    auto partitions = table->GetVersionedPartitions();
    resp->mutable_partitions()->set_version(partitions->version);
    for (const auto& key : partitions->keys) {
      *resp->mutable_partitions()->mutable_keys()->Add() = key;
    }

    return Status::OK();
  }

  CHECKED_STATUS GetDatabaseInfo(
      const PgGetDatabaseInfoRequestPB& req, PgGetDatabaseInfoResponsePB* resp,
      rpc::RpcContext* context) {
    RETURN_NOT_OK(client().GetNamespaceInfo(
        GetPgsqlNamespaceId(req.oid()), "" /* namespace_name */, YQL_DATABASE_PGSQL,
        resp->mutable_info()));

    return Status::OK();
  }

  CHECKED_STATUS IsInitDbDone(
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

  CHECKED_STATUS ReserveOids(
      const PgReserveOidsRequestPB& req, PgReserveOidsResponsePB* resp, rpc::RpcContext* context) {
    uint32_t begin_oid, end_oid;
    RETURN_NOT_OK(client().ReservePgsqlOids(
        GetPgsqlNamespaceId(req.database_oid()), req.next_oid(), req.count(), &begin_oid,
        &end_oid));
    resp->set_begin_oid(begin_oid);
    resp->set_end_oid(end_oid);

    return Status::OK();
  }

  CHECKED_STATUS GetCatalogMasterVersion(
      const PgGetCatalogMasterVersionRequestPB& req,
      PgGetCatalogMasterVersionResponsePB* resp,
      rpc::RpcContext* context) {
    uint64_t version;
    RETURN_NOT_OK(client().GetYsqlCatalogMasterVersion(&version));
    resp->set_version(version);
    return Status::OK();
  }

  CHECKED_STATUS CreateSequencesDataTable(
      const PgCreateSequencesDataTableRequestPB& req,
      PgCreateSequencesDataTableResponsePB* resp,
      rpc::RpcContext* context) {
    const client::YBTableName table_name(YQL_DATABASE_PGSQL,
                                         kPgSequencesDataNamespaceId,
                                         kPgSequencesNamespaceName,
                                         kPgSequencesDataTableName);
    RETURN_NOT_OK(client().CreateNamespaceIfNotExists(kPgSequencesNamespaceName,
                                                      YQLDatabase::YQL_DATABASE_PGSQL,
                                                      "" /* creator_role_name */,
                                                      kPgSequencesDataNamespaceId));

    // Set up the schema.
    client::YBSchemaBuilder schemaBuilder;
    schemaBuilder.AddColumn(kPgSequenceDbOidColName)->HashPrimaryKey()->Type(yb::INT64)->NotNull();
    schemaBuilder.AddColumn(kPgSequenceSeqOidColName)->HashPrimaryKey()->Type(yb::INT64)->NotNull();
    schemaBuilder.AddColumn(kPgSequenceLastValueColName)->Type(yb::INT64)->NotNull();
    schemaBuilder.AddColumn(kPgSequenceIsCalledColName)->Type(yb::BOOL)->NotNull();
    client::YBSchema schema;
    CHECK_OK(schemaBuilder.Build(&schema));

    // Generate the table id.
    PgObjectId oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);

    // Try to create the table.
    auto table_creator(client().NewTableCreator());

    auto status = table_creator->table_name(table_name)
        .schema(&schema)
        .table_type(yb::client::YBTableType::PGSQL_TABLE_TYPE)
        .table_id(oid.GetYBTableId())
        .hash_schema(YBHashSchema::kPgsqlHash)
        .Create();
    // If we could create it, then all good!
    if (status.ok()) {
      LOG(INFO) << "Table '" << table_name.ToString() << "' created.";
      // If the table was already there, also not an error...
    } else if (status.IsAlreadyPresent()) {
      LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
    } else {
      // If any other error, report that!
      LOG(ERROR) << "Error creating table '" << table_name.ToString() << "': " << status;
      return status;
    }
    return Status::OK();
  }

  CHECKED_STATUS TabletServerCount(
      const PgTabletServerCountRequestPB& req, PgTabletServerCountResponsePB* resp,
      rpc::RpcContext* context) {
    int result = 0;
    RETURN_NOT_OK(client().TabletServerCount(&result, req.primary_only(), /* use_cache= */ true));
    resp->set_count(result);
    return Status::OK();
  }

  CHECKED_STATUS ListLiveTabletServers(
      const PgListLiveTabletServersRequestPB& req, PgListLiveTabletServersResponsePB* resp,
      rpc::RpcContext* context) {
    auto tablet_servers = VERIFY_RESULT(client().ListLiveTabletServers(req.primary_only()));
    for (const auto& server : tablet_servers) {
      server.ToPB(resp->mutable_servers()->Add());
    }
    return Status::OK();
  }

  CHECKED_STATUS ValidatePlacement(
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
    }
    live_replicas->set_num_replicas(req.num_replicas());

    return client().ValidateReplicationInfo(replication_info);
  }

  #define PG_CLIENT_SESSION_METHOD_FORWARD(r, data, method) \
  CHECKED_STATUS method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext* context) { \
    return VERIFY_RESULT(GetSession(req))->method(req, resp, context); \
  }

  BOOST_PP_SEQ_FOR_EACH(PG_CLIENT_SESSION_METHOD_FORWARD, ~, PG_CLIENT_SESSION_METHODS);

 private:
  client::YBClient& client() { return *client_future_.get(); }

  template <class Req>
  Result<PgClientSessionLocker> GetSession(const Req& req) {
    return GetSession(req.session_id());
  }

  Result<PgClientSession&> DoGetSession(uint64_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    DCHECK_NE(session_id, 0);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      return STATUS_FORMAT(InvalidArgument, "Unknown session: $0", session_id);
    }
    sessions_.modify(it, [](auto& session) {
      session.Touch();
    });
    return *it->value();
  }

  Result<PgClientSessionLocker> GetSession(uint64_t session_id) {
    return PgClientSessionLocker(&VERIFY_RESULT_REF(DoGetSession(session_id)));
  }

  void ScheduleCheckExpiredSessions(CoarseTimePoint now) REQUIRES(mutex_) {
    auto time = sessions_.empty()
        ? CoarseTimePoint(now + FLAGS_pg_client_session_expiration_ms * 1ms)
        : sessions_.get<ExpirationTag>().begin()->expiration();
    check_expired_sessions_.Schedule([this](const Status& status) {
      if (!status.ok()) {
        return;
      }
      this->CheckExpiredSessions();
    }, time - now);
  }

  void CheckExpiredSessions() {
    auto now = CoarseMonoClock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto& index = sessions_.get<ExpirationTag>();
    while (!sessions_.empty() && index.begin()->expiration() < now) {
      index.erase(index.begin());
    }
    ScheduleCheckExpiredSessions(now);
  }

  std::shared_future<client::YBClient*> client_future_;
  TransactionPoolProvider transaction_pool_provider_;
  std::mutex mutex_;

  class ExpirationTag;

  using SessionsEntry = Expirable<std::shared_ptr<PgClientSession>>;
  boost::multi_index_container<
      SessionsEntry,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              ApplyToValue<
                  boost::multi_index::const_mem_fun<PgClientSession, uint64_t, &PgClientSession::id>
              >
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<ExpirationTag>,
              boost::multi_index::const_mem_fun<
                  SessionsEntry, CoarseTimePoint, &SessionsEntry::expiration
              >
          >
      >
  > sessions_ GUARDED_BY(mutex_);
  int64_t session_serial_no_ GUARDED_BY(mutex_) = 0;

  rpc::ScheduledTaskTracker check_expired_sessions_;
};

PgClientServiceImpl::PgClientServiceImpl(
    const std::shared_future<client::YBClient*>& client_future,
    TransactionPoolProvider transaction_pool_provider,
    const scoped_refptr<MetricEntity>& entity,
    rpc::Scheduler* scheduler)
    : PgClientServiceIf(entity),
      impl_(new Impl(client_future, std::move(transaction_pool_provider), scheduler)) {}

PgClientServiceImpl::~PgClientServiceImpl() {}

template <class Resp>
void Respond(const Status& status, Resp* resp, rpc::RpcContext* context) {
  if (!status.ok()) {
    StatusToPB(status, resp->mutable_status());
  }
  context->RespondSuccess();
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
