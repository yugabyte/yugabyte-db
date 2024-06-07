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

#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema_pbutil.h"

#include "yb/master/master_defaults.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/stol_utils.h"

#include "yb/util/string_util.h"
#include "yb/yql/cql/ql/util/statement_result.h"

DEFINE_RUNTIME_int32(cdc_state_table_num_tablets, 0,
    "Number of tablets to use when creating the CDC state table. "
    "0 to use the same default num tablets as for regular tables.");

DEFINE_RUNTIME_bool(enable_cdc_state_table_caching, true,
    "Enable caching the cdc_state table schema.");

#define VERIFY_PARSE_COLUMN(expr) \
  VERIFY_RESULT_PREPEND( \
      (expr), Format( \
                  "Failed to parse $0 column from $1 row $2", column_name, \
                  kCdcStateYBTableName.table_name(), entry->key.ToString()))

namespace yb::cdc {

static const char* const kCdcTabletId = "tablet_id";
constexpr size_t kCdcTabletIdIdx = 0;
static const char* const kCdcStreamId = "stream_id";
constexpr size_t kCdcStreamIdIdx = 1;
static const char* const kCdcCheckpoint  = "checkpoint";
static const char* const kCdcData = "data";
static const char* const kCdcLastReplicationTime = "last_replication_time";
static const char* const kCDCSDKSafeTime = "cdc_sdk_safe_time";
static const char* const kCDCSDKActiveTime = "active_time";
static const char* const kCDCSDKSnapshotKey = "snapshot_key";
static const char* const kCDCSDKConfirmedFlushLSN = "confirmed_flush_lsn";
static const char* const kCDCSDKRestartLSN = "restart_lsn";
static const char* const kCDCSDKXmin = "xmin";
static const char* const kCDCSDKRecordIdCommitTime = "record_id_commit_time";
static const char* const kCDCSDKLastPubRefreshTime = "last_pub_refresh_time";
static const char* const kCDCSDKPubRefreshTimes = "pub_refresh_times";

namespace {
const client::YBTableName kCdcStateYBTableName(
    YQL_DATABASE_CQL, master::kSystemNamespaceName, kCdcStateTableName);

std::optional<std::string> GetValueFromMap(const QLMapValuePB& map_value, const std::string& key) {
  for (int index = 0; index < map_value.keys_size(); ++index) {
    if (map_value.keys(index).string_value() == key) {
      return map_value.values(index).string_value();
    }
  }
  return std::nullopt;
}

template <class T>
Result<std::optional<T>> GetIntValueFromMap(const QLMapValuePB& map_value, const std::string& key) {
  auto str_value = GetValueFromMap(map_value, key);
  if (!str_value) {
    return std::nullopt;
  }

  return CheckedStol<T>(*str_value);
}

Result<std::optional<uint32_t>> GetUInt32ValueFromMap(
    const QLMapValuePB& map_value, const std::string& key) {
  auto str_value = GetValueFromMap(map_value, key);
  if (!str_value) {
    return std::nullopt;
  }

  return CheckedStoui(*str_value);
}

void SerializeEntry(
    const CDCStateTableKey& key, client::TableHandle* cdc_table, QLWriteRequestPB* req,
    const bool replace_full_map = false) {
  DCHECK(key.stream_id && !key.tablet_id.empty());

  QLAddStringHashValue(req, key.tablet_id);
  QLAddStringRangeValue(req, key.CompositeStreamId());
}

void SerializeEntry(
    const CDCStateTableEntry& entry, client::TableHandle* cdc_table, QLWriteRequestPB* req,
    const bool replace_full_map = false) {
  SerializeEntry(entry.key, cdc_table, req);

  if (entry.checkpoint) {
    cdc_table->AddStringColumnValue(req, kCdcCheckpoint, entry.checkpoint->ToString());
  }

  if (entry.last_replication_time) {
    cdc_table->AddTimestampColumnValue(req, kCdcLastReplicationTime, *entry.last_replication_time);
  }

  if (replace_full_map) {
    QLMapValuePB* map_value_pb = nullptr;
    auto get_map_value_pb = [&map_value_pb, &req, &cdc_table]() {
      if (!map_value_pb) {
        map_value_pb = client::AddMapColumn(req, cdc_table->ColumnId(kCdcData));
      }
      return map_value_pb;
    };

    if (entry.active_time) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKActiveTime, AsString(*entry.active_time));
    }

