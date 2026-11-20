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

#include "yb/tserver/cdc_table_output_client.h"

#include <array>
#include <cmath>
#include <sstream>

#include "yb/cdc/cdc_service.pb.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/rocksdb/rate_limiter.h"

#include "yb/rpc/sidecars.h"

#include "yb/tserver/xcluster_poller.h"

#include "yb/util/jsonwriter.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/yb_partition.h"

DECLARE_int32(cdc_write_rpc_timeout_ms);

namespace yb::tserver {

namespace {

// The CDC table spreads the changes over this many values of its hash column.
constexpr uint32_t kNumBuckets = 256;

// The primary key columns of the CDC table, in the order of the key.
constexpr std::array<const char*, 7> kKeyColumns = {
    "bucket_id",   "commit_time", "transaction_id", "record_time",
    "write_id",    "table_id",    "row_ybctid"};

constexpr size_t kUuidSize = 16;

void WriteQLValue(const QLValuePB& value, JsonWriter& writer) {
  switch (value.value_case()) {
    case QLValuePB::kInt8Value: return writer.Int(value.int8_value());
    case QLValuePB::kInt16Value: return writer.Int(value.int16_value());
    case QLValuePB::kInt32Value: return writer.Int(value.int32_value());
    case QLValuePB::kInt64Value: return writer.Int64(value.int64_value());
    case QLValuePB::kUint32Value: return writer.Uint(value.uint32_value());
    case QLValuePB::kUint64Value: return writer.Uint64(value.uint64_value());
    case QLValuePB::kBoolValue: return writer.Bool(value.bool_value());
    case QLValuePB::kStringValue: return writer.String(value.string_value());
    case QLValuePB::kBinaryValue: return writer.String(b2a_hex(value.binary_value()));
    case QLValuePB::VALUE_NOT_SET: return writer.Null();
    default: return writer.String(value.ShortDebugString());
  }
}

void WriteDouble(double value, JsonWriter& writer) {
  // JSON has no NaN or infinity.
  if (std::isfinite(value)) {
    writer.Double(value);
  } else {
    writer.String(std::to_string(value));
  }
}

void WriteDatum(const DatumMessagePB& datum, JsonWriter& writer) {
  switch (datum.datum_case()) {
    case DatumMessagePB::kDatumInt32: return writer.Int(datum.datum_int32());
    case DatumMessagePB::kDatumInt64: return writer.Int64(datum.datum_int64());
    case DatumMessagePB::kDatumFloat: return WriteDouble(datum.datum_float(), writer);
    case DatumMessagePB::kDatumDouble: return WriteDouble(datum.datum_double(), writer);
    case DatumMessagePB::kDatumBool: return writer.Bool(datum.datum_bool());
    case DatumMessagePB::kDatumString: return writer.String(datum.datum_string());
    case DatumMessagePB::kDatumBytes: return writer.String(b2a_hex(datum.datum_bytes()));
    case DatumMessagePB::kDatumMissing:
    case DatumMessagePB::DATUM_NOT_SET:
      // Values of the catalog tables are not converted by the CDC service.
      if (datum.has_pg_catalog_value()) {
        return WriteQLValue(datum.pg_catalog_value(), writer);
      }
      return writer.Null();
  }
  FATAL_INVALID_ENUM_VALUE(DatumMessagePB::DatumCase, datum.datum_case());
}

// The columns of the tuple as a JSON object. nullopt if the tuple has no columns, which is how
// the CDC service sends an image it does not have.
std::optional<std::string> TupleToJson(
    const google::protobuf::RepeatedPtrField<DatumMessagePB>& tuple) {
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::COMPACT);
  bool has_columns = false;
  writer.StartObject();
  for (const auto& datum : tuple) {
    // An entry without a name only keeps the old and the new tuple the same length.
    if (!datum.has_column_name()) {
      continue;
    }
    has_columns = true;
    writer.String(datum.column_name());
    WriteDatum(datum, writer);
  }
  writer.EndObject();
  if (!has_columns) {
    return std::nullopt;
  }
  return out.str();
}

// The 16 bytes that YSQL stores for a uuid, from its hex form with or without dashes.
Result<std::string> UuidBytesFromHex(const std::string& hex) {
  std::string digits;
  digits.reserve(hex.size());
  for (char c : hex) {
    if (c != '-') {
      digits.push_back(c);
    }
  }
  SCHECK_EQ(digits.size(), 2 * kUuidSize, InvalidArgument, Format("Not a uuid: $0", hex));
  std::string bytes;
  a2b_hex(digits.c_str(), &bytes, kUuidSize);
  return bytes;
}

// Same value as yb_hash_code(row_ybctid) % kNumBuckets in YSQL.
int32_t BucketId(const std::string& ybctid) {
  QLValuePB value;
  value.set_binary_value(ybctid);
  std::string encoded;
  AppendToKey(value, &encoded);
  return YBPartition::HashColumnCompoundValue(encoded) % kNumBuckets;
}

}  // namespace

