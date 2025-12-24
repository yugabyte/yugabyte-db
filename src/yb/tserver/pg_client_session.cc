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

#include "yb/tserver/pg_client_session.h"

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/common_util.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/transaction_error.h"
#include "yb/common/transaction_priority.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/sidecars.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/pg_response_cache.h"
#include "yb/tserver/pg_sequence_cache.h"
#include "yb/tserver/pg_shared_mem_pool.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/service_util.h"
#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/tserver_shared_mem.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/cast.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/lw_function.h"
#include "yb/util/pb_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/std_util.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"
#include "yb/util/write_buffer.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

using namespace std::chrono_literals;

DEFINE_RUNTIME_bool(report_ysql_ddl_txn_status_to_master, true,
                    "If set, at the end of DDL operation, the TServer will notify the YB-Master "
                    "whether the DDL operation was committed or aborted");

DEFINE_NON_RUNTIME_bool(ysql_enable_table_mutation_counter, false,
                    "Enable counting of mutations on a per-table basis. These mutations are used "
                    "to automatically trigger ANALYZE as soon as the mutations of a table cross a "
                    "certain threshold (decided based on ysql_auto_analyze_tuples_threshold and "
                    "ysql_auto_analyze_scale_factor).");
TAG_FLAG(ysql_enable_table_mutation_counter, experimental);

DEFINE_RUNTIME_bool(ysql_ddl_transaction_wait_for_ddl_verification, true,
                    "If set, DDL transactions will wait for DDL verification to complete before "
                    "returning to the client. ");

DEFINE_RUNTIME_bool(use_tablespace_based_transaction_placement, false,
                    "Use tablespace-local locality will be used instead of region-local locality.");

DEFINE_RUNTIME_uint64(big_shared_memory_segment_session_expiration_time_ms, 5000,
    "Time to release unused allocated big memory segment from session to pool.");

DEFINE_RUNTIME_bool(xcluster_target_manual_override, false,
    "When set, arbitrary DDLs, DMLs, and sequence manipulation functions are allowed on an "
    "automatic-mode target database.  This is likely to cause data loss, so consult with "
    "YugabyteDB support before using.");
TAG_FLAG(xcluster_target_manual_override, hidden);

DEFINE_test_flag(
    bool, request_unknown_tables_during_perform, false,
    "Add several unknown tables while processing perfrom request. "
    "It is expected that opening of such tables will fail");

DEFINE_test_flag(bool, perform_ignore_pg_is_region_local, false,
    "Ignore the is_all_region_local field of PgPerformOptionsPB. The intended state is for "
    "everything to work when this field is not set, as the field is only left in for upgrade from "
    "older versions and to be removed in the future.");

DEFINE_test_flag(bool, force_initial_region_local, false,
    "Force transaction to start as region-local initially.");

DEFINE_test_flag(bool, fail_create_table_rpc, false,
    "Fail all create table requests received at PgClientSession layer.");

DECLARE_bool(vector_index_dump_stats);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_bool(ysql_enable_db_catalog_version_mode);
DECLARE_bool(ysql_serializable_isolation_for_ddl_txn);
DECLARE_bool(ysql_yb_enable_ddl_atomicity_infra);
DECLARE_bool(ysql_yb_allow_replication_slot_lsn_types);
DECLARE_bool(ysql_yb_allow_replication_slot_ordering_modes);
DECLARE_bool(ysql_yb_enable_advisory_locks);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_object_locking_infra);
DECLARE_bool(ysql_yb_enable_ddl_savepoint_support);

DECLARE_string(ysql_sequence_cache_method);

DECLARE_uint64(rpc_max_message_size);

DECLARE_int32(tserver_yb_client_default_timeout_ms);
DECLARE_int32(txn_print_trace_every_n);
DECLARE_int32(txn_slow_op_threshold_ms);

METRIC_DEFINE_event_stats(server, pg_client_exchange_response_size,
    "The size of PgClient exchange response in bytes", yb::MetricUnit::kBytes,
    "The size of PgClient exchange response in bytes");
METRIC_DEFINE_event_stats(server, vector_index_fetch_us,
    "Time to fetch vector index results from tablets", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that query spent fetching list of vectors from tablets.");
METRIC_DEFINE_event_stats(server, vector_index_collect_us,
    "Time to collect vector index tablets results", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that query spent parsing vectors index tablets results.");
METRIC_DEFINE_event_stats(server, vector_index_reduce_us,
    "Time to reduce vector index results", yb::MetricUnit::kMicroseconds,
    "Time (microseconds) that query spent reducing list of vectors from tablets.");

namespace yb::tserver {
namespace {

YB_DEFINE_ENUM(PgClientSessionKind, (kPlain)(kDdl)(kCatalog)(kSequence)(kPgSession));

constexpr const size_t kPgSequenceLastValueColIdx = 2;
constexpr const size_t kPgSequenceIsCalledColIdx = 3;
const std::string kTxnLogPrefixTagSource("Session ");
client::LogPrefixName kTxnLogPrefixTag = client::LogPrefixName::Build<&kTxnLogPrefixTagSource>();

struct TabletReadTime {
  TabletId tablet_id;
  ReadHybridTime value;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(tablet_id, value);
  }
};

using UsedReadTimeApplier = std::function<void(TabletReadTime&&)>;

struct UsedReadTime {
  simple_spinlock lock;
  std::optional<TabletReadTime> data GUARDED_BY(lock);
  size_t signature GUARDED_BY(lock) = 0;
};

struct PendingUsedReadTime {
  UsedReadTime value;
  bool pending_update = false;
};

struct SessionData {
  client::YBSessionPtr session;
  client::YBTransactionPtr transaction;
};

struct SetupSessionResult {
  SessionData session_data;
  bool is_plain = false;
};

class PrefixLogger {
 public:
  explicit PrefixLogger(uint64_t id, pid_t pid = 0) : id_(id), pid_(pid) {}

  friend std::ostream& operator<<(std::ostream&, const PrefixLogger&);

 private:
  const uint64_t id_;
  const pid_t pid_;
};

std::ostream& operator<<(std::ostream& str, const PrefixLogger& logger) {
  if (logger.pid_ != 0) {
    return str << "Session id " << logger.id_ << " (pid " << logger.pid_ << "): ";
  }
  return str << "Session id " << logger.id_ << ": ";
}

std::string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<std::string> status_strings;
  for (const auto& error : errors) {
    status_strings.insert(error->status().ToString());
  }
  return RangeToString(status_strings.begin(), status_strings.end());
}

bool IsHomogeneousErrors(const client::CollectedErrors& errors) {
  if (errors.size() < 2) {
    return true;
  }
  auto i = errors.begin();
  const auto& status = (**i).status();
  const auto codes = status.ErrorCodesSlice();
  for (++i; i != errors.end(); ++i) {
    const auto& s = (**i).status();
    if (s.code() != status.code() || codes != s.ErrorCodesSlice()) {
      return false;
    }
  }
  return true;
}

std::optional<YBPgErrorCode> PsqlErrorCode(const Status& status) {
  const auto* err_data = status.ErrorData(PgsqlErrorTag::kCategory);
  return err_data ? std::optional(PgsqlErrorTag::Decode(err_data)) : std::nullopt;
}

// Get a common Postgres error code from the status and all errors, and append it to a previous
// Status.
// If any of those have different conflicting error codes, previous result is returned as-is.
Status AppendPsqlErrorCode(
    const Status& status, const client::CollectedErrors& errors) {
  std::optional<YBPgErrorCode> common_psql_error;
  for(const auto& error : errors) {
    const auto psql_error = PsqlErrorCode(error->status());
    if (!common_psql_error) {
      common_psql_error = psql_error;
    } else if (psql_error && common_psql_error != psql_error) {
      common_psql_error.reset();
      break;
    }
  }
  return common_psql_error ? status.CloneAndAddErrorCode(PgsqlError(*common_psql_error)) : status;
}

TransactionErrorCode GetTransactionErrorCode(const Status& status) {
  return status.ok() ? TransactionErrorCode::kNone : TransactionError(status).value();
}

struct PgClientSessionOperation {
  std::shared_ptr<client::YBPgsqlOp> op;
  std::unique_ptr<PgsqlReadRequestPB> vector_index_read_request;
  // TODO(vector_index): Handle table-splitting when it will be supported.
  size_t partition_idx = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(partition_idx, vector_index_read_request, op);
  }
};

using PgClientSessionOperations = std::vector<PgClientSessionOperation>;

Status TryAppendTxnConflictOpIndex(
    const Status& status, const client::CollectedErrors& errors,
    const PgClientSessionOperations& ops) {
  DCHECK(!status.ok());
  if (GetTransactionErrorCode(status) != TransactionErrorCode::kNone && !ops.empty()) {
    for (const auto& error : errors) {
      if (GetTransactionErrorCode(error->status()) == TransactionErrorCode::kConflict) {
        const auto ops_begin = ops.begin();
        const auto ops_end = ops.end();
        const auto op_it = std::find_if(
            ops.begin(), ops_end,
            [failed_op = &error->failed_op()](const auto& op) {return op.op.get() == failed_op; });

        if (PREDICT_FALSE(op_it == ops_end)) {
          LOG(DFATAL) << "Unknown operation failed with conflict";
          break;
        }

        return status.CloneAndAddErrorCode(OpIndex(op_it - ops_begin));
      }
    }
  }
  return status;
}

// Get a common transaction error code for all the errors and append it to the previous Status.
Status AppendTxnErrorCode(const Status& status, const client::CollectedErrors& errors) {
  // The list of all known TransactionErrorCode (except kNone), ordered in decreasing of priority.
  static constexpr std::array precedence_list = {
      TransactionErrorCode::kDeadlock,
      TransactionErrorCode::kAborted,
      TransactionErrorCode::kConflict,
      TransactionErrorCode::kReadRestartRequired,
      TransactionErrorCode::kSnapshotTooOld,
      TransactionErrorCode::kSkipLocking,
      TransactionErrorCode::kLockNotFound};
  static_assert(precedence_list.size() + 1 == MapSize(static_cast<TransactionErrorCode*>(nullptr)));

  static const auto precedence_begin = precedence_list.begin();
  static const auto precedence_end = precedence_list.end();
  auto common_txn_error_it = precedence_end;
  for (const auto& error : errors) {
    const auto txn_error = GetTransactionErrorCode(error->status());
    if (txn_error == TransactionErrorCode::kNone ||
        (common_txn_error_it != precedence_end && *common_txn_error_it == txn_error)) {
      continue;
    }

    const auto txn_error_it = std::find(precedence_begin, precedence_end, txn_error);
    if (PREDICT_FALSE(txn_error_it == precedence_end)) {
      LOG(DFATAL) << "Unknown transaction error code: " << txn_error;
      return status;
    }

    if (txn_error_it < common_txn_error_it) {
      common_txn_error_it = txn_error_it;
      VLOG(4) << "updating common_txn_error_idx to: " << *common_txn_error_it;
    }
  }

  return common_txn_error_it == precedence_end
      ? status : status.CloneAndAddErrorCode(TransactionError(*common_txn_error_it));
}

Status CombineErrorsToStatusImpl(const client::CollectedErrors& errors, const Status& status) {
  DCHECK(!errors.empty());

  if (status.IsIOError() &&
      // TODO: move away from string comparison here and use a more specific status than IOError.
      // See https://github.com/YugaByte/yugabyte-db/issues/702
      status.message() == client::internal::Batcher::kErrorReachingOutToTServersMsg &&
      IsHomogeneousErrors(errors)) {
    const auto& result = errors.front()->status();
    if (errors.size() == 1) {
      return result;
    }
    return Status(result.code(),
                  __FILE__,
                  __LINE__,
                  GetStatusStringSet(errors),
                  result.ErrorCodesSlice(),
                  /* file_name_len= */ size_t(0));
  }

  const auto result = status.ok()
      ? STATUS(InternalError, GetStatusStringSet(errors))
      : status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));

  return AppendTxnErrorCode(AppendPsqlErrorCode(result, errors), errors);
}

// Given a set of errors from operations, this function attempts to combine them into one status
// that is later passed to PostgreSQL and further converted into a more specific error code.
Status CombineErrorsToStatus(
    const client::CollectedErrors& errors, const Status& status,
    const PgClientSessionOperations& ops = {}) {
  return errors.empty()
      ? status
      : TryAppendTxnConflictOpIndex(CombineErrorsToStatusImpl(errors, status), errors, ops);
}

Status ProcessUsedReadTime(uint64_t session_id,
                           const client::YBPgsqlOp& op,
                           PgPerformResponsePB* resp,
                           TabletReadTime* used_read_time) {
  if (op.type() != client::YBOperation::PGSQL_READ) {
    return Status::OK();
  }
  const auto& read_op = down_cast<const client::YBPgsqlReadOp&>(op);
  const auto& op_used_read_time = read_op.used_read_time();
  if (!op_used_read_time) {
    return Status::OK();
  }

  if (op.table()->schema().table_properties().is_ysql_catalog_table()) {
    // Non empty used_read_time field in catalog read operation means this is the very first
    // catalog read operation after catalog read time resetting. read_time for the operation
    // has been chosen by master. All further reads from catalog must use same read point.
    auto catalog_read_time = op_used_read_time;

    // We set global limit to read time to avoid read restart errors because they are
    // disruptive to system catalog reads and it is not always possible to handle them there.
    // This might lead to reading slightly outdated state of the system catalog if a recently
    // committed DDL transaction used a transaction status tablet whose leader's clock is skewed
    // and is in the future compared to the master leader's clock.
    // TODO(dmitry) This situation will be handled in context of #7964.
    catalog_read_time.global_limit = catalog_read_time.read;
    catalog_read_time.ToPB(resp->mutable_catalog_read_time());
    VLOG(2) << "Got catalog_read_time: " << catalog_read_time.ToString();
  }

  if (used_read_time) {
    RSTATUS_DCHECK(
        !used_read_time->value, IllegalState,
        "Multiple used_read_time are not expected: $0, $1",
        used_read_time->value, op_used_read_time);
    *used_read_time = {.tablet_id = read_op.used_tablet(), .value = op_used_read_time};
  }
  return Status::OK();
}

Status HandleOperationResponse(uint64_t session_id,
                               const client::YBPgsqlOp& op,
                               PgPerformResponsePB* resp,
                               TabletReadTime* used_read_time) {
  const auto& response = op.response();
  if (response.status() == PgsqlResponsePB::PGSQL_STATUS_OK) {
    return ProcessUsedReadTime(session_id, op, resp, used_read_time);
  }

  if (response.error_status().size() > 0) {
    // TODO(14814, 18387):  We do not currently expect more than one status, when we do, we need
    // to decide how to handle them. Possible options: aggregate multiple statuses into one, discard
    // all but one, etc. Historically, for the one set of status fields (like error_message), new
    // error message was overwriting the previous one, that's why let's return the last entry from
    // error_status to mimic that past behavior, refer AsyncRpc::Finished for details.
    return StatusFromPB(*response.error_status().rbegin());
  }

  // Older nodes may still use deprecated fields for status, so keep legacy handling
  auto status = STATUS(
      QLError, response.error_message(), Slice(), PgsqlRequestStatus(response.status()));

  if (response.has_pg_error_code()) {
    status = status.CloneAndAddErrorCode(
        PgsqlError(static_cast<YBPgErrorCode>(response.pg_error_code())));
  }

  if (response.has_txn_error_code()) {
    status = status.CloneAndAddErrorCode(
        TransactionError(static_cast<TransactionErrorCode>(response.txn_error_code())));
  }

  return status;
}

template <class TableProvider>
Status GetTable(const TableId& table_id, TableProvider& provider, client::YBTablePtr* table) {
  if (*table && (**table).id() == table_id) {
    return Status::OK();
  }
  *table = VERIFY_RESULT(provider.Get(table_id));
  return Status::OK();
}

struct FetchedVector {
  uint64_t distance;
  RefCntSlice data;
  size_t partition_idx;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(partition_idx, distance, data);
  }
};

struct VectorIndexQueryPartitionData {
  size_t number_of_vectors_returned_to_postgres = 0;
  size_t number_of_vectors_fetched_from_tablet = 0;
  bool whether_all_vectors_was_fetched = false;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        number_of_vectors_returned_to_postgres, number_of_vectors_fetched_from_tablet,
        whether_all_vectors_was_fetched);
  }
};

class VectorIndexQuery {
 public:
  explicit VectorIndexQuery(std::reference_wrapper<const PgClientSessionMetrics> metrics)
      : metrics_(metrics) {}

  bool active() const {
    return active_;
  }

  bool IsContinuation(const PgsqlPagingStatePB& paging_state) const {
    return paging_state.main_key() == id_.ToString();
  }

  Status Prepare(
      const PgsqlReadRequestPB& read_req, const client::YBTablePtr& table,
      PgClientSessionOperations& ops) {
    DCHECK(!active_);
    active_ = true;

    auto partitions = table->GetVersionedPartitions();
    RETURN_NOT_OK(UpdatePartitions(table->id(), *partitions));

    table_ = table;
    auto prefetch_size = read_req.index_request().vector_idx_options().prefetch_size();
    prefetch_size_ = prefetch_size < 0 ? std::numeric_limits<size_t>::max() : prefetch_size;

    sidecars_ = std::make_unique<rpc::Sidecars>();
    size_t partition_idx = 0;
    for (const auto& key : partitions->keys) {
      const auto& partition_state = partitions_[partition_idx];
      VLOG_WITH_FUNC(4) << partition_idx << ": " << partition_state.ToString();
      if (!partition_state.whether_all_vectors_was_fetched &&
          partition_state.number_of_vectors_returned_to_postgres + prefetch_size_
              > partition_state.number_of_vectors_fetched_from_tablet) {
        auto new_read_req = std::make_unique<PgsqlReadRequestPB>(read_req);
        new_read_req->set_partition_key(key);
        new_read_req->mutable_index_request()->mutable_vector_idx_options()
            ->set_num_top_vectors_to_remove(partition_state.number_of_vectors_fetched_from_tablet);
        auto read_op = std::make_shared<client::YBPgsqlReadOp>(
            table, *sidecars_, new_read_req.get());
        ops.push_back(PgClientSessionOperation {
          .op = std::move(read_op),
          .vector_index_read_request = std::move(new_read_req),
          .partition_idx = partition_idx,
        });
      }
      ++partition_idx;
    }
    return_paging_state_ = read_req.return_paging_state();
    fetch_start_ = MonoTime::Now();

    return Status::OK();
  }

  Status ProcessResponse(
      const PgClientSessionOperations& ops, TabletReadTime* used_read_time,
      PgPerformResponsePB& resp, rpc::Sidecars& sidecars) {
    VLOG_WITH_FUNC(4) << "Resp: " << resp.ShortDebugString();
    active_ = false;
    auto process_start_time = MonoTime::Now();

    MonoTime reduce_start_time;
    bool partitions_are_stale = false;
    const auto table_partition_list_version = table_->GetPartitionListVersion();
    if (partitions_version_ != table_partition_list_version) {
      LOG_WITH_FUNC(INFO) << Format(
          "Partition list version changed ($0 => $1), request results should be ignored",
          partitions_version_, table_partition_list_version);
      // If a paging state has not been requested, the PG layer will not retry the request,
      // so we must notify the user that the request needs to be retried explicitly.
      if (!return_paging_state_) {
        return STATUS(TryAgain, "Partition list was changed. Please retry the request");
      }

      // TODO(vector_index): it seems possible to refresh the batcher logic to retry vector index
      // query internally, as the PG layer does not track partitions list for a vector index.
      partitions_are_stale = true;
    }

    // If partitions became stale, it is required to return an empty response to let the PG
    // to retrigger the request transparently and to force partitions refresh by Prepare().
    if (!partitions_are_stale && !ops.empty()) {
      ProcessOperationsResponse(ops, used_read_time);
      reduce_start_time = MonoTime::Now();

      // TODO(vector_index): Actually "old" vectors already sorted.
      // So we could sort newly added vectors, then just merge with this first part.
      // Or even more advances, there are several sorted chunks in vectors.
      // Could merge them instead of full sort.
      std::ranges::sort(vectors_, [](const auto& lhs, const auto& rhs) {
        return lhs.distance < rhs.distance;
      });
    } else {
      reduce_start_time = process_start_time;
    }

    auto& responses = *resp.mutable_responses();
    auto& out_resp = *responses.Add();
    auto& write_buffer = sidecars.Start();

    size_t response_size = std::min(prefetch_size_, vectors_.size());

    pggate::PgWire::WriteInt64(response_size, &write_buffer);
    for (size_t i = 0; i != response_size; ++i) {
      const auto& vector = vectors_[i];
      ++partitions_[vector.partition_idx].number_of_vectors_returned_to_postgres;
      out_resp.mutable_vector_index_distances()->Add(vector.distance);
      write_buffer.Append(vector.data.AsSlice());
    }
    vectors_.erase(vectors_.begin(), vectors_.begin() + response_size);

    // TODO(vector_index): Check actual response status.
    out_resp.set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
    out_resp.set_rows_data_sidecar(narrow_cast<int32_t>(sidecars.Complete()));
    out_resp.set_partition_list_version(table_partition_list_version);
    if (return_paging_state_ && (CouldFetchMore() || partitions_are_stale)) {
      auto& paging_state = *out_resp.mutable_paging_state();
      paging_state.set_table_id(table_->id());
      paging_state.set_main_key(id_.ToString());
      if (used_read_time) {
        used_read_time->value.ToPB(paging_state.mutable_read_time());
      }
      VLOG_WITH_FUNC(3) << "Paging state: " << AsString(paging_state);
    }

    auto reduce_time = MonoTime::Now() - reduce_start_time;
    auto fetch_time = process_start_time - fetch_start_;
    auto collect_time = reduce_start_time - process_start_time;
    metrics_.vector_index_fetch_us->Increment(fetch_time.ToMicroseconds());
    metrics_.vector_index_collect_us->Increment(collect_time.ToMicroseconds());
    metrics_.vector_index_reduce_us->Increment(reduce_time.ToMicroseconds());
    LOG_IF(INFO, FLAGS_vector_index_dump_stats)
        << "VI_STATS: Fetch time: " << fetch_time.ToPrettyString()
        << ", collect time: " << collect_time.ToPrettyString()
        << ", reduce time: " << reduce_time.ToPrettyString();

    return Status::OK();
  }