    if (entry.cdc_sdk_safe_time) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKSafeTime, AsString(*entry.cdc_sdk_safe_time));
    }

    if (entry.snapshot_key) {
      client::AddMapEntryToColumn(get_map_value_pb(), kCDCSDKSnapshotKey, *entry.snapshot_key);
    }

    if (entry.confirmed_flush_lsn) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKConfirmedFlushLSN, AsString(*entry.confirmed_flush_lsn));
    }

    if (entry.restart_lsn) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKRestartLSN, AsString(*entry.restart_lsn));
    }

    if (entry.xmin) {
      client::AddMapEntryToColumn(get_map_value_pb(), kCDCSDKXmin, AsString(*entry.xmin));
    }

    if (entry.record_id_commit_time) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKRecordIdCommitTime, AsString(*entry.record_id_commit_time));
    }

    if (entry.last_pub_refresh_time) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKLastPubRefreshTime, AsString(*entry.last_pub_refresh_time));
    }

    if (entry.pub_refresh_times) {
      client::AddMapEntryToColumn(
          get_map_value_pb(), kCDCSDKPubRefreshTimes, AsString(*entry.pub_refresh_times));
    }

  } else {
    if (entry.active_time) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKActiveTime, AsString(*entry.active_time));
    }

    if (entry.cdc_sdk_safe_time) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKSafeTime, AsString(*entry.cdc_sdk_safe_time));
    }

    if (entry.snapshot_key) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKSnapshotKey, *entry.snapshot_key);
    }

    if (entry.confirmed_flush_lsn) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKConfirmedFlushLSN,
          AsString(*entry.confirmed_flush_lsn));
    }

    if (entry.restart_lsn) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKRestartLSN, AsString(*entry.restart_lsn));
    }

    if (entry.xmin) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKXmin, AsString(*entry.xmin));
    }

    if (entry.record_id_commit_time) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKRecordIdCommitTime,
          AsString(*entry.record_id_commit_time));
    }

    if (entry.last_pub_refresh_time) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKLastPubRefreshTime,
          AsString(*entry.last_pub_refresh_time));
    }

    if (entry.pub_refresh_times) {
      client::UpdateMapUpsertKeyValue(
          req, cdc_table->ColumnId(kCdcData), kCDCSDKPubRefreshTimes,
          AsString(*entry.pub_refresh_times));
    }
  }
}

Status DeserializeColumn(
    const QLValue& column, const std::string& column_name, CDCStateTableEntry* entry) {
  if (column.IsNull()) {
    return Status::OK();
  }

  if (column_name == kCdcCheckpoint) {
    entry->checkpoint = VERIFY_PARSE_COLUMN(OpId::FromString(column.string_value()));
  } else if (column_name == kCdcLastReplicationTime) {
    entry->last_replication_time = column.timestamp_value().ToInt64();
  } else if (column_name == kCdcData) {
    const auto& map_value = column.map_value();

    auto active_time_result =
        VERIFY_PARSE_COLUMN(GetIntValueFromMap<uint64_t>(map_value, kCDCSDKActiveTime));
    if (active_time_result) {
      entry->active_time = *active_time_result;
    }

    auto safe_time_result =
        VERIFY_PARSE_COLUMN(GetIntValueFromMap<uint64_t>(map_value, kCDCSDKSafeTime));
    if (safe_time_result) {
      entry->cdc_sdk_safe_time = *safe_time_result;
    }

    entry->snapshot_key = GetValueFromMap(map_value, kCDCSDKSnapshotKey);

    auto confirmed_flush_result =
        VERIFY_PARSE_COLUMN(GetIntValueFromMap<uint64_t>(map_value, kCDCSDKConfirmedFlushLSN));
    if (confirmed_flush_result) {
      entry->confirmed_flush_lsn = *confirmed_flush_result;
    }

    auto restart_lsn_result =
        VERIFY_PARSE_COLUMN(GetIntValueFromMap<uint64_t>(map_value, kCDCSDKRestartLSN));
    if (restart_lsn_result) {
      entry->restart_lsn = *restart_lsn_result;
    }

    auto xmin_result = VERIFY_PARSE_COLUMN(GetUInt32ValueFromMap(map_value, kCDCSDKXmin));
    if (xmin_result) {
      entry->xmin = *xmin_result;
    }

    auto record_id_commit_time_result =
        VERIFY_PARSE_COLUMN(GetIntValueFromMap<uint64_t>(map_value, kCDCSDKRecordIdCommitTime));
    if (record_id_commit_time_result) {
      entry->record_id_commit_time = *record_id_commit_time_result;
    }

    auto last_pub_refresh_time_result =
        VERIFY_PARSE_COLUMN(GetIntValueFromMap<uint64_t>(map_value, kCDCSDKLastPubRefreshTime));
    if (last_pub_refresh_time_result) {
      entry->last_pub_refresh_time = *last_pub_refresh_time_result;
    }

    entry->pub_refresh_times = GetValueFromMap(map_value, kCDCSDKPubRefreshTimes);
  }

  return Status::OK();
}