struct CdcTableOutputClient::Batch {
  std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response;
  client::YBSessionPtr session;
  rpc::Sidecars sidecars;
  std::vector<client::YBPgsqlWriteOpPtr> ops;
};

CdcTableOutputClient::CdcTableOutputClient(
    XClusterPoller* poller, const TableId& cdc_table_id, client::YBClient& local_client,
    rocksdb::RateLimiter* rate_limiter,
    std::optional<std::unordered_set<TableId>> published_table_ids)
    : cdc_table_id_(cdc_table_id),
      local_client_(local_client),
      rate_limiter_(rate_limiter),
      published_table_ids_(std::move(published_table_ids)),
      poller_(poller) {}

void CdcTableOutputClient::StartShutdown() { shutdown_ = true; }

void CdcTableOutputClient::CompleteShutdown() {
  // Wait for a call into the poller that started before the shutdown.
  std::lock_guard l(mutex_);
}

Status CdcTableOutputClient::OpenTable() {
  auto table = VERIFY_RESULT(local_client_.OpenTable(cdc_table_id_));
  const auto& schema = table->schema();

  SCHECK_EQ(
      schema.num_key_columns(), kKeyColumns.size(), IllegalState,
      "Unexpected primary key of the CDC table");
  for (size_t i = 0; i < kKeyColumns.size(); ++i) {
    // Column() returns a temporary.
    const std::string column_name = schema.Column(i).name();
    SCHECK_EQ(
        column_name, kKeyColumns[i], IllegalState, "Unexpected primary key of the CDC table");
  }

  auto column_id = [&schema](const char* name) -> Result<int32_t> {
    auto idx = schema.FindColumn(name);
    SCHECK_GE(idx, 0, NotFound, Format("Column $0 not found in the CDC table", name));
    return schema.ColumnId(idx);
  };
  origin_id_column_id_ = VERIFY_RESULT(column_id("origin_id"));
  op_column_id_ = VERIFY_RESULT(column_id("op"));
  before_column_id_ = VERIFY_RESULT(column_id("before"));
  after_column_id_ = VERIFY_RESULT(column_id("after"));

  table_ = std::move(table);
  return Status::OK();
}