  std::string ToString() const {
    return YB_CLASS_TO_STRING(active, id);
  }

 private:
  size_t CalculateNumVectorSentToPg() const {
    DCHECK(!partitions_.empty());
    return std::accumulate(
        partitions_.begin(), partitions_.end(), /* initial value = */ 0,
        [](size_t sum, const auto& partition) {
          return sum + partition.number_of_vectors_returned_to_postgres;
        });
  }

  void ResetPartitions(const client::VersionedTablePartitionList& table_partitions) {
    partitions_.clear();
    partitions_.resize(table_partitions.keys.size());
    partitions_version_ = table_partitions.version;
    VLOG_WITH_FUNC(2) << AsString(table_partitions);
  }

  Status UpdatePartitions(
      const TableId& table_id, const client::VersionedTablePartitionList& table_partitions) {
    if (partitions_.empty()) {
      ResetPartitions(table_partitions);
      return Status::OK();
    }

    // Sanity check to make sure table is the same.
    DCHECK_EQ(table_->id(), table_id);
    DCHECK_LE(partitions_version_, table_partitions.version);

    // No update is required if partition version hasn't been changed. Only sanity checks.
    if (partitions_version_ == table_partitions.version) {
      DCHECK_EQ(partitions_.size(), table_partitions.keys.size());
      return Status::OK();
    }

    // Partitions has been updated due to a split operation. Let's try to refresh partitions
    // if nothing got sent to PG layer. Otherwise, an error should be returned to notify
    // the user that the request retry is required.
    const auto num_vectors_sent_to_pg = CalculateNumVectorSentToPg();
    LOG_WITH_FUNC(INFO) << Format(
        "Partitions version changed ($0 => $1), num vectors sent to PG: $2",
        partitions_version_, table_partitions.version, num_vectors_sent_to_pg);

    // Currently, there's no way to correctly update the paging state if a partition has been split,
    // so an error is returned to the PG layer to notify the user to retry the entire request.
    // However, if no vectors have been returned to PG yet, we can try to tolerate the split and
    // just refresh the partitions.
    if (num_vectors_sent_to_pg > 0) {
      return STATUS(TryAgain, "Partition list changed. Please retry the request");
    }

    // We don't expect any vector to be stored.
    LOG_IF_WITH_FUNC(DFATAL, !vectors_.empty()) <<
        "No vector are expected, current number of vectors: " << vectors_.size();
    vectors_.clear();

    ResetPartitions(table_partitions);
    return Status::OK();
  }

  bool CouldFetchMore() const {
    if (!vectors_.empty()) {
      return true;
    }
    auto pred = [](const auto& partition) {
      return !partition.whether_all_vectors_was_fetched;
    };
    return std::ranges::any_of(partitions_, pred);
  }

  void ProcessOperationsResponse(
      const PgClientSessionOperations& ops, TabletReadTime* used_read_time) {
    for (const auto& op : ops) {
      auto op_sidecars = sidecars_->Extract(*op.op->sidecar_index());
      auto& op_resp = op.op->response();
      auto& distances = op_resp.vector_index_distances();
      auto& ends = op_resp.vector_index_ends();
      size_t sidecar_offset = 8;
      for (int index = 0; index != distances.size(); ++index) {
        auto new_offset = ends[index];
        vectors_.push_back(FetchedVector {
          .distance = distances[index],
          .data = op_sidecars.SubSlice(sidecar_offset, new_offset),
          .partition_idx = op.partition_idx,
        });
        sidecar_offset = new_offset;
      }
      partitions_[op.partition_idx].number_of_vectors_fetched_from_tablet += distances.size();
      if (!op_resp.vector_index_could_have_more_data()) {
        partitions_[op.partition_idx].whether_all_vectors_was_fetched = true;
      }
      VLOG_WITH_FUNC(4) << op.partition_idx << ": " << partitions_[op.partition_idx].ToString();
    }
  }

  const PgClientSessionMetrics& metrics_;
  Uuid id_ = Uuid::Generate();
  bool active_ = false;
  size_t prefetch_size_ = 0;
  std::vector<FetchedVector> vectors_;
  client::YBTablePtr table_;
  std::vector<VectorIndexQueryPartitionData> partitions_;
  client::PartitionListVersion partitions_version_ = 0;
  std::unique_ptr<rpc::Sidecars> sidecars_;
  bool return_paging_state_ = false;
  MonoTime fetch_start_;
};
using VectorIndexQueryPtr = std::shared_ptr<VectorIndexQuery>;

[[nodiscard]] std::vector<RefCntSlice> ExtractRowsSidecar(
    const PgPerformResponsePB& resp, const rpc::Sidecars& sidecars) {
  std::vector<RefCntSlice> result;
  result.reserve(resp.responses_size());
  for (const auto& r : resp.responses()) {
    result.push_back(r.has_rows_data_sidecar()
        ? sidecars.Extract(r.rows_data_sidecar())
        : RefCntSlice());
  }
  return result;
}

std::byte* WriteVarint32ToArray(uint32_t value, std::byte* out) {
  return pointer_cast<std::byte*>(google::protobuf::io::CodedOutputStream::WriteVarint32ToArray(
      value, pointer_cast<uint8_t*>(out)));
}

std::byte* SerializeWithCachedSizesToArray(
    const google::protobuf::MessageLite& msg, std::byte* out) {
  return pointer_cast<std::byte*>(msg.SerializeWithCachedSizesToArray(pointer_cast<uint8_t*>(out)));
}

template <typename Req, typename Resp>
struct QueryTraits {
  using ReqPB = Req;
  using RespPB = Resp;
};

template <class T>
concept QueryTraitsType = std::is_same_v<QueryTraits<typename T::ReqPB, typename T::RespPB>, T>;

using PerformQueryTraits = QueryTraits<PgPerformRequestPB, PgPerformResponsePB>;
using ObjectLockQueryTraits =
    QueryTraits<const PgAcquireObjectLockRequestPB, PgAcquireObjectLockResponsePB>;

using ResponseSender = std::function<void()>;

template <QueryTraitsType T>
struct QueryDataBase {
  using ReqPB = T::ReqPB;
  using RespPB = T::RespPB;

  ReqPB& req;
  RespPB& resp;
  rpc::Sidecars& sidecars;

 protected:
  const uint64_t session_id_;

  QueryDataBase(
      uint64_t session_id, ReqPB& req_, RespPB& resp_, rpc::Sidecars& sidecars_,
      ResponseSender&& response_sender)
      : req(req_),
        resp(resp_),
        sidecars(sidecars_),
        session_id_(session_id),
        response_sender_(std::move(response_sender)) {}

  PrefixLogger LogPrefix() const { return PrefixLogger{session_id_}; }

  Status ValidateSidecars() const {
    const size_t max_size = GetAtomicFlag(&FLAGS_rpc_max_message_size);
    return sidecars.size() > max_size
        ? STATUS_FORMAT(InvalidArgument,
                        "Sending too long RPC message ($0 bytes of data), limit: $1 bytes",
                        sidecars.size(), max_size)
        : Status::OK();
  }

  void SendResponse() { response_sender_(); }

 private:
  ResponseSender response_sender_;
};

template <QueryTraitsType T>
struct QueryData;

template <>
struct QueryData<PerformQueryTraits> final : public QueryDataBase<PerformQueryTraits>,
                                             public PgResponseCacheWaiter {
  PgTableCache& table_cache;
  PgClientSessionOperations ops;
  client::YBTransactionPtr transaction;
  PgMutationCounter* pg_node_level_mutation_counter;
  SubTransactionId subtxn_id;
  UsedReadTimeApplier used_read_time_applier;
  PgResponseCache::Setter cache_setter;
  HybridTime used_in_txn_limit;
  VectorIndexQueryPtr vector_index_query;

  template <class... Args>
  explicit QueryData(PgTableCache& table_cache_, Args&&... args_)
    : QueryDataBase(std::forward<Args>(args_)...), table_cache(table_cache_) {}

  void FlushDone(client::FlushStatus* flush_status) {
    TabletReadTime used_read_time;
    if (VLOG_IS_ON(3)) {
      std::vector<std::string> status_strings;
      for (const auto& error : flush_status->errors) {
        status_strings.push_back(error->status().ToString());
      }
      VLOG_WITH_PREFIX(3)
          << "Flush status: " << flush_status->status << ", Errors: " << AsString(status_strings);
    }
    auto status = CombineErrorsToStatus(flush_status->errors, flush_status->status, ops);
    VLOG_WITH_PREFIX(3) << "Combined status: " << status;
    if (status.ok()) {
      status = ProcessResponse(used_read_time_applier ? &used_read_time : nullptr);
    }

    if (status.ok()) {
      status = ValidateSidecars();
    }

    if (!status.ok()) {
      StatusToPB(status, resp.mutable_status());
      sidecars.Reset();
      used_read_time = {};
    }
    if (cache_setter) {
      cache_setter({resp, ExtractRowsSidecar(resp, sidecars)});
    }
    if (used_read_time_applier) {
      used_read_time_applier(std::move(used_read_time));
    }
    SendResponse();
  }

  void Apply(const PgResponseCache::Response& value) override {
    resp = value.response;
    auto rows_data_it = value.rows_data.begin();
    for (auto& op : *resp.mutable_responses()) {
      if (op.has_rows_data_sidecar()) {
        sidecars.Start().Append(rows_data_it->AsSlice());
        op.set_rows_data_sidecar(narrow_cast<int>(sidecars.Complete()));
      } else {
        DCHECK(!*rows_data_it);
      }
      ++rows_data_it;
    }
    SendResponse();
  }

 private:
  Status ProcessResponse(TabletReadTime* used_read_time) {
    RETURN_NOT_OK(HandleResponse(used_read_time));
    if (used_in_txn_limit) {
      resp.set_used_in_txn_limit_ht(used_in_txn_limit.ToUint64());
    }
    if (vector_index_query && vector_index_query->active()) {
      return vector_index_query->ProcessResponse(ops, used_read_time, resp, sidecars);
    }
    auto& responses = *resp.mutable_responses();
    responses.Reserve(narrow_cast<int>(ops.size()));
    for (const auto& op : ops) {
      auto& op_resp = *responses.Add();
      op_resp.Swap(op.op->mutable_response());
      if (const auto sidecar_index = op.op->sidecar_index(); sidecar_index) {
        op_resp.set_rows_data_sidecar(narrow_cast<int>(*sidecar_index));
      }
      if (op_resp.has_paging_state()) {
        if (resp.has_catalog_read_time()) {
          // Prevent further paging reads from read restart errors.
          // See the ProcessUsedReadTime(...) function for details.
          *op_resp.mutable_paging_state()->mutable_read_time() = resp.catalog_read_time();
        } else {
          // Clear read time for the next page here unless absolutely necessary.
          //
          // Otherwise, if we do not clear read time here, a request for the
          // next page with this read time can be sent back by the pg layer.
          // Explicit read time in the request clears out existing local limits
          // since the pg client session incorrectly believes that this passed
          // read time is new. However, paging read time is simply a copy of
          // the previous read time.
          //
          // Rely on
          // 1. Either pg client session to set the read time.
          //   See pg_client_session.cc's SetupSession
          //     and transaction.cc's SetReadTimeIfNeeded
          //     and batcher.cc's ExecuteOperations
          // 2. Or transaction used read time logic in transaction.cc
          // 3. Or plain session's used read time logic in CheckPlainSessionPendingUsedReadTime
          //   to set the read time for the next page.
          //
          // Catalog sessions are not handled by the above logic, so
          // we set the paging read time above.
          op_resp.mutable_paging_state()->clear_read_time();
        }
      }
      op_resp.set_partition_list_version(op.op->table()->GetPartitionListVersion());
    }

    return Status::OK();
  }

  Status HandleResponse(TabletReadTime* used_read_time) {
    int idx = -1;
    for (const auto& op : ops) {
      ++idx;
      const auto status = HandleOperationResponse(session_id_, *op.op, &resp, used_read_time);
      if (!status.ok()) {
        if (PgsqlRequestStatus(status) == PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH) {
          table_cache.Invalidate(op.op->table()->id());
        }
        VLOG_WITH_PREFIX_AND_FUNC(2)
            << "status: " << status << ", failed op[" << idx << "]: " << AsString(op.op);
        return status.CloneAndAddErrorCode(OpIndex(idx));
      }
      // In case of write operations, increase mutation counters for non-index relations.
      if (!op.op->read_only() && !op.op->table()->IsIndex() && pg_node_level_mutation_counter) {
        const auto& table_id = down_cast<const client::YBPgsqlWriteOp&>(*op.op).table()->id();

        VLOG_WITH_PREFIX(4)
            << "Increasing "
            << (transaction ? "transaction's mutation counters" : "pg_node_level_mutation_counter")
            << " by 1 for table_id: " << table_id;

        // If there is no distributed transaction, it means we can directly update the TServer
        // level aggregate. Otherwise increase the transaction level aggregate.
        if (!transaction) {
          pg_node_level_mutation_counter->Increase(table_id, 1);
        } else {
          transaction->IncreaseMutationCounts(subtxn_id, table_id, 1);
        }
      }
      if (op.op->response().is_backfill_batch_done() &&
          op.op->type() == client::YBOperation::Type::PGSQL_READ &&
          down_cast<const client::YBPgsqlReadOp&>(*op.op).request().is_for_backfill()) {
        // After backfill table schema version is updated, so we reset cache in advance.
        table_cache.Invalidate(op.op->table()->id());
      }
    }

    return Status::OK();
  }
};

template <>
struct QueryData<ObjectLockQueryTraits> final : public QueryDataBase<ObjectLockQueryTraits> {

  template <class... Args>
  explicit QueryData(Args&&... args_) : QueryDataBase(std::forward<Args>(args_)...) {}

  void FlushDone(client::FlushStatus* flush_status) {
    auto status = CombineErrorsToStatus(flush_status->errors, flush_status->status);
    VLOG_WITH_PREFIX(3) << "Combined status: " << status;
    if (status.ok()) {
      status = ValidateSidecars();
    }

    if (!status.ok()) {
      StatusToPB(status, resp.mutable_status());
      sidecars.Reset();
    }
    SendResponse();
  }
};

template <QueryTraitsType Traits>
using QueryDataPtr = std::shared_ptr<QueryData<Traits>>;

using ObjectLockQueryDataPtr = QueryDataPtr<ObjectLockQueryTraits>;
using PerformQueryDataPtr = QueryDataPtr<PerformQueryTraits>;

class AsyncPgTablesQueryResultProvider : public PgTablesQueryListener {
 public:
  [[nodiscard]] std::future<PgTablesQueryResult> GetTables() {
    return promise_.get_future();
  }

 private:
  void Ready(const PgTablesQueryResult& result) override {
    promise_.set_value(result);
  }

  std::promise<PgTablesQueryResult> promise_;
};

[[nodiscard]] std::future<PgTablesQueryResult> GetTablesAsync(
  PgTableCache& table_cache, std::span<const TableId> table_ids,
  const PgTableCacheGetOptions& options = {}) {
  auto provider = std::make_shared<AsyncPgTablesQueryResultProvider>();
  table_cache.GetTables(table_ids, provider);
  return provider->GetTables();
}

template <QueryTraitsType T>
class SharedExchangeQuery : public std::enable_shared_from_this<SharedExchangeQuery<T>> {
  class PrivateTag {};
 public:
  SharedExchangeQuery(
      PrivateTag, std::shared_ptr<PgClientSession>&& session, SharedExchange& exchange,
      const EventStatsPtr& stats_exchange_response_size)
      : session_(std::move(session)), exchange_(exchange),
        stats_exchange_response_size_(stats_exchange_response_size) {}

  ~SharedExchangeQuery() {
    LOG_IF(DFATAL, !responded_.load()) << "Response did not send";
  }

  using RequestInfo = std::pair<std::shared_ptr<QueryData<T>>, CoarseTimePoint>;

  // Initialize query from data stored in exchange with specified size.
  // The serialized query has the following format:
  // 8 bytes - timeout in milliseconds.
  // next - size of serialized AshMetadataPB protobuf (say 'x').
  // next 'x' bytes - serialized AshMetadataPB protobuf.
  // remaining bytes - serialized PgPerformRequestPB protobuf.
  template <class... Args>
  Result<RequestInfo> ParseRequest(
      uint8_t* input, size_t size, uint64_t session_id, Args&&... args) {
    DCHECK(!data_);
    DCHECK(DCHECK_NOTNULL(session_.lock().get())->id() == session_id);
    const auto end = input + size;
    const auto timeout = MonoDelta::FromMilliseconds(LittleEndian::Load64(input));
    input += sizeof(uint64_t);
    RETURN_NOT_OK(rpc::ParseMetadataFromSharedMemory(
        &input, end - input, rpc::AnyMessagePtr(&ash_metadata_)));
    RETURN_NOT_OK(pb_util::ParseFromArray(&req_, input, end - input));
    data_.emplace(
        std::forward<Args>(args)..., session_id, req_, resp_, sidecars_,
        [this] { SendResponse(); });
    return RequestInfo{
        SharedField(this->shared_from_this(), &*data_), CoarseMonoClock::Now() + timeout};
  }

  void SendErrorResponse(const Status& s) {
    DCHECK(!s.ok());
    StatusToPB(s, resp_.mutable_status());
    SendResponse();
  }

  template <class... Args>
  [[nodiscard]] static auto MakeShared(Args&&... args) {
    return std::make_shared<SharedExchangeQuery>(PrivateTag{}, std::forward<Args>(args)...);
  }

  const AshMetadataPB& ash_metadata() const { return ash_metadata_; }

 private:
  void SendResponse() {
    auto locked_session = session_.lock();
    if (!locked_session) {
      responded_ = true;
      return;
    }

    using google::protobuf::io::CodedOutputStream;
    rpc::ResponseHeader header;
    header.set_call_id(42);
    const auto resp_size = resp_.ByteSizeLong();
    sidecars_.MoveOffsetsTo(resp_size, header.mutable_sidecar_offsets());
    const auto header_size = header.ByteSize();
    const auto body_size = narrow_cast<uint32_t>(resp_size + sidecars_.size());
    const auto full_size =
        CodedOutputStream::VarintSize32(header_size) + header_size +
        CodedOutputStream::VarintSize32(body_size) + body_size;

    if (stats_exchange_response_size_) {
      stats_exchange_response_size_->Increment(full_size);
    }

    auto* start = exchange_.Obtain(full_size);
    std::pair<uint64_t, std::byte*> shared_memory_segment(0, nullptr);
    RefCntBuffer buffer;
    if (!start) {
      shared_memory_segment = locked_session->ObtainBigSharedMemorySegment(full_size);
      if (shared_memory_segment.second) {
        start = shared_memory_segment.second;
      }

      if (!start) {
        buffer = RefCntBuffer(full_size - sidecars_.size());
        start = pointer_cast<std::byte*>(buffer.data());
      }
    }
    auto* out = start;
    out = WriteVarint32ToArray(header_size, out);
    out = SerializeWithCachedSizesToArray(header, out);
    out = WriteVarint32ToArray(body_size, out);
    out = SerializeWithCachedSizesToArray(resp_, out);
    auto response_size = full_size;
    if (!buffer) {
      sidecars_.CopyTo(out);
      out += sidecars_.size();
      DCHECK_EQ(out - start, full_size);
      if (shared_memory_segment.second) {
        response_size =
            kTooBigResponseMask | kBigSharedMemoryMask | full_size |
            (shared_memory_segment.first << kBigSharedMemoryIdShift);
      }
    } else {
      auto id = locked_session->SaveData(buffer, std::move(sidecars_.buffer()));
      response_size = kTooBigResponseMask | id;
    }
    exchange_.Respond(response_size);
    responded_ = true;
  }

  std::remove_const_t<typename T::ReqPB> req_;
  typename T::RespPB resp_;
  AshMetadataPB ash_metadata_;
  rpc::Sidecars sidecars_;
  std::weak_ptr<PgClientSession> session_;
  SharedExchange& exchange_;
  EventStatsPtr stats_exchange_response_size_;
  std::atomic<bool> responded_{false};
  std::optional<QueryData<T>> data_;
};

client::YBSessionPtr CreateSession(
    client::YBClient* client, CoarseTimePoint deadline, const scoped_refptr<ClockBase>& clock) {
  auto result = std::make_shared<client::YBSession>(client, deadline, clock);
  result->SetForceConsistentRead(client::ForceConsistentRead::kTrue);
  result->set_allow_local_calls_in_curr_thread(false);
  return result;
}

HybridTime GetInTxnLimit(const PgPerformOptionsPB& options, ClockBase* clock) {
  if (!options.has_in_txn_limit_ht()) {
    return {};
  }
  auto in_txn_limit = HybridTime::FromPB(options.in_txn_limit_ht().value());
  return in_txn_limit ? in_txn_limit : clock->Now();
}

Status Commit(client::YBTransaction* txn, PgResponseCache::Disabler disabler) {
  return txn->CommitFuture().get();
}

UsedReadTimeApplier BuildUsedReadTimeApplier(
    std::weak_ptr<UsedReadTime> used_read_time, size_t signature) {
  return [used_read_time_weak = std::move(used_read_time), signature](
      TabletReadTime&& read_time_data) {
    const auto used_read_time_ptr = used_read_time_weak.lock();
    if (!used_read_time_ptr) {
      return;
    }
    auto& used_read_time = *used_read_time_ptr;
    std::lock_guard guard(used_read_time.lock);
    if (used_read_time.signature == signature) {
      DCHECK(!used_read_time.data);
      used_read_time.data = std::move(read_time_data);
    } else {
      LOG(INFO) << "Skipping used_read_time update " << read_time_data.value
                << " due to signature mismatch "
                << signature << " vs " << used_read_time.signature;
    }
  };
}