Result<CDCStateTableEntry> DeserializeRow(
    const qlexpr::QLRow& row, const std::vector<std::string>& columns) {
  DCHECK_GE(columns.size(), 2);
  auto key = VERIFY_RESULT(CDCStateTableKey::FromString(
      row.column(kCdcTabletIdIdx).string_value(), row.column(kCdcStreamIdIdx).string_value()));

  CDCStateTableEntry entry(std::move(key));

  for (size_t i = 2; i < columns.size(); i++) {
    RETURN_NOT_OK(DeserializeColumn(row.column(i), columns[i], &entry));
  }
  return entry;
}
}  // namespace

CDCStateTable::CDCStateTable(std::shared_future<client::YBClient*> client_future)
    : client_future_(std::move(client_future)) {
  CHECK(client_future_.valid());
}

CDCStateTable::CDCStateTable(client::YBClient* client) : client_(client) { CHECK_NOTNULL(client); }

std::string CDCStateTableKey::ToString() const {
  return Format(
      "TabletId: $0, StreamId: $1 $2", tablet_id, stream_id,
      colocated_table_id.empty() ? "" : Format(", ColocatedTableId: $0", colocated_table_id));
}

std::string CDCStateTableKey::CompositeStreamId() const {
  if (colocated_table_id.empty()) {
    return stream_id.ToString();
  }
  return Format("$0_$1", stream_id, colocated_table_id);
}

Result<CDCStateTableKey> CDCStateTableKey::FromString(
    const TabletId& tablet_id, const std::string& composite_stream_id) {
  auto composite_stream_id_parts = StringSplit(composite_stream_id, '_');
  SCHECK_LE(
      composite_stream_id_parts.size(), static_cast<uint32_t>(2), IllegalState,
      Format("Invalid stream id: $0", composite_stream_id));
  auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(composite_stream_id_parts[0]));

  CDCStateTableKey key{
      tablet_id, std::move(stream_id),
      composite_stream_id_parts.size() == 2 ? std::move(composite_stream_id_parts[1]) : ""};

  return key;
}

std::string CDCStateTableEntry::ToString() const {
  std::string result = key.ToString();
  if (checkpoint) {
    result += Format(", Checkpoint: $0", *checkpoint);
  }
  if (last_replication_time) {
    result += Format(", LastReplicationTime: $0", *last_replication_time);
  }
  if (active_time) {
    result += Format(", ActiveTime: $0", *active_time);
  }
  if (cdc_sdk_safe_time) {
    result += Format(", CdcSdkSafeTime: $0", *cdc_sdk_safe_time);
  }
  if (snapshot_key) {
    result += Format(", SnapshotKey: $0", *snapshot_key);
  }
  if (confirmed_flush_lsn) {
    result += Format(", ConfirmedFlushLSN: $0", *confirmed_flush_lsn);
  }
  if (restart_lsn) {
    result += Format(", RestartLSN: $0", *restart_lsn);
  }

  if (xmin) {
    result += Format(", Xmin: $0", *xmin);
  }

  if (record_id_commit_time) {
    result += Format(", RecordIdCommitTime: $0", *record_id_commit_time);
  }

  if (last_pub_refresh_time) {
    result += Format(", LastPubRefreshTime: $0", *last_pub_refresh_time);
  }

  if (pub_refresh_times) {
    result += Format(", PubRefreshTimes: $0", *pub_refresh_times);
  }

  return result;
}

