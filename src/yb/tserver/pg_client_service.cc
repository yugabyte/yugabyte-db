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

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/master/master.proxy.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/net/net_util.h"

namespace yb {
namespace tserver {

class PgClientServiceImpl::Impl {
 public:
  explicit Impl(const std::shared_future<client::YBClient*>& client_future,
                TransactionPoolProvider transaction_pool_provider)
      : client_future_(client_future),
        transaction_pool_provider_(std::move(transaction_pool_provider)) {
  }

  CHECKED_STATUS OpenTable(
      const PgOpenTableRequestPB& req, PgOpenTableResponsePB* resp, rpc::RpcContext* context) {
    client::YBTablePtr table;
    RETURN_NOT_OK(client().OpenTable(req.table_id(), &table, resp->mutable_info()));
    RSTATUS_DCHECK_EQ(table->table_type(), client::YBTableType::PGSQL_TABLE_TYPE, RuntimeError,
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
    auto proxy  = std::make_shared<master::MasterServiceProxy>(
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
        GetPgsqlNamespaceId(req.database_oid()), req.next_oid(), req.count(),
        &begin_oid, &end_oid));
    resp->set_begin_oid(begin_oid);
    resp->set_end_oid(end_oid);

    return Status::OK();
  }

 private:
  client::YBClient& client() {
    return *client_future_.get();
  }

  std::shared_future<client::YBClient*> client_future_;
  TransactionPoolProvider transaction_pool_provider_;
};

PgClientServiceImpl::PgClientServiceImpl(
    const std::shared_future<client::YBClient*>& client_future,
    TransactionPoolProvider transaction_pool_provider,
    const scoped_refptr<MetricEntity>& entity)
    : PgClientServiceIf(entity),
      impl_(new Impl(client_future, std::move(transaction_pool_provider))) {}

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
