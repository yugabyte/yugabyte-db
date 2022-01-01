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

#include "yb/master/ysql_transaction_ddl.h"

#include "yb/client/transaction_rpc.h"

#include "yb/common/ql_expr.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/gutil/casts.h"

#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/status_log.h"

DEFINE_int32(ysql_transaction_bg_task_wait_ms, 200,
  "Amount of time the catalog manager background task thread waits "
  "between runs");

namespace yb {
namespace master {

YsqlTransactionDdl::~YsqlTransactionDdl() {
  // Shutdown any outstanding RPCs.
  rpcs_.Shutdown();
}

void YsqlTransactionDdl::VerifyTransaction(
    const TransactionMetadata& transaction_metadata,
    std::function<Status(bool)> complete_callback) {
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_ysql_transaction_bg_task_wait_ms));

  YB_LOG_EVERY_N_SECS(INFO, 1) << "Verifying Transaction " << transaction_metadata;

  tserver::GetTransactionStatusRequestPB req;
  req.set_tablet_id(transaction_metadata.status_tablet);
  req.add_transaction_id()->assign(
      pointer_cast<const char*>(transaction_metadata.transaction_id.data()),
      transaction_metadata.transaction_id.size());

  auto rpc_handle = rpcs_.Prepare();
  if (rpc_handle == rpcs_.InvalidHandle()) {
    LOG(WARNING) << "Shutting down. Cannot send GetTransactionStatus: " << transaction_metadata;
    return;
  }
  auto client = client_future_.get();
  if (!client) {
    LOG(WARNING) << "Shutting down. Cannot get GetTransactionStatus: " << transaction_metadata;
    return;
  }
  // We need to query the TransactionCoordinator here.  Can't use TransactionStatusResolver in
  // TransactionParticipant since this TransactionMetadata may not have any actual data flushed yet.
  *rpc_handle = client::GetTransactionStatus(
      TransactionRpcDeadline(),
      nullptr /* tablet */,
      client,
      &req,
      [this, rpc_handle, transaction_metadata, complete_callback]
          (Status status, const tserver::GetTransactionStatusResponsePB& resp) {
        auto retained = rpcs_.Unregister(rpc_handle);
        TransactionReceived(transaction_metadata, complete_callback, status, resp);
      });
  (**rpc_handle).SendRpc();
}

void YsqlTransactionDdl::TransactionReceived(
    const TransactionMetadata& transaction,
    std::function<Status(bool)> complete_callback,
    Status txn_status, const tserver::GetTransactionStatusResponsePB& resp) {
  YB_LOG_EVERY_N_SECS(INFO, 1) << "TransactionReceived: " << txn_status.ToString()
                               << " : " << resp.DebugString();

  if (!txn_status.ok()) {
    LOG(WARNING) << "Transaction Status attempt (" << transaction.ToString()
                 << ") failed with status " << txn_status;
    WARN_NOT_OK(thread_pool_->SubmitFunc([complete_callback] () {
      WARN_NOT_OK(complete_callback(false /* txn_rpc_success */), "Callback failure");
    }), "Failed to enqueue callback");
    // #5981: Improve failure handling to retry transient errors or recognize transaction complete.
  } else if (resp.has_error()) {
    const Status s = StatusFromPB(resp.error().status());
    const tserver::TabletServerErrorPB::Code code = resp.error().code();
    LOG(WARNING) << "Transaction Status attempt (" << transaction.ToString()
                 << ") failed with error code " << tserver::TabletServerErrorPB::Code_Name(code)
                 << ": " << s;
    WARN_NOT_OK(thread_pool_->SubmitFunc([complete_callback] () {
      WARN_NOT_OK(complete_callback(false /* txn_rpc_success */), "Callback failure");
    }), "Failed to enqueue callback");
    // #5981: Maybe have the same heuristic as above?
  } else {
    YB_LOG_EVERY_N_SECS(INFO, 1) << "Got Response for " << transaction.ToString()
                                 << ": " << resp.DebugString();
    bool is_pending = (resp.status_size() == 0);
    for (int i = 0; i < resp.status_size() && !is_pending; ++i) {
      // NOTE: COMMITTED state is also "pending" because we need APPLIED.
      is_pending = resp.status(i) == TransactionStatus::PENDING ||
                   resp.status(i) == TransactionStatus::COMMITTED;
    }
    if (is_pending) {
      // Re-enqueue if transaction is still pending.
      WARN_NOT_OK(thread_pool_->SubmitFunc(
          std::bind(&YsqlTransactionDdl::VerifyTransaction, this, transaction, complete_callback)),
          "Could not submit VerifyTransaction to thread pool");
    } else {
      // If this transaction isn't pending, then the transaction is in a terminal state.
      // Note: We ignore the resp.status() now, because it could be ABORT'd but actually a SUCCESS.
      WARN_NOT_OK(thread_pool_->SubmitFunc([complete_callback] () {
        WARN_NOT_OK(complete_callback(true /* txn_rpc_success */), "Callback failure");
      }), "Failed to enqueue callback");
    }
  }
}

Result<bool> YsqlTransactionDdl::PgEntryExists(TableId pg_table_id, Result<uint32_t> entry_oid) {
  auto tablet_peer = sys_catalog_->tablet_peer();
  if (!tablet_peer || !tablet_peer->tablet()) {
    return STATUS(ServiceUnavailable, "SysCatalog unavailable");
  }
  const tablet::Tablet* catalog_tablet = tablet_peer->tablet();
  const Schema& pg_database_schema =
      *VERIFY_RESULT(catalog_tablet->metadata()->GetTableInfo(pg_table_id))->schema;

  // Use Scan to query the 'pg_database' table, filtering by our 'oid'.
  Schema projection;
  RETURN_NOT_OK(pg_database_schema.CreateProjectionByNames({"oid"}, &projection,
                pg_database_schema.num_key_columns()));
  const auto oid_col_id = VERIFY_RESULT(projection.ColumnIdByName("oid")).rep();
  auto iter = VERIFY_RESULT(catalog_tablet->NewRowIterator(
      projection.CopyWithoutColumnIds(), {} /* read_hybrid_time */, pg_table_id));
  auto e_oid_val = VERIFY_RESULT(std::move(entry_oid));
  {
    auto doc_iter = down_cast<docdb::DocRowwiseIterator*>(iter.get());
    PgsqlConditionPB cond;
    cond.add_operands()->set_column_id(oid_col_id);
    cond.set_op(QL_OP_EQUAL);
    cond.add_operands()->mutable_value()->set_uint32_value(e_oid_val);
    const std::vector<docdb::PrimitiveValue> empty_key_components;
    docdb::DocPgsqlScanSpec spec(
        projection, rocksdb::kDefaultQueryId, empty_key_components, empty_key_components,
        &cond, boost::none /* hash_code */, boost::none /* max_hash_code */, nullptr /* where */);
    RETURN_NOT_OK(doc_iter->Init(spec));
  }

  // Expect exactly one row, which means the transaction was a success.
  QLTableRow row;
  if (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&row));
    return true;
  }
  return false;
}

}  // namespace master
}  // namespace yb
