//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pggate_if_cxx_decl.h"

#include "yb/client/yb_op.h"
#include "yb/client/transaction.h"
#include "yb/client/batcher.h"

#include "yb/util/string_util.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using client::YBSchema;
using client::YBColumnSchema;
using client::YBTable;
using client::YBTableName;
using client::YBTableType;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(
    std::shared_ptr<client::YBClient> client,
    const string& database_name,
    scoped_refptr<PgTxnManager> pg_txn_manager)
    : client_(client),
      session_(client_->NewSession()),
      pg_txn_manager_(std::move(pg_txn_manager)) {
  session_->SetTimeout(kSessionTimeout);
}

PgSession::~PgSession() {
}

//--------------------------------------------------------------------------------------------------

void PgSession::Reset() {
  errmsg_.clear();
  status_ = Status::OK();
}

Status PgSession::ConnectDatabase(const string& database_name) {
  Result<bool> namespace_exists = client_->NamespaceExists(database_name, YQL_DATABASE_PGSQL);
  if (namespace_exists.ok() && namespace_exists.get()) {
    connected_database_ = database_name;
    return Status::OK();
  }

  return STATUS_FORMAT(NotFound, "Database '$0' does not exist", database_name);
}

//--------------------------------------------------------------------------------------------------

Status PgSession::CreateDatabase(const string& database_name,
                                 const PgOid database_oid,
                                 const PgOid source_database_oid,
                                 const PgOid next_oid) {
  return client_->CreateNamespace(database_name,
                                  YQL_DATABASE_PGSQL,
                                  "" /* creator_role_name */,
                                  GetPgsqlNamespaceId(database_oid),
                                  source_database_oid != kPgInvalidOid
                                  ? GetPgsqlNamespaceId(source_database_oid) : "",
                                  next_oid);
}

Status PgSession::DropDatabase(const string& database_name, bool if_exist) {
  return client_->DeleteNamespace(database_name, YQL_DATABASE_PGSQL);
}

Status PgSession::ReserveOids(const PgOid database_oid,
                              const PgOid next_oid,
                              const uint32_t count,
                              PgOid *begin_oid,
                              PgOid *end_oid) {
  return client_->ReservePgsqlOids(GetPgsqlNamespaceId(database_oid), next_oid, count,
                                   begin_oid, end_oid);
}

//--------------------------------------------------------------------------------------------------

client::YBTableCreator *PgSession::NewTableCreator() {
  return client_->NewTableCreator();
}

Status PgSession::DropTable(const PgObjectId& table_id) {
  return client_->DeleteTable(table_id.GetYBTableId());
}

Status PgSession::TruncateTable(const PgObjectId& table_id) {
  return client_->TruncateTable(table_id.GetYBTableId());
}

//--------------------------------------------------------------------------------------------------

Result<PgTableDesc::ScopedRefPtr> PgSession::LoadTable(const PgObjectId& table_id) {
  VLOG(3) << "Loading table descriptor for " << table_id;
  const TableId yb_table_id = table_id.GetYBTableId();
  shared_ptr<YBTable> table;

  auto cached_yb_table = table_cache_.find(yb_table_id);
  if (cached_yb_table == table_cache_.end()) {
    Status s = client_->OpenTable(yb_table_id, &table);
    if (!s.ok()) {
      VLOG(3) << "LoadTable: Server returns an error: " << s;
      // TODO: NotFound might not always be the right status here.
      return STATUS_FORMAT(
          NotFound, "Error loading table with id $0: $1", yb_table_id, s.ToString());
    }
    table_cache_[yb_table_id] = table;
  } else {
    table = cached_yb_table->second;
  }

  DCHECK_EQ(table->table_type(), YBTableType::PGSQL_TABLE_TYPE);

  return make_scoped_refptr<PgTableDesc>(table);
}

Status PgSession::PgApplyAsync(const std::shared_ptr<client::YBPgsqlOp>& op) {
  VLOG(2) << __PRETTY_FUNCTION__ << " called, is_transactional="
          << op->IsTransactional();
  if (op->IsTransactional()) {
    has_txn_ops_ = true;
  } else {
    has_non_txn_ops_ = true;
  }
  return VERIFY_RESULT(GetSessionForOp(op))->Apply(op);
}

Status PgSession::PgFlushAsync(StatusFunctor callback) {
  VLOG(2) << __PRETTY_FUNCTION__ << " called";
  if (has_txn_ops_ && has_non_txn_ops_) {
    return STATUS(IllegalState,
        "Cannot flush transactional and non-transactional operations together");
  }
  bool transactional = has_txn_ops_;
  VLOG(2) << __PRETTY_FUNCTION__
          << ": has_txn_ops_=" << has_txn_ops_ << ", has_non_txn_ops_=" << has_non_txn_ops_;
  has_txn_ops_ = false;
  has_non_txn_ops_ = false;
  // We specify read_only_op true here because we never start a new write transaction at this point.
  client::YBSessionPtr session =
      VERIFY_RESULT(GetSession(transactional, /* read_only_op */ true))->shared_from_this();
  session->FlushAsync([this, session, callback] (const Status& status) {
    callback(CombineErrorsToStatus(session->GetPendingErrors(), status));
  });
  return Status::OK();
}

Result<client::YBSession*> PgSession::GetSessionForOp(
    const std::shared_ptr<client::YBPgsqlOp>& op) {
  return GetSession(op->IsTransactional(), op->read_only());
}

namespace {

string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<string> status_strings;
  for (const auto& error : errors) {
    status_strings.insert(error->status().ToString());
  }
  return RangeToString(status_strings.begin(), status_strings.end());
}

} // anonymous namespace

Status PgSession::CombineErrorsToStatus(client::CollectedErrors errors, Status status) {
  if (errors.empty())
    return status;

  if (status.IsIOError() &&
      // TODO: move away from string comparison here and use a more specific status than IOError.
      // See https://github.com/YugaByte/yugabyte-db/issues/702
      status.message() == client::internal::Batcher::kErrorReachingOutToTServersMsg &&
      errors.size() == 1) {
    return errors.front()->status();
  }

  return status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));
}

Result<YBSession*> PgSession::GetSession(bool transactional, bool read_only_op) {
  YBSession* const txn_session =
      transactional ? pg_txn_manager_->GetTransactionalSession() : nullptr;
  if (transactional && !txn_session) {
    return STATUS_FORMAT(
        IllegalState,
        "GetSession called for a transactional operation, read_only_op=$0, but the transactional "
            "session has not been created",
        read_only_op);
  }
  if (txn_session) {
    VLOG(2) << __PRETTY_FUNCTION__
            << ": read_only_op=" << read_only_op << ", returning transactional session";
    if (!read_only_op) {
      pg_txn_manager_->BeginWriteTransactionIfNecessary();
    }
    return txn_session;
  }
  VLOG(2) << __PRETTY_FUNCTION__
          << ": read_only_op=" << read_only_op << ", returning non-transactional session";
  return session_.get();
}

int PgSession::CountPendingErrors() const {
  return session_->CountPendingErrors();
}

std::vector<std::unique_ptr<client::YBError>> PgSession::GetPendingErrors() {
  return session_->GetPendingErrors();
}

}  // namespace pggate
}  // namespace yb
