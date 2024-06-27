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

#include <chrono>
#include <cstddef>
#include <mutex>
#include <set>

#include "yb/cdc/cdc_service.h"

#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/common_util.h"
#include "yb/common/ql_type.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/sidecars.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/pg_response_cache.h"
#include "yb/tserver/pg_sequence_cache.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/trace.h"
#include "yb/util/write_buffer.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

using std::string;
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

DEFINE_RUNTIME_string(ysql_sequence_cache_method, "connection",
    "Where sequence values are cached for both existing and new sequences. Valid values are "
    "\"connection\" and \"server\"");

DEFINE_RUNTIME_bool(ysql_ddl_transaction_wait_for_ddl_verification, true,
                    "If set, DDL transactions will wait for DDL verification to complete before "
                    "returning to the client. ");

DECLARE_bool(ysql_serializable_isolation_for_ddl_txn);
DECLARE_bool(ysql_yb_enable_ddl_atomicity_infra);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);

namespace yb::tserver {
namespace {

constexpr const size_t kPgSequenceLastValueColIdx = 2;
constexpr const size_t kPgSequenceIsCalledColIdx = 3;
const std::string kTxnLogPrefixTagSource("Session ");
client::LogPrefixName kTxnLogPrefixTag = client::LogPrefixName::Build<&kTxnLogPrefixTagSource>();

std::string SessionLogPrefix(uint64_t id) {
  return Format("Session id $0: ", id);
}

string GetStatusStringSet(const client::CollectedErrors& errors) {
  std::set<string> status_strings;
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

boost::optional<YBPgErrorCode> PsqlErrorCode(const Status& status) {
  const uint8_t* err_data = status.ErrorData(PgsqlErrorTag::kCategory);
  if (err_data) {
    return PgsqlErrorTag::Decode(err_data);
  }
  return boost::none;
}

// Get a common Postgres error code from the status and all errors, and append it to a previous
// Status.
// If any of those have different conflicting error codes, previous result is returned as-is.
Status AppendPsqlErrorCode(const Status& status,
                           const client::CollectedErrors& errors) {
  boost::optional<YBPgErrorCode> common_psql_error =  boost::make_optional(false, YBPgErrorCode());
  for(const auto& error : errors) {
    const auto psql_error = PsqlErrorCode(error->status());
    if (!common_psql_error) {
      common_psql_error = psql_error;
    } else if (psql_error && common_psql_error != psql_error) {
      common_psql_error = boost::none;
      break;
    }
  }
  return common_psql_error ? status.CloneAndAddErrorCode(PgsqlError(*common_psql_error)) : status;
}

// Get a common transaction error code for all the errors and append it to the previous Status.
Status AppendTxnErrorCode(const Status& status, const client::CollectedErrors& errors) {
  TransactionErrorCode common_txn_error = TransactionErrorCode::kNone;
  constexpr auto kNumTxnErrorCodes = 6;
  static const TransactionErrorCode precedence_list[kNumTxnErrorCodes] = {
      TransactionErrorCode::kDeadlock, TransactionErrorCode::kAborted,
      TransactionErrorCode::kConflict, TransactionErrorCode::kReadRestartRequired,
      TransactionErrorCode::kSnapshotTooOld, TransactionErrorCode::kSkipLocking};
  size_t common_txn_error_idx = kNumTxnErrorCodes;
  for (const auto& error : errors) {
    const TransactionErrorCode txn_error = TransactionError(error->status()).value();
    if (txn_error == TransactionErrorCode::kNone || txn_error == common_txn_error) {
      continue;
    }

    size_t txn_error_idx = std::find(
        precedence_list, precedence_list + kNumTxnErrorCodes, txn_error) - precedence_list;
    if ((txn_error_idx >= kNumTxnErrorCodes)) {
      LOG(DFATAL) << "Unknown transaction error code: " << ToString(txn_error);
      return status;
    }

    if ((common_txn_error == TransactionErrorCode::kNone) ||
        (txn_error_idx < common_txn_error_idx)) {
      common_txn_error = txn_error;
      common_txn_error_idx = txn_error_idx;
      VLOG(4) << "updating common_txn_error to: " << ToString(common_txn_error);
    }
  }

  return (common_txn_error != TransactionErrorCode::kNone) ?
    status.CloneAndAddErrorCode(TransactionError(common_txn_error)) : status;
}

// Given a set of errors from operations, this function attempts to combine them into one status
// that is later passed to PostgreSQL and further converted into a more specific error code.
Status CombineErrorsToStatus(const client::CollectedErrors& errors, const Status& status) {
  if (errors.empty())
    return status;

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

  Status result =
    status.ok()
    ? STATUS(InternalError, GetStatusStringSet(errors))
    : status.CloneAndAppend(". Errors from tablet servers: " + GetStatusStringSet(errors));

  return AppendTxnErrorCode(AppendPsqlErrorCode(result, errors), errors);
}

Status ProcessUsedReadTime(uint64_t session_id,
                           const client::YBPgsqlOp& op,
                           PgPerformResponsePB* resp,
                           PgClientSession::UsedReadTimeData* used_read_time) {
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
  }

  if (used_read_time) {
    RSTATUS_DCHECK(
        !used_read_time->value, IllegalState,
        "Multiple used_read_time are not expected: $0, $1",
        used_read_time->value, op_used_read_time);
    *used_read_time = {op_used_read_time, read_op.used_tablet()};
  }
  return Status::OK();
}

Status HandleResponse(uint64_t session_id,
                      const client::YBPgsqlOp& op,
                      PgPerformResponsePB* resp,
                      PgClientSession::UsedReadTimeData* used_read_time) {
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

Status GetTable(const TableId& table_id, PgTableCache* cache, client::YBTablePtr* table) {
  if (*table && (**table).id() == table_id) {
    return Status::OK();
  }
  *table = VERIFY_RESULT(cache->Get(table_id));
  return Status::OK();
}

Result<PgClientSessionOperations> PrepareOperations(
    PgPerformRequestPB* req, client::YBSession* session, rpc::Sidecars* sidecars,
    PgTableCache* table_cache) {
  auto write_time = HybridTime::FromPB(req->write_time());
  std::vector<std::shared_ptr<client::YBPgsqlOp>> ops;
  ops.reserve(req->ops().size());
  client::YBTablePtr table;
  bool finished = false;
  auto se = ScopeExit([&finished, session] {
    if (!finished) {
      session->Abort();
    }
  });
  const auto read_from_followers = req->options().read_from_followers();
  for (auto& op : *req->mutable_ops()) {
    if (op.has_read()) {
      auto& read = *op.mutable_read();
      RETURN_NOT_OK(GetTable(read.table_id(), table_cache, &table));
      auto read_op = std::make_shared<client::YBPgsqlReadOp>(table, sidecars, &read);
      if (read_from_followers) {
        read_op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
      }
      ops.push_back(read_op);
      session->Apply(std::move(read_op));
    } else {
      auto& write = *op.mutable_write();
      RETURN_NOT_OK(GetTable(write.table_id(), table_cache, &table));
      auto write_op = std::make_shared<client::YBPgsqlWriteOp>(table, sidecars, &write);
      if (write_time) {
        write_op->SetWriteTime(write_time);
        write_time = HybridTime::kInvalid;
      }
      ops.push_back(write_op);
      session->Apply(std::move(write_op));
    }
  }
  finished = true;
  return ops;
}

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

struct PerformData {
  uint64_t session_id;
  PgTableCache& table_cache;

  PgPerformRequestPB& req;
  PgPerformResponsePB& resp;
  rpc::Sidecars& sidecars;

  PgClientSessionOperations ops;
  client::YBTransactionPtr transaction;
  PgMutationCounter* pg_node_level_mutation_counter;
  SubTransactionId subtxn_id;
  PgClientSession::UsedReadTimeApplier used_read_time_applier;
  PgResponseCache::Setter cache_setter;
  HybridTime used_in_txn_limit;

  PerformData(uint64_t session_id_, PgTableCache* table_cache_, PgPerformRequestPB* req_,
              PgPerformResponsePB* resp_, rpc::Sidecars* sidecars_)
      : session_id(session_id_), table_cache(*table_cache_), req(*req_), resp(*resp_),
        sidecars(*sidecars_) {}

  virtual ~PerformData() = default;
  virtual void SendResponse() = 0;

  void FlushDone(client::FlushStatus* flush_status) {
    PgClientSession::UsedReadTimeData used_read_time;
    if (VLOG_IS_ON(3)) {
      std::vector<string> status_strings;
      for (const auto& error : flush_status->errors) {
        status_strings.push_back(error->status().ToString());
      }
      VLOG(3) << SessionLogPrefix(session_id) << "Flush status: " << flush_status->status
          << ", Errors: " << AsString(status_strings);
    }
    auto status = CombineErrorsToStatus(flush_status->errors, flush_status->status);
    VLOG(3) << SessionLogPrefix(session_id) << "Combined status: " << status;
    if (status.ok()) {
      status = ProcessResponse(used_read_time_applier ? &used_read_time : nullptr);
    }
    if (!status.ok()) {
      StatusToPB(status, resp.mutable_status());
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

 private:
  Status ProcessResponse(PgClientSession::UsedReadTimeData* used_read_time) {
    int idx = 0;
    for (const auto& op : ops) {
      const auto status = HandleResponse(session_id, *op, &resp, used_read_time);
      if (!status.ok()) {
        if (PgsqlRequestStatus(status) == PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH) {
          table_cache.Invalidate(op->table()->id());
        }
        VLOG_WITH_FUNC(2) << SessionLogPrefix(session_id) << "status: " << status
                          << ", failed op[" << idx << "]: " << AsString(op);
        return status.CloneAndAddErrorCode(OpIndex(idx));
      }
      // In case of non-DDL write operations, increase mutation counters
      if (!op->read_only() && (!req.has_options() || !req.options().ddl_mode()) &&
          GetAtomicFlag(&FLAGS_ysql_enable_table_mutation_counter) &&
          pg_node_level_mutation_counter) {
        const auto& table_id = down_cast<const client::YBPgsqlWriteOp&>(*op).table()->id();

        VLOG(4) << SessionLogPrefix(session_id) << "Increasing "
                << (transaction ? "transaction's mutation counters"
                                : "pg_node_level_mutation_counter")
                << " by 1 for table_id: " << table_id;

        // If there is no distributed transaction, it means we can directly update the TServer
        // level aggregate. Otherwise increase the transaction level aggregate.
        if (!transaction) {
          pg_node_level_mutation_counter->Increase(table_id, 1);
        } else {
          transaction->IncreaseMutationCounts(subtxn_id, table_id, 1);
        }
      }
      if (op->response().is_backfill_batch_done() &&
          op->type() == client::YBOperation::Type::PGSQL_READ &&
          down_cast<const client::YBPgsqlReadOp&>(*op).request().is_for_backfill()) {
        // After backfill table schema version is updated, so we reset cache in advance.
        table_cache.Invalidate(op->table()->id());
      }
      ++idx;
    }
    auto& responses = *resp.mutable_responses();
    responses.Reserve(narrow_cast<int>(ops.size()));
    for (const auto& op : ops) {
      auto& op_resp = *responses.Add();
      op_resp.Swap(op->mutable_response());
      if (op->has_sidecar()) {
        op_resp.set_rows_data_sidecar(narrow_cast<int>(op->sidecar_index()));
      }
      if (op_resp.has_paging_state()) {
        if (resp.has_catalog_read_time()) {
          // Prevent further paging reads from read restart errors.
          // See the ProcessUsedReadTime(...) function for details.
          *op_resp.mutable_paging_state()->mutable_read_time() = resp.catalog_read_time();
        }
        if (transaction && transaction->isolation() == IsolationLevel::SERIALIZABLE_ISOLATION) {
          // Delete read time from paging state since a read time is not used in serializable
          // isolation level.
          op_resp.mutable_paging_state()->clear_read_time();
        }
      }
      op_resp.set_partition_list_version(op->table()->GetPartitionListVersion());
    }
    if (used_in_txn_limit) {
      resp.set_used_in_txn_limit_ht(used_in_txn_limit.ToUint64());
    }

    return Status::OK();
  }
};

struct SharedExchangeQueryParams {
  PgPerformRequestPB exchange_req;
  PgPerformResponsePB exchange_resp;
  rpc::Sidecars exchange_sidecars;
};

struct SharedExchangeQuery : public SharedExchangeQueryParams, public PerformData {
  std::weak_ptr<PgClientSession> session;

  SharedExchange* exchange;

  CoarseTimePoint deadline;

  CountDownLatch latch{1};

  SharedExchangeQuery(
      std::shared_ptr<PgClientSession> session_, PgTableCache* table_cache_,
      SharedExchange* exchange_)
      : PerformData(
            session_->id(), table_cache_, &exchange_req, &exchange_resp, &exchange_sidecars),
        session(std::move(session_)), exchange(exchange_) {
  }

  // Initialize query from data stored in exchange with specified size.
  // The serialized query has the following format:
  // 8 bytes - timeout in milliseconds.
  // remaining bytes - serialized PgPerformRequestPB protobuf.
  Status Init(size_t size) {
    auto input = to_uchar_ptr(exchange->Obtain(size));
    auto end = input + size;
    deadline = CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(LittleEndian::Load64(input));
    input += sizeof(uint64_t);
    return pb_util::ParseFromArray(&req, input, end - input);
  }

  void Wait() {
    latch.Wait();
  }

  void SendResponse() override {
    using google::protobuf::io::CodedOutputStream;
    rpc::ResponseHeader header;
    header.set_call_id(42);
    auto resp_size = resp.ByteSizeLong();
    sidecars.MoveOffsetsTo(resp_size, header.mutable_sidecar_offsets());
    auto header_size = header.ByteSize();
    auto body_size = narrow_cast<uint32_t>(resp_size + sidecars.size());
    auto full_size =
        CodedOutputStream::VarintSize32(header_size) + header_size +
        CodedOutputStream::VarintSize32(body_size) + body_size;
    auto* start = exchange->Obtain(full_size);
    RefCntBuffer buffer;
    if (!start) {
      buffer = RefCntBuffer(full_size - sidecars.size());
      start = pointer_cast<std::byte*>(buffer.data());
    }
    auto* out = start;
    out = WriteVarint32ToArray(header_size, out);
    out = SerializeWithCachedSizesToArray(header, out);
    out = WriteVarint32ToArray(body_size, out);
    out = SerializeWithCachedSizesToArray(resp, out);
    if (!buffer) {
      sidecars.CopyTo(out);
      out += sidecars.size();
      DCHECK_EQ(out - start, full_size);
      exchange->Respond(full_size);
    } else {
      auto locked_session = session.lock();
      auto id = locked_session ? locked_session->SaveData(buffer, std::move(sidecars.buffer())) : 0;
      exchange->Respond(kTooBigResponseMask | id);
    }
    latch.CountDown();
  }
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
    return HybridTime();
  }
  auto in_txn_limit = HybridTime::FromPB(options.in_txn_limit_ht().value());
  return in_txn_limit ? in_txn_limit : clock->Now();
}

Status Commit(client::YBTransaction* txn, PgResponseCache::Disabler disabler) {
  return txn->CommitFuture().get();
}

PgClientSession::UsedReadTimeApplier BuildUsedReadTimeApplier(
    std::weak_ptr<PgClientSession::UsedReadTime>&& used_read_time, size_t signature) {
  return [used_read_time_weak = std::move(used_read_time), signature](
      PgClientSession::UsedReadTimeData&& read_time_data) {
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

} // namespace

PgClientSession::PgClientSession(
    TransactionBuilder&& transaction_builder, SharedThisSource shared_this_source, uint64_t id,
    client::YBClient* client, const scoped_refptr<ClockBase>& clock, PgTableCache* table_cache,
    const TserverXClusterContextIf* xcluster_context,
    PgMutationCounter* pg_node_level_mutation_counter, PgResponseCache* response_cache,
    PgSequenceCache* sequence_cache)
    : shared_this_(std::shared_ptr<PgClientSession>(std::move(shared_this_source), this)),
      id_(id),
      client_(*client),
      clock_(clock),
      transaction_builder_(std::move(transaction_builder)),
      table_cache_(*table_cache),
      xcluster_context_(xcluster_context),
      pg_node_level_mutation_counter_(pg_node_level_mutation_counter),
      response_cache_(*response_cache),
      sequence_cache_(*sequence_cache) {}

Status PgClientSession::CreateTable(
    const PgCreateTableRequestPB& req, PgCreateTableResponsePB* resp, rpc::RpcContext* context) {
  PgCreateTable helper(req);
  RETURN_NOT_OK(helper.Prepare());
  const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
      req.use_transaction(), context->GetClientDeadline()));
  RETURN_NOT_OK(helper.Exec(&client(), metadata, context->GetClientDeadline()));
  VLOG_WITH_PREFIX(1) << __func__ << ": " << req.table_name();
  const auto& indexed_table_id = helper.indexed_table_id();
  if (indexed_table_id.IsValid()) {
    table_cache_.Invalidate(indexed_table_id.GetYbTableId());
  }
  return Status::OK();
}

Status PgClientSession::CreateDatabase(
    const PgCreateDatabaseRequestPB& req, PgCreateDatabaseResponsePB* resp,
    rpc::RpcContext* context) {
  bool is_clone =
      req.source_database_name() != "" &&
      req.source_database_name() != "template0" &&
      req.source_database_name() != "template1";
  std::optional<YbCloneInfo> yb_clone_info = std::nullopt;
  if (is_clone) {
    yb_clone_info = YbCloneInfo {
      .clone_time = req.clone_time(),
      .src_db_name = req.source_database_name().c_str(),
      .src_owner = req.source_owner().c_str(),
      .tgt_owner = req.target_owner().c_str(),
    };
  }
  return client().CreateNamespace(
      req.database_name(), YQL_DATABASE_PGSQL, "" /* creator_role_name */,
      GetPgsqlNamespaceId(req.database_oid()),
      req.source_database_oid() != kPgInvalidOid ? GetPgsqlNamespaceId(req.source_database_oid())
                                                 : "",
      req.next_oid(),
      VERIFY_RESULT(GetDdlTransactionMetadata(req.use_transaction(), context->GetClientDeadline())),
      req.colocated(), context->GetClientDeadline(), yb_clone_info);
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
  const auto yb_table_id = PgObjectId::GetYbTableIdFromPB(req.table_id());
  const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
      true /* use_transaction */, context->GetClientDeadline()));
  // If ddl rollback is enabled, the table will not be deleted now, so we cannot wait for the
  // table/index deletion to complete. The table will be deleted in the background only after the
  // transaction has been determined to be a success.
  if (req.index()) {
    client::YBTableName indexed_table;
    RETURN_NOT_OK(client().DeleteIndexTable(
        yb_table_id, &indexed_table, !YsqlDdlRollbackEnabled() /* wait */,
        metadata, context->GetClientDeadline()));
    indexed_table.SetIntoTableIdentifierPB(resp->mutable_indexed_table());
    table_cache_.Invalidate(indexed_table.table_id());
    table_cache_.Invalidate(yb_table_id);
    return Status::OK();
  }