Result<PgReplicaIdentity> GetReplicaIdentityEnumValue(
    PgReplicaIdentityType replica_identity_proto) {
  switch (replica_identity_proto) {
    case DEFAULT:
      return PgReplicaIdentity::DEFAULT;
    case FULL:
      return PgReplicaIdentity::FULL;
    case NOTHING:
      return PgReplicaIdentity::NOTHING;
    case CHANGE:
      return PgReplicaIdentity::CHANGE;
    default:
      RSTATUS_DCHECK(false, InvalidArgument, "Invalid Replica Identity Type");
  }
}

std::atomic<bool>& InUseAtomic(const SharedMemorySegmentHandle& handle) {
  return *pointer_cast<std::atomic<bool>*>(handle.address() - sizeof(std::atomic<bool>));
}

class ReadPointHistory {
 public:
  explicit ReadPointHistory(const PrefixLogger& prefix_logger) : prefix_logger_(prefix_logger) {}

  [[nodiscard]] bool Restore(ConsistentReadPoint* read_point, uint64_t read_time_serial_no) {
    auto result = false;
    if (const auto i = read_points_.find(read_time_serial_no);
        i != read_points_.end() && read_time_serial_no >= min_) {
      read_point->SetMomento(i->second);
      result = true;
    }
    VLOG_WITH_PREFIX(4) << "ReadPointHistory::Restore read_time_serial_no=" << read_time_serial_no
                        << " return " << result
                        << " read time is " << read_point->GetReadTime();
    return result;
  }

  void Save(const ConsistentReadPoint& read_point, uint64_t read_time_serial_no) {
    auto momento = read_point.GetMomento();
    const auto& read_time = momento.read_time();
    DCHECK(read_time);
    VLOG_WITH_PREFIX(4) << "ReadPointHistory::Save read_time_serial_no=" << read_time_serial_no
                        << " read time is " << AsString(read_time);
    if (read_points_.empty()) {
      max_ = read_time_serial_no;
      min_ = read_time_serial_no;
    } else {
      min_ = std::min(min_, read_time_serial_no);
      max_ = std::max(max_, read_time_serial_no);
    }
    auto ipair = read_points_.try_emplace(read_time_serial_no, std::move(momento));
    if (!ipair.second) {
      // Potentially read time could be set to same read_time_serial_no multiple times.
      // It is expected that read time is the same or fresher (due to possible restart)
      // but not older.
      DCHECK(read_time.read >= ipair.first->second.read_time().read);
      ipair.first->second = std::move(momento);
    }
  }

  void Cleanup(uint64_t min) {
    VLOG_WITH_PREFIX(4) << "ReadTimeHistory::Cleanup " << min;
    if (read_points_.empty()) {
      return;
    }
    if (max_ < min) {
      VLOG_WITH_PREFIX(4) << "Clearing history [" << min_ << ", " << max_ << "]";
      read_points_.clear();
      return;
    }
    min_ = std::max(min_, min);
  }

 private:
  const PrefixLogger& LogPrefix() const { return prefix_logger_; }

  const PrefixLogger prefix_logger_;
  uint64_t min_ = 0;
  uint64_t max_ = 0;
  std::unordered_map<uint64_t, ConsistentReadPoint::Momento> read_points_;
};

class ObjectLockOwnerInfo {
 public:
  ObjectLockOwnerInfo(
      PgSessionLockOwnerTagShared& shared, docdb::ObjectLockOwnerRegistry& registry,
      const TransactionId& txn_id, const TabletId& tablet_id)
      : shared_(shared), guard_(registry.Register(txn_id, tablet_id)), txn_id_(txn_id) {
    UpdateShared(guard_.tag());
  }

  ~ObjectLockOwnerInfo() {
    UpdateShared({});
  }

  const TransactionId& txn_id() const {
    return txn_id_;
  }

 private:
  void UpdateShared(docdb::SessionLockOwnerTag tag) {
    ParentProcessGuard g;
    shared_.Get() = tag;
  }

  PgSessionLockOwnerTagShared& shared_;
  docdb::ObjectLockOwnerRegistry::RegistrationGuard guard_;
  TransactionId txn_id_;
};

class TransactionProvider {
 public:
  YB_STRONGLY_TYPED_BOOL(EnsureGlobal);

  TransactionProvider(
      PgClientSession::TransactionBuilder&& builder,
      docdb::ObjectLockOwnerRegistry* lock_owner_registry)
      : builder_(std::move(builder)), lock_owner_registry_(lock_owner_registry) {}

  template<PgClientSessionKind kind, class... Args>
  requires(
      kind == PgClientSessionKind::kPlain ||
      kind == PgClientSessionKind::kDdl ||
      kind == PgClientSessionKind::kPgSession)
  auto Take(Args&&... args) {
    if constexpr (kind == PgClientSessionKind::kPlain) {
      return TakeForPlain(std::forward<Args>(args)...);
    } else if constexpr (kind == PgClientSessionKind::kDdl) {
      return TakeForDdl(std::forward<Args>(args)...);
    } else if constexpr (kind == PgClientSessionKind::kPgSession) {
      return TakeForPgSession(std::forward<Args>(args)...);
    }
  }

  Result<TransactionMetadata> CheckTxnMetaForPlainFinish() {
    RSTATUS_DCHECK(
        next_plain_, IllegalState,
        "Attempt to access next_plain_ transaction where there isn't one");
    // next_plain_ would be ready at this point i.e status tablet picked.
    auto txn_meta_res = next_plain_->metadata();
    if (txn_meta_res.ok()) {
      next_plain_->SetStartTimeIfNecessary();
      return *txn_meta_res;
    }
    VLOG_WITH_FUNC(1) << "transaction already failed";
    // If the transaction has already failed due to some reason, we should release the locks.
    // And also reset next_plain_, so the subsequent ysql transaction would use a new docdb txn.
    TransactionMetadata txn_meta_for_release;
    txn_meta_for_release.transaction_id = next_plain_->id();
    ConsumePlainTxn();
    return txn_meta_for_release;
  }

  void ConsumePlainTxn() {
    if (next_plain_) {
      VLOG_WITH_FUNC(1)
          << "Consuming re-usable kPlain txn " << next_plain_->id()
          << ", next_plain_ transaction reset";
      next_plain_ = nullptr;
    }
    ResetObjectLockOwner();
  }

  void ResetObjectLockOwner() {
    object_lock_owner_.reset();
  }

  Result<TransactionMetadata> NextTxnMetaForPlain(
      TransactionFullLocality locality, CoarseTimePoint deadline) {
    client::internal::InFlightOpsGroupsWithMetadata ops_info;
    if (!next_plain_) {
      VLOG_WITH_FUNC(1) << "requesting new transaction of locality " << locality;
      auto txn = Build(locality, deadline, {});
      // Don't execute txn->GetMetadata() here since the transaction is not iniatialized with
      // its full metadata yet, like isolation level.
      Synchronizer synchronizer;
      if (txn->batcher_if().Prepare(
          &ops_info, client::ForceConsistentRead::kFalse, deadline, client::Initial::kFalse,
          synchronizer.AsStdStatusCallback())) {
        synchronizer.StatusCB(Status::OK());
      }
      RETURN_NOT_OK(synchronizer.Wait());
      next_plain_.swap(txn);
    } else {
      VLOG_WITH_FUNC(1) << "trying to reuse existing transaction " << next_plain_->id();
    }
    // next_plain_ would be ready at this point i.e status tablet picked.
    auto metadata = VERIFY_RESULT(next_plain_->metadata());
    next_plain_->SetStartTimeIfNecessary();
    if (object_lock_shared_ &&
        (!object_lock_owner_ || object_lock_owner_->txn_id() != metadata.transaction_id)) {
      object_lock_owner_.emplace(
          *object_lock_shared_, *DCHECK_NOTNULL(lock_owner_registry_), metadata.transaction_id,
          metadata.status_tablet);
    }
    return metadata;
  }

  bool HasNextTxnForPlain() const {
    return next_plain_ != nullptr;
  }

  void SetupSharedObjectLocking(PgSessionLockOwnerTagShared& object_lock_shared) {
    DCHECK(!object_lock_shared_);
    object_lock_shared_ = &object_lock_shared;
  }

 private:
  struct BuildStrategy {
    bool is_ddl = false;
    bool force_create = false;
  };

  client::YBTransactionPtr Build(
      TransactionFullLocality locality, CoarseTimePoint deadline, const BuildStrategy& strategy) {
    return builder_(
        IsDDL{strategy.is_ddl}, locality, deadline,
        client::ForceCreateTransaction{strategy.force_create});
  }

  client::YBTransactionPtr TakeForPgSession(CoarseTimePoint deadline) {
    // The transaction coordinator needs to know that this is a session level transaction as the
    // handling on deadlocks and heartbeats etc are different for regular docdb transactions and
    // session level transactions. Hence, we create a new transaction instead of using a ready
    // transaction (whose state at the coordinator would be different from what we want to set).
    //
    // Advisory locks table is not placement local, hence we need a global transaction for tagging
    // the requested session advisory locks.
    return Build(TransactionFullLocality::Global(), deadline, {.force_create = true});
  }

  client::YBTransactionPtr TakeForDdl(CoarseTimePoint deadline) {
    return Build(TransactionFullLocality::Global(), deadline, {.is_ddl = true});
  }

  client::YBTransactionPtr TakeForPlain(
      TransactionFullLocality locality, CoarseTimePoint deadline) {
    return next_plain_ ? std::exchange(next_plain_, {}) : Build(locality, deadline, {});
  }

  const PgClientSession::TransactionBuilder builder_;
  PgSessionLockOwnerTagShared* object_lock_shared_ = nullptr;
  std::optional<ObjectLockOwnerInfo> object_lock_owner_;
  docdb::ObjectLockOwnerRegistry* lock_owner_registry_ = nullptr;
  client::YBTransactionPtr next_plain_;
};

YB_STRONGLY_TYPED_BOOL(IsTxnUsingTableLocks);

Result<std::pair<PgClientSessionOperations, VectorIndexQueryPtr>> PrepareOperations(
    PgPerformRequestPB* req, client::YBSession* session, rpc::Sidecars* sidecars,
    const PgTablesQueryResult& tables, VectorIndexQueryPtr& vector_index_query,
    bool has_distributed_txn,
    const LWFunction<Result<TransactionMetadata>()>& object_locking_txn_meta_provider,
    IsTxnUsingTableLocks is_txn_using_table_locks,
    const PgClientSessionMetrics& metrics) {
  auto write_time = HybridTime::FromPB(req->write_time());
  std::pair<PgClientSessionOperations, VectorIndexQueryPtr> result;
  auto& ops = result.first;
  ops.reserve(req->ops().size());
  client::YBTablePtr table;
  CancelableScopeExit abort_se{[session] { session->Abort(); }};
  const auto read_from_followers = req->options().read_from_followers();
  bool has_write_ops = false;

  // TODO(vector_index): it is unexpected to have a mix of vector index read ops and
  // non-vector index read ops. A sanity DCHECK is required.
  for (auto& op : *req->mutable_ops()) {
    if (op.has_read()) {
      auto& read = *op.mutable_read();
      RETURN_NOT_OK(GetTable(read.table_id(), tables, &table));
      if (read.index_request().has_vector_idx_options()) {
        if (req->ops_size() != 1) {
          auto status = STATUS_FORMAT(
              NotSupported, "Only single vector index query is supported, while $0 provided",
              req->ops_size());
          LOG(DFATAL) << status << ": " << req->ShortDebugString();
          return status;
        }
        if (!vector_index_query ||
            !vector_index_query->IsContinuation(read.index_request().paging_state())) {
          vector_index_query = std::make_shared<VectorIndexQuery>(metrics);
        }
        result.second = vector_index_query;
        RETURN_NOT_OK(result.second->Prepare(read, table, ops));
      } else {
        auto read_op = std::make_shared<client::YBPgsqlReadOp>(table, *sidecars, &read);
        if (read_from_followers) {
          read_op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
        }
        ops.push_back(PgClientSessionOperation {
          .op = std::move(read_op),
          .vector_index_read_request = nullptr,
        });
      }
    } else {
      auto& write = *op.mutable_write();
      RETURN_NOT_OK(GetTable(write.table_id(), tables, &table));
      auto write_op = std::make_shared<client::YBPgsqlWriteOp>(table, *sidecars, &write);
      if (write_time) {
        write_op->SetWriteTime(write_time);
      }
      if (req->options().xrepl_origin_id()) {
        write_op->SetXreplOriginId(req->options().xrepl_origin_id());
      }
      ops.push_back(PgClientSessionOperation {
        .op = std::move(write_op),
        .vector_index_read_request = nullptr,
      });
      has_write_ops = true;
    }
  }

  for (const auto& pg_client_session_operation : ops) {
    session->Apply(pg_client_session_operation.op);
  }
  if (has_write_ops && !has_distributed_txn && is_txn_using_table_locks) {
    session->SetObjectLockingTxnMeta(VERIFY_RESULT(object_locking_txn_meta_provider()));
  }

  abort_se.Cancel();
  return result;
}

template <QueryTraitsType T>
class RpcQuery : public std::enable_shared_from_this<RpcQuery<T>> {
  class PrivateTag {};
  using ReqPB = T::ReqPB;
  using RespPB = T::RespPB;

 public:
  rpc::RpcContext context;

  template <class... Args>
  RpcQuery(
      PrivateTag, uint64_t session_id, pid_t session_pid, ReqPB& req, RespPB& resp,
      rpc::RpcContext&& context_, Args&&... args)
      : context(std::move(context_)),
        data_(
            std::forward<Args>(args)..., session_id, req, resp, context.sidecars(),
            [this] { SendResponse(); }) {}

  [[nodiscard]] auto SharedData() { return SharedField(this->shared_from_this(), &data_); }

  void SendErrorResponse(const Status& s) {
    DCHECK(!s.ok());
    StatusToPB(s, data_.resp.mutable_status());
    SendResponse();
  }

  template <class... Args>
  [[nodiscard]] static auto MakeShared(Args&&... args) {
    return std::make_shared<RpcQuery>(PrivateTag{}, std::forward<Args>(args)...);
  }

 private:
  void SendResponse() { context.RespondSuccess(); }

  QueryData<T> data_;
};

YsqlAdvisoryLocksTableLockId MakeAdvisoryLockId(uint32_t db_oid, const AdvisoryLockIdPB& lock_pb) {
  return {.db_oid = db_oid,
          .class_oid = lock_pb.classid(),
          .objid = lock_pb.objid(),
          .objsubid = lock_pb.objsubid()};
}

size_t RenewSignature(UsedReadTime& used_read_time) {
  std::lock_guard guard(used_read_time.lock);
  used_read_time.data.reset();
  return ++used_read_time.signature;
}

bool IsTableLockTypeGlobal(TableLockType lock_type) {
  switch (lock_type) {
    // ACCESS_SHARE, ROW_SHARE, ROW_EXCLUSIVE don't conflict among themselves, and hence acquire
    // these locks just locally.
    case TableLockType::NONE: [[fallthrough]];
    case ACCESS_SHARE: [[fallthrough]];
    case ROW_SHARE: [[fallthrough]];
    case ROW_EXCLUSIVE: return false;
    // The rest either conflict among themselves or with the above ACCESS_SHARE, ROW_SHARE &
    // ROW_EXCLUSIVE. Hence we try acquiring them globally in order to rightly detect conflicts.
    case SHARE_UPDATE_EXCLUSIVE: [[fallthrough]];
    case SHARE: [[fallthrough]];
    case SHARE_ROW_EXCLUSIVE: [[fallthrough]];
    case EXCLUSIVE: [[fallthrough]];
    case ACCESS_EXCLUSIVE: return true;
  }
  FATAL_INVALID_ENUM_VALUE(TableLockType, lock_type);
}

Status MergeStatus(Status&& main, Status&& aux) {
  if (!main.ok() && !aux.ok()) {
    return main.CloneAndPrepend(aux.message());
  }
  return std::move(main.ok() ? aux : main);
}

template <typename Request>
Request AcquireRequestFor(
    const std::string& session_host_uuid, const TransactionId& txn_id, SubTransactionId subtxn_id,
    auto lock_oid, TableLockType lock_type, uint64_t lease_epoch, ClockBase* clock,
    CoarseTimePoint deadline, const TabletId& status_tablet) {
  auto now = clock->Now();
  Request req;
  req.set_txn_id(txn_id.data(), txn_id.size());
  req.set_subtxn_id(subtxn_id);
  req.set_session_host_uuid(session_host_uuid);
  req.set_lease_epoch(lease_epoch);
  auto deadline_ht = now.AddMicroseconds(ToMicroseconds(deadline - ToCoarse(MonoTime::Now())));
  req.set_ignore_after_hybrid_time(deadline_ht.ToUint64());
  if (clock) {
    req.set_propagated_hybrid_time(now.ToUint64());
  }
  auto* lock = req.add_object_locks();
  lock->set_database_oid(lock_oid.database_oid());
  lock->set_relation_oid(lock_oid.relation_oid());
  lock->set_object_oid(lock_oid.object_oid());
  lock->set_object_sub_oid(lock_oid.object_sub_oid());
  lock->set_lock_type(lock_type);
  req.set_status_tablet(status_tablet);
  return req;
}

template <typename Request>
Request ReleaseRequestFor(
    const std::string& session_host_uuid, const TransactionId& txn_id,
    std::optional<SubTransactionId> subtxn_id, uint64_t lease_epoch = 0,
    ClockBase* clock = nullptr) {
  Request req;
  req.set_txn_id(txn_id.data(), txn_id.size());
  if (subtxn_id) {
    req.set_subtxn_id(*subtxn_id);
  }
  req.set_session_host_uuid(session_host_uuid);
  if (lease_epoch) {
    req.set_lease_epoch(lease_epoch);
  }
  if (clock) {
    req.set_propagated_hybrid_time(clock->Now().ToUint64());
  }
  return req;
}

bool IsReadPointResetRequested(const PgPerformOptionsPB& options) {
  return options.has_read_time() && !options.read_time().has_read_ht();
}

[[nodiscard]] auto DoTrackSharedMemoryPgMethodExecution(
    const std::shared_ptr<yb::ash::WaitStateInfo>& wait_state, const AshMetadataPB& metadata,
    const char* method_name) {
  static std::atomic<int64_t> next_rpc_id{0};
  DCHECK(wait_state);
  wait_state->UpdateMetadataFromPB(metadata);
  wait_state->UpdateMetadata(
    {.rpc_request_id = next_rpc_id.fetch_add(1, std::memory_order_relaxed)});
  wait_state->UpdateAuxInfo({.method = method_name});
  ash::SharedMemoryPgPerformTracker().Track(wait_state);
  return MakeOptionalScopeExit(
      [wait_state] { ash::SharedMemoryPgPerformTracker().Untrack(wait_state); });
}

template <QueryTraitsType T>
[[nodiscard]] auto TrackSharedMemoryPgMethodExecution(
    const std::shared_ptr<yb::ash::WaitStateInfo>& wait_state, const AshMetadataPB& metadata);

template <>
[[nodiscard]] auto TrackSharedMemoryPgMethodExecution<PerformQueryTraits>(
    const std::shared_ptr<yb::ash::WaitStateInfo>& wait_state, const AshMetadataPB& metadata) {
  return DoTrackSharedMemoryPgMethodExecution(wait_state, metadata, "Perform");
}

template <>
[[nodiscard]] auto TrackSharedMemoryPgMethodExecution<ObjectLockQueryTraits>(
    const std::shared_ptr<yb::ash::WaitStateInfo>& wait_state, const AshMetadataPB& metadata) {
  return DoTrackSharedMemoryPgMethodExecution(wait_state, metadata, "AcquireObjectLock");
}

PgTablespaceOid GetOpTablespaceOid(const PgPerformOpPB& op) {
  return op.has_write() ? op.write().tablespace_oid() : op.read().tablespace_oid();
}

} // namespace

class PgClientSession::Impl {
 public:
  Impl(
      TransactionBuilder&& transaction_builder, std::shared_ptr<PgClientSession> shared_this,
      client::YBClient& client, const PgClientSessionContext& context, uint64_t id, pid_t pid,
      uint64_t lease_epoch, TSLocalLockManagerPtr lock_manager, rpc::Scheduler& scheduler)
      : client_(client),
        context_(context),
        shared_this_(std::move(shared_this)),
        id_(id),
        pid_(pid),
        lease_epoch_(lease_epoch),
        ts_lock_manager_(std::move(lock_manager)),
        transaction_provider_(std::move(transaction_builder), context_.lock_owner_registry),
        big_shared_mem_expiration_task_("big_shared_mem_expiration_task", &scheduler),
        read_point_history_(PrefixLogger(id_, pid_)) {}

  [[nodiscard]] auto id() const {return id_; }

  void SetupSharedObjectLocking(PgSessionLockOwnerTagShared& object_lock_shared) {
    transaction_provider_.SetupSharedObjectLocking(object_lock_shared);
  }