const std::string& CDCStateTable::GetNamespaceName() {
  return kCdcStateYBTableName.namespace_name();
}
const std::string& CDCStateTable::GetTableName() { return kCdcStateYBTableName.table_name(); }

Result<master::CreateTableRequestPB> CDCStateTable::GenerateCreateCdcStateTableRequest() {
  master::CreateTableRequestPB req;
  req.set_name(GetTableName());
  req.mutable_namespace_()->set_name(GetNamespaceName());
  req.set_table_type(TableType::YQL_TABLE_TYPE);

  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kCdcTabletId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(kCdcStreamId)->PrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(kCdcCheckpoint)->Type(DataType::STRING);
  schema_builder.AddColumn(kCdcData)
      ->Type(QLType::CreateTypeMap(DataType::STRING, DataType::STRING));
  schema_builder.AddColumn(kCdcLastReplicationTime)->Type(DataType::TIMESTAMP);

  client::YBSchema yb_schema;
  RETURN_NOT_OK(schema_builder.Build(&yb_schema));

  auto schema = yb::client::internal::GetSchema(yb_schema);
  SchemaToPB(schema, req.mutable_schema());
  // Explicitly set the number tablets if the corresponding flag is set, otherwise
  // CreateTable will use the same defaults as for regular tables.
  if (FLAGS_cdc_state_table_num_tablets > 0) {
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(
        FLAGS_cdc_state_table_num_tablets);
  }

  return req;
}

Status CDCStateTable::WaitForCreateTableToFinishWithCache() {
  if (created_) {
    return Status::OK();
  }
  auto* client = VERIFY_RESULT(GetClient());
  RETURN_NOT_OK(client->WaitForCreateTableToFinish(kCdcStateYBTableName));
  created_ = true;
  return Status::OK();
}

Status CDCStateTable::WaitForCreateTableToFinishWithoutCache() {
  auto* client = VERIFY_RESULT(GetClient());
  return client->WaitForCreateTableToFinish(kCdcStateYBTableName);
}

Status CDCStateTable::OpenTable(client::TableHandle* cdc_table) {
  auto* client = VERIFY_RESULT(GetClient());
  RETURN_NOT_OK(cdc_table->Open(kCdcStateYBTableName, client));
  return Status::OK();
}

Result<std::shared_ptr<client::TableHandle>> CDCStateTable::GetTable() {
  bool use_cache = GetAtomicFlag(&FLAGS_enable_cdc_state_table_caching);
  if (!use_cache) {
    RETURN_NOT_OK(WaitForCreateTableToFinishWithoutCache());
    auto cdc_table = std::make_shared<client::TableHandle>();
    RETURN_NOT_OK(OpenTable(cdc_table.get()));
    return cdc_table;
  }

  {
    SharedLock sl(mutex_);
    if (cdc_table_) {
      return cdc_table_;
    }
  }

  std::lock_guard l(mutex_);
  if (cdc_table_) {
    return cdc_table_;
  }
  RETURN_NOT_OK(WaitForCreateTableToFinishWithCache());
  auto cdc_table = std::make_shared<client::TableHandle>();
  RETURN_NOT_OK(OpenTable(cdc_table.get()));
  cdc_table_.swap(cdc_table);
  return cdc_table_;
}

Result<client::YBClient*> CDCStateTable::GetClient() {
  if (!client_) {
    CHECK(client_future_.valid());
    client_ = client_future_.get();
  }

  SCHECK(client_, IllegalState, "CDC Client not initialized or shutting down");
  return client_;
}

Result<std::shared_ptr<client::YBSession>> CDCStateTable::GetSession() {
  auto* client = VERIFY_RESULT(GetClient());
  auto session = client->NewSession(client->default_rpc_timeout());
  return session;
}