  RETURN_NOT_OK(client().DeleteTable(yb_table_id, !YsqlDdlRollbackEnabled(), metadata,
        context->GetClientDeadline()));
  table_cache_.Invalidate(yb_table_id);
  return Status::OK();
}

Status PgClientSession::AlterDatabase(
    const PgAlterDatabaseRequestPB& req, PgAlterDatabaseResponsePB* resp,
    rpc::RpcContext* context) {
  const auto alterer = client().NewNamespaceAlterer(
      req.database_name(), GetPgsqlNamespaceId(req.database_oid()));
  alterer->SetDatabaseType(YQL_DATABASE_PGSQL);
  alterer->RenameTo(req.new_name());
  return alterer->Alter(context->GetClientDeadline());
}

Status PgClientSession::AlterTable(
    const PgAlterTableRequestPB& req, PgAlterTableResponsePB* resp, rpc::RpcContext* context) {
  const auto table_id = PgObjectId::GetYbTableIdFromPB(req.table_id());
  const auto alterer = client().NewTableAlterer(table_id);
  const auto txn = VERIFY_RESULT(GetDdlTransactionMetadata(
      req.use_transaction(), context->GetClientDeadline()));
  if (txn) {
    alterer->part_of_transaction(txn);
  }
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
  if (!req.rename_table().table_name().empty()) {
    client::YBTableName new_table_name(
        YQL_DATABASE_PGSQL, req.rename_table().database_name(), req.rename_table().table_name());
    alterer->RenameTo(new_table_name);
  }
  if (req.has_replica_identity()) {
    client::YBTablePtr yb_table;
    RETURN_NOT_OK(GetTable(table_id, &table_cache_, &yb_table));
    auto table_properties = yb_table->schema().table_properties();
    PgReplicaIdentity replica_identity;
    RETURN_NOT_OK(
        GetReplicaIdentityEnumValue(req.replica_identity().replica_identity(), &replica_identity));
    table_properties.SetReplicaIdentity(replica_identity);
    alterer->SetTableProperties(table_properties);
  }
  alterer->timeout(context->GetClientDeadline() - CoarseMonoClock::now());
  RETURN_NOT_OK(alterer->Alter());
  table_cache_.Invalidate(table_id);
  return Status::OK();
}

Status PgClientSession::TruncateTable(
    const PgTruncateTableRequestPB& req, PgTruncateTableResponsePB* resp,
    rpc::RpcContext* context) {
  return client().TruncateTable(PgObjectId::GetYbTableIdFromPB(req.table_id()));
}

Status PgClientSession::CreateReplicationSlot(
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
      case REPLICATION_SLOT_UNKNOWN_SNAPSHOT:
        // Crash in debug and return InvalidArgument in release mode.
        RSTATUS_DCHECK(false, InvalidArgument, "invalid snapshot_action UNKNOWN");
      default:
        return STATUS_FORMAT(InvalidArgument, "invalid snapshot_action $0", req.snapshot_action());
    }
  }

  uint64_t consistent_snapshot_time;
  auto stream_result = VERIFY_RESULT(client().CreateCDCSDKStreamForNamespace(
      GetPgsqlNamespaceId(req.database_oid()), options,
      /* populate_namespace_id_as_table_id */ false,
      ReplicationSlotName(req.replication_slot_name()),
      req.output_plugin_name(), snapshot_option,
      context->GetClientDeadline(), &consistent_snapshot_time));
  *resp->mutable_stream_id() = stream_result.ToString();
  resp->set_cdcsdk_consistent_snapshot_time(consistent_snapshot_time);
  return Status::OK();
}