  Status CreateTable(
      const PgCreateTableRequestPB& req, PgCreateTableResponsePB* resp, rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();

    if (PREDICT_FALSE(FLAGS_TEST_fail_create_table_rpc)) {
      return STATUS_FORMAT(IllegalState, "FLAGS_TEST_fail_create_table_rpc set");
    }

    PgCreateTable helper(req);
    RETURN_NOT_OK(helper.Prepare());

    if (xcluster_context()) {
      xcluster_context()->PrepareCreateTableHelper(req, helper);
    }

    RETURN_NOT_OK(SetupSessionForDdl(
        req.use_regular_transaction_block(), req.options(), context->GetClientDeadline()));
    const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
        req.use_transaction(), req.use_regular_transaction_block(), context->GetClientDeadline(),
        IsTxnUsingTableLocks(req.options().is_using_table_locks())));
    RETURN_NOT_OK(helper.Exec(
        &client_, metadata, req.options().active_sub_transaction_id(),
        context->GetClientDeadline()));
    VLOG_WITH_PREFIX(1) << __func__ << ": " << req.table_name();
    const auto& indexed_table_id = helper.indexed_table_id();
    if (indexed_table_id.IsValid()) {
      table_cache().Invalidate(indexed_table_id.GetYbTableId());
    }
    return Status::OK();
  }

  Status CreateDatabase(
      const PgCreateDatabaseRequestPB& req, PgCreateDatabaseResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    bool is_clone =
        req.source_database_name() != "" &&
        req.source_database_name() != "template0" &&
        req.source_database_name() != "template1";
    std::optional<YbcCloneInfo> yb_clone_info;
    if (is_clone) {
      yb_clone_info = YbcCloneInfo {
        .clone_time = req.clone_time(),
        .src_db_name = req.source_database_name().c_str(),
        .src_owner = req.source_owner().c_str(),
        .tgt_owner = req.target_owner().c_str(),
      };
    }
    RETURN_NOT_OK(SetupSessionForDdl(
      req.use_regular_transaction_block(), req.options(), context->GetClientDeadline()));
    return client_.CreateNamespace(
        req.database_name(), YQL_DATABASE_PGSQL, "" /* creator_role_name */,
        GetPgsqlNamespaceId(req.database_oid()),
        req.source_database_oid() != kPgInvalidOid
            ? GetPgsqlNamespaceId(req.source_database_oid()) : "",
        req.next_oid(),
        VERIFY_RESULT(GetDdlTransactionMetadata(
            req.use_transaction(), req.use_regular_transaction_block(),
            context->GetClientDeadline(),
            IsTxnUsingTableLocks(req.options().is_using_table_locks()))),
        req.colocated(), context->GetClientDeadline(), yb_clone_info);
  }

  Status DropDatabase(
      const PgDropDatabaseRequestPB& req, PgDropDatabaseResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    return client_.DeleteNamespace(
        req.database_name(), YQL_DATABASE_PGSQL, GetPgsqlNamespaceId(req.database_oid()),
        context->GetClientDeadline());
  }

  Status DropTable(
      const PgDropTableRequestPB& req, PgDropTableResponsePB* resp, rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    const auto yb_table_id = PgObjectId::GetYbTableIdFromPB(req.table_id());
    RETURN_NOT_OK(SetupSessionForDdl(
      req.use_regular_transaction_block(), req.options(), context->GetClientDeadline()));
    const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
        true /* use_transaction */, req.use_regular_transaction_block(),
        context->GetClientDeadline(), IsTxnUsingTableLocks(req.options().is_using_table_locks())));
    // If ddl rollback is enabled, the table will not be deleted now, so we cannot wait for the
    // table/index deletion to complete. The table will be deleted in the background only after the
    // transaction has been determined to be a success.
    if (req.index()) {
      client::YBTableName indexed_table;
      RETURN_NOT_OK(client_.DeleteIndexTable(
          yb_table_id, &indexed_table, !YsqlDdlRollbackEnabled() /* wait */,
          metadata, req.options().active_sub_transaction_id(), context->GetClientDeadline()));
      indexed_table.SetIntoTableIdentifierPB(resp->mutable_indexed_table());
      table_cache().Invalidate(indexed_table.table_id());
      table_cache().Invalidate(yb_table_id);
      return Status::OK();
    }

    RETURN_NOT_OK(client_.DeleteTable(yb_table_id, !YsqlDdlRollbackEnabled(), metadata,
      req.options().active_sub_transaction_id(), context->GetClientDeadline()));
    table_cache().Invalidate(yb_table_id);
    return Status::OK();
  }

  Status AlterDatabase(
      const PgAlterDatabaseRequestPB& req, PgAlterDatabaseResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    const auto alterer = client_.NewNamespaceAlterer(
        req.database_name(), GetPgsqlNamespaceId(req.database_oid()));
    alterer->SetDatabaseType(YQL_DATABASE_PGSQL);
    alterer->RenameTo(req.new_name());
    return alterer->Alter(context->GetClientDeadline());
  }

  Status AlterTable(
      const PgAlterTableRequestPB& req, PgAlterTableResponsePB* resp, rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    const auto table_id = PgObjectId::GetYbTableIdFromPB(req.table_id());
    const auto alterer = client_.NewTableAlterer(table_id);
    RETURN_NOT_OK(SetupSessionForDdl(
      req.use_regular_transaction_block(), req.options(), context->GetClientDeadline()));
    const auto txn = VERIFY_RESULT(GetDdlTransactionMetadata(
        req.use_transaction(), req.use_regular_transaction_block(), context->GetClientDeadline(),
        IsTxnUsingTableLocks(req.options().is_using_table_locks())));
    if (txn) {
      alterer->part_of_transaction(txn);
    }
    alterer->part_of_sub_transaction(req.options().active_sub_transaction_id());
    if (req.increment_schema_version()) {
      alterer->set_increment_schema_version();
    }
    for (const auto& add_column : req.add_columns()) {
      const auto yb_type = QLType::Create(ToLW(
          static_cast<PersistentDataType>(add_column.attr_ybtype())));
      alterer->AddColumn(add_column.attr_name())
            ->Type(yb_type)->Order(add_column.attr_num())->PgTypeOid(add_column.attr_pgoid())
            ->SetMissing(add_column.attr_missing_val());
      // Do not set 'nullable' attribute as PgCreateTable::AddColumn() does not do it.
    }
    for (const auto& rename_column : req.rename_columns()) {
      alterer->AlterColumn(rename_column.old_name())->RenameTo(rename_column.new_name());
    }
    for (const auto& drop_column : req.drop_columns()) {
      alterer->DropColumn(drop_column);
    }

    if (!req.rename_table().table_name().empty() ||
        !req.rename_table().schema_name().empty()) {
      const auto ns_id = PgObjectId::GetYbNamespaceIdFromPB(req.table_id());
      // Change table name and/or schema name. DB name cannot be changed.
      client::YBTableName new_table_name(YQL_DATABASE_PGSQL);
      new_table_name.set_table_id(table_id);
      new_table_name.set_namespace_id(ns_id);
      if (!req.rename_table().table_name().empty()) {
        new_table_name.set_table_name(req.rename_table().table_name());
      }
      if (!req.rename_table().schema_name().empty()) {
        new_table_name.set_pgschema_name(req.rename_table().schema_name());
      }
      alterer->RenameTo(new_table_name);
    }

    if (req.has_replica_identity()) {
      client::YBTablePtr yb_table;
      RETURN_NOT_OK(GetTable(table_id, table_cache(), &yb_table));
      auto table_properties = yb_table->schema().table_properties();
      auto replica_identity = VERIFY_RESULT(GetReplicaIdentityEnumValue(
          req.replica_identity().replica_identity()));
      table_properties.SetReplicaIdentity(replica_identity);
      alterer->SetTableProperties(table_properties);
    }
    alterer->timeout(context->GetClientDeadline() - CoarseMonoClock::now());
    RETURN_NOT_OK(alterer->Alter());
    table_cache().Invalidate(table_id);
    return Status::OK();
  }

  Status TruncateTable(
      const PgTruncateTableRequestPB& req, PgTruncateTableResponsePB* resp,
      rpc::RpcContext* context) {
    return client_.TruncateTable(PgObjectId::GetYbTableIdFromPB(req.table_id()));
  }

  Status CreateReplicationSlot(
      const PgCreateReplicationSlotRequestPB& req, PgCreateReplicationSlotResponsePB* resp,
      rpc::RpcContext* context) {
    std::unordered_map<std::string, std::string> options;
    options.reserve(4);
    options.emplace(cdc::kIdType, cdc::kNamespaceId);
    options.emplace(cdc::kRecordFormat, CDCRecordFormat_Name(cdc::CDCRecordFormat::PROTO));
    options.emplace(cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
    options.emplace(cdc::kCheckpointType, CDCCheckpointType_Name(cdc::CDCCheckpointType::EXPLICIT));

    std::optional<CDCSDKSnapshotOption> snapshot_option;
    if (FLAGS_yb_enable_cdc_consistent_snapshot_streams) {
      switch (req.snapshot_action()) {
        case REPLICATION_SLOT_NOEXPORT_SNAPSHOT:
          snapshot_option = CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT;
          break;
        case REPLICATION_SLOT_USE_SNAPSHOT:
          snapshot_option = CDCSDKSnapshotOption::USE_SNAPSHOT;
          break;
        case REPLICATION_SLOT_EXPORT_SNAPSHOT:
          snapshot_option = CDCSDKSnapshotOption::EXPORT_SNAPSHOT;
          break;
        case REPLICATION_SLOT_UNKNOWN_SNAPSHOT:
          // Crash in debug and return InvalidArgument in release mode.
          RSTATUS_DCHECK(false, InvalidArgument, "invalid snapshot_action UNKNOWN");
        default:
          return STATUS_FORMAT(
              InvalidArgument, "invalid snapshot_action $0", req.snapshot_action());
      }
    }

    std::optional<yb::ReplicationSlotLsnType> lsn_type;
    if (FLAGS_ysql_yb_allow_replication_slot_lsn_types) {
      switch (req.lsn_type()) {
        case ReplicationSlotLsnTypePg_SEQUENCE:
          lsn_type = ReplicationSlotLsnType::ReplicationSlotLsnType_SEQUENCE;
          break;
        case ReplicationSlotLsnTypePg_HYBRID_TIME:
          lsn_type = ReplicationSlotLsnType::ReplicationSlotLsnType_HYBRID_TIME;
          break;
        default:
          return STATUS_FORMAT(InvalidArgument, "invalid lsn_type $0", req.lsn_type());
      }
    }

    std::optional<yb::ReplicationSlotOrderingMode> ordering_mode;
    if (FLAGS_ysql_yb_allow_replication_slot_ordering_modes) {
      switch (req.ordering_mode()) {
        case ReplicationSlotOrderingModePg_ROW:
          ordering_mode = ReplicationSlotOrderingMode::ReplicationSlotOrderingMode_ROW;
          break;
        case ReplicationSlotOrderingModePg_TRANSACTION:
          ordering_mode = ReplicationSlotOrderingMode::ReplicationSlotOrderingMode_TRANSACTION;
          break;
        default:
          return STATUS_FORMAT(InvalidArgument, "invalid ordering_mode $0", req.ordering_mode());
      }
    }

    uint64_t consistent_snapshot_time;
    auto stream_result = VERIFY_RESULT(client_.CreateCDCSDKStreamForNamespace(
        GetPgsqlNamespaceId(req.database_oid()), options,
        /* populate_namespace_id_as_table_id */ false,
        ReplicationSlotName(req.replication_slot_name()),
        req.output_plugin_name(), snapshot_option,
        context->GetClientDeadline(),
        CDCSDKDynamicTablesOption::DYNAMIC_TABLES_ENABLED,
        &consistent_snapshot_time,
        lsn_type,
        ordering_mode));
    *resp->mutable_stream_id() = stream_result.ToString();
    resp->set_cdcsdk_consistent_snapshot_time(consistent_snapshot_time);
    return Status::OK();
  }

  Status DropReplicationSlot(
      const PgDropReplicationSlotRequestPB& req, PgDropReplicationSlotResponsePB* resp,
      rpc::RpcContext* context) {
    return client_.DeleteCDCStream(ReplicationSlotName(req.replication_slot_name()));
  }

  Result<ReadHybridTime> GetTxnSnapshotReadTime(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline) {
    auto setup_session_result = VERIFY_RESULT(SetupSession(options, deadline));
    RSTATUS_DCHECK(setup_session_result.is_plain, IllegalState, "Unexpected session is prepared");
    return setup_session_result.session_data.session->read_point()->GetReadTime();
  }

  Status SetTxnSnapshotReadTime(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline) {
    SCHECK(options.has_read_time(), InvalidArgument, "Snapshot Read Time not provided");
    SCHECK(
        txn_serial_no_ != options.txn_serial_no(), IllegalState,
        "Snapshot read time can only be set at the very beginning of transaction.");
    return ResultToStatus(SetupSession(options, deadline));
  }

  Status WaitForBackendsCatalogVersion(
      const PgWaitForBackendsCatalogVersionRequestPB& req,
      PgWaitForBackendsCatalogVersionResponsePB* resp,
      rpc::RpcContext* context) {
    // TODO(jason): send deadline to client.
    const int num_lagging_backends = VERIFY_RESULT(client_.WaitForYsqlBackendsCatalogVersion(
        req.database_oid(), req.catalog_version(), context->GetClientDeadline(),
        req.requestor_pg_backend_pid()));
    resp->set_num_lagging_backends(num_lagging_backends);
    return Status::OK();
  }

  Status BackfillIndex(
      const PgBackfillIndexRequestPB& req, PgBackfillIndexResponsePB* resp,
      rpc::RpcContext* context) {
    return client_.BackfillIndex(
        PgObjectId::GetYbTableIdFromPB(req.table_id()), /* wait= */ true,
        context->GetClientDeadline());
  }

  Status CreateTablegroup(
      const PgCreateTablegroupRequestPB& req, PgCreateTablegroupResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    const auto id = PgObjectId::FromPB(req.tablegroup_id());
    const auto tablespace_id = PgObjectId::FromPB(req.tablespace_id());
    RETURN_NOT_OK(SetupSessionForDdl(
      req.use_regular_transaction_block(), req.options(), context->GetClientDeadline()));
    const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
        true /* use_transaction */, req.use_regular_transaction_block(),
        context->GetClientDeadline(), IsTxnUsingTableLocks(req.options().is_using_table_locks())));
    const auto s = client_.CreateTablegroup(
        req.database_name(), GetPgsqlNamespaceId(id.database_oid), id.GetYbTablegroupId(),
        tablespace_id.IsValid() ? tablespace_id.GetYbTablespaceId() : "", metadata,
        req.options().active_sub_transaction_id());
    if (s.ok()) {
      return Status::OK();
    }

    SCHECK(!s.IsAlreadyPresent(), InvalidArgument, "Duplicate tablegroup");

    return STATUS_FORMAT(
        InvalidArgument, "Invalid table definition: $0",
        s.ToString(false /* include_file_and_line */, false /* include_code */));
  }

  Status DropTablegroup(
      const PgDropTablegroupRequestPB& req, PgDropTablegroupResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG_WITH_FUNC(2) << "req: " << req.DebugString();
    const auto id = PgObjectId::FromPB(req.tablegroup_id());
    RETURN_NOT_OK(SetupSessionForDdl(
      req.use_regular_transaction_block(), req.options(), context->GetClientDeadline()));
    const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
        true /* use_transaction */, req.use_regular_transaction_block(),
        context->GetClientDeadline(), IsTxnUsingTableLocks(req.options().is_using_table_locks())));
    const auto status =
        client_.DeleteTablegroup(GetPgsqlTablegroupId(id.database_oid, id.object_oid), metadata,
        req.options().active_sub_transaction_id());
    if (status.IsNotFound()) {
      return Status::OK();
    }
    return status;
  }

  PgClientSessionKind GetSessionKindBasedOnDDLOptions(
      bool ddl_mode, bool ddl_use_regular_transaction_block) const {
    return (ddl_mode && !ddl_use_regular_transaction_block) ? PgClientSessionKind::kDdl
                                                            : PgClientSessionKind::kPlain;
  }

  Status RollbackToSubTransaction(
      const PgRollbackToSubTransactionRequestPB& req, PgRollbackToSubTransactionResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << req.ShortDebugString();
    const auto subtxn_id = req.sub_transaction_id();
    RSTATUS_DCHECK_GE(
        subtxn_id, kMinSubTransactionId,
        InvalidArgument,
        Format("Expected sub_transaction_id to be >= $0", kMinSubTransactionId));

    /*
    * Currently we do not support a transaction block that has both DDL and DML statements (we
    * support it syntactically but not semantically). Thus, when a DDL is encountered in a
    * transaction block, a separate transaction is created for the DDL statement, which is
    * committed at the end of that statement. This is why there are 2 session objects here, one
    * corresponds to the DML transaction, and the other to a possible separate transaction object
    * created for the DDL. However, subtransaction-id increases across both sessions in YSQL.
    *
    * Rolling back to a savepoint from either the DDL or DML txn will only revert any writes/ lock
    * acquisitions done as part of that txn. Consider the below example, the "Rollback to
    * Savepoint 1" will only revert things done in the DDL's context and not the commands that
    * follow Savepoint 1 in the DML's context.
    *
    * -- Start DML
    * ---- Commands...
    * ---- Savepoint 1
    * ---- Commands...
    * ---- Start DDL
    * ------ Commands...
    * ------ Savepoint 2
    * ------ Commands...
    * ------ Rollback to Savepoint 1
    */
    const auto kind = GetSessionKindBasedOnDDLOptions(
        req.has_options() && req.options().ddl_mode(),
        req.has_options() && req.options().ddl_use_regular_transaction_block());

    auto transaction = Transaction(kind);

    if (!transaction) {
      LOG_WITH_PREFIX_AND_FUNC(WARNING)
        << "RollbackToSubTransaction " << subtxn_id
        << " when no distributed transaction of kind"
        << (kind == PgClientSessionKind::kPlain ? "kPlain" : "kDdl")
        << " is running. This can happen if no distributed transaction has been started yet"
        << " e.g., BEGIN; SAVEPOINT a; ROLLBACK TO a;";
      return Status::OK();
    }

    // Before rolling back to req.sub_transaction_id(), set the active sub transaction id to be the
    // same as that in the request. This is necessary because of the following reasoning:
    //
    // ROLLBACK TO SAVEPOINT leads to many calls to YBCRollbackToSubTransaction(), not just 1:
    // Assume the current sub-txns are from 1 to 10 and then a ROLLBACK TO X is performed where
    // X corresponds to sub-txn 5. In this case, 6 calls are made to
    // YBCRollbackToSubTransaction() with sub-txn ids: 5, 10, 9, 8, 7, 6, 5. The first call is
    // made in RollbackToSavepoint() but the latter 5 are redundant and called from the
    // AbortSubTransaction() handling for each sub-txn.
    //
    // Now consider the following scenario:
    //   1. In READ COMMITTED isolation, a new internal sub transaction is created at the start of
    //      each statement (even a ROLLBACK TO). So, a ROLLBACK TO X like above, will first create a
    //      new internal sub-txn 11.
    //   2. YBCRollbackToSubTransaction() will be called 7 times on sub-txn ids:
    //        5, 11, 10, 9, 8, 7, 6
    //
    //  So, it is necessary to first bump the active-sub txn id to 11 and then perform the rollback.
    //  Otherwise, an error will be thrown that the sub-txn doesn't exist when
    //  YBCRollbackToSubTransaction() is called for sub-txn id 11.

    if (req.has_options()) {
      RSTATUS_DCHECK_GE(
          req.options().active_sub_transaction_id(), kMinSubTransactionId,
          InvalidArgument,
          Format("Expected active_sub_transaction_id to be >= $0", kMinSubTransactionId));
      transaction->SetActiveSubTransaction(req.options().active_sub_transaction_id());
    }

    RSTATUS_DCHECK(transaction->HasSubTransaction(subtxn_id), InvalidArgument,
                   Format("Transaction $0 of kind $1 doesn't have sub transaction $2",
                          transaction->id(),
                          kind == PgClientSessionKind::kPlain ? "kPlain" : "kDdl",
                          subtxn_id));
    const auto deadline = context->GetClientDeadline();
    RETURN_NOT_OK(transaction->RollbackToSubTransaction(subtxn_id, deadline));

    if (YsqlDdlSavepointEnabled() &&
        req.has_options() && req.options().ddl_mode() &&
        req.options().ddl_use_regular_transaction_block() &&
        // Ensure that we have executed a DDL in this transaction block.
        // We can arrive here in case a transaction aborts due to a failure
        // during processing a DDL. In that case, ddl_mode in the request will
        // be true since we start the ddl_mode upon receiving a DDL statement.
        !ddl_txn_metadata_.transaction_id.IsNil()) {
      RSTATUS_DCHECK(ddl_txn_metadata_.transaction_id == transaction->id(), IllegalState,
                     Format("Unexpected DDL transaction metadata found. Expected: $0, found: $1",
                            transaction->id(), ddl_txn_metadata_.transaction_id));
      RETURN_NOT_OK(client_.RollbackDocdbSchemaToSubtxn(ddl_txn_metadata_, subtxn_id));
      RETURN_NOT_OK(
          client_.WaitForRollbackDocdbSchemaToSubtxnToFinish(ddl_txn_metadata_, subtxn_id));
    }

    return ReleaseObjectLocksIfNecessary(transaction, kind, deadline, subtxn_id);
  }

  Status FinishTransaction(
      const PgFinishTransactionRequestPB& req, PgFinishTransactionResponsePB* resp,
      rpc::RpcContext* context) {
    saved_priority_.reset();
    const bool is_ddl = req.has_ddl_mode();
    const bool is_commit = req.commit();
    const bool ddl_use_regular_transaction_block =
        is_ddl && req.ddl_mode().use_regular_transaction_block();
    const auto kind = GetSessionKindBasedOnDDLOptions(is_ddl, ddl_use_regular_transaction_block);
    const auto deadline = context->GetClientDeadline();
    auto& txn = GetSessionData(kind).transaction;
    if (!txn) {
      VLOG_WITH_PREFIX_AND_FUNC(2)
          << "ddl: " << is_ddl << ", " << (is_commit ? "commit" : "abort")
          << ", ddl_use_regular_transaction_block: " << ddl_use_regular_transaction_block
          << ", no running distributed transaction";
      if (is_commit || is_ddl || !IsObjectLockingEnabled()) {
        return ReleaseObjectLocksIfNecessary(txn, kind, deadline);
      }
      // When object locking is enabled, prevent re-use of plain docdb txn is case of abort.
      if (!transaction_provider_.HasNextTxnForPlain()) {
        return Status::OK();
      }
      txn = transaction_provider_.Take<PgClientSessionKind::kPlain>(
          TransactionFullLocality::RegionLocal(), deadline);
      VLOG_WITH_PREFIX_AND_FUNC(1) << "Consuming re-usable kPlain txn " << txn->id();
    }

    // If this transaction executed a DDL statement, then cleanup the ddl_txn_metadata_ post
    // finishing. This is applicable to both separate DDL transactions or regular transactions with
    // txn ddl enabled.
    bool requires_ddl_txn_metadata_cleanup = ddl_txn_metadata_.transaction_id == txn->id();
    client::YBTransactionPtr txn_value;
    txn.swap(txn_value);
    Session(kind)->SetTransaction(nullptr);
    auto s = DoFinishTransaction(req, deadline, txn_value, kind);
    if (requires_ddl_txn_metadata_cleanup) {
      ddl_txn_metadata_ = TransactionMetadata();
    }
    return s;
  }

  Status CleanupObjectLocks() {
    auto deadline = CoarseMonoClock::Now() +
                    MonoDelta::FromMilliseconds(FLAGS_tserver_yb_client_default_timeout_ms);
    return MergeStatus(
        ReleaseObjectLocksIfNecessary(
            GetSessionData(PgClientSessionKind::kPlain).transaction, PgClientSessionKind::kPlain,
            deadline),
        ReleaseObjectLocksIfNecessary(
            GetSessionData(PgClientSessionKind::kDdl).transaction, PgClientSessionKind::kDdl,
            deadline));
  }

  void Perform(
      PgPerformRequestPB& req, PgPerformResponsePB& resp, rpc::RpcContext&& context,
      const PgTablesQueryResult& tables) {
    auto query = RpcQuery<PerformQueryTraits>::MakeShared(
        id_, pid_, req, resp, std::move(context), table_cache());
    auto& ctx = query->context;
    const auto status = DoPerform(tables, query->SharedData(), ctx.GetClientDeadline(), &ctx);
    if (!status.ok()) {
      query->SendErrorResponse(status);
    }
  }

  Status InsertSequenceTuple(
      const PgInsertSequenceTupleRequestPB& req, PgInsertSequenceTupleResponsePB* resp,
      rpc::RpcContext* context) {
    PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
    auto result = table_cache().Get(table_oid.GetYbTableId());
    if (!result.ok()) {
      RETURN_NOT_OK(CreateSequencesDataTable(&client_, context->GetClientDeadline()));
      // Try one more time.
      result = table_cache().Get(table_oid.GetYbTableId());
    }
    auto table = VERIFY_RESULT(std::move(result));

    auto psql_write(client::YBPgsqlWriteOp::NewInsert(table, &context->sidecars()));

    auto write_request = psql_write->mutable_request();
    RETURN_NOT_OK(SetCatalogVersion(req, write_request));
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

    PgsqlColumnValuePB* column_value = write_request->add_column_values();
    column_value->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
    column_value->mutable_expr()->mutable_value()->set_int64_value(req.last_val());

    column_value = write_request->add_column_values();
    column_value->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    column_value->mutable_expr()->mutable_value()->set_bool_value(req.is_called());

    auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    auto s = session->TEST_ApplyAndFlush(psql_write);
    if (!s.ok() || psql_write->response().status() ==
        PgsqlResponsePB_RequestStatus::PgsqlResponsePB_RequestStatus_PGSQL_STATUS_OK) {
      return s;
    }
    return STATUS_FORMAT(
        InternalError, "Unknown error while trying to insert into sequences_data DocDB table: $0",
        PgsqlResponsePB::RequestStatus_Name(psql_write->response().status()));
  }

  Status UpdateSequenceTuple(
      const PgUpdateSequenceTupleRequestPB& req, PgUpdateSequenceTupleResponsePB* resp,
      rpc::RpcContext* context) {
    RETURN_NOT_OK(ValidateSequenceModificationFunctionForXCluster(req.db_oid()));

    PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
    auto table = VERIFY_RESULT(table_cache().Get(table_oid.GetYbTableId()));

    auto psql_write = client::YBPgsqlWriteOp::NewUpdate(table, &context->sidecars());

    auto write_request = psql_write->mutable_request();
    RETURN_NOT_OK(SetCatalogVersion(req, write_request));
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

    PgsqlColumnValuePB* column_value = write_request->add_column_new_values();
    column_value->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
    column_value->mutable_expr()->mutable_value()->set_int64_value(req.last_val());

    column_value = write_request->add_column_new_values();
    column_value->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    column_value->mutable_expr()->mutable_value()->set_bool_value(req.is_called());

    auto where_pb = write_request->mutable_where_expr()->mutable_condition();

    if (req.has_expected()) {
      // WHERE clause => WHERE last_val == expected_last_val AND is_called == expected_is_called.
      where_pb->set_op(QL_OP_AND);

      auto cond = where_pb->add_operands()->mutable_condition();
      cond->set_op(QL_OP_EQUAL);
      cond->add_operands()->set_column_id(table->schema().ColumnId(kPgSequenceLastValueColIdx));
      cond->add_operands()->mutable_value()->set_int64_value(req.expected_last_val());

      cond = where_pb->add_operands()->mutable_condition();
      cond->set_op(QL_OP_EQUAL);
      cond->add_operands()->set_column_id(table->schema().ColumnId(kPgSequenceIsCalledColIdx));
      cond->add_operands()->mutable_value()->set_bool_value(req.expected_is_called());
    } else {
      where_pb->set_op(QL_OP_EXISTS);
    }

    // For compatibility set deprecated column_refs
    write_request->mutable_column_refs()->add_ids(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    write_request->mutable_column_refs()->add_ids(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    // Same values, to be consumed by current TServers
    write_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    write_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));

    auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(session->TEST_ApplyAndFlush(psql_write));
    resp->set_skipped(psql_write->response().skipped());
    return Status::OK();
  }

  size_t SaveData(const RefCntBuffer& buffer, WriteBuffer&& sidecars) {
    std::lock_guard lock(pending_data_mutex_);
    for (size_t i = 0; i != pending_data_.size(); ++i) {
      if (pending_data_[i].empty()) {
        pending_data_[i].AddBlock(buffer, 0);
        pending_data_[i].Take(&sidecars);
        return i;
      }
    }
    pending_data_.emplace_back(0);
    pending_data_.back().AddBlock(buffer, 0);
    pending_data_.back().Take(&sidecars);
    return pending_data_.size() - 1;
  }

  Status FetchData(
      const PgFetchDataRequestPB& req, PgFetchDataResponsePB* resp,
      rpc::RpcContext* context) {
    size_t data_id = req.data_id();
    std::lock_guard lock(pending_data_mutex_);
    if (data_id >= pending_data_.size() || pending_data_[data_id].empty()) {
      return STATUS_FORMAT(NotFound, "Data $0 not found for session $1", data_id, id_);
    }
    context->sidecars().Start().Take(&pending_data_[data_id]);
    return Status::OK();
  }

  Status FetchSequenceTuple(
      const PgFetchSequenceTupleRequestPB& req, PgFetchSequenceTupleResponsePB* resp,
      rpc::RpcContext* context) {
    using pggate::PgDocData;
    using pggate::PgWireDataHeader;

    RETURN_NOT_OK(ValidateSequenceModificationFunctionForXCluster(req.db_oid()));

    const auto inc_by = req.inc_by();
    std::shared_ptr<PgSequenceCache::Entry> cache_entry;
    if (FLAGS_ysql_sequence_cache_method == "server") {
      const PgObjectId sequence_id(
          narrow_cast<uint32_t>(req.db_oid()), narrow_cast<uint32_t>(req.seq_oid()));
      cache_entry = VERIFY_RESULT(
          sequence_cache().GetWhenAvailable(sequence_id, ToSteady(context->GetClientDeadline())));
      DCHECK(cache_entry);
    }
    // On exit we should notify a waiter for this sequence id that it is available.
    auto se = cache_entry
        ? MakeOptionalScopeExit([&cache_entry] { cache_entry->NotifyWaiter(); }) : std::nullopt;

    // If the cache has a value, return immediately.
    if (auto sequence_value = cache_entry ? cache_entry->GetValueIfCached(inc_by) : std::nullopt;
        sequence_value) {
      // Since the tserver cache is enabled, the connection cache size is implicitly 1 so the
      // first and last value are the same.
      resp->set_first_value(*sequence_value);
      resp->set_last_value(*sequence_value);
      return Status::OK();
    }

    PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
    auto table = VERIFY_RESULT(table_cache().Get(table_oid.GetYbTableId()));

    auto psql_write = client::YBPgsqlWriteOp::NewFetchSequence(table, &context->sidecars());

    auto* write_request = psql_write->mutable_request();
    RETURN_NOT_OK(SetCatalogVersion(req, write_request));
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
    write_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

    auto* fetch_sequence_params = write_request->mutable_fetch_sequence_params();
    fetch_sequence_params->set_fetch_count(req.fetch_count());
    fetch_sequence_params->set_inc_by(inc_by);
    fetch_sequence_params->set_min_value(req.min_value());
    fetch_sequence_params->set_max_value(req.max_value());
    fetch_sequence_params->set_cycle(req.cycle());

    write_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    write_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));

    auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
    session->Apply(std::move(psql_write));
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    auto fetch_status = session->TEST_FlushAndGetOpsErrors();
    RETURN_NOT_OK(CombineErrorsToStatus(fetch_status.errors, fetch_status.status));

    // Expect exactly two rows on success: sequence value range start and end, each as a single
    // value in its own row. Even if a single value is fetched, both should be equal. If no value is
    // fetched, the response should come with an error.
    Slice cursor;
    int64_t row_count;
    PgDocData::LoadCache(context->sidecars().GetFirst(), &row_count, &cursor);
    if (row_count != 2) {
      return STATUS_SUBSTITUTE(
        InternalError, "Invalid row count has been fetched from sequence $0", req.seq_oid());
    }

    // Get the range start
    if (PgDocData::ReadHeaderIsNull(&cursor)) {
      return STATUS_SUBSTITUTE(InternalError,
                              "Invalid value range start has been fetched from sequence $0",
                              req.seq_oid());
    }
    auto first_value = PgDocData::ReadNumber<int64_t>(&cursor);

    // Get the range end
    if (PgDocData::ReadHeaderIsNull(&cursor)) {
      return STATUS_SUBSTITUTE(InternalError,
                              "Invalid value range end has been fetched from sequence $0",
                              req.seq_oid());
    }
    auto last_value = PgDocData::ReadNumber<int64_t>(&cursor);

    if (cache_entry) {
      cache_entry->SetRange(first_value, last_value);
      auto optional_sequence_value = cache_entry->GetValueIfCached(inc_by);

      RSTATUS_DCHECK(
          optional_sequence_value.has_value(), InternalError,
          "Value for sequence $0 was not found.", req.seq_oid());
      // Since the tserver cache is enabled, the connection cache size is implicitly 1 so the first
      // and last value are the same.
      last_value = first_value = *optional_sequence_value;
    }

    resp->set_first_value(first_value);
    resp->set_last_value(last_value);
    return Status::OK();
  }

  Status ReadSequenceTuple(
      const PgReadSequenceTupleRequestPB& req, PgReadSequenceTupleResponsePB* resp,
      rpc::RpcContext* context) {
    using pggate::PgDocData;
    using pggate::PgWireDataHeader;
    VLOG(5) << Format("Servicing ReadSequenceTuple RPC: $0", req.ShortDebugString());
    PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
    auto table = VERIFY_RESULT(table_cache().Get(table_oid.GetYbTableId()));

    auto psql_read = client::YBPgsqlReadOp::NewSelect(table, &context->sidecars());

    auto read_request = psql_read->mutable_request();
    RETURN_NOT_OK(SetCatalogVersion(req, read_request));
    read_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
    read_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

    read_request->add_targets()->set_column_id(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    read_request->add_targets()->set_column_id(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));

    // For compatibility set deprecated column_refs
    read_request->mutable_column_refs()->add_ids(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    read_request->mutable_column_refs()->add_ids(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    // Same values, to be consumed by current TServers
    read_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceLastValueColIdx));
    read_request->add_col_refs()->set_column_id(
        table->schema().ColumnId(kPgSequenceIsCalledColIdx));
    std::optional<uint64_t> read_time = std::nullopt;
    if (req.read_time() != 0) {
      read_time = req.read_time();
    }

    auto& session =
        EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline(), read_time);
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(session->TEST_ApplyAndFlush(psql_read));

    CHECK_EQ(*psql_read->sidecar_index(), 0);

    Slice cursor;
    int64_t row_count = 0;
    PgDocData::LoadCache(context->sidecars().GetFirst(), &row_count, &cursor);
    if (row_count == 0) {
      return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", req.seq_oid());
    }
    if (PgDocData::ReadHeaderIsNull(&cursor)) {
      return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", req.seq_oid());
    }
    auto last_val = PgDocData::ReadNumber<int64_t>(&cursor);
    resp->set_last_val(last_val);

    if (PgDocData::ReadHeaderIsNull(&cursor)) {
      return STATUS_SUBSTITUTE(NotFound, "Unable to find relation for sequence $0", req.seq_oid());
    }
    auto is_called = PgDocData::ReadNumber<bool>(&cursor);
    resp->set_is_called(is_called);
    return Status::OK();
  }

  Status DeleteSequenceTuple(
      const PgDeleteSequenceTupleRequestPB& req, PgDeleteSequenceTupleResponsePB* resp,
      rpc::RpcContext* context) {
    PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
    auto table = VERIFY_RESULT(table_cache().Get(table_oid.GetYbTableId()));

    auto psql_delete(client::YBPgsqlWriteOp::NewDelete(table, &context->sidecars()));
    auto delete_request = psql_delete->mutable_request();

    delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
    delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

    auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    return session->TEST_ApplyAndFlush(std::move(psql_delete));
  }

  Status DeleteDBSequences(
      const PgDeleteDBSequencesRequestPB& req, PgDeleteDBSequencesResponsePB* resp,
      rpc::RpcContext* context) {
    PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
    auto table_res = table_cache().Get(table_oid.GetYbTableId());
    if (!table_res.ok()) {
      // Sequence table is not yet created.
      return Status::OK();
    }

    auto table = std::move(*table_res);
    if (table == nullptr) {
      return Status::OK();
    }

    auto psql_delete = client::YBPgsqlWriteOp::NewDelete(table, &context->sidecars());
    auto delete_request = psql_delete->mutable_request();

    delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());

    auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    return session->TEST_ApplyAndFlush(std::move(psql_delete));
  }

  void GetTableKeyRanges(
      PgGetTableKeyRangesRequestPB const& req,
      PgGetTableKeyRangesResponsePB* resp, rpc::RpcContext&& context) {
    const auto table = table_cache().Get(PgObjectId::GetYbTableIdFromPB(req.table_id()));
    resp->set_current_ht(clock()->Now().ToUint64());
    if (!table.ok()) {
      StatusToPB(table.status(), resp->mutable_status());
      context.RespondSuccess();
      return;
    }

    auto session = EnsureSession(PgClientSessionKind::kPlain, context.GetClientDeadline());

    // TODO(get_table_key_ranges): consider using separate GetTabletKeyRanges RPC to tablet leader
    // instead of passing through YBSession.
    auto psql_read = client::YBPgsqlReadOp::NewSelect(*table, &context.sidecars());

    auto* read_request = psql_read->mutable_request();

    read_request->set_limit(req.max_num_ranges());
    read_request->set_is_forward_scan(req.is_forward());
    auto* embedded_req = read_request->mutable_get_tablet_key_ranges_request();

    // IsInclusive is actually ignored by Tablet::GetTabletKeyRanges, and it always treats both
    // boundaries as inclusive. But we are setting it here to avoid check failures inside
    // YBPgsqlReadOp.
    if (!req.lower_bound_key().empty()) {
      read_request->mutable_lower_bound()->set_is_inclusive(true);
      for (auto* dest_key :
           {embedded_req->mutable_lower_bound_key(),
            read_request->mutable_lower_bound()->mutable_key(),
            read_request->mutable_partition_key()}) {
        dest_key->assign(req.lower_bound_key());
      }
    }
    if (!req.upper_bound_key().empty()) {
      read_request->mutable_upper_bound()->set_is_inclusive(true);
      for (auto* dest_key :
           {embedded_req->mutable_upper_bound_key(),
            read_request->mutable_upper_bound()->mutable_key()}) {
        dest_key->assign(req.upper_bound_key());
      }
    }
    embedded_req->set_range_size_bytes(req.range_size_bytes());
    embedded_req->set_max_key_length(req.max_key_length());

    session->Apply(psql_read);
    session->FlushAsync([callback = MakeRpcOperationCompletionCallback(
                             std::move(context), resp, /* clock = */ nullptr)](
                            client::FlushStatus* flush_status) {
      callback(CombineErrorsToStatus(flush_status->errors, flush_status->status));
    });
  }

  Status DoHandleSharedExchangeQuery(PerformQueryDataPtr&& data, CoarseTimePoint deadline) {
    boost::container::small_vector<TableId, 4> table_ids;
    PreparePgTablesQuery(data->req, table_ids);
    if (PREDICT_FALSE(FLAGS_TEST_request_unknown_tables_during_perform)) {
      table_ids.insert(
          table_ids.end(), { GetPgsqlTableId(0, 0), GetPgsqlTableId(0, 1), GetPgsqlTableId(0, 2) });
    }
    auto tables_future = GetTablesAsync(table_cache(), table_ids);
    RETURN_NOT_OK(Wait(tables_future, ToSteady(deadline)));
    return DoPerform(tables_future.get(), data, deadline);
  }

  Status DoHandleSharedExchangeQuery(ObjectLockQueryDataPtr&& data, CoarseTimePoint deadline) {
    return DoAcquireObjectLock(data, deadline);
  }

  template <QueryTraitsType T, class... Args>
  Status DoHandleSharedExchangeQuery(
      SharedExchangeQuery<T>& query, uint8_t* input, size_t size, uint64_t session_id,
      pid_t session_pid, Args&&... args) {
    auto [data, deadline] = VERIFY_RESULT(
        query.ParseRequest(input, size, session_id, std::forward<Args>(args)...));
    auto wait_state = yb::ash::WaitStateInfo::CreateIfAshIsEnabled<yb::ash::WaitStateInfo>();
    ADOPT_WAIT_STATE(wait_state);
    SCOPED_WAIT_STATUS(OnCpu_Active);
    auto track_guard = wait_state
        ? TrackSharedMemoryPgMethodExecution<T>(wait_state, query.ash_metadata())
        : std::nullopt;
    return DoHandleSharedExchangeQuery(std::move(data), deadline);
  }

  template <QueryTraitsType T, class... Args>
  void HandleSharedExchangeQuery(
      SharedExchange& exchange, uint8_t* input, size_t size, Args&&... args) {
    auto query = SharedExchangeQuery<T>::MakeShared(
        SharedSessionFromThis(), exchange, stats_exchange_response_size());
    const auto status = DoHandleSharedExchangeQuery(
        *query, input, size, id(), pid_, std::forward<Args>(args)...);
    if (!status.ok()) {
      query->SendErrorResponse(status);
    }
  }

  void ProcessSharedRequest(size_t size, SharedExchange* exchange) {
    auto input = to_uchar_ptr(exchange->Obtain(size));
    const auto req_type_id = *reinterpret_cast<const uint8_t *>(input);
    input += sizeof(req_type_id);
    size -= sizeof(req_type_id);
    auto req_type = static_cast<tserver::PgSharedExchangeReqType>(req_type_id);
    switch (req_type) {
      case tserver::PgSharedExchangeReqType::PERFORM: {
        return HandleSharedExchangeQuery<PerformQueryTraits>(*exchange, input, size, table_cache());
      }
      case tserver::PgSharedExchangeReqType::ACQUIRE_OBJECT_LOCK: {
        return HandleSharedExchangeQuery<ObjectLockQueryTraits>(*exchange, input, size);
      }
      case tserver::PgSharedExchangeReqType_INT_MIN_SENTINEL_DO_NOT_USE_: [[fallthrough]];
      case tserver::PgSharedExchangeReqType_INT_MAX_SENTINEL_DO_NOT_USE_: break;
    }
    LOG_WITH_PREFIX_AND_FUNC(DFATAL)
        << "Unexpected request type with id: " << req_type_id
        << ", min allowed: " << tserver::PgSharedExchangeReqType_MIN
        << ", max allowed: " << tserver::PgSharedExchangeReqType_MAX
        << ". Would lead to pg backend timing out/entering a stuck state.";
    FATAL_INVALID_PB_ENUM_VALUE(tserver::PgSharedExchangeReqType, req_type);
  }

  std::pair<uint64_t, std::byte*> ObtainBigSharedMemorySegment(size_t size) {
    std::pair<uint64_t, std::byte*> result;
    bool schedule_expiration_task = false;
    {
      std::lock_guard lock(big_shared_mem_mutex_);
      if (big_shared_mem_handle_ && big_shared_mem_handle_.size() >= size) {
        auto& in_use = InUseAtomic(big_shared_mem_handle_);
        LOG_IF_WITH_PREFIX(DFATAL, in_use.load()) << "Big shared mem segment still in use";
        in_use.store(true);
      } else {
        auto new_handle = shared_mem_pool().Obtain(size + sizeof(std::atomic<bool>));
        if (!new_handle) {
          return {0, nullptr};
        }
        new (new_handle.address()) std::atomic<bool>(true);
        new_handle.TruncateLeft(sizeof(std::atomic<bool>));
        big_shared_mem_handle_ = std::move(new_handle);
      }
      result = {big_shared_mem_handle_.id(), big_shared_mem_handle_.address()};
      last_big_shared_memory_access_ = CoarseMonoClock::now();
      if (!big_shared_mem_expiration_task_scheduled_) {
        big_shared_mem_expiration_task_scheduled_ = true;
        schedule_expiration_task = true;
      }
    }
    if (schedule_expiration_task) {
      ScheduleBigSharedMemExpirationCheck(
          FLAGS_big_shared_memory_segment_session_expiration_time_ms * 1ms);
    }
    return result;
  }

  Status AcquireAdvisoryLock(
      const PgAcquireAdvisoryLockRequestPB& req, PgAcquireAdvisoryLockResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG(2) << "Servicing AcquireAdvisoryLock: " << req.ShortDebugString();
    SCHECK(FLAGS_ysql_yb_enable_advisory_locks, NotSupported, "advisory locks are disabled");
    const auto deadline = context->GetClientDeadline();
    auto* primary_session_data = &GetSessionData(GetSessionKindBasedOnDDLOptions(
        req.has_options() && req.options().ddl_mode(),
        req.has_options() && req.options().ddl_use_regular_transaction_block()));
    auto* background_session_data = &GetSessionData(PgClientSessionKind::kPgSession);
    if (req.session()) {
      // Update subtxn of host transaction as it is required for retries with statement rollbacks.
      if (const auto& txn = primary_session_data->transaction; txn) {
        txn->SetActiveSubTransaction(req.options().active_sub_transaction_id());
      }
      std::swap(primary_session_data, background_session_data);
      const auto& pg_session_data = VERIFY_RESULT_REF(BeginPgSessionLevelTxnIfNecessary(deadline));
      DCHECK(&pg_session_data == primary_session_data) << "Expected session of kind kPgSession.";
    } else {
      RETURN_NOT_OK(SetupSession(req.options(), deadline));
    }
    RSTATUS_DCHECK(
        primary_session_data->session && primary_session_data->transaction,
        IllegalState, "Transaction on primary session is required.");

    auto& session = *primary_session_data->session;
    // Set background transaction to achieve the following:
    // - When acquiring a session advisory lock, the session level txn should ignore conflicts with
    //   the current active regular/plain txn, if any.
    // - When acquiring a txn advisory lock, the regular/plain txn should ignore conflicts with the
    //   session level transaction, if exists.
    if (const auto& background_txn = background_session_data->transaction; background_txn) {
      auto background_txn_meta_res = background_txn->GetMetadata(deadline).get();
      RETURN_NOT_OK(background_txn_meta_res);
      session.SetBatcherBackgroundTransactionMeta(*background_txn_meta_res);
    }

    auto& txn = *primary_session_data->transaction;
    for (const auto& lock : req.locks()) {
      auto lock_op = VERIFY_RESULT(advisory_locks_table().MakeLockOp(
          MakeAdvisoryLockId(req.db_oid(), lock.lock_id()), lock.lock_mode(), req.wait()));
      VLOG(4) << "Applying lock op: " << lock_op;
      session.Apply(lock_op);
    }
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    auto flush_status = session.FlushFuture().get();
    auto status = CombineErrorsToStatus(flush_status.errors, flush_status.status);
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Acquired advisory locks with transaction " << txn.id() << " status: " << status;
    return status;
  }

  Status ReleaseAdvisoryLock(
      const PgReleaseAdvisoryLockRequestPB& req, PgReleaseAdvisoryLockResponsePB* resp,
      rpc::RpcContext* context) {
    VLOG(2) << "Servicing ReleaseAdvisoryLock: " << req.ShortDebugString();
    // Release Advisory lock api is only invoked for session advisory locks.
    const auto& session_data =
        VERIFY_RESULT_REF(BeginPgSessionLevelTxnIfNecessary(context->GetClientDeadline()));
    DCHECK(session_data.session && session_data.transaction)
        << "Expected non null session and transaction for PgClientSessionKind::kPgSession";
    auto& session = *session_data.session;
    auto& txn = *session_data.transaction;
    if (const auto& locks = req.locks(); !locks.empty()) {
      for (const auto& lock : locks) {
        session.Apply(VERIFY_RESULT(advisory_locks_table().MakeUnlockOp(
            MakeAdvisoryLockId(req.db_oid(), lock.lock_id()), lock.lock_mode())));
      }
    } else {
      session.Apply(VERIFY_RESULT(advisory_locks_table().MakeUnlockAllOp(req.db_oid())));
    }
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    auto flush_status = session.FlushFuture().get();
    auto status = CombineErrorsToStatus(flush_status.errors, flush_status.status);
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Releasing advisory locks with transaction " << txn.id() << " status: " << status;
    return status;
  }

  Status DoAcquireObjectLock(const ObjectLockQueryDataPtr& data, CoarseTimePoint deadline) {
    const auto& options = data->req.options();
    RSTATUS_DCHECK(
        options.is_using_table_locks(), IllegalState, "Table Locking feature not enabled.");
    auto setup_session_result = VERIFY_RESULT(SetupSession(
        options, deadline, GetInTxnLimit(options, clock().get())));
    RSTATUS_DCHECK(
        setup_session_result.is_plain ||
        (options.ddl_mode() && setup_session_result.session_data.transaction),
        IllegalState, "Expected kPlain/kDdl session");
    if (setup_session_result.is_plain && setup_session_result.session_data.transaction) {
      RETURN_NOT_OK(setup_session_result.session_data.transaction->GetMetadata(deadline).get());
    }
    auto locality = GetTargetTransactionLocality(data->req);
    auto txn_meta_res = setup_session_result.session_data.transaction
        ? setup_session_result.session_data.transaction->GetMetadata(deadline).get()
        : NextObjectLockingTxnMeta(locality, deadline);
    RETURN_NOT_OK(txn_meta_res);
    const auto lock_type = static_cast<TableLockType>(data->req.lock_type());
    VLOG_WITH_PREFIX_AND_FUNC(1) << "txn_id " << txn_meta_res->transaction_id << " subtxn_id "
                                 << options.active_sub_transaction_id()
                                 << " lock_type: " << AsString(lock_type)
                                 << " req: " << data->req.ShortDebugString();
    DEBUG_ONLY_TEST_SYNC_POINT_CALLBACK(
        "PgClientSession::Impl::DoAcquireObjectLock", &txn_meta_res->transaction_id);

    auto callback = [data](const Status& s) {
      client::FlushStatus flush_status;
      flush_status.status = s;
      data->FlushDone(&flush_status);
    };
    if (IsTableLockTypeGlobal(lock_type)) {
      if (setup_session_result.is_plain) {
        plain_session_has_exclusive_object_locks_.store(true);
      }
      ts_lock_manager()->TrackDeadlineForGlobalAcquire(
          txn_meta_res->transaction_id, options.active_sub_transaction_id(), deadline);
      auto lock_req = AcquireRequestFor<master::AcquireObjectLocksGlobalRequestPB>(
          instance_uuid(), txn_meta_res->transaction_id, options.active_sub_transaction_id(),
          data->req.lock_oid(), lock_type, lease_epoch_, context_.clock.get(), deadline,
          txn_meta_res->status_tablet);
      client_.AcquireObjectLocksGlobalAsync(
          lock_req, std::move(callback), deadline,
          [txn = setup_session_result.session_data.transaction]() -> Status {
            RETURN_NOT_OK(txn->metadata());
            return Status::OK();
          });
      return Status::OK();
    }
    auto lock_req = AcquireRequestFor<tserver::AcquireObjectLockRequestPB>(
        instance_uuid(), txn_meta_res->transaction_id, options.active_sub_transaction_id(),
        data->req.lock_oid(), lock_type, lease_epoch_, context_.clock.get(), deadline,
        txn_meta_res->status_tablet);
    AcquireObjectLockLocallyWithRetries(
        ts_lock_manager(), std::move(lock_req), deadline, std::move(callback),
        [session_impl = this, txn = setup_session_result.session_data.transaction, locality]
            (CoarseTimePoint deadline) -> Status {
          if (txn) {
            RETURN_NOT_OK(txn->metadata());
          } else {
            RETURN_NOT_OK(session_impl->NextObjectLockingTxnMeta(locality, deadline));
          }
          return Status::OK();
        });
    return Status::OK();
  }

  void AcquireObjectLock(
      const PgAcquireObjectLockRequestPB& req, PgAcquireObjectLockResponsePB* resp,
      yb::rpc::RpcContext&& context) {
    auto query =
        RpcQuery<ObjectLockQueryTraits>::MakeShared(id_, pid_, req, *resp, std::move(context));
    const auto s = DoAcquireObjectLock(
        query->SharedData(), query->context.GetClientDeadline());
    if (!s.ok()) {
      query->SendErrorResponse(s);
    }
  }

  void StartShutdown(bool pg_service_shutting_down) {
    VLOG(2) << "StartShutdown for session id: " << id();
    if (!pg_service_shutting_down) {
      WARN_NOT_OK(CleanupObjectLocks(), "Error cleaning up object locks");

      // Abort txns attached to this session
      for (const auto kind :
           {PgClientSessionKind::kPlain, PgClientSessionKind::kDdl,
            PgClientSessionKind::kPgSession}) {
        const auto& txn = Transaction(kind);
        if (!txn) {
          continue;
        }
        LOG(INFO) << "Aborting txn of kind " << yb::ToString(kind) << " with id " << txn->id()
                  << " belonging to expired PG session ID: " << id();
        txn->Abort();
      }
    }
    big_shared_mem_expiration_task_.StartShutdown();
  }

  bool ReadyToShutdown() {
    return big_shared_mem_expiration_task_.ReadyToShutdown();
  }

  void CompleteShutdown() {
    big_shared_mem_expiration_task_.CompleteShutdown();
  }

 private:
  const TserverXClusterContextIf* xcluster_context() const {
    return context_.xcluster_context;
  }

  YsqlAdvisoryLocksTable& advisory_locks_table() const {
    return context_.advisory_locks_table;
  }

  PgMutationCounter* pg_node_level_mutation_counter() const {
    return context_.pg_node_level_mutation_counter;
  }

  const scoped_refptr<ClockBase>& clock() const {
    return context_.clock;
  }

  PgTableCache& table_cache() const {
    return context_.table_cache;
  }

  PgResponseCache& response_cache() const {
    return context_.response_cache;
  }

  PgSequenceCache& sequence_cache() const {
    return context_.sequence_cache;
  }

  PgSharedMemoryPool& shared_mem_pool() const {
    return context_.shared_mem_pool;
  }

  const EventStatsPtr& stats_exchange_response_size() const {
    return context_.metrics.exchange_response_size;
  }

  const tserver::TSLocalLockManagerPtr& ts_lock_manager() const {
    return ts_lock_manager_;
  }

  const std::string instance_uuid() const {
    return context_.instance_uuid;
  }

  PrefixLogger LogPrefix() const { return PrefixLogger{id_, pid_}; }

  Status DdlAtomicityFinishTransaction(
      const client::YBTransactionPtr& txn, PgClientSessionKind used_session_kind,
      bool has_docdb_schema_changes, const TransactionMetadata* metadata,
      std::optional<bool> commit, CoarseTimePoint deadline) {
    // If this transaction was DDL that had DocDB syscatalog changes, then the YB-Master may have
    // any operations postponed to the end of transaction. If the status is known
    // (commit.has_value() is true), then report the status of the transaction and wait for the
    // post-processing by YB-Master to end.
    if (YsqlDdlRollbackEnabled() && metadata && !metadata->transaction_id.IsNil()) {
      if (has_docdb_schema_changes ) {
        if (commit.has_value() && FLAGS_report_ysql_ddl_txn_status_to_master) {
          // If we failed to report the status of this DDL transaction, we can just log and ignore
          // it, as the poller in the YB-Master will figure out the status of this transaction using
          // the transaction status tablet and PG catalog.
          WARN_NOT_OK(client_.ReportYsqlDdlTxnStatus(*metadata, *commit),
                      Format("Sending ReportYsqlDdlTxnStatus call of $0 failed", *commit));
        }

        if (FLAGS_ysql_ddl_transaction_wait_for_ddl_verification) {
          // Wait for DDL verification to end. This may include actions such as a) removing an added
          // column in case of ADD COLUMN abort b) dropping a column marked for deletion in case of
          // DROP COLUMN commit. c) removing DELETE marker on a column if DROP COLUMN aborted d)
          // rollback changes to table/column names in case of txn abort. d) dropping a table in
          // case of DROP TABLE commit. All the above actions take place only after the transaction
          // is completed.
          // Note that this is called even when the DDL transaction status is not known
          // (commit.has_value() is false), the purpose is to use the side effect of
          // WaitForDdlVerificationToFinish to trigger the start of a background task to
          // complete the DDL transaction at the DocDB side.
          WARN_NOT_OK(client_.WaitForDdlVerificationToFinish(*metadata),
                      "WaitForDdlVerificationToFinish call failed");
        }
      }
    }
    // Notify master/local tserver's lock manager of the release. We expect 3 types of transactions
    // here.
    // 1. transactions with docdb schema changes tracked by master's ddl verifier.
    // 2. transactions without docdb schema changes (hence not tracked by master's ddl verification
    //    task) but with exclusive object locks. For instance, as part of CREATE INDEX, we launch
    //    a DDL that changes the permissions of the index and increments the catalog version.
    // 3. DMLs without any exclusive locks
    return ReleaseObjectLocksIfNecessary(txn, used_session_kind, deadline);
  }

  template <class DataPtr, class Options>
  Status ValidateRequestForXCluster(const Options& options, const DataPtr& data) {
    if (options.yb_non_ddl_txn_for_sys_tables_allowed() || !xcluster_context()) {
      return Status::OK();
    }
    bool is_automatic_target = xcluster_context()->GetXClusterRole(options.namespace_id()) ==
                               XClusterNamespaceInfoPB::AUTOMATIC_TARGET;
    if (is_automatic_target && FLAGS_xcluster_target_manual_override) {
      return Status::OK();
    }

    if (options.ddl_mode()) {
      // In xCluster Automatic mode, DDLs are not allowed on the target database unless it is run
      // via the target poller or in forced manual mode.
      if (is_automatic_target && !options.xcluster_target_ddl_bypass()) {
        // Force catalog modifications is set for temp table, and in-place materialized view
        // refresh. These DDLs are safe to perform on xCluster target in automatic mode.
        for (const auto& op : data->req.ops()) {
          SCHECK(
              !op.has_write(), IllegalState,
              "DDL operations are forbidden on a database that is the target of automatic mode "
              "xCluster replication");
        }
      }

      return Status::OK();
    }

    // DMLs.
    if (xcluster_context()->IsReadOnlyMode(options.namespace_id())) {
      for (const auto& op : data->req.ops()) {
        if (op.has_write() && !op.write().is_backfill()) {
          TEST_SYNC_POINT_CALLBACK("WriteDetectedOnXClusterReadOnlyModeTarget", nullptr);
          // Only DDLs and index backfill is allowed in xcluster read only mode.
          return STATUS(
              IllegalState,
              "Data modification is forbidden on database that is the target of a transactional "
              "xCluster replication");
        }
      }
    }

    return Status::OK();
  }

  Status ValidateSequenceModificationFunctionForXCluster(int64_t db_oid) {
    if (FLAGS_xcluster_target_manual_override) {
      return Status::OK();
    }
    if (xcluster_context() &&
        xcluster_context()->GetXClusterRole(GetPgsqlNamespaceId(narrow_cast<uint32_t>(db_oid))) ==
            XClusterNamespaceInfoPB::AUTOMATIC_TARGET) {
      return STATUS(
          IllegalState,
          "Sequence manipulation functions are forbidden on a database that is the target of "
          "automatic mode xCluster replication");
    }
    return Status::OK();
  }

  Status DoPerform(
      const PgTablesQueryResult& tables, const PerformQueryDataPtr& data, CoarseTimePoint deadline,
      rpc::RpcContext* context = nullptr) {
    auto& options = *data->req.mutable_options();
    VLOG(5) << "Perform request: " << data->req.ShortDebugString();

    RETURN_NOT_OK(ValidateRequestForXCluster(options, data));

    if (options.has_caching_info()) {
      VLOG_WITH_PREFIX(3)
          << "Executing read from response cache for session " << data->req.session_id();
      data->cache_setter = VERIFY_RESULT(response_cache().Get(
          options.mutable_caching_info(), deadline, data));
      if (!data->cache_setter) {
        return Status::OK();
      }
    }

    const auto in_txn_limit = GetInTxnLimit(options, clock().get());
    VLOG_WITH_PREFIX(5) << "using in_txn_limit_ht: " << in_txn_limit;

    TransactionFullLocality locality = GetTargetTransactionLocality(data->req);
    auto setup_session_result = VERIFY_RESULT(SetupSession(
        data->req.options(), deadline, in_txn_limit, locality));
    auto* session = setup_session_result.session_data.session.get();
    auto& transaction = setup_session_result.session_data.transaction;

    if (transaction && options.xrepl_origin_id()) {
      transaction->SetOriginId(options.xrepl_origin_id());
    }

    TracePtr trace = Trace::CurrentTrace();
    bool trace_created_locally = false;
    MonoTime start_time = MonoTime::kUninitialized;
    if (options.trace_requested()) {
      if (transaction) {
        transaction->EnsureTraceCreated();
        trace = transaction->trace();
        trace->set_must_print(true);
      }
      if (context) {
        context->EnsureTraceCreated();
        // If available, prefer to use the current Rpc's trace for logging.
        trace = context->trace();
        if (transaction) {
          // Make current Rpc-trace the child of the transaction trace.
          // Traces will be printed at the end of the transaction.
          transaction->trace()->AddChildTrace(context->trace());
          transaction->trace()->set_must_print(true);
        } else {
          // There is no transaction here, so the trace will be printed at the end of the Rpc.
          context->trace()->set_must_print(true);
        }
      } else if (!transaction) {
        // This is the codepath where there is no rpc (because we are using shared memory) and
        // no transaction. A trace will be locally created and printed at the end of the
        // session->FlushAsync callback.
        DCHECK(!trace) << "trace should not be set if context and transaction are not set";
        trace = new Trace();
        TRACE_TO(trace.get(), "DoPerform performing operation with no context or transaction");
        trace_created_locally = true;
        start_time = ToSteady(CoarseMonoClock::now());
        trace->set_must_print(true);
      }
    }
    ADOPT_TRACE(trace.get());

    data->used_read_time_applier = MakeUsedReadTimeApplier(setup_session_result);
    data->used_in_txn_limit = in_txn_limit;
    data->transaction = std::move(transaction);
    data->pg_node_level_mutation_counter = pg_node_level_mutation_counter();
    data->subtxn_id = options.active_sub_transaction_id();

    std::tie(data->ops, data->vector_index_query) = VERIFY_RESULT(PrepareOperations(
        &data->req, session, &data->sidecars, tables, vector_index_query_data_,
        data->transaction != nullptr /* has_distributed_txn */,
        make_lw_function([this, locality, deadline] {
          return NextObjectLockingTxnMeta(locality, deadline);
        }),
        IsTxnUsingTableLocks(options.is_using_table_locks()),
        context_.metrics));
    if (VLOG_IS_ON(2) || options.trace_requested()) {
      const auto& read_point = *session->read_point();
      const char* session_kind_str =
          options.use_catalog_session()                                          ? "kCatalog"
          : (options.ddl_mode() && !options.ddl_use_regular_transaction_block()) ? "kDdl"
                                                                                 : "kPlain";
      std::vector<std::string> op_summaries;
      op_summaries.reserve(data->ops.size());
      for (const auto& op_data : data->ops) {
        const auto& op = *op_data.op;
        op_summaries.push_back(
            Format("$0:$1", op.read_only() ? "R" : "W", op.table()->name().table_name()));
      }
      auto read_time_str =
          read_point.GetReadTime() ? AsString(read_point.GetReadTime()) : "not set yet";
      auto txn_str = data->transaction ? data->transaction->id().ToString() : "none";
      VLOG_WITH_PREFIX(2) << "Performing RPC: pid=" << pid_ << ", session_kind=" << session_kind_str
                          << ", ops=[" << JoinStrings(op_summaries, ", ") << "]"
                          << ", read_time=" << read_time_str
                          << ", read_time_serial_no=" << read_time_serial_no_
                          << ", txn=" << txn_str;
      if (options.trace_requested()) {
        TRACE(
            "Performing RPC: session_id=$0, pid=$1, session_kind=$2, ops=[$3], read_time=$4, "
            "read_time_serial_no=$5, txn=$6",
            id_, pid_, session_kind_str, JoinStrings(op_summaries, ", "), read_time_str,
            read_time_serial_no_, txn_str);
      }
    }
    session->FlushAsync([this, data, trace, trace_created_locally,
                         start_time](client::FlushStatus* flush_status) {
      ADOPT_TRACE(trace.get());
      data->FlushDone(flush_status);
      const auto ops_count = data->ops.size();
      if (data->transaction) {
        VLOG_WITH_PREFIX(2)
            << "FlushAsync of " << ops_count << " ops completed with transaction id "
            << data->transaction->id();
      } else {
        VLOG_WITH_PREFIX(2)
            << "FlushAsync of " << ops_count << " ops completed for non-distributed transaction";
      }
      VLOG_WITH_PREFIX(5) << "Perform resp: " << data->resp.ShortDebugString();
      if (trace_created_locally) {
        const bool must_log_trace =
            ToSteady(CoarseMonoClock::now()) - start_time >= FLAGS_txn_slow_op_threshold_ms * 1ms;
        Trace::DumpTraceIfNecessary(trace.get(), FLAGS_txn_print_trace_every_n, must_log_trace);
      }
    });
    if (setup_session_result.is_plain) {
      const auto& read_point = *session->read_point();
      if (read_point.GetReadTime()) {
        VLOG_WITH_PREFIX(3) << "Read time is already picked, saving it "
            << AsString(read_point.GetReadTime()) << " to read time serial no: "
            << read_time_serial_no_;
        read_point_history_.Save(read_point, read_time_serial_no_);
      }
    }
    return Status::OK();
  }

  void ProcessReadTimeManipulation(
      ReadTimeManipulation manipulation, uint64_t read_time_serial_no,
      ClampUncertaintyWindow clamp) {
    VLOG_WITH_PREFIX(2) << "ProcessReadTimeManipulation: " << manipulation
                        << ", read_time_serial_no: " << read_time_serial_no
                        << ", read_time_serial_no_: " << read_time_serial_no_;

    auto& read_point = *Session(PgClientSessionKind::kPlain)->read_point();
    switch (manipulation) {
      case ReadTimeManipulation::RESTART:
        read_point.Restart();
        VLOG(1) << "Restarted read point " << read_point.GetReadTime();
        return;
      case ReadTimeManipulation::ENSURE_READ_TIME_IS_SET:
        if (!read_point.GetReadTime() || (read_time_serial_no_ != read_time_serial_no)) {
          read_point.SetCurrentReadTime(clamp);
          VLOG(1) << "Setting current ht as read point " << read_point.GetReadTime();
        }
        return;
      case ReadTimeManipulation::NONE:
        return;
      case ReadTimeManipulation::ReadTimeManipulation_INT_MIN_SENTINEL_DO_NOT_USE_:
      case ReadTimeManipulation::ReadTimeManipulation_INT_MAX_SENTINEL_DO_NOT_USE_:
        break;
    }
    FATAL_INVALID_ENUM_VALUE(ReadTimeManipulation, manipulation);
  }

  Status UpdateReadPointForXClusterConsistentReads(
      const PgPerformOptionsPB& options, const CoarseTimePoint& deadline,
      ConsistentReadPoint* read_point) {
    const auto& namespace_id = options.namespace_id();
    const auto& requested_read_time = read_point->GetReadTime().read;

    // Early exit if namespace not provided or atomic reads not enabled
    if (namespace_id.empty() ||
        !xcluster_context() ||
        !options.use_xcluster_database_consistency()) {
      return Status::OK();
    }

    auto xcluster_safe_time = VERIFY_RESULT(xcluster_context()->GetSafeTime(namespace_id));
    if (!xcluster_safe_time) {
      // No xCluster safe time for this namespace.
      return Status::OK();
    }

    RSTATUS_DCHECK(
        !xcluster_safe_time->is_special(), TryAgain,
        Format("xCluster safe time for namespace $0 is invalid", namespace_id));

    // read_point is set for Distributed txns.
    // Single shard implicit txn will not have read_point set and the serving tablet uses its latest
    // time. If read_point is not set then we set it to the xCluster safe time.
    if (requested_read_time.is_special()) {
      read_point->SetReadTime(
          ReadHybridTime::SingleTime(*xcluster_safe_time), {} /* local_limits */);
      VLOG_WITH_PREFIX(3) << "Reset read time to xCluster safe time: " << read_point->GetReadTime();
      return Status::OK();
    }

    // If read_point is set to a time ahead of the xcluster safe time then we wait.
    return WaitFor(
        [&requested_read_time, &namespace_id, this]() -> Result<bool> {
          auto safe_time = VERIFY_RESULT(xcluster_context()->GetSafeTime(namespace_id));
          if (!safe_time) {
            // We dont have a safe time anymore so no need to wait.
            return true;
          }
          return requested_read_time <= *safe_time;
        },
        deadline - CoarseMonoClock::now(),
        Format(
            "Wait for xCluster safe time of namespace $0 to move above the requested read time $1",
            namespace_id, read_point->GetReadTime().read),
        100ms /* initial_delay */, 1 /* delay_multiplier */);
  }

  Result<const SessionData&> BeginPgSessionLevelTxnIfNecessary(CoarseTimePoint deadline) {
    constexpr auto kSessionKind = PgClientSessionKind::kPgSession;
    EnsureSession(kSessionKind, deadline);
    auto& session_data = GetSessionData(kSessionKind);
    auto& txn = session_data.transaction;
    if (!txn) {
      txn = transaction_provider_.Take<kSessionKind>(deadline);
      txn->SetLogPrefixTag(kTxnLogPrefixTag, id_);
      txn->InitPgSessionRequestVersion();
      // Set the start time before initializing the transaction to allow start time to be
      // propagated to txn coordinator.
      // Session level txns is only used for advisory locks. These would not touch regular tables.
      RETURN_NOT_OK(
          txn->SetPgTxnStart(MonoTime::Now().ToUint64(), IsTxnUsingTableLocks::kFalse));
      // Isolation level doesn't matter but we need to set it for conflict resolution to not treat
      // it as a single shard/fast-path transaction.
      RETURN_NOT_OK(txn->Init(IsolationLevel::READ_COMMITTED));
      session_data.session->SetTransaction(txn);
    }
    return session_data;
  }

  Result<SetupSessionResult> SetupSession(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline, HybridTime in_txn_limit = {},
      TransactionFullLocality locality = TransactionFullLocality::RegionLocal()) {
    const auto read_time_serial_no = options.read_time_serial_no();
    auto kind = PgClientSessionKind::kPlain;
    if (options.use_catalog_session()) {
      SCHECK(!options.read_from_followers(),
          InvalidArgument, "Reading catalog from followers is not allowed");
      kind = PgClientSessionKind::kCatalog;
      EnsureSession(kind, deadline);
    } else if (options.ddl_mode() && !options.ddl_use_regular_transaction_block()) {
      kind = PgClientSessionKind::kDdl;
      EnsureSession(kind, deadline);
      RETURN_NOT_OK(GetDdlTransactionMetadata(
          true /* use_transaction */, false /* use_regular_transaction_block */, deadline,
          IsTxnUsingTableLocks(options.is_using_table_locks()), options.priority(),
          options.pg_txn_start_us()));
    } else {
      DCHECK(kind == PgClientSessionKind::kPlain);
      auto& session = EnsureSession(kind, deadline);
      RETURN_NOT_OK(CheckPlainSessionPendingUsedReadTime(options));
      read_point_history_.Cleanup(options.read_time_serial_no_history_min());
      if (read_time_serial_no != read_time_serial_no_) {
        auto& read_point = *session->read_point();
        if (read_point_history_.Restore(&read_point, read_time_serial_no)) {
          read_time_serial_no_ = read_time_serial_no;
        }
      }
      RETURN_NOT_OK(BeginTransactionIfNecessary(options, deadline, locality));
    }

    auto& session_data = GetSessionData(kind);
    auto& session = *session_data.session;
    auto& txn = session_data.transaction;

    VLOG_WITH_PREFIX_AND_FUNC(4) << options.ShortDebugString() << ", deadline: "
        << MonoDelta(deadline - CoarseMonoClock::now());

    RETURN_NOT_OK(UpdateReadTime(options, kind, deadline, in_txn_limit));

    session.SetDeadline(deadline);

    if (txn) {
      RSTATUS_DCHECK_GE(
          options.active_sub_transaction_id(), kMinSubTransactionId,
          InvalidArgument,
          Format("Expected active_sub_transaction_id to be >= $0", kMinSubTransactionId));
      txn->SetActiveSubTransaction(options.active_sub_transaction_id());
      if (const auto& pg_session_txn = Transaction(PgClientSessionKind::kPgSession);
          pg_session_txn) {
        pg_session_txn->SetBackgroundTransaction(txn);
      }
    }

    return SetupSessionResult{
        .session_data = session_data,
        .is_plain = (kind == PgClientSessionKind::kPlain)};
  }

  Status UpdateReadTime(
      const PgPerformOptionsPB& options, PgClientSessionKind kind, CoarseTimePoint deadline,
      HybridTime in_txn_limit = {}) {
    auto& session_data = GetSessionData(kind);
    auto& session = *session_data.session;
    auto& txn = session_data.transaction;

    const auto txn_serial_no = options.txn_serial_no();
    const auto read_time_serial_no = options.read_time_serial_no();

    if (options.restart_transaction()) {
      if (options.ddl_mode()) {
        return STATUS(NotSupported, "Restarting a DDL transaction not supported");
      }
      RETURN_NOT_OK(RestartTransaction(kind, deadline));
    } else {
      const auto is_plain_session = (kind == PgClientSessionKind::kPlain);
      const auto has_time_manipulation =
          options.read_time_manipulation() != ReadTimeManipulation::NONE;
      RSTATUS_DCHECK(
          !(has_time_manipulation && options.has_read_time()),
          IllegalState, "read_time_manipulation and read_time fields can't be satisfied together");

      if (has_time_manipulation) {
        RSTATUS_DCHECK(
            is_plain_session, IllegalState,
            "Read time manipulation can't be specified for non kPlain sessions");
        RSTATUS_DCHECK(
            !options.defer_read_point(), IllegalState,
            "Cannot manipulate read time when read point needs to be deferred.");
        ProcessReadTimeManipulation(
            options.read_time_manipulation(), read_time_serial_no,
            ClampUncertaintyWindow(options.clamp_uncertainty_window()));
      } else if (options.has_read_time() && options.read_time().has_read_ht()) {
        const auto read_time = ReadHybridTime::FromPB(options.read_time());
        session.SetReadPoint(read_time);
        VLOG_WITH_PREFIX(3) << "Read time: " << read_time;
      } else if (IsReadPointResetRequested(options) ||
                options.use_catalog_session() ||
                (is_plain_session && (read_time_serial_no_ != read_time_serial_no))) {
                  VLOG_WITH_PREFIX(3) << "Resetting read point for session kind " << kind
                  << " read point reset requested: " << IsReadPointResetRequested(options)
                  << " use catalog session: " << options.use_catalog_session()
                  << " change in read time serial number "
                  << read_time_serial_no_ << ", " << read_time_serial_no;
        ResetReadPoint(kind);
      } else {
        VLOG_WITH_PREFIX(3) << "Keep read time: " << session.read_point()->GetReadTime();
      }
    }

    RETURN_NOT_OK(
        UpdateReadPointForXClusterConsistentReads(options, deadline, session.read_point()));

    if (!options.ddl_mode() && !options.use_catalog_session() && options.defer_read_point()) {
      // For DMLs, only fast path writes cannot be deferred.
      RETURN_NOT_OK(session.read_point()->TrySetDeferredCurrentReadTime());
    }

    // TODO: Reset in_txn_limit which might be on session from past Perform? Not resetting will not
    // cause any issue, but should we reset for safety?
    if (!(options.ddl_mode() && !options.ddl_use_regular_transaction_block()) &&
        !options.use_catalog_session()) {
      txn_serial_no_ = txn_serial_no;
      read_time_serial_no_ = read_time_serial_no;
      if (in_txn_limit) {
        // TODO: Shouldn't the below logic for DDL transactions as well?
        session.SetInTxnLimit(in_txn_limit);
      }

      if (options.clamp_uncertainty_window() &&
          !session.read_point()->GetReadTime()) {
        RSTATUS_DCHECK(
          !(txn && txn->isolation() == SERIALIZABLE_ISOLATION),
          IllegalState, "Clamping does not apply to SERIALIZABLE txns.");
        // Set read time with clamped uncertainty window when requested by
        // the query layer.
        // Do not mess with the read time if already set.
        session.read_point()->SetCurrentReadTime(ClampUncertaintyWindow::kTrue);
        VLOG_WITH_PREFIX_AND_FUNC(2)
          << "Setting read time to "
          << session.read_point()->GetReadTime()
          << " for read only txn/stmt";
      }
    }
    return Status::OK();
  }

  void ResetReadPoint(PgClientSessionKind kind) {
    const auto is_plain_session = (kind == PgClientSessionKind::kPlain);
    DCHECK(is_plain_session || kind == PgClientSessionKind::kCatalog);
    const auto& data = GetSessionData(kind);
    auto& session = *data.session;
    session.SetReadPoint({});
    VLOG_WITH_PREFIX(3) << "Reset read time: " << session.read_point()->GetReadTime();

    if (!is_plain_session || data.transaction || !plain_session_used_read_time_.pending_update) {
      return;
    }
    LOG(INFO) << "Previous pending used_read_time update is still active, deactivating";
    plain_session_used_read_time_.pending_update = false;
    RenewSignature(plain_session_used_read_time_.value);
  }

  Status BeginTransactionIfNecessary(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline,
      TransactionFullLocality locality) {
    RETURN_NOT_OK(DoBeginTransactionIfNecessary(options, deadline, locality));
    const auto& data = GetSessionData(PgClientSessionKind::kPlain);
    data.session->SetForceConsistentRead(client::ForceConsistentRead(!data.transaction));
    return Status::OK();
  }

  Status DoBeginTransactionIfNecessary(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline,
      TransactionFullLocality locality) {
    const auto isolation = static_cast<IsolationLevel>(options.isolation());

    auto priority = options.priority();
    constexpr auto kSessionKind = PgClientSessionKind::kPlain;
    auto& session = EnsureSession(kSessionKind, deadline);
    auto& txn = GetSessionData(kSessionKind).transaction;
    if (txn && txn_serial_no_ != options.txn_serial_no()) {
      VLOG_WITH_PREFIX(2)
          << "Abort previous transaction, use existing priority: "
          << options.use_existing_priority()
          << ", new isolation: " << IsolationLevel_Name(isolation);

      if (options.use_existing_priority()) {
        saved_priority_ = txn->GetPriority();
      }
      RETURN_NOT_OK(ReleaseObjectLocksIfNecessary(txn, kSessionKind, deadline));
      txn->Abort();
      session->SetTransaction(nullptr);
      txn = nullptr;
    }

    // TODO(advisory-locks): The session level transaction could get aborted in the background, for
    // instance, it could happen if the heartbeats get delayed due to load etc. If we decide to
    // honor the assumption that all session advisory locks taken in the past are active till the
    // ysql session ends, then we need to explicitly check status of pg_session_transaction_, if
    // exists, and fail all read/write ops if pg_session_transaction_ has failed.
    //
    // Refer https://github.com/yugabyte/yugabyte-db/issues/25566 for details.
    if (isolation == IsolationLevel::NON_TRANSACTIONAL) {
      return Status::OK();
    }

    if (txn) {
      if (txn->isolation() != isolation) {
        return STATUS_FORMAT(
          IllegalState,
          "Attempt to change isolation level of running transaction from $0 to $1",
          txn->isolation(), isolation);
      }

      return options.ddl_mode() && options.ddl_use_regular_transaction_block()
                 ? txn->EnsureGlobal(deadline)
                 : Status::OK();
    }

    const bool global_required =
        options.force_global_transaction() ||
        (options.ddl_mode() && options.ddl_use_regular_transaction_block());
    if (global_required) {
      locality = TransactionFullLocality::Global();
    }
    // Amit: Is it possible that we'd be enabling table locks for one type of session (say plain)
    // but have it disabled for another type (say kDDl) ? Would that cause a problem?
    // should only go from off -> on. Not the other way around.
    // The txn should be marked correctly?
    txn = transaction_provider_.Take<kSessionKind>(locality, deadline);
    txn->SetLogPrefixTag(kTxnLogPrefixTag, id_);
    RETURN_NOT_OK(txn->SetPgTxnStart(
        options.pg_txn_start_us(), IsTxnUsingTableLocks(options.is_using_table_locks())));
    auto* read_point = session->read_point();
    if ((isolation == IsolationLevel::SNAPSHOT_ISOLATION ||
         isolation == IsolationLevel::READ_COMMITTED) &&
        txn_serial_no_ == options.txn_serial_no() &&
        (read_point && read_point->GetReadTime())) {
      txn->InitWithReadPoint(isolation, std::move(*read_point));
      VLOG_WITH_PREFIX(2) << "Start transaction " << IsolationLevel_Name(isolation)
                          << ", id: " << txn->id()
                          << ", kept read time: " << txn->read_point().GetReadTime();
    } else {
      VLOG_WITH_PREFIX(2) << "Start transaction " << IsolationLevel_Name(isolation)
                          << ", id: " << txn->id()
                          << ", new read time";
      RETURN_NOT_OK(txn->Init(isolation));
    }
    if (global_required) {
      RETURN_NOT_OK(txn->EnsureGlobal(deadline));
    }

    RETURN_NOT_OK(UpdateReadPointForXClusterConsistentReads(options, deadline, &txn->read_point()));

    if (saved_priority_) {
      priority = *saved_priority_;
      saved_priority_ = std::nullopt;
    }
    txn->SetPriority(priority);
    session->SetTransaction(txn);
    return Status::OK();
  }

  Status SetupSessionForDdl(
      bool use_regular_transaction_block, const PgPerformOptionsPB& options,
      CoarseTimePoint deadline) {
    if (!use_regular_transaction_block) {
      // Separate DDL transactions do not need to setup the session. They will create the
      // transaction in GetDdlTransactionMetadata().
      return Status::OK();
    }

    VLOG_WITH_PREFIX(1) << "Setting up session for DDL with options: "
                        << options.ShortDebugString();
    const auto in_txn_limit = GetInTxnLimit(options, clock().get());
    VLOG_WITH_PREFIX(5) << "using in_txn_limit_ht: " << in_txn_limit;
    RETURN_NOT_OK(SetupSession(
        options, deadline, in_txn_limit, TransactionFullLocality::Global()));
    return Status::OK();
  }

  // All DDLs use kHighestPriority unless specified otherwise.
  Result<const TransactionMetadata*> GetDdlTransactionMetadata(
      bool use_transaction, bool use_regular_transaction_block, CoarseTimePoint deadline,
      IsTxnUsingTableLocks is_txn_using_table_locks, uint64_t priority = kHighPriTxnUpperBound,
      uint64_t pg_txn_start_us = 0) {
    if (!use_transaction) {
      return nullptr;
    }

    const auto kSessionKind =
      use_regular_transaction_block ? PgClientSessionKind::kPlain : PgClientSessionKind::kDdl;
    auto& txn = GetSessionData(kSessionKind).transaction;
    if (use_regular_transaction_block) {
      RSTATUS_DCHECK(FLAGS_ysql_yb_ddl_transaction_block_enabled, IllegalState,
                     "Received DDL request in regular transaction block, but DDL transaction block "
                     "support is disabled");
      // Since this DDL is being executed in the regular transaction block, we should never need to
      // create a new transaction here.
      RSTATUS_DCHECK(txn, IllegalState,
                     "Transaction unexpectly not set for DDL request in regular transaction block");
      if (ddl_txn_metadata_.transaction_id == txn->id()) {
        return &ddl_txn_metadata_;
      }
      // Set and return the plain transaction metadata as the DDL transaction metadata.
      ddl_txn_metadata_ = VERIFY_RESULT(Copy(txn->GetMetadata(deadline).get()));
      return &ddl_txn_metadata_;
    }

    if (!txn) {
      const auto isolation = FLAGS_ysql_serializable_isolation_for_ddl_txn
          ? IsolationLevel::SERIALIZABLE_ISOLATION : IsolationLevel::SNAPSHOT_ISOLATION;
      txn = transaction_provider_.Take<PgClientSessionKind::kDdl>(deadline);
      RETURN_NOT_OK(txn->SetPgTxnStart(
          pg_txn_start_us ? pg_txn_start_us : MonoTime::Now().ToUint64(),
          is_txn_using_table_locks));
      RETURN_NOT_OK(txn->Init(isolation));
      txn->SetPriority(priority);
      txn->SetLogPrefixTag(kTxnLogPrefixTag, id_);
      ddl_txn_metadata_ = VERIFY_RESULT(Copy(txn->GetMetadata(deadline).get()));
      EnsureSession(kSessionKind, deadline)->SetTransaction(txn);
    }

    return &ddl_txn_metadata_;
  }

  Status RestartTransaction(PgClientSessionKind kind, CoarseTimePoint deadline) {
    auto& session_data = GetSessionData(kind);
    auto& session = *session_data.session;
    auto& txn = session_data.transaction;
    if (!txn) {
      SCHECK(
        session.IsRestartRequired(), IllegalState,
        "Attempted to restart when session does not require restart");

      const auto old_read_time = session.read_point()->GetReadTime();
      session.RestartNonTxnReadPoint(client::Restart::kTrue);
      const auto new_read_time = session.read_point()->GetReadTime();
      VLOG_WITH_PREFIX(3) << "Restarted read: " << old_read_time << " => " << new_read_time;
      LOG_IF_WITH_PREFIX(DFATAL, old_read_time == new_read_time)
          << "Read time did not change during restart: " << old_read_time
          << " => " << new_read_time;
      return ReleaseObjectLocksIfNecessary(txn, kind, deadline);
    }

    SCHECK(
        txn->IsRestartRequired(), IllegalState,
        "Attempted to restart when transaction does not require restart");
    RETURN_NOT_OK(ReleaseObjectLocksIfNecessary(txn, kind, deadline));
    txn = VERIFY_RESULT(txn->CreateRestartedTransaction());
    session.SetTransaction(txn);
    VLOG_WITH_PREFIX(3) << "Restarted transaction";
    return Status::OK();
  }

  client::YBSessionPtr& EnsureSession(
      PgClientSessionKind kind, CoarseTimePoint deadline,
      std::optional<uint64_t> read_time = std::nullopt) {
    auto& session = Session(kind);
    if (!session) {
      session = CreateSession(&client_, deadline, clock());
    } else {
      session->SetDeadline(deadline);
    }
    if (read_time) {
      // Set the read_time only for sequence YBSession. Other types of sessions set their read_time
      // differently.
      DCHECK(kind == PgClientSessionKind::kSequence);
      VLOG(4) << "Setting read_time of sequences_data table to: "
              << ReadHybridTime::FromMicros(*read_time);
      session->SetReadPoint(ReadHybridTime::FromMicros(*read_time));
    } else {
      // Reset the read_time for sequence queries to read recent data. This is required in case the
      // user reset yb_read_time to 0 in a session.
      if (kind == PgClientSessionKind::kSequence) {
        session->SetReadPoint({});
      }
    }
    return session;
  }

  Status CheckPlainSessionPendingUsedReadTime(const PgPerformOptionsPB& options) {
    if (!plain_session_used_read_time_.pending_update) {
      return Status::OK();
    }
    auto& session_data = GetSessionData(PgClientSessionKind::kPlain);
    auto& session = *session_data.session;
    const auto& read_point = *session.read_point();
    TabletReadTime read_time_data;
    if (!read_point.GetReadTime()) {
      auto& used_read_time = plain_session_used_read_time_.value;
      std::lock_guard guard(used_read_time.lock);
      if (!used_read_time.data) {
        if (txn_serial_no_ != options.txn_serial_no()) {
          // Allow sending request with new txn_serial_no in case previous request with different
          // txn_serial_no has not been finished yet. This may help to prevent stuck in case of
          // request timeout.
          return Status::OK();
        }
        if (options.read_time_serial_no() == read_time_serial_no_ &&
            !session_data.transaction &&
            options.isolation() == IsolationLevel::NON_TRANSACTIONAL &&
            IsReadPointResetRequested(options)) {
          // Read time from previous operations is not required for non-transaction operation which
          // will reset session's read time prior to the execution.
          return Status::OK();
        }

        return STATUS(
            IllegalState, "Expecting a used read time from the previous RPC but didn't find one");
      }
      read_time_data = std::move(*used_read_time.data);
      used_read_time.data.reset();
    }
    plain_session_used_read_time_.pending_update = false;
    // At this point the read_time_data.value could be empty in 2 cases:
    // - session already has a read time (i.e. read_point.GetReadTime() is true)
    // - pending request has finished failed with an error (status != OK). Empty read time is used
    //   in this case.
    // Both cases are valid and in both cases the plain_session_used_read_time_.pending_update
    // must be set to false because no further update is expected.
    if (read_time_data.value) {
      VLOG_WITH_PREFIX(3) << "Applying non empty used read time: " << read_time_data.value
          << " to read time serial no: " << read_time_serial_no_;
      session.SetReadPoint(read_time_data.value, read_time_data.tablet_id);
      read_point_history_.Save(read_point, read_time_serial_no_);
    }
    return Status::OK();
  }

  void ScheduleBigSharedMemExpirationCheck(std::chrono::steady_clock::duration delay) {
    big_shared_mem_expiration_task_.Schedule([this](const Status& status) {
      if (!status.ok()) {
        std::lock_guard lock(big_shared_mem_mutex_);
        big_shared_mem_expiration_task_scheduled_ = false;
        return;
      }
      auto expiration_time =
          last_big_shared_memory_access_.load() +
          FLAGS_big_shared_memory_segment_session_expiration_time_ms * 1ms;
      auto now = CoarseMonoClock::Now();
      if (expiration_time < now) {
        expiration_time = now + 100ms; // in case of scheduling recheck
        std::lock_guard lock(big_shared_mem_mutex_);
        if (big_shared_mem_handle_ && !InUseAtomic(big_shared_mem_handle_).load()) {
          big_shared_mem_handle_ = {};
          big_shared_mem_expiration_task_scheduled_ = false;
          return;
        }
      }
      ScheduleBigSharedMemExpirationCheck(expiration_time - now);
    }, delay);
  }

  template <class T>
  static auto& DoSessionData(T* that, PgClientSessionKind kind) {
    return that->sessions_[std::to_underlying(kind)];
  }

  SessionData& GetSessionData(PgClientSessionKind kind) {
    return DoSessionData(this, kind);
  }

  const SessionData& GetSessionData(PgClientSessionKind kind) const {
    return DoSessionData(this, kind);
  }

  client::YBSessionPtr& Session(PgClientSessionKind kind) {
    return GetSessionData(kind).session;
  }

  const client::YBSessionPtr& Session(PgClientSessionKind kind) const {
    return GetSessionData(kind).session;
  }

  const client::YBTransactionPtr& Transaction(PgClientSessionKind kind) const {
    return GetSessionData(kind).transaction;
  }

  template <class InRequestPB, class OutRequestPB>
  Status SetCatalogVersion(const InRequestPB& in_req, OutRequestPB* out_req) const {
    // Note that in initdb/bootstrap mode, even if FLAGS_enable_db_catalog_version_mode is
    // on it will be ignored and we'll use ysql_catalog_version not ysql_db_catalog_version.
    // That's why we must use in_req as the source of truth. Unlike the older version google
    // protobuf, this protobuf of in_req (google proto3) does not have has_ysql_catalog_version()
    // and has_ysql_db_catalog_version() member functions so we use invalid version 0 as an
    // alternative.
    // For now we either use global catalog version or db catalog version but not both.
    // So it is an error if both are set.
    // It is possible that neither is set during initdb.
    SCHECK(in_req.ysql_catalog_version() == 0 || in_req.ysql_db_catalog_version() == 0,
           InvalidArgument, "Wrong catalog versions: $0 and $1",
           in_req.ysql_catalog_version(), in_req.ysql_db_catalog_version());
    if (in_req.ysql_db_catalog_version()) {
      CHECK(FLAGS_ysql_enable_db_catalog_version_mode);
      out_req->set_ysql_db_catalog_version(in_req.ysql_db_catalog_version());
      out_req->set_ysql_db_oid(narrow_cast<uint32_t>(in_req.db_oid()));
    } else if (in_req.ysql_catalog_version()) {
      out_req->set_ysql_catalog_version(in_req.ysql_catalog_version());
    }
    return Status::OK();
  }

  Status DoFinishTransaction(
      const PgFinishTransactionRequestPB& req, CoarseTimePoint deadline,
      const client::YBTransactionPtr& txn, PgClientSessionKind used_session_kind) {
    const auto is_ddl = req.has_ddl_mode();
    auto ddl_use_regular_transaction_block = false;
    auto has_docdb_schema_changes = false;
    std::optional<uint32_t> silently_altered_db;
    const TransactionMetadata* metadata = nullptr;
    if (is_ddl) {
      const auto& ddl_mode = req.ddl_mode();
      ddl_use_regular_transaction_block = ddl_mode.use_regular_transaction_block();
      has_docdb_schema_changes = ddl_mode.has_docdb_schema_changes();
      if (ddl_mode.has_silently_altered_db()) {
        silently_altered_db = ddl_mode.silently_altered_db().value();
      }
      metadata = &ddl_txn_metadata_;
    }
    // DDL abort might not have metadata set in the following cases:
    // 1. backend detects timeout before the previous rpc (which sets ddl_txn_metadata_) returns.
    // 2. DDL fails even before moving to the phase where ddl_txn_metadata_ gets set.
    //
    // In all such cases, no state would exist with the master's ddl verifier, it is ok to
    // just abort the YBTransaction and move on.
    RSTATUS_DCHECK(
        !has_docdb_schema_changes || !metadata->transaction_id.IsNil() || !req.commit(),
        IllegalState, "Valid ddl metadata is required for ddl commit");

    if (req.commit()) {
      auto commit_status = Commit(
          txn.get(),
          silently_altered_db ? response_cache().Disable(*silently_altered_db)
                              : PgResponseCache::Disabler());

      VLOG_WITH_PREFIX_AND_FUNC(2)
          << "ddl: " << is_ddl
          << ", ddl_use_regular_transaction_block: " << ddl_use_regular_transaction_block
          << ", has_docdb_schema_changes: " << has_docdb_schema_changes
          << ", txn: " << txn->id() << ", commit: " << commit_status;
      // If commit_status is not ok, we cannot be sure whether the commit was successful or not. It
      // is possible that the commit succeeded at the transaction coordinator but we failed to get
      // the response back. Thus we will not report any status to the YB-Master in this case. But
      // we still need to call WaitForDdlVerificationToFinish so that YB-Master can start its
      // background task to figure out whether the transaction succeeded or failed.
      if (!commit_status.ok()) {
        auto status = DdlAtomicityFinishTransaction(
            txn, used_session_kind, has_docdb_schema_changes, metadata, std::nullopt, deadline);
        if (!status.ok()) {
          // As of 2024-09-24, it is known that if we come here it is possible that YB-Master will
          // not be able to start a background task to figure out whether the DDL transaction
          // status (committed or aborted) and do the necessary cleanup of leftover any DocDB index
          // table. Therefore we can have orphaned DocDB tables/indexes that are not garbage
          // collected. One way to fix this we need to add a periodic scan job in YB-Master to look
          // for any table/index that are involved in a DDL transaction and start a background task
          // to complete the DDL transaction at the DocDB side.
          LOG(DFATAL) << "DdlAtomicityFinishTransaction failed: " << status;
        }
        return MergeStatus(std::move(commit_status), std::move(status));
      }
      if (pg_node_level_mutation_counter()) {
        // Gather # of mutated rows for each table (count only the committed sub-transactions).
        auto table_mutations = txn->GetTableMutationCounts();
        VLOG_WITH_PREFIX(4) << "Incrementing global mutation count using table to mutations map: "
                            << AsString(table_mutations) << " for txn: " << txn->id();
        pg_node_level_mutation_counter()->IncreaseBatch(table_mutations);
      }
    } else {
      VLOG_WITH_PREFIX_AND_FUNC(2)
          << "ddl: " << is_ddl
          << ", ddl_use_regular_transaction_block: " << ddl_use_regular_transaction_block
          << ", has_docdb_schema_changes: " << has_docdb_schema_changes
          << ", txn: " << txn->id() << ", abort";
      txn->Abort();
    }
    return DdlAtomicityFinishTransaction(
        txn, used_session_kind, has_docdb_schema_changes, metadata, req.commit(), deadline);
  }

  Status ReleaseObjectLocksIfNecessary(
      const client::YBTransactionPtr& txn, PgClientSessionKind kind, CoarseTimePoint deadline,
      std::optional<SubTransactionId> subtxn_id = std::nullopt) {
    if (!IsObjectLockingEnabled()) {
      return Status::OK();
    }
    if (!txn && kind != PgClientSessionKind::kPlain) {
      // kDdl might not have a txn when this function is invoked on Shutdown.
      return Status::OK();
    }

    const bool is_final_release = !subtxn_id;
    auto unregister_scope = is_final_release && txn
        ? MakeOptionalScopeExit([this] { transaction_provider_.ResetObjectLockOwner(); })
        : std::nullopt;

    const auto has_exclusive_locks =
        kind == PgClientSessionKind::kDdl || plain_session_has_exclusive_object_locks_.load();
    if (has_exclusive_locks && kind == PgClientSessionKind::kPlain && !subtxn_id) {
      plain_session_has_exclusive_object_locks_.store(false);
      DEBUG_ONLY_TEST_SYNC_POINT("PlainTxnStateReset");
    }
    if (txn) {
      return ResultToStatus(
          DoReleaseObjectLocks(txn->id(), subtxn_id, deadline, has_exclusive_locks));
    }
    // It could happen that transaction_provider_.next_plain_ is null when txn finish
    // calls are redundant. If so, treat it as a no-op instead of setting next_plain_.
    if (!transaction_provider_.HasNextTxnForPlain()) {
      return Status::OK();
    }
    auto txn_meta = VERIFY_RESULT(transaction_provider_.CheckTxnMetaForPlainFinish());
    if (VERIFY_RESULT((DoReleaseObjectLocks(
            txn_meta.transaction_id, subtxn_id, deadline, has_exclusive_locks)))) {
      // This re-usable transaction was a blocker for some other transaction. Consume it to
      // prevent re-use so in order to avoid false deadlock issues.
      transaction_provider_.ConsumePlainTxn();
    }
    return Status::OK();
  }

  Result<docdb::TxnBlockedTableLockRequests> DoReleaseObjectLocks(
      const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id,
      CoarseTimePoint deadline, bool has_exclusive_locks) {
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Requesting release of "
                                 << (has_exclusive_locks ? "global" : "local") << " locks for txn "
                                 << txn_id << " subtxn_id " << AsString(subtxn_id);
    if (!has_exclusive_locks) {
      return ts_lock_manager()->ReleaseObjectLocks(
          ReleaseRequestFor<tserver::ReleaseObjectLockRequestPB>(
              instance_uuid(), txn_id, subtxn_id),
          deadline);
    }
    auto release_req = std::make_shared<master::ReleaseObjectLocksGlobalRequestPB>(
        ReleaseRequestFor<master::ReleaseObjectLocksGlobalRequestPB>(
            instance_uuid(), txn_id, subtxn_id, lease_epoch_, context_.clock.get()));
    ReleaseWithRetriesGlobal(client_, ts_lock_manager(), txn_id, subtxn_id, release_req);
    return docdb::TxnBlockedTableLockRequests::kTrue;
  }

  UsedReadTimeApplier MakeUsedReadTimeApplier(const SetupSessionResult& result) {
    auto* read_point = result.session_data.session->read_point();
    if (!result.is_plain ||
        result.session_data.transaction ||
        (read_point && read_point->GetReadTime())) {
      return {};
    }

    plain_session_used_read_time_.pending_update = true;
    auto& used_read_time = plain_session_used_read_time_.value;
    return BuildUsedReadTimeApplier(
        SharedField(SharedSessionFromThis(), &used_read_time), RenewSignature(used_read_time));
  }

  [[nodiscard]] bool IsObjectLockingEnabled() const {
    return ts_lock_manager() != nullptr &&
           FLAGS_enable_object_locking_for_table_locks &&
           FLAGS_ysql_enable_object_locking_infra;
  }

  Result<TransactionMetadata> NextObjectLockingTxnMeta(
      TransactionFullLocality locality, CoarseTimePoint deadline) {
    return transaction_provider_.NextTxnMetaForPlain(locality, deadline);
  }

  TransactionFullLocality GetTargetTransactionLocality(const PgPerformRequestPB& request) const {
    return GetTargetTransactionLocality(
        request.options(), request.ops() | std::views::transform(GetOpTablespaceOid));
  }

  TransactionFullLocality GetTargetTransactionLocality(
      const PgAcquireObjectLockRequestPB& request) const {
    return GetTargetTransactionLocality(
        request.options(), std::views::single(request.lock_oid().tablespace_oid()));
  }

  TransactionFullLocality GetTargetTransactionLocality(
      const PgPerformOptionsPB& options, std::ranges::range auto&& tablespace_oids) const {
    if (PREDICT_FALSE(FLAGS_TEST_force_initial_region_local)) {
      return TransactionFullLocality::RegionLocal();
    }

    if (context_.transaction_manager_provider().TablespaceLocalTransactionsPossible() &&
        (FLAGS_use_tablespace_based_transaction_placement || options.force_tablespace_locality())) {
      if (auto oid = options.force_tablespace_locality_oid()) {
        return TransactionFullLocality::TablespaceLocal(oid);
      }
      return CalculateTablespaceBasedLocality(std::move(tablespace_oids));
    }

    // TODO: is_all_region_local() handles exactly two cases that tablespace oid check does not:
    // 1. until upgrade is finalized (enable_tablespace_based_transaction_placement autoflag on
    //    master), tablespace oid check does not work, so it is needed to avoid global latencies
    //    during the upgrade. This is only for upgrades from before 2025.1.2/2025.2.0.
    // 2. if auto_create_local_transaction_tables is OFF (not default), and transaction tables
    //    are manually created, tablespace oid will not be mapped to a transaction table, and
    //    TablespaceIsRegionLocal() returns false. But this case is not really supported, aside
    //    from use setting up some unit tests.
    // Once these are no longer of concern, is_all_region_local and corresponding code in
    // pggate/pg can be removed.
    if (!FLAGS_TEST_perform_ignore_pg_is_region_local && options.is_all_region_local()) {
      return TransactionFullLocality::RegionLocal();
    }
    return CalculateRegionBasedLocality(std::move(tablespace_oids));
  }

  TransactionFullLocality CalculateRegionBasedLocality(
      std::ranges::range auto&& tablespace_oids) const {
    auto& transaction_manager = context_.transaction_manager_provider();
    bool all_region_local = transaction_manager.RegionLocalTransactionsPossible();
    for (PgTablespaceOid oid : tablespace_oids) {
      all_region_local = all_region_local && transaction_manager.TablespaceIsRegionLocal(oid);
    }
    return all_region_local
        ? TransactionFullLocality::RegionLocal() : TransactionFullLocality::Global();
  }

  TransactionFullLocality CalculateTablespaceBasedLocality(
      std::ranges::range auto&& tablespace_oids) const {
    auto& transaction_manager = context_.transaction_manager_provider();
    PgTablespaceOid tablespace_oid = kInvalidOid;
    for (PgTablespaceOid oid : tablespace_oids) {
      if (tablespace_oid == kInvalidOid ||
          transaction_manager.TablespaceContainsTablespace(oid, tablespace_oid)) {
        tablespace_oid = oid;
      } else if (!transaction_manager.TablespaceContainsTablespace(tablespace_oid, oid)) {
        return TransactionFullLocality::Global();
      }
    }
    return tablespace_oid == kInvalidOid
        ? TransactionFullLocality::Global()
        : TransactionFullLocality::TablespaceLocal(tablespace_oid);
  }

  std::shared_ptr<PgClientSession> SharedSessionFromThis() const {
    return shared_this_.lock();
  }

  client::YBClient& client_;
  const PgClientSessionContext& context_;
  const std::weak_ptr<PgClientSession> shared_this_;
  const uint64_t id_;
  const pid_t pid_;
  const uint64_t lease_epoch_;
  const tserver::TSLocalLockManagerPtr ts_lock_manager_;
  TransactionProvider transaction_provider_;
  std::mutex big_shared_mem_mutex_;
  std::atomic<CoarseTimePoint> last_big_shared_memory_access_;
  SharedMemorySegmentHandle big_shared_mem_handle_ GUARDED_BY(big_shared_mem_mutex_);
  bool big_shared_mem_expiration_task_scheduled_ GUARDED_BY(big_shared_mem_mutex_) = false;
  rpc::ScheduledTaskTracker big_shared_mem_expiration_task_;

  std::array<SessionData, kPgClientSessionKindMapSize> sessions_;
  uint64_t txn_serial_no_ = 0;
  uint64_t read_time_serial_no_ = 0;
  ReadPointHistory read_point_history_;
  std::optional<uint64_t> saved_priority_;
  TransactionMetadata ddl_txn_metadata_;
  PendingUsedReadTime plain_session_used_read_time_;

  simple_spinlock pending_data_mutex_;
  std::vector<WriteBuffer> pending_data_ GUARDED_BY(pending_data_mutex_);

  std::atomic<bool> plain_session_has_exclusive_object_locks_{false};
  VectorIndexQueryPtr vector_index_query_data_;
};