template <class CDCEntry>
Status CDCStateTable::WriteEntriesAsync(
    const std::vector<CDCEntry>& entries, QLWriteRequestPB::QLStmtType statement_type,
    StdStatusCallback callback, QLOperator condition_op, const bool replace_full_map,
    const std::vector<std::string>& keys_to_delete) {
  if (entries.empty()) {
    callback(Status::OK());
    return Status::OK();
  }

  auto cdc_table = VERIFY_RESULT(GetTable());
  auto session = VERIFY_RESULT(GetSession());

  std::vector<client::YBOperationPtr> ops;
  ops.reserve(entries.size() * 2);
  for (const auto& entry : entries) {
    const auto op = cdc_table->NewWriteOp(statement_type);
    auto* const req = op->mutable_request();

    SerializeEntry(entry, cdc_table.get(), req, replace_full_map);

    if (condition_op != QL_OP_NOOP) {
      auto* condition = req->mutable_if_expr()->mutable_condition();
      condition->set_op(condition_op);
    }

    ops.push_back(std::move(op));
  }

  if (!replace_full_map && !keys_to_delete.empty()) {
    if constexpr (std::is_same<CDCEntry, CDCStateTableEntry>::value) {
      for (const auto& entry : entries) {
        const auto op = cdc_table->NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
        auto* const req = op->mutable_request();

        SerializeEntry(entry.key, cdc_table.get(), req);

        for (const auto& key : keys_to_delete) {
          client::UpdateMapRemoveKey(req, cdc_table->ColumnId(kCdcData), key);
        }

        ops.push_back(std::move(op));
      }
    }
  }

  session->Apply(std::move(ops));
  session->FlushAsync([session_holder = session,
                       callback = std::move(callback)](client::FlushStatus* flush_status) mutable {
    for (auto& error : flush_status->errors) {
      LOG_WITH_FUNC(WARNING) << "Flush of operation " << error->failed_op().ToString()
                             << " failed: " << error->status();
    }
    session_holder = {};
    callback(std::move(flush_status->status));
  });
  return Status::OK();
}

template <class CDCEntry>
Status CDCStateTable::WriteEntries(
    const std::vector<CDCEntry>& entries, QLWriteRequestPB::QLStmtType statement_type,
    QLOperator condition_op, const bool replace_full_map,
    const std::vector<std::string>& keys_to_delete) {
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  Synchronizer sync;
  RETURN_NOT_OK(WriteEntriesAsync<CDCEntry>(
      entries, statement_type, sync.AsStdStatusCallback(), condition_op, replace_full_map,
      keys_to_delete));
  return sync.Wait();
}

Status CDCStateTable::InsertEntriesAsync(
    const std::vector<CDCStateTableEntry>& entries, StdStatusCallback callback) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  return WriteEntriesAsync(
      entries, QLWriteRequestPB::QL_STMT_INSERT, std::move(callback), QL_OP_NOT_EXISTS,
      /*replace_full_map=*/true);
}

Status CDCStateTable::InsertEntries(
    const std::vector<CDCStateTableEntry>& entries) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  Synchronizer sync;
  RETURN_NOT_OK(InsertEntriesAsync(entries, sync.AsStdStatusCallback()));
  return sync.Wait();
}

Status CDCStateTable::UpdateEntries(
    const std::vector<CDCStateTableEntry>& entries, const bool replace_full_map,
    const std::vector<std::string>& keys_to_delete) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  return WriteEntries(
      entries, QLWriteRequestPB::QL_STMT_UPDATE, QL_OP_EXISTS, replace_full_map, keys_to_delete);
}

Status CDCStateTable::UpsertEntries(
    const std::vector<CDCStateTableEntry>& entries, const bool replace_full_map,
    const std::vector<std::string>& keys_to_delete) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  return WriteEntries(
      entries, QLWriteRequestPB::QL_STMT_UPDATE, QL_OP_NOOP, replace_full_map, keys_to_delete);
}

Status CDCStateTable::DeleteEntries(const std::vector<CDCStateTableKey>& entry_keys) {
  VLOG_WITH_FUNC(1) << yb::ToString(entry_keys);
  return WriteEntries(entry_keys, QLWriteRequestPB::QL_STMT_DELETE);
}

Result<CDCStateTableRange> CDCStateTable::GetTableRange(
    CDCStateTableEntrySelector&& field_filter, Status* iteration_status) {
  std::vector<std::string> columns;
  columns.emplace_back(kCdcTabletId);
  columns.emplace_back(kCdcStreamId);
  MoveCollection(&field_filter.columns_, &columns);
  VLOG_WITH_FUNC(1) << yb::ToString(columns);

  return CDCStateTableRange(VERIFY_RESULT(GetTable()), iteration_status, std::move(columns));
}