Status PgClientSession::DropReplicationSlot(
    const PgDropReplicationSlotRequestPB& req, PgDropReplicationSlotResponsePB* resp,
    rpc::RpcContext* context) {
  return client().DeleteCDCStream(ReplicationSlotName(req.replication_slot_name()));
}

Status PgClientSession::WaitForBackendsCatalogVersion(
    const PgWaitForBackendsCatalogVersionRequestPB& req,
    PgWaitForBackendsCatalogVersionResponsePB* resp,
    rpc::RpcContext* context) {
  // TODO(jason): send deadline to client.
  const int num_lagging_backends = VERIFY_RESULT(client().WaitForYsqlBackendsCatalogVersion(
      req.database_oid(), req.catalog_version(), context->GetClientDeadline()));
  resp->set_num_lagging_backends(num_lagging_backends);
  return Status::OK();
}

Status PgClientSession::BackfillIndex(
    const PgBackfillIndexRequestPB& req, PgBackfillIndexResponsePB* resp,
    rpc::RpcContext* context) {
  return client().BackfillIndex(
      PgObjectId::GetYbTableIdFromPB(req.table_id()), /* wait= */ true,
      context->GetClientDeadline());
}

Status PgClientSession::CreateTablegroup(
    const PgCreateTablegroupRequestPB& req, PgCreateTablegroupResponsePB* resp,
    rpc::RpcContext* context) {
  const auto id = PgObjectId::FromPB(req.tablegroup_id());
  auto tablespace_id = PgObjectId::FromPB(req.tablespace_id());
  const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
      true /* use_transaction */, context->GetClientDeadline()));
  auto s = client().CreateTablegroup(
      req.database_name(),
      GetPgsqlNamespaceId(id.database_oid),
      id.GetYbTablegroupId(),
      tablespace_id.IsValid() ? tablespace_id.GetYbTablespaceId() : "",
      metadata);
  if (s.ok()) {
    return Status::OK();
  }

  if (s.IsAlreadyPresent()) {
    return STATUS(InvalidArgument, "Duplicate tablegroup");
  }

  return STATUS_FORMAT(
      InvalidArgument, "Invalid table definition: $0",
      s.ToString(false /* include_file_and_line */, false /* include_code */));
}