PgClientSession::PgClientSession(
    TransactionBuilder&& transaction_builder, SharedThisSource shared_this_source,
    client::YBClient& client, std::reference_wrapper<const PgClientSessionContext> context,
    uint64_t id, pid_t pid, uint64_t lease_epoch,
    tserver::TSLocalLockManagerPtr ts_local_lock_manager, rpc::Scheduler& scheduler)
    : impl_(new Impl(
          std::move(transaction_builder), {std::move(shared_this_source), this}, client, context,
          id, pid, lease_epoch, std::move(ts_local_lock_manager), scheduler)) {}

PgClientSession::~PgClientSession() = default;

uint64_t PgClientSession::id() const {
  return impl_->id();
}

void PgClientSession::SetupSharedObjectLocking(PgSessionLockOwnerTagShared& object_lock_shared) {
  impl_->SetupSharedObjectLocking(object_lock_shared);
}

void PgClientSession::Perform(
    PgPerformRequestPB& req, PgPerformResponsePB& resp, rpc::RpcContext&& context,
    const PgTablesQueryResult& tables) {
  impl_->Perform(req, resp, std::move(context), tables);
}

void PgClientSession::ProcessSharedRequest(size_t size, SharedExchange* exchange) {
  impl_->ProcessSharedRequest(size, exchange);
}

