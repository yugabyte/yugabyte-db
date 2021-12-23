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

#include "yb/tserver/pg_client_session.h"

#include "yb/client/client.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/table_alterer.h"

#include "yb/common/ql_type.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_create_table.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace tserver {

PgClientSession::PgClientSession(client::YBClient* client, uint64_t id)
    : client_(*client), id_(id) {
}

uint64_t PgClientSession::id() const {
  return id_;
}

Status PgClientSession::CreateTable(
    const PgCreateTableRequestPB& req, PgCreateTableResponsePB* resp, rpc::RpcContext* context) {
  PgCreateTable helper(req);
  RETURN_NOT_OK(helper.Prepare());
  return helper.Exec(
      &client(), VERIFY_RESULT(GetDdlTransactionMetadata(req.use_transaction())),
      context->GetClientDeadline());
}

Status PgClientSession::CreateDatabase(
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

Status PgClientSession::DropDatabase(
    const PgDropDatabaseRequestPB& req, PgDropDatabaseResponsePB* resp, rpc::RpcContext* context) {
  return client().DeleteNamespace(
      req.database_name(),
      YQL_DATABASE_PGSQL,
      GetPgsqlNamespaceId(req.database_oid()),
      context->GetClientDeadline());
}

Status PgClientSession::DropTable(
    const PgDropTableRequestPB& req, PgDropTableResponsePB* resp, rpc::RpcContext* context) {
  auto yb_table_id = PgObjectId::FromPB(req.table_id()).GetYBTableId();
  if (req.index()) {
    client::YBTableName indexed_table;
    RETURN_NOT_OK(client().DeleteIndexTable(
        yb_table_id, &indexed_table, true, context->GetClientDeadline()));
    indexed_table.SetIntoTableIdentifierPB(resp->mutable_indexed_table());
    return Status::OK();
  }

  return client().DeleteTable(yb_table_id, true, context->GetClientDeadline());
}

Status PgClientSession::AlterDatabase(
    const PgAlterDatabaseRequestPB& req, PgAlterDatabaseResponsePB* resp,
    rpc::RpcContext* context) {
  auto alterer = client().NewNamespaceAlterer(
      req.database_name(), GetPgsqlNamespaceId(req.database_oid()));
  alterer->SetDatabaseType(YQL_DATABASE_PGSQL);
  alterer->RenameTo(req.new_name());
  return alterer->Alter(context->GetClientDeadline());
}

Status PgClientSession::AlterTable(
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

Status PgClientSession::TruncateTable(
    const PgTruncateTableRequestPB& req, PgTruncateTableResponsePB* resp,
    rpc::RpcContext* context) {
  return client().TruncateTable(PgObjectId::FromPB(req.table_id()).GetYBTableId());
}

Status PgClientSession::BackfillIndex(
    const PgBackfillIndexRequestPB& req, PgBackfillIndexResponsePB* resp,
    rpc::RpcContext* context) {
  return client().BackfillIndex(PgObjectId::FromPB(req.table_id()).GetYBTableId());
}

Status PgClientSession::CreateTablegroup(
    const PgCreateTablegroupRequestPB& req, PgCreateTablegroupResponsePB* resp,
    rpc::RpcContext* context) {
  auto id = PgObjectId::FromPB(req.tablegroup_id());
  auto s = client().CreateTablegroup(
      req.database_name(), GetPgsqlNamespaceId(id.database_oid),
      GetPgsqlTablegroupId(id.database_oid, id.object_oid));
  if (s.ok()) {
    return Status::OK();
  }

  if (s.IsAlreadyPresent()) {
    return STATUS(InvalidArgument, "Duplicate tablegroup");
  }

  if (s.IsNotFound()) {
    return STATUS(InvalidArgument, "Database not found", req.database_name());
  }

  return STATUS_FORMAT(
      InvalidArgument, "Invalid table definition: $0",
      s.ToString(false /* include_file_and_line */, false /* include_code */));
}

Status PgClientSession::DropTablegroup(
    const PgDropTablegroupRequestPB& req, PgDropTablegroupResponsePB* resp,
    rpc::RpcContext* context) {
  auto id = PgObjectId::FromPB(req.tablegroup_id());
  auto status = client().DeleteTablegroup(
      GetPgsqlNamespaceId(id.database_oid),
      GetPgsqlTablegroupId(id.database_oid, id.object_oid));
  if (status.IsNotFound()) {
    return Status::OK();
  }
  return status;
}

Result<const TransactionMetadata*> PgClientSession::GetDdlTransactionMetadata(
    const TransactionMetadataPB& metadata) {
  if (!metadata.has_transaction_id()) {
    return nullptr;
  }
  last_txn_metadata_ = VERIFY_RESULT(TransactionMetadata::FromPB(metadata));
  return &last_txn_metadata_;
}

client::YBClient& PgClientSession::client() {
  return client_;
}

}  // namespace tserver
}  // namespace yb