Status CdcTableOutputClient::AddWrites(Batch& batch) {
  if (!table_) {
    RETURN_NOT_OK(OpenTable());
  }
  // The jsonb conversion allocates in a postgres memory context of this thread.
  auto se = ScopeExit([] { docdb::DeleteMemoryContextIfSet(); });

  auto arena = SharedThreadSafeArena();
  for (const auto& record : batch.get_changes_response->cdc_sdk_proto_records()) {
    const auto& row = record.row_message();
    const char* op;
    switch (row.op()) {
      case cdc::RowMessage::INSERT: op = "I"; break;
      case cdc::RowMessage::UPDATE: op = "U"; break;
      case cdc::RowMessage::DELETE: op = "D"; break;
      // TODO: DDL records. BEGIN and COMMIT are implied by the commit time and transaction id of
      // the rows, and SAFEPOINT only carries the safe time.
      default: continue;
    }
    // A colocated tablet also returns the changes of the unpublished tables of the stream.
    if (published_table_ids_ && !published_table_ids_->contains(row.table_id())) {
      continue;
    }
    SCHECK(
        row.has_commit_time() && row.has_record_time() && row.has_table_id() &&
            row.has_primary_key(),
        IllegalState, "Change cannot be identified uniquely: $0", record.ShortDebugString());

    if (!batch.session) {
      batch.session = local_client_.NewSession(
          MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));
      // FlushDone must not run inside FlushAsync, which is called with the poller's mutex held.
      batch.session->set_allow_local_calls_in_curr_thread(false);
    }

    auto write = client::YBPgsqlWriteOp::NewInsert(table_, arena, &batch.sidecars);
    auto* req = write->mutable_request();
    // An upsert neither reads the row nor fails if it exists, which is what makes applying a
    // change twice harmless.
    req->set_stmt_type(PgsqlWriteRequestPB::PGSQL_UPSERT);

    // The key columns, in the order of kKeyColumns.
    req->add_partition_column_values()->mutable_value()->set_int32_value(
        BucketId(row.primary_key()));
    req->add_range_column_values()->mutable_value()->set_int64_value(row.commit_time());
    // A single shard transaction has no transaction id.
    std::string transaction_id(kUuidSize, '\0');
    if (!row.transaction_id().empty()) {
      transaction_id = VERIFY_RESULT(UuidBytesFromHex(row.transaction_id()));
    }
    req->add_range_column_values()->mutable_value()->dup_binary_value(transaction_id);
    req->add_range_column_values()->mutable_value()->set_int64_value(row.record_time());
    req->add_range_column_values()->mutable_value()->set_int64_value(
        record.cdc_sdk_op_id().write_id());
    req->add_range_column_values()->mutable_value()->dup_binary_value(
        VERIFY_RESULT(UuidBytesFromHex(row.table_id())));
    req->add_range_column_values()->mutable_value()->dup_binary_value(row.primary_key());

    auto add_column = [req](int32_t column_id) {
      auto* column = req->add_column_values();
      column->set_column_id(column_id);
      return column->mutable_expr()->mutable_value();
    };
    if (row.has_xrepl_origin_id()) {
      add_column(origin_id_column_id_)->set_int32_value(row.xrepl_origin_id());
    }
    add_column(op_column_id_)->dup_string_value(op);
    if (auto before = TupleToJson(row.old_tuple())) {
      add_column(before_column_id_)->dup_binary_value(
          VERIFY_RESULT(docdb::JsonTextToPgJsonb(*before)));
    }
    if (auto after = TupleToJson(row.new_tuple())) {
      add_column(after_column_id_)->dup_binary_value(
          VERIFY_RESULT(docdb::JsonTextToPgJsonb(*after)));
    }

    batch.session->Apply(write);
    batch.ops.push_back(std::move(write));
  }
  return Status::OK();
}

void CdcTableOutputClient::ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> resp) {
  auto batch = std::make_shared<Batch>();
  batch->get_changes_response = std::move(resp);

  auto status = AddWrites(*batch);
  if (!status.ok() || batch->ops.empty()) {
    if (batch->session) {
      batch->session->Abort();
    }
    Respond(status, *batch);
    return;
  }

  rate_limiter_->Request(batch->get_changes_response->ByteSizeLong(), IOPriority::kHigh);
  batch->session->FlushAsync(
      [weak_this = weak_from_this(), batch](client::FlushStatus* flush_status) {
        if (auto shared_this = weak_this.lock()) {
          shared_this->FlushDone(batch, flush_status);
        }
      });
}

void CdcTableOutputClient::FlushDone(
    const std::shared_ptr<Batch>& batch, client::FlushStatus* flush_status) {
  auto status = flush_status->status;
  if (!status.ok() && !flush_status->errors.empty()) {
    status = flush_status->errors.front()->status();
  }
  for (const auto& op : batch->ops) {
    if (!status.ok()) {
      break;
    }
    const auto& response = op->response();
    if (response.status() != PgsqlResponsePB::PGSQL_STATUS_OK) {
      status = response.error_status().empty()
          ? STATUS_FORMAT(
                InternalError, "Write to the CDC table failed: $0",
                PgsqlResponsePB::RequestStatus_Name(response.status()))
          : StatusFromPB(*response.error_status().rbegin());
    }
  }
  Respond(status, *batch);
}

void CdcTableOutputClient::Respond(const Status& status, const Batch& batch) {
  std::lock_guard l(mutex_);
  if (shutdown_) {
    return;
  }
  XClusterOutputClientResponse response;
  response.status = status;
  response.get_changes_response = batch.get_changes_response;
  response.processed_record_count = status.ok() ? static_cast<uint32_t>(batch.ops.size()) : 0;
  poller_->ApplyChangesCallback(std::move(response));
}

}  // namespace yb::tserver