size_t PgClientSession::SaveData(const RefCntBuffer& buffer, WriteBuffer&& sidecars) {
  return impl_->SaveData(buffer, std::move(sidecars));
}

std::pair<uint64_t, std::byte*> PgClientSession::ObtainBigSharedMemorySegment(size_t size) {
  return impl_->ObtainBigSharedMemorySegment(size);
}

void PgClientSession::StartShutdown(bool pg_service_shutting_down) {
  return impl_->StartShutdown(pg_service_shutting_down);
}

bool PgClientSession::ReadyToShutdown() const {
  return impl_->ReadyToShutdown();
}

void PgClientSession::CompleteShutdown() {
  impl_->CompleteShutdown();
}

Result<ReadHybridTime> PgClientSession::GetTxnSnapshotReadTime(
    const PgPerformOptionsPB& options, CoarseTimePoint deadline) {
  return impl_->GetTxnSnapshotReadTime(options, deadline);
}

Status PgClientSession::SetTxnSnapshotReadTime(
    const PgPerformOptionsPB& options, CoarseTimePoint deadline) {
  return impl_->SetTxnSnapshotReadTime(options, deadline);
}

#define PG_CLIENT_SESSION_METHOD_DEFINE_IMPL(ret, ctx_type, method) \
  ret PgClientSession::method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      ctx_type context) { \
        return impl_->method(req, resp, std::move(context)); \
      }