Status PgClientSession::DropTablegroup(
    const PgDropTablegroupRequestPB& req, PgDropTablegroupResponsePB* resp,
    rpc::RpcContext* context) {
  const auto id = PgObjectId::FromPB(req.tablegroup_id());
  const auto* metadata = VERIFY_RESULT(GetDdlTransactionMetadata(
      true /* use_transaction */, context->GetClientDeadline()));
  const auto status =
      client().DeleteTablegroup(GetPgsqlTablegroupId(id.database_oid, id.object_oid), metadata);
  if (status.IsNotFound()) {
    return Status::OK();
  }
  return status;
}

Status PgClientSession::RollbackToSubTransaction(
    const PgRollbackToSubTransactionRequestPB& req, PgRollbackToSubTransactionResponsePB* resp,
    rpc::RpcContext* context) {
  VLOG_WITH_PREFIX_AND_FUNC(2) << req.ShortDebugString();
  RSTATUS_DCHECK_GE(
      req.sub_transaction_id(), kMinSubTransactionId,
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
   * Savepoint 1" will only revert things done in the DDL's context and not the commands that follow
   * Savepoint 1 in the DML's context.
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
  auto kind = PgClientSessionKind::kPlain;

  if (req.has_options() && req.options().ddl_mode())
    kind = PgClientSessionKind::kDdl;

  auto transaction = Transaction(kind);

  if (!transaction) {
    LOG_WITH_PREFIX_AND_FUNC(WARNING)
      << "RollbackToSubTransaction " << req.sub_transaction_id()
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
  //  So, it is neccessary to first bump the active-sub txn id to 11 and then perform the rollback.
  //  Otherwise, an error will be thrown that the sub-txn doesn't exist when
  //  YBCRollbackToSubTransaction() is called for sub-txn id 11.

  if (req.has_options()) {
    RSTATUS_DCHECK_GE(
        req.options().active_sub_transaction_id(), kMinSubTransactionId,
        InvalidArgument,
        Format("Expected active_sub_transaction_id to be >= $0", kMinSubTransactionId));
    transaction->SetActiveSubTransaction(req.options().active_sub_transaction_id());
  }

  RSTATUS_DCHECK(transaction->HasSubTransaction(req.sub_transaction_id()), InvalidArgument,
                 Format("Transaction of kind $0 doesn't have sub transaction $1",
                        kind == PgClientSessionKind::kPlain ? "kPlain" : "kDdl",
                        req.sub_transaction_id()));

  return transaction->RollbackToSubTransaction(req.sub_transaction_id(),
                                               context->GetClientDeadline());
}

// The below RPC is DEPRECATED.
Status PgClientSession::SetActiveSubTransaction(
    const PgSetActiveSubTransactionRequestPB& req, PgSetActiveSubTransactionResponsePB* resp,
    rpc::RpcContext* context) {
  VLOG_WITH_PREFIX_AND_FUNC(2) << req.ShortDebugString();

  auto kind = PgClientSessionKind::kPlain;
  if (req.has_options()) {
    if (req.options().ddl_mode()) {
      kind = PgClientSessionKind::kDdl;
    } else {
      RETURN_NOT_OK(BeginTransactionIfNecessary(req.options(), context->GetClientDeadline()));
      txn_serial_no_ = req.options().txn_serial_no();
    }
  }

  auto transaction = Transaction(kind);

  SCHECK(transaction, IllegalState,
         Format("Set active sub transaction $0, when no transaction is running",
                req.sub_transaction_id()));

  DCHECK_GE(req.sub_transaction_id(), kMinSubTransactionId);
  transaction->SetActiveSubTransaction(req.sub_transaction_id());
  return Status::OK();
}

Status PgClientSession::FinishTransaction(
    const PgFinishTransactionRequestPB& req, PgFinishTransactionResponsePB* resp,
    rpc::RpcContext* context) {
  saved_priority_ = std::nullopt;
  auto is_ddl = false;
  auto kind = PgClientSessionKind::kPlain;
  auto has_docdb_schema_changes = false;
  std::optional<uint32_t> silently_altered_db;
  if (req.has_ddl_mode()) {
    const auto& ddl_mode = req.ddl_mode();
    is_ddl = true;
    kind = PgClientSessionKind::kDdl;
    has_docdb_schema_changes = ddl_mode.has_docdb_schema_changes();
    if (ddl_mode.has_silently_altered_db()) {
      silently_altered_db = ddl_mode.silently_altered_db().value();
    }
  }
  auto& txn = Transaction(kind);
  if (!txn) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "ddl: " << is_ddl << ", no running transaction";
    return Status::OK();
  }

  const TransactionMetadata* metadata = nullptr;
  if (has_docdb_schema_changes) {
    metadata = VERIFY_RESULT(GetDdlTransactionMetadata(true, context->GetClientDeadline()));
    LOG_IF(DFATAL, !metadata) << "metadata is required";
  }
  client::YBTransactionPtr txn_value;
  txn.swap(txn_value);
  Session(kind)->SetTransaction(nullptr);

  if (req.commit()) {
    const auto commit_status = Commit(
        txn_value.get(),
        silently_altered_db ? response_cache_.Disable(*silently_altered_db)
                            : PgResponseCache::Disabler());

    VLOG_WITH_PREFIX_AND_FUNC(2)
        << "ddl: " << is_ddl << ", txn: " << txn_value->id()
        << ", commit: " << commit_status;
    // If commit_status is not ok, we cannot be sure whether the commit was successful or not. It
    // is possible that the commit succeeded at the transaction coordinator but we failed to get
    // the response back. Thus we will not report any status to the YB-Master in this case. It
    // will run its background task to figure out whether the transaction succeeded or failed.
    if (!commit_status.ok()) {
      return commit_status;
    } else if (GetAtomicFlag(&FLAGS_ysql_enable_table_mutation_counter) &&
               pg_node_level_mutation_counter_) {
      // Gather # of mutated rows for each table (count only the committed sub-transactions).
      auto table_mutations = txn_value->GetTableMutationCounts();
      VLOG_WITH_PREFIX(4) << "Incrementing global mutation count using table to mutations map: "
                          << AsString(table_mutations) << " for txn: " << txn_value->id();
      for(const auto& [table_id, mutation_count] : table_mutations) {
        pg_node_level_mutation_counter_->Increase(table_id, mutation_count);
      }
    }
  } else {
    VLOG_WITH_PREFIX_AND_FUNC(2)
        << "ddl: " << is_ddl << ", txn: " << txn_value->id() << ", abort";
    txn_value->Abort();
  }

  // If this transaction was DDL that had DocDB syscatalog changes, then the YB-Master may have
  // any operations postponed to the end of transaction. Report the status of the transaction and
  // wait for the post-processing by YB-Master to end.
  if (YsqlDdlRollbackEnabled() && has_docdb_schema_changes && metadata) {
    if (FLAGS_report_ysql_ddl_txn_status_to_master) {
      // If we failed to report the status of this DDL transaction, we can just log and ignore it,
      // as the poller in the YB-Master will figure out the status of this transaction using the
      // transaction status tablet and PG catalog.
      ERROR_NOT_OK(client().ReportYsqlDdlTxnStatus(*metadata, req.commit()),
                  "Sending ReportYsqlDdlTxnStatus call failed");
    }

    if (FLAGS_ysql_ddl_transaction_wait_for_ddl_verification) {
      // Wait for DDL verification to end. This may include actions such as a) removing an added
      // column in case of ADD COLUMN abort b) dropping a column marked for deletion in case of
      // DROP COLUMN commit. c) removing DELETE marker on a column if DROP COLUMN aborted d) Roll
      // back changes to table/column names in case of transaction abort. d) dropping a table in
      // case of DROP TABLE commit. All the above actions take place only after the transaction
      // is completed.
      ERROR_NOT_OK(client().WaitForDdlVerificationToFinish(*metadata),
                  "WaitForDdlVerificationToFinish call failed");
    }
  }
  return Status::OK();
}

struct RpcPerformQuery : public PerformData {
  rpc::RpcContext context;

  RpcPerformQuery(
      uint64_t session_id_, PgTableCache* table_cache_, PgPerformRequestPB* req,
      PgPerformResponsePB* resp, rpc::RpcContext* context_)
      : PerformData(session_id_, table_cache_, req, resp, &context_->sidecars()),
        context(std::move(*context_)) {}

  void SendResponse() override {
    context.RespondSuccess();
  }
};

Status PgClientSession::Perform(
    PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext* context) {
  VLOG(5) << "Perform rpc: " << req->ShortDebugString();
  auto data = std::make_shared<RpcPerformQuery>(id_, &table_cache_, req, resp, context);
  auto status = DoPerform(data, data->context.GetClientDeadline(), &data->context);
  if (!status.ok()) {
    *context = std::move(data->context);
    return status;
  }
  return Status::OK();
}

template <class DataPtr>
Status PgClientSession::DoPerform(const DataPtr& data, CoarseTimePoint deadline,
                                  rpc::RpcContext* context) {
  auto& options = *data->req.mutable_options();
  if (const auto& wait_state = ash::WaitStateInfo::CurrentWaitState()) {
    if (options.has_ash_metadata()) {
      wait_state->UpdateMetadataFromPB(options.ash_metadata());
      wait_state->set_session_id(id_);
    }
  }
  auto ddl_mode = options.ddl_mode() || options.yb_non_ddl_txn_for_sys_tables_allowed();
  if (!ddl_mode && xcluster_context_ && xcluster_context_->IsReadOnlyMode(options.namespace_id())) {
    for (const auto& op : data->req.ops()) {
      if (op.has_write() && !op.write().is_backfill()) {
        // Only DDLs and index backfill is allowed in xcluster read only mode.
        return STATUS(
            IllegalState,
            "Data modification is forbidden on database that is the target of a transactional "
            "xCluster replication");
      }
    }
  }

  if (options.has_caching_info()) {
    data->cache_setter = VERIFY_RESULT(response_cache_.Get(
        options.mutable_caching_info(), &data->resp, &data->sidecars, deadline));
    if (!data->cache_setter) {
      data->SendResponse();
      return Status::OK();
    }
  }

  const auto in_txn_limit = GetInTxnLimit(options, clock_.get());
  VLOG_WITH_PREFIX(5) << "using in_txn_limit_ht: " << in_txn_limit;
  auto session_info = VERIFY_RESULT(SetupSession(data->req, deadline, in_txn_limit));
  auto* session = session_info.first.session.get();
  auto& transaction = session_info.first.transaction;

  if (context) {
    if (options.trace_requested()) {
      context->EnsureTraceCreated();
      if (transaction) {
        transaction->EnsureTraceCreated();
        context->trace()->AddChildTrace(transaction->trace());
        transaction->trace()->set_must_print(true);
      } else {
        context->trace()->set_must_print(true);
      }
    }
  }
  ADOPT_TRACE(context ? context->trace() : Trace::CurrentTrace());

  data->used_read_time_applier = session_info.second;
  data->used_in_txn_limit = in_txn_limit;
  data->transaction = std::move(transaction);
  data->pg_node_level_mutation_counter = pg_node_level_mutation_counter_;
  data->subtxn_id = options.active_sub_transaction_id();

  data->ops = VERIFY_RESULT(PrepareOperations(
      &data->req, session, &data->sidecars, &table_cache_));
  TracePtr trace(Trace::CurrentTrace());
  session->FlushAsync([this, data, trace](client::FlushStatus* flush_status) {
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
  });

  return Status::OK();
}

void PgClientSession::ProcessReadTimeManipulation(
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
    case ReadTimeManipulation::ENSURE_READ_TIME_IS_SET :
      if (!read_point.GetReadTime() || read_time_serial_no_ != read_time_serial_no) {
          // Clamp read uncertainty window when requested by the query layer.
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

Status PgClientSession::UpdateReadPointForXClusterConsistentReads(
    const PgPerformOptionsPB& options, const CoarseTimePoint& deadline,
    ConsistentReadPoint* read_point) {
  const auto& namespace_id = options.namespace_id();
  const auto& requested_read_time = read_point->GetReadTime().read;

  // Early exit if namespace not provided or atomic reads not enabled
  if (namespace_id.empty() || !xcluster_context_ || !options.use_xcluster_database_consistency()) {
    return Status::OK();
  }

  auto xcluster_safe_time = VERIFY_RESULT(xcluster_context_->GetSafeTime(namespace_id));
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
    read_point->SetReadTime(ReadHybridTime::SingleTime(*xcluster_safe_time), {} /* local_limits */);
    VLOG_WITH_PREFIX(3) << "Reset read time to xCluster safe time: " << read_point->GetReadTime();
    return Status::OK();
  }

  // If read_point is set to a time ahead of the xcluster safe time then we wait.
  return WaitFor(
      [&requested_read_time, &namespace_id, this]() -> Result<bool> {
        auto safe_time = VERIFY_RESULT(xcluster_context_->GetSafeTime(namespace_id));
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

Result<std::pair<PgClientSession::SessionData, PgClientSession::UsedReadTimeApplier>>
PgClientSession::SetupSession(
    const PgPerformRequestPB& req, CoarseTimePoint deadline, HybridTime in_txn_limit) {
  const auto& options = req.options();
  PgClientSessionKind kind;
  if (options.use_catalog_session()) {
    SCHECK(!options.read_from_followers(),
           InvalidArgument,
           "Reading catalog from followers is not allowed");
    kind = PgClientSessionKind::kCatalog;
    EnsureSession(kind, deadline);
  } else if (options.ddl_mode()) {
    kind = PgClientSessionKind::kDdl;
    EnsureSession(kind, deadline);
    RETURN_NOT_OK(GetDdlTransactionMetadata(true /* use_transaction */, deadline));
  } else {
    kind = PgClientSessionKind::kPlain;
    EnsureSession(kind, deadline);
    RETURN_NOT_OK(CheckPlainSessionPendingUsedReadTime());
    RETURN_NOT_OK(BeginTransactionIfNecessary(options, deadline));
  }

  auto& session_data = GetSessionData(kind);
  auto& session = *session_data.session;
  auto* transaction = session_data.transaction.get();

  VLOG_WITH_PREFIX_AND_FUNC(4) << options.ShortDebugString() << ", deadline: "
                               << MonoDelta(deadline - CoarseMonoClock::now());

  const auto txn_serial_no = options.txn_serial_no();
  const auto read_time_serial_no = options.read_time_serial_no();
  UsedReadTimeApplier used_read_time_applier;
  if (options.restart_transaction()) {
    if (options.ddl_mode()) {
      return STATUS(NotSupported, "Restarting a DDL transaction not supported");
    }
    session_data.transaction = VERIFY_RESULT(RestartTransaction(&session, transaction));
    transaction = session_data.transaction.get();
  } else {
    const auto has_time_manipulation =
        options.read_time_manipulation() != ReadTimeManipulation::NONE;
    RSTATUS_DCHECK(
        !(has_time_manipulation && options.has_read_time()),
        IllegalState, "read_time_manipulation and read_time fields can't be satisfied together");

    if (has_time_manipulation) {
      RSTATUS_DCHECK(
          kind == PgClientSessionKind::kPlain, IllegalState,
          "Read time manipulation can't be specified for non kPlain sessions");
      ProcessReadTimeManipulation(
        options.read_time_manipulation(), read_time_serial_no,
        ClampUncertaintyWindow(options.clamp_uncertainty_window()));
    } else if (options.has_read_time() && options.read_time().has_read_ht()) {
      const auto read_time = ReadHybridTime::FromPB(options.read_time());
      session.SetReadPoint(read_time);
      VLOG_WITH_PREFIX(3) << "Read time: " << read_time;
    } else if (
        options.has_read_time() ||
        options.use_catalog_session() ||
        (kind == PgClientSessionKind::kPlain && (read_time_serial_no_ != read_time_serial_no))) {
      used_read_time_applier = ResetReadPoint(kind);
    } else {
      if (!transaction && kind == PgClientSessionKind::kPlain) {
        RETURN_NOT_OK(CheckPlainSessionReadTimeIsSet());
      }
      VLOG_WITH_PREFIX(3) << "Keep read time: " << session.read_point()->GetReadTime();
    }
  }

  RETURN_NOT_OK(
      UpdateReadPointForXClusterConsistentReads(options, deadline, session.read_point()));

  if (options.defer_read_point()) {
    // Deferring allows avoiding read restart errors in case of a READ ONLY transaction by setting
    // the read point to the global limit (i.e., read time + max clock skew) and hence waiting out
    // any ambiguity of data visibility that might arise from clock skew.
    RSTATUS_DCHECK(
      !transaction, IllegalState,
      "Deferring read point is only allowed in SERIALIZABLE DEFERRABLE READ ONLY, a distributed "
      "transaction is unexpected here.");

    RETURN_NOT_OK(session.read_point()->TrySetDeferredCurrentReadTime());
  }

  // TODO: Reset in_txn_limit which might be on session from past Perform? Not resetting will not
  // cause any issue, but should we reset for safety?
  if (!options.ddl_mode() && !options.use_catalog_session()) {
    txn_serial_no_ = txn_serial_no;
    read_time_serial_no_ = read_time_serial_no;
    if (in_txn_limit) {
      // TODO: Shouldn't the below logic for DDL transactions as well?
      session.SetInTxnLimit(in_txn_limit);
    }

    if (options.clamp_uncertainty_window()
        && !session.read_point()->GetReadTime()) {
      RSTATUS_DCHECK(
        !(transaction && transaction->isolation() == SERIALIZABLE_ISOLATION),
        IllegalState, "Clamping does not apply to SERIALIZABLE txns.");
      // Set read time with clamped uncertainty window when reqeusted by
      // the query layer.
      // Do not mess with the read time if already set.
      session.read_point()->SetCurrentReadTime(ClampUncertaintyWindow::kTrue);
      VLOG_WITH_PREFIX_AND_FUNC(2)
        << "Setting read time to "
        << session.read_point()->GetReadTime()
        << " for read only txn/stmt";
    }
  }

  session.SetDeadline(deadline);

  if (transaction) {
    RSTATUS_DCHECK_GE(
        options.active_sub_transaction_id(), kMinSubTransactionId,
        InvalidArgument,
        Format("Expected active_sub_transaction_id to be >= $0", kMinSubTransactionId));
    transaction->SetActiveSubTransaction(options.active_sub_transaction_id());
    RETURN_NOT_OK(transaction->SetPgTxnStart(options.pg_txn_start_us()));
  }

  return std::make_pair(session_data, used_read_time_applier);
}

PgClientSession::UsedReadTimeApplier PgClientSession::ResetReadPoint(PgClientSessionKind kind) {
  DCHECK(kind == PgClientSessionKind::kCatalog || kind == PgClientSessionKind::kPlain);
  const auto& data = GetSessionData(kind);
  auto& session = *data.session;
  session.SetReadPoint(ReadHybridTime());
  VLOG_WITH_PREFIX(3) << "Reset read time: " << session.read_point()->GetReadTime();

  if (kind != PgClientSessionKind::kPlain || data.transaction) {
    return {};
  }
  if (plain_session_used_read_time_.pending_update) {
    LOG(INFO) << "Previous pending used_read_time update is still active, overwriting";
  }
  plain_session_used_read_time_.pending_update = true;
  auto& used_read_time = plain_session_used_read_time_.value;
  size_t signature = 0;
  std::weak_ptr<UsedReadTime> used_read_time_ptr;
  {
    std::lock_guard guard(used_read_time.lock);
    used_read_time.data.reset();
    used_read_time_ptr = std::shared_ptr<UsedReadTime>{shared_this_.lock(), &used_read_time};
    signature = ++used_read_time.signature;
  }
  return BuildUsedReadTimeApplier(std::move(used_read_time_ptr), signature);
}

std::string PgClientSession::LogPrefix() {
  return SessionLogPrefix(id_);
}

Status PgClientSession::BeginTransactionIfNecessary(
    const PgPerformOptionsPB& options, CoarseTimePoint deadline) {
  RETURN_NOT_OK(DoBeginTransactionIfNecessary(options, deadline));
  const auto& data = GetSessionData(PgClientSessionKind::kPlain);
  data.session->SetForceConsistentRead(client::ForceConsistentRead(!data.transaction));
  return Status::OK();
}

Status PgClientSession::DoBeginTransactionIfNecessary(
    const PgPerformOptionsPB& options, CoarseTimePoint deadline) {

  const auto isolation = static_cast<IsolationLevel>(options.isolation());

  auto priority = options.priority();
  auto& session = EnsureSession(PgClientSessionKind::kPlain, deadline);
  auto& txn = Transaction(PgClientSessionKind::kPlain);
  if (txn && txn_serial_no_ != options.txn_serial_no()) {
    VLOG_WITH_PREFIX(2)
        << "Abort previous transaction, use existing priority: " << options.use_existing_priority()
        << ", new isolation: " << IsolationLevel_Name(isolation);

    if (options.use_existing_priority()) {
      saved_priority_ = txn->GetPriority();
    }
    txn->Abort();
    session->SetTransaction(nullptr);
    txn = nullptr;
  }

  if (isolation == IsolationLevel::NON_TRANSACTIONAL) {
    return Status::OK();
  }

  if (txn) {
    return txn->isolation() != isolation
        ? STATUS_FORMAT(
            IllegalState,
            "Attempt to change isolation level of running transaction from $0 to $1",
            txn->isolation(), isolation)
        : Status::OK();
  }

  txn = transaction_builder_(
    IsDDL::kFalse, client::ForceGlobalTransaction(options.force_global_transaction()), deadline);
  txn->SetLogPrefixTag(kTxnLogPrefixTag, id_);
  if ((isolation == IsolationLevel::SNAPSHOT_ISOLATION ||
           isolation == IsolationLevel::READ_COMMITTED) &&
      txn_serial_no_ == options.txn_serial_no()) {
    RETURN_NOT_OK(CheckPlainSessionReadTimeIsSet());
    txn->InitWithReadPoint(isolation, std::move(*session->read_point()));
    VLOG_WITH_PREFIX(2) << "Start transaction " << IsolationLevel_Name(isolation)
                        << ", id: " << txn->id()
                        << ", kept read time: " << txn->read_point().GetReadTime();
  } else {
    VLOG_WITH_PREFIX(2) << "Start transaction " << IsolationLevel_Name(isolation)
                        << ", id: " << txn->id()
                        << ", new read time";
    RETURN_NOT_OK(txn->Init(isolation));
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

Result<const TransactionMetadata*> PgClientSession::GetDdlTransactionMetadata(
    bool use_transaction, CoarseTimePoint deadline) {
  if (!use_transaction) {
    return nullptr;
  }

  auto& txn = Transaction(PgClientSessionKind::kDdl);
  if (!txn) {
    const auto isolation = FLAGS_ysql_serializable_isolation_for_ddl_txn
        ? IsolationLevel::SERIALIZABLE_ISOLATION : IsolationLevel::SNAPSHOT_ISOLATION;
    txn = transaction_builder_(IsDDL::kTrue, client::ForceGlobalTransaction::kTrue, deadline);
    RETURN_NOT_OK(txn->Init(isolation));
    txn->SetLogPrefixTag(kTxnLogPrefixTag, id_);
    ddl_txn_metadata_ = VERIFY_RESULT(Copy(txn->GetMetadata(deadline).get()));
    EnsureSession(PgClientSessionKind::kDdl, deadline)->SetTransaction(txn);
  }

  return &ddl_txn_metadata_;
}

client::YBClient& PgClientSession::client() {
  return client_;
}

Result<client::YBTransactionPtr> PgClientSession::RestartTransaction(
    client::YBSession* session, client::YBTransaction* transaction) {
  if (!transaction) {
    SCHECK(session->IsRestartRequired(), IllegalState,
           "Attempted to restart when session does not require restart");

    const auto old_read_time = session->read_point()->GetReadTime();
    session->RestartNonTxnReadPoint(client::Restart::kTrue);
    const auto new_read_time = session->read_point()->GetReadTime();
    VLOG_WITH_PREFIX(3) << "Restarted read: " << old_read_time << " => " << new_read_time;
    LOG_IF_WITH_PREFIX(DFATAL, old_read_time == new_read_time)
        << "Read time did not change during restart: " << old_read_time << " => " << new_read_time;
    return nullptr;
  }

  if (!transaction->IsRestartRequired()) {
    return STATUS(IllegalState, "Attempted to restart when transaction does not require restart");
  }
  const auto result = VERIFY_RESULT(transaction->CreateRestartedTransaction());
  session->SetTransaction(result);
  VLOG_WITH_PREFIX(3) << "Restarted transaction";
  return result;
}

Status PgClientSession::InsertSequenceTuple(
    const PgInsertSequenceTupleRequestPB& req, PgInsertSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto result = table_cache_.Get(table_oid.GetYbTableId());
  if (!result.ok()) {
    RETURN_NOT_OK(CreateSequencesDataTable(&client_, context->GetClientDeadline()));
    // Try one more time.
    result = table_cache_.Get(table_oid.GetYbTableId());
  }
  auto table = VERIFY_RESULT(std::move(result));

  auto psql_write(client::YBPgsqlWriteOp::NewInsert(table, &context->sidecars()));

  auto write_request = psql_write->mutable_request();
  RETURN_NOT_OK(
      (SetCatalogVersion<PgInsertSequenceTupleRequestPB, PgsqlWriteRequestPB>(req, write_request)));
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
  return session->TEST_ApplyAndFlush(std::move(psql_write));
}

Status PgClientSession::UpdateSequenceTuple(
    const PgUpdateSequenceTupleRequestPB& req, PgUpdateSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  auto psql_write = client::YBPgsqlWriteOp::NewUpdate(table, &context->sidecars());

  auto write_request = psql_write->mutable_request();
  RETURN_NOT_OK(
      (SetCatalogVersion<PgUpdateSequenceTupleRequestPB, PgsqlWriteRequestPB>(req, write_request)));
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

size_t PgClientSession::SaveData(const RefCntBuffer& buffer, WriteBuffer&& sidecars) {
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

Status PgClientSession::FetchData(
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

Status PgClientSession::FetchSequenceTuple(
    const PgFetchSequenceTupleRequestPB& req, PgFetchSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  using pggate::PgDocData;
  using pggate::PgWireDataHeader;

  const int64_t inc_by = req.inc_by();
  const bool use_sequence_cache = FLAGS_ysql_sequence_cache_method == "server";
  std::shared_ptr<PgSequenceCache::Entry> entry;
  // On exit we should notify a waiter for this sequence id that it is available.
  auto se = ScopeExit([&entry, use_sequence_cache] {
    if (use_sequence_cache && entry) {
      entry->NotifyWaiter();
    }
  });
  if (use_sequence_cache) {
    const PgObjectId sequence_id(
        narrow_cast<uint32_t>(req.db_oid()), narrow_cast<uint32_t>(req.seq_oid()));
    entry = VERIFY_RESULT(
        sequence_cache_.GetWhenAvailable(sequence_id, ToSteady(context->GetClientDeadline())));

    // If the cache has a value, return immediately.
    std::optional<int64_t> sequence_value = entry->GetValueIfCached(inc_by);
    if (sequence_value.has_value()) {
      // Since the tserver cache is enabled, the connection cache size is implicitly 1 so the first
      // and last value are the same.
      resp->set_first_value(*sequence_value);
      resp->set_last_value(*sequence_value);
      return Status::OK();
    }
  }

  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  auto psql_write = client::YBPgsqlWriteOp::NewFetchSequence(table, &context->sidecars());

  auto* write_request = psql_write->mutable_request();
  RETURN_NOT_OK(
      (SetCatalogVersion<PgFetchSequenceTupleRequestPB, PgsqlWriteRequestPB>(req, write_request)));
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

  // Expect exactly two rows on success: sequence value range start and end, each as a single value
  // in its own row. Even if a single value is fetched, both should be equal. If no value is
  // fetched, the response should come with an error.
  Slice cursor;
  int64_t row_count;
  PgDocData::LoadCache(context->sidecars().GetFirst(), &row_count, &cursor);
  if (row_count != 2) {
    return STATUS_SUBSTITUTE(InternalError, "Invalid row count has been fetched from sequence $0",
                             req.seq_oid());
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

  if (use_sequence_cache) {
    entry->SetRange(first_value, last_value);
    std::optional<int64_t> optional_sequence_value = entry->GetValueIfCached(inc_by);

    RSTATUS_DCHECK(
        optional_sequence_value.has_value(), InternalError, "Value for sequence $0 was not found.",
        req.seq_oid());
    // Since the tserver cache is enabled, the connection cache size is implicitly 1 so the first
    // and last value are the same.
    last_value = first_value = *optional_sequence_value;
  }

  resp->set_first_value(first_value);
  resp->set_last_value(last_value);
  return Status::OK();
}

Status PgClientSession::ReadSequenceTuple(
    const PgReadSequenceTupleRequestPB& req, PgReadSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  using pggate::PgDocData;
  using pggate::PgWireDataHeader;

  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  auto psql_read = client::YBPgsqlReadOp::NewSelect(table, &context->sidecars());

  auto read_request = psql_read->mutable_request();
  RETURN_NOT_OK(
      (SetCatalogVersion<PgReadSequenceTupleRequestPB, PgsqlReadRequestPB>(req, read_request)));
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

  auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(session->TEST_ApplyAndFlush(psql_read));

  CHECK_EQ(psql_read->sidecar_index(), 0);

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

Status PgClientSession::DeleteSequenceTuple(
    const PgDeleteSequenceTupleRequestPB& req, PgDeleteSequenceTupleResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table = VERIFY_RESULT(table_cache_.Get(table_oid.GetYbTableId()));

  auto psql_delete(client::YBPgsqlWriteOp::NewDelete(table, &context->sidecars()));
  auto delete_request = psql_delete->mutable_request();

  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.db_oid());
  delete_request->add_partition_column_values()->mutable_value()->set_int64_value(req.seq_oid());

  auto& session = EnsureSession(PgClientSessionKind::kSequence, context->GetClientDeadline());
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  return session->TEST_ApplyAndFlush(std::move(psql_delete));
}

Status PgClientSession::DeleteDBSequences(
    const PgDeleteDBSequencesRequestPB& req, PgDeleteDBSequencesResponsePB* resp,
    rpc::RpcContext* context) {
  PgObjectId table_oid(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid);
  auto table_res = table_cache_.Get(table_oid.GetYbTableId());
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

void PgClientSession::GetTableKeyRanges(
    yb::tserver::PgGetTableKeyRangesRequestPB const& req,
    yb::tserver::PgGetTableKeyRangesResponsePB* resp, yb::rpc::RpcContext context) {
  const auto table = table_cache_.Get(PgObjectId::GetYbTableIdFromPB(req.table_id()));
  resp->set_current_ht(clock_->Now().ToUint64());
  if (!table.ok()) {
    StatusToPB(table.status(), resp->mutable_status());
    context.RespondSuccess();
    return;
  }

  auto session = EnsureSession(PgClientSessionKind::kPlain, context.GetClientDeadline());
  auto shared_context = std::make_shared<rpc::RpcContext>(std::move(context));
  const auto read_time_serial_no = req.read_time_serial_no();
  // TODO: Remove read time management from GetTableKeyRanges function.
  UsedReadTimeApplier used_read_time_applier;
  if (read_time_serial_no_ != read_time_serial_no) {
    used_read_time_applier = ResetReadPoint(PgClientSessionKind::kPlain);
    read_time_serial_no_ = read_time_serial_no;
  }
  GetTableKeyRanges(
      session, *table, req.lower_bound_key(), req.upper_bound_key(), req.max_num_ranges(),
      req.range_size_bytes(), req.is_forward(), req.max_key_length(), &shared_context->sidecars(),
      /* paging_state = */ nullptr,
      [resp, shared_context](Status status) {
        if (!status.ok()) {
          StatusToPB(status, resp->mutable_status());
        }
        shared_context->RespondSuccess();
      }, std::move(used_read_time_applier));
}

void PgClientSession::GetTableKeyRanges(
    client::YBSessionPtr session, const std::shared_ptr<client::YBTable>& table,
    Slice lower_bound_key, Slice upper_bound_key, uint64_t max_num_ranges,
    uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length, rpc::Sidecars* sidecars,
    PgsqlPagingStatePB* paging_state, std::function<void(Status)> callback,
    UsedReadTimeApplier&& used_read_time_applier) {
  // TODO(get_table_key_ranges): consider using separate GetTabletKeyRanges RPC to tablet leader
  // instead of passing through YBSession.
  auto psql_read = client::YBPgsqlReadOp::NewSelect(table, sidecars);

  auto* read_request = psql_read->mutable_request();
  if (paging_state) {
    if (paging_state->total_num_rows_read() >= max_num_ranges) {
      callback(Status::OK());
      return;
    }
    read_request->set_limit(max_num_ranges - paging_state->total_num_rows_read());
    read_request->set_allocated_paging_state(paging_state);
  } else {
    read_request->set_limit(max_num_ranges);
  }

  read_request->set_is_forward_scan(is_forward);
  auto* req = read_request->mutable_get_tablet_key_ranges_request();

  // IsInclusive is actually ignored by Tablet::GetTabletKeyRanges, and it always treats both
  // boundaries as inclusive. But we are setting it here to avoid check failures inside
  // YBPgsqlReadOp.
  if (!lower_bound_key.empty()) {
    read_request->mutable_lower_bound()->mutable_key()->assign(
        lower_bound_key.cdata(), lower_bound_key.size());
    read_request->mutable_lower_bound()->set_is_inclusive(true);
    read_request->mutable_partition_key()->assign(lower_bound_key.cdata(), lower_bound_key.size());
    req->mutable_lower_bound_key()->assign(lower_bound_key.cdata(), lower_bound_key.size());
  }
  if (!upper_bound_key.empty()) {
    read_request->mutable_upper_bound()->mutable_key()->assign(
        upper_bound_key.cdata(), upper_bound_key.size());
    read_request->mutable_upper_bound()->set_is_inclusive(true);
    req->mutable_upper_bound_key()->assign(upper_bound_key.cdata(), upper_bound_key.size());
  }
  req->set_range_size_bytes(range_size_bytes);
  req->set_max_key_length(max_key_length);

  session->Apply(psql_read);
  session->FlushAsync([this, session, psql_read, callback = std::move(callback), table,
                       lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes,
                       is_forward, max_key_length, sidecars,
                       used_read_time_applier = std::move(used_read_time_applier)](
                           client::FlushStatus* flush_status) {
    {
      // TODO: Remove read time management from GetTableKeyRanges function.
      UsedReadTimeData used_read_time;
      auto used_read_time_guard = ScopeExit([&used_read_time_applier, &used_read_time] {
        if (used_read_time_applier)
          used_read_time_applier(std::move(used_read_time));
      });
      const auto status = CombineErrorsToStatus(flush_status->errors, flush_status->status);
      if (!status.ok()) {
        callback(status);
        return;
      }

      if (used_read_time_applier) {
        used_read_time = {psql_read->used_read_time(), psql_read->used_tablet()};
      }
    }

    auto* resp = psql_read->mutable_response();
    if (!resp->has_paging_state()) {
      callback(Status::OK());
      return;
    }
    GetTableKeyRanges(
        session, table, lower_bound_key, upper_bound_key, max_num_ranges, range_size_bytes,
        is_forward, max_key_length, sidecars, resp->release_paging_state(), std::move(callback),
        UsedReadTimeApplier());
  });
}

client::YBSessionPtr& PgClientSession::EnsureSession(
    PgClientSessionKind kind, CoarseTimePoint deadline) {
  auto& session = Session(kind);
  if (!session) {
    session = CreateSession(&client_, deadline, clock_);
  } else {
    session->SetDeadline(deadline);
  }
  return session;
}

Status PgClientSession::CheckPlainSessionReadTimeIsSet() const {
  RSTATUS_DCHECK(
      Session(PgClientSessionKind::kPlain)->read_point()->GetReadTime(),
      IllegalState, "Read time is not set");
  return Status::OK();
}

Status PgClientSession::CheckPlainSessionPendingUsedReadTime() {
  if (!plain_session_used_read_time_.pending_update) {
    return Status::OK();
  }
  auto& session = *Session(PgClientSessionKind::kPlain);
  const auto& read_point = *session.read_point();
  UsedReadTimeData read_time_data;
  if (!read_point.GetReadTime()) {
    auto& used_read_time = plain_session_used_read_time_.value;
    std::lock_guard guard(used_read_time.lock);
    if (!used_read_time.data) {
      return Status::OK();
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
    VLOG_WITH_PREFIX(3) << "Applying non empty used read time: " << read_time_data.value;
    session.SetReadPoint(read_time_data.value, read_time_data.tablet_id);
  }
  return Status::OK();
}

std::shared_ptr<CountDownLatch> PgClientSession::ProcessSharedRequest(
    size_t size, SharedExchange* exchange) {
  auto data = std::make_shared<SharedExchangeQuery>(shared_this_.lock(), &table_cache_, exchange);
  auto status = data->Init(size);
  if (status.ok()) {
    static std::atomic<int64_t> next_rpc_id{0};
    auto wait_state = yb::ash::WaitStateInfo::CreateIfAshIsEnabled<yb::ash::WaitStateInfo>();
    ADOPT_WAIT_STATE(wait_state);
    SCOPED_WAIT_STATUS(OnCpu_Active);
    if (wait_state) {
      wait_state->UpdateMetadata(
          {.rpc_request_id = next_rpc_id.fetch_add(1, std::memory_order_relaxed)});
      wait_state->UpdateAuxInfo({.method = "Perform"});
      ash::SharedMemoryPgPerformTracker().Track(wait_state);
    }
    status = DoPerform(data, data->deadline, nullptr);
    if (wait_state) {
      ash::SharedMemoryPgPerformTracker().Untrack(wait_state);
    }
  }
  if (!status.ok()) {
    StatusToPB(status, data->resp.mutable_status());
    data->SendResponse();
    return nullptr;
  }
  return rpc::SharedField(data, &data->latch);
}

}  // namespace yb::tserver
