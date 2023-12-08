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

#include "yb/cdc/cdc_types.h"

#include "yb/client/async_initializer.h"
#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema_pbutil.h"

#include "yb/gutil/walltime.h"

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

void SerializeEntry(
    const CDCStateTableKey& key, client::TableHandle* cdc_table, QLWriteRequestPB* req) {
  DCHECK(key.stream_id && !key.tablet_id.empty());

  QLAddStringHashValue(req, key.tablet_id);
  QLAddStringRangeValue(req, key.CompositeStreamId());
}

void SerializeEntry(
    const CDCStateTableEntry& entry, client::TableHandle* cdc_table, QLWriteRequestPB* req) {
  SerializeEntry(entry.key, cdc_table, req);

  if (entry.checkpoint) {
    cdc_table->AddStringColumnValue(req, kCdcCheckpoint, entry.checkpoint->ToString());
  }

  if (entry.last_replication_time) {
    cdc_table->AddTimestampColumnValue(
        req, kCdcLastReplicationTime, *entry.last_replication_time);
  }

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

Status CDCStateTable::OpenTable(client::TableHandle* cdc_table) {
  auto* client = VERIFY_RESULT(GetClient());
  RETURN_NOT_OK(client->WaitForCreateTableToFinish(kCdcStateYBTableName));
  RETURN_NOT_OK(cdc_table->Open(kCdcStateYBTableName, client));
  return Status::OK();
}

Result<std::shared_ptr<client::TableHandle>> CDCStateTable::GetTable() {
  bool use_cache = GetAtomicFlag(&FLAGS_enable_cdc_state_table_caching);
  if (!use_cache) {
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
  auto cdc_table = std::make_shared<client::TableHandle>();
  RETURN_NOT_OK(OpenTable(cdc_table.get()));
  cdc_table_.swap(cdc_table);
  return cdc_table_;
}

Result<client::YBClient*> CDCStateTable::GetClient() {
  if (!client_) {
    client_ = async_client_init_->client();
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
Status CDCStateTable::WriteEntries(
    const std::vector<CDCEntry>& entries, QLWriteRequestPB::QLStmtType statement_type,
    QLOperator condition_op) {
  if (entries.empty()) {
    return Status::OK();
  }

  auto cdc_table = VERIFY_RESULT(GetTable());
  auto session = VERIFY_RESULT(GetSession());

  std::vector<client::YBOperationPtr> ops;
  ops.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto op = cdc_table->NewWriteOp(statement_type);
    auto* const req = op->mutable_request();

    SerializeEntry(entry, cdc_table.get(), req);

    if (condition_op != QL_OP_NOOP) {
      auto* condition = req->mutable_if_expr()->mutable_condition();
      condition->set_op(condition_op);
    }

    ops.push_back(std::move(op));
  }
  return session->ApplyAndFlushSync(ops);
}

Status CDCStateTable::InsertEntries(const std::vector<CDCStateTableEntry>& entries) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  return WriteEntries(entries, QLWriteRequestPB::QL_STMT_INSERT, QL_OP_NOT_EXISTS);
}

Status CDCStateTable::UpdateEntries(const std::vector<CDCStateTableEntry>& entries) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  return WriteEntries(entries, QLWriteRequestPB::QL_STMT_UPDATE, QL_OP_EXISTS);
}

Status CDCStateTable::UpsertEntries(const std::vector<CDCStateTableEntry>& entries) {
  VLOG_WITH_FUNC(1) << yb::ToString(entries);
  return WriteEntries(entries, QLWriteRequestPB::QL_STMT_UPDATE);
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
  RETURN_NOT_OK(session->ReadSync(read_op));
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