#define PG_CLIENT_SESSION_METHOD_DEFINE(r, data_tuple, method) \
  PG_CLIENT_SESSION_METHOD_DEFINE_IMPL( \
      BOOST_PP_TUPLE_ELEM(2, 0, data_tuple), BOOST_PP_TUPLE_ELEM(2, 1, data_tuple), method)

BOOST_PP_SEQ_FOR_EACH(
    PG_CLIENT_SESSION_METHOD_DEFINE, (Status, rpc::RpcContext*), PG_CLIENT_SESSION_METHODS);
BOOST_PP_SEQ_FOR_EACH(
    PG_CLIENT_SESSION_METHOD_DEFINE, (void, rpc::RpcContext&&), PG_CLIENT_SESSION_ASYNC_METHODS);

void PreparePgTablesQuery(
    const PgPerformRequestPB& req, boost::container::small_vector_base<TableId>& table_ids) {
  for (const auto& op : req.ops()) {
    AddIfMissing(table_ids, op.has_read() ? op.read().table_id() : op.write().table_id());
  }
}

PgClientSessionMetrics::PgClientSessionMetrics(MetricEntity* metric_entity)
    : exchange_response_size(METRIC_pg_client_exchange_response_size.Instantiate(metric_entity)),
      vector_index_fetch_us(METRIC_vector_index_fetch_us.Instantiate(metric_entity)),
      vector_index_collect_us(METRIC_vector_index_collect_us.Instantiate(metric_entity)),
      vector_index_reduce_us(METRIC_vector_index_reduce_us.Instantiate(metric_entity)) {
}

}  // namespace yb::tserver
