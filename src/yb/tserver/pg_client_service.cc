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

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"

#include "yb/common/pg_system_attr.h"
#include "yb/common/pg_types.h"

#include "yb/docdb/doc_key.h"

#include "yb/master/master.proxy.h"

#include "yb/rpc/rpc_context.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/pg_create_table.h"

#include "yb/util/net/net_util.h"

using namespace std::literals;

DEFINE_uint64(pg_client_session_expiration_ms, 60000,
              "Pg client session expiration time in milliseconds.");

namespace yb {
namespace tserver {

class PgClientSession {
 public:
  PgClientSession(client::YBClient* client, uint64_t id)
      : client_(*client), id_(id) {
  }

  uint64_t id() const {
    return id_;
  }

  CHECKED_STATUS CreateTable(
      const PgCreateTableRequestPB& req, PgCreateTableResponsePB* resp, rpc::RpcContext* context) {
    PgCreateTable helper(req);
    RETURN_NOT_OK(helper.Prepare());
    return helper.Exec(
        &client(), VERIFY_RESULT(GetDdlTransactionMetadata(req.use_transaction())),
        context->GetClientDeadline());
  }

  CHECKED_STATUS CreateDatabase(
      const PgCreateDatabaseRequestPB& req, PgCreateDatabaseResponsePB* resp,
      rpc::RpcContext* context) {
    return client().CreateNamespace(
        req.database_name(),
        YQL_DATABASE_PGSQL,
        "" /* creator_role_name */,
        GetPgsqlNamespaceId(req.database_oid()),
        req.source_database_oid() != kPgInvalidOid
            ? GetPgsqlNamespaceId(req.source_database_oid()) : "",
        req.next_oid(),
        VERIFY_RESULT(GetDdlTransactionMetadata(req.use_transaction())),
        req.colocated(),
        context->GetClientDeadline());
  }

  CHECKED_STATUS AlterTable(
      const PgAlterTableRequestPB& req, PgAlterTableResponsePB* resp, rpc::RpcContext* context) {
    auto alterer = client().NewTableAlterer(PgObjectId::FromPB(req.table_id()).GetYBTableId());
    auto txn = VERIFY_RESULT(GetDdlTransactionMetadata(req.use_transaction()));
    if (txn) {
      alterer->part_of_transaction(txn);
    }
    for (const auto& add_column : req.add_columns()) {
      auto yb_type = QLType::Create(static_cast<DataType>(add_column.attr_ybtype()));
      alterer->AddColumn(add_column.attr_name())->Type(yb_type)->Order(add_column.attr_num());
      // Do not set 'nullable' attribute as PgCreateTable::AddColumn() does not do it.
    }
    for (const auto& rename_column : req.rename_columns()) {
      alterer->AlterColumn(rename_column.old_name())->RenameTo(rename_column.new_name());
    }
    for (const auto& drop_column : req.drop_columns()) {
      alterer->DropColumn(drop_column);
    }
    if (!req.rename_table().table_name().empty()) {
      client::YBTableName new_table_name(
          YQL_DATABASE_PGSQL, req.rename_table().database_name(), req.rename_table().table_name());
      alterer->RenameTo(new_table_name);
    }

    alterer->timeout(context->GetClientDeadline() - CoarseMonoClock::now());
    return alterer->Alter();
  }

 private:
  friend class PgClientSessionLocker;

  Result<const TransactionMetadata*> GetDdlTransactionMetadata(
      const TransactionMetadataPB& metadata) {
    if (!metadata.has_transaction_id()) {
      return nullptr;
    }
    last_txn_metadata_ = VERIFY_RESULT(TransactionMetadata::FromPB(metadata));
    return &last_txn_metadata_;
  }

  client::YBClient& client() {
    return client_;
  }

  client::YBClient& client_;
  const uint64_t id_;

  std::mutex mutex_;
  TransactionMetadata last_txn_metadata_; // TODO(PG_CLIENT) Remove after migration.
};

class PgClientSessionLocker {
 public:
  explicit PgClientSessionLocker(PgClientSession* session)
      : session_(session), lock_(session->mutex_) {}

  PgClientSession* operator->() const {
    return session_;
  }
 private:
  PgClientSession* session_;
  std::unique_lock<std::mutex> lock_;
};

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
    return ResultToStatus(DoGetSession(req.session_id(), req.create()));
  }

  CHECKED_STATUS OpenTable(
      const PgOpenTableRequestPB& req, PgOpenTableResponsePB* resp, rpc::RpcContext* context) {
    client::YBTablePtr table;
    RETURN_NOT_OK(client().OpenTable(req.table_id(), &table, resp->mutable_info()));
    RSTATUS_DCHECK_EQ(
        table->table_type(), client::YBTableType::PGSQL_TABLE_TYPE, RuntimeError,
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
    auto proxy = std::make_shared<master::MasterServiceProxy>(
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

  CHECKED_STATUS CreateTable(
      const PgCreateTableRequestPB& req, PgCreateTableResponsePB* resp, rpc::RpcContext* context) {
    return VERIFY_RESULT(GetSession(req))->CreateTable(req, resp, context);
  }

  CHECKED_STATUS CreateDatabase(
      const PgCreateDatabaseRequestPB& req, PgCreateDatabaseResponsePB* resp,
      rpc::RpcContext* context) {
    return VERIFY_RESULT(GetSession(req))->CreateDatabase(req, resp, context);
  }

  CHECKED_STATUS AlterTable(
      const PgAlterTableRequestPB& req, PgAlterTableResponsePB* resp, rpc::RpcContext* context) {
    return VERIFY_RESULT(GetSession(req))->AlterTable(req, resp, context);
  }

 private:
  client::YBClient& client() { return *client_future_.get(); }

  template <class Req>
  Result<PgClientSessionLocker> GetSession(const Req& req, bool create = false) {
    return GetSession(req.session_id(), create);
  }

  Result<PgClientSession&> DoGetSession(uint64_t session_id, bool create) {
    DCHECK_NE(session_id, 0);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      sessions_.modify(it, [](auto& session) {
        session.Touch();
      });
      return *it->value();
    }
    if (!create) {
      return STATUS_FORMAT(InvalidArgument, "Unknown session: $0", session_id);
    }
    it = sessions_.emplace(
        FLAGS_pg_client_session_expiration_ms * 1ms,
        std::make_unique<PgClientSession>(&client(), session_id)).first;
    return *it->value();
  }

  Result<PgClientSessionLocker> GetSession(uint64_t session_id, bool create = false) {
    return PgClientSessionLocker(&VERIFY_RESULT_REF(DoGetSession(session_id, create)));
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

  using SessionsEntry = Expirable<std::unique_ptr<PgClientSession>>;
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