Result<CDCStateTableRange> CDCStateTable::GetTableRangeAsync(
    CDCStateTableEntrySelector&& field_filter, Status* iteration_status) {
  auto* client = VERIFY_RESULT(GetClient());

  bool table_creation_in_progress = false;
  RETURN_NOT_OK(client->IsCreateTableInProgress(kCdcStateYBTableName, &table_creation_in_progress));
  if (table_creation_in_progress) {
    return STATUS(Uninitialized, "CDC State Table creation is in progress");
  }

  return GetTableRange(std::move(field_filter), iteration_status);
}

Result<std::optional<CDCStateTableEntry>> CDCStateTable::TryFetchEntry(
    const CDCStateTableKey& key, CDCStateTableEntrySelector&& field_filter) {
  DCHECK(!key.tablet_id.empty() && key.stream_id);

  std::vector<std::string> columns;
  MoveCollection(&field_filter.columns_, &columns);

  VLOG_WITH_FUNC(1) << yb::ToString(key) << ", Columns: " << yb::ToString(columns);

  const auto kCdcStreamIdColumnId =
      narrow_cast<ColumnIdRep>(Schema::first_column_id() + kCdcStreamIdIdx);

  auto cdc_table = VERIFY_RESULT(GetTable());
  auto session = VERIFY_RESULT(GetSession());

  const auto read_op = cdc_table->NewReadOp();
  auto* const req_read = read_op->mutable_request();
  QLAddStringHashValue(req_read, key.tablet_id);
  QLSetStringCondition(
      req_read->mutable_where_expr()->mutable_condition(), kCdcStreamIdColumnId, QL_OP_EQUAL,
      key.CompositeStreamId());
  req_read->mutable_column_refs()->add_ids(kCdcStreamIdColumnId);

  cdc_table->AddColumns(columns, req_read);
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK(session->TEST_ApplyAndFlush(read_op));
  auto row_block = ql::RowsResult(read_op.get()).GetRowBlock();
  if (row_block->row_count() == 0) {
    return std::nullopt;
  }

  SCHECK(
      row_block->row_count() == 1, IllegalState, "Multiple rows returned for key ", key.ToString());

  CDCStateTableEntry entry(key);
  for (size_t i = 0; i < columns.size(); i++) {
    RETURN_NOT_OK(DeserializeColumn(row_block->row(0).column(i), columns[i], &entry));
  }

  VLOG(1) << "TryFetchEntry row: " << entry.ToString();
  return entry;
}

Result<CDCStateTableEntry> CdcStateTableIterator::operator*() const {
  DCHECK(columns_);
  auto& row = client::TableIterator::operator*();
  auto entry = DeserializeRow(row, *columns_);

  VLOG(1) << "CDCStateTableRange row: "
          << (entry ? yb::ToString(*entry) : entry.status().ToString());
  return entry;
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeCheckpoint() {
  columns_.insert(kCdcCheckpoint);
  return std::move(*this);
}
CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeLastReplicationTime() {
  columns_.insert(kCdcLastReplicationTime);
  return std::move(*this);
}
CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeData() {
  columns_.insert(kCdcData);
  return std::move(*this);
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeAll() {
  return std::move(IncludeCheckpoint().IncludeLastReplicationTime().IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeActiveTime() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeCDCSDKSafeTime() {
  return IncludeData();
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeSnapshotKey() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeConfirmedFlushLSN() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeRestartLSN() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeXmin() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeRecordIdCommitTime() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludeLastPubRefreshTime() {
  return std::move(IncludeData());
}

CDCStateTableEntrySelector&& CDCStateTableEntrySelector::IncludePubRefreshTimes() {
  return std::move(IncludeData());
}


CDCStateTableRange::CDCStateTableRange(
    const std::shared_ptr<client::TableHandle>& table, Status* failure_status,
    std::vector<std::string>&& columns)
    : table_(table) {
  options_.columns = std::move(columns);

  options_.error_handler = [failure_status](const Status& status) {
    *failure_status = status.CloneAndPrepend(
        Format("Scan of table $0 failed", kCdcStateYBTableName.table_name()));
  };
}

}  // namespace yb::cdc
