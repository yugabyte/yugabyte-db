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

#include "yb/cdc/cdc_producer.h"

#include "yb/cdc/xrepl_stream_metadata.h"

#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/colocated_util.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema_pbutil.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/log_cache.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/docdb/doc_reader.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/docdb.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/packed_value.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_util.h"

#include "yb/qlexpr/ql_expr.h"
#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_types.pb.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/opid.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using std::string;

DEFINE_RUNTIME_int32(cdc_snapshot_batch_size, 250, "Batch size for the snapshot operation in CDC");

DEFINE_RUNTIME_bool(stream_truncate_record, false, "Enable streaming of TRUNCATE record");

DECLARE_int64(cdc_intent_retention_ms);

DEFINE_RUNTIME_bool(
    enable_single_record_update, true,
    "Enable packing updates corresponding to a row in single CDC record");

DEFINE_RUNTIME_bool(
    cdc_populate_safepoint_record, true,
    "If 'true' we will also send a 'SAFEPOINT' record at the end of each GetChanges call.");

DEFINE_NON_RUNTIME_bool(
    cdc_enable_consistent_records, true,
    "If 'true' we will ensure that the records are order by the commit_time.");

DEFINE_RUNTIME_uint64(
    cdc_stream_records_threshold_size_bytes, 4_MB,
    "The threshold for the size of the response of a GetChanges call. The actual size may be a "
    "little higher than this value.");

DEFINE_test_flag(
    bool, cdc_snapshot_failure, false,
    "For testing only, When it is set to true, the CDC snapshot operation will fail.");

DEFINE_RUNTIME_bool(
    cdc_populate_end_markers_transactions, true,
    "If 'true', we will also send 'BEGIN' and 'COMMIT' records for both single shard and multi "
    "shard transactions");

DEFINE_NON_RUNTIME_int64(
    cdc_resolve_intent_lag_threshold_ms, 5 * 60 * 1000,
    "The lag threshold in milli seconds between the hybrid time returned by "
    "GetMinStartTimeAmongAllRunningTransactions and LeaderSafeTime, when we decide the "
    "ConsistentStreamSafeTime for CDCSDK by resolving all committed intetns");

DECLARE_bool(ysql_enable_packed_row);

namespace yb {
namespace cdc {

using consensus::ReplicateMsgPtr;
using consensus::ReplicateMsgs;
using dockv::PrimitiveValue;
using dockv::SchemaPackingStorage;

namespace {
YB_DEFINE_ENUM(OpType, (INSERT)(UPDATE)(DELETE));

Result<TransactionStatusResult> GetTransactionStatus(
    const TransactionId& txn_id,
    const HybridTime& hybrid_time,
    tablet::TransactionParticipant* txn_participant) {
  static const std::string reason = "cdc";

  std::promise<Result<TransactionStatusResult>> txn_status_promise;
  auto future = txn_status_promise.get_future();
  auto callback = [&txn_status_promise](Result<TransactionStatusResult> result) {
    txn_status_promise.set_value(std::move(result));
  };

  txn_participant->RequestStatusAt(
      {&txn_id, hybrid_time, hybrid_time, 0, &reason, TransactionLoadFlags{}, callback});
  future.wait();
  return future.get();
}

void SetOperation(RowMessage* row_message, OpType type, const Schema& schema) {
  switch (type) {
    case OpType::INSERT:
      row_message->set_op(RowMessage_Op_INSERT);
      break;
    case OpType::UPDATE:
      row_message->set_op(RowMessage_Op_UPDATE);
      break;
    case OpType::DELETE:
      row_message->set_op(RowMessage_Op_DELETE);
      break;
  }

  row_message->set_pgschema_name(schema.SchemaName());
}

bool AddBothOldAndNewValues(const CDCRecordType& record_type) {
  return record_type == CDCRecordType::ALL ||
         record_type == CDCRecordType::MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES;
}

bool IsOldRowNeededOnDelete(const CDCRecordType& record_type) {
  return record_type == CDCRecordType::ALL ||
         record_type == CDCRecordType::FULL_ROW_NEW_IMAGE;
}

template <class Value>
Status AddColumnToMap(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const ColumnSchema& col_schema,
    const Value& col, const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map, DatumMessagePB* cdc_datum_message,
    const QLValuePB* old_ql_value_passed) {
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  cdc_datum_message->set_column_name(col_schema.name());
  QLValuePB ql_value;
  if (old_ql_value_passed) {
    ql_value = *old_ql_value_passed;
  } else {
    col.ToQLValuePB(col_schema.type(), &ql_value);
  }
  if (tablet->table_type() == PGSQL_TABLE_TYPE) {
    if (!IsNull(ql_value) && col_schema.pg_type_oid() != 0 /*kInvalidOid*/) {
      RETURN_NOT_OK(docdb::SetValueFromQLBinaryWrapper(
          ql_value, col_schema.pg_type_oid(), enum_oid_label_map, composite_atts_map,
          cdc_datum_message));
    } else {
      cdc_datum_message->set_column_type(col_schema.pg_type_oid());
      cdc_datum_message->set_pg_type(col_schema.pg_type_oid());
    }
  } else {
    cdc_datum_message->mutable_cql_value()->CopyFrom(ql_value);
    col_schema.type()->ToQLTypePB(cdc_datum_message->mutable_cql_type());
  }
  return Status::OK();
}

DatumMessagePB* AddTuple(RowMessage* row_message, const StreamMetadata& metadata) {
  if (!row_message) {
    return nullptr;
  }
  DatumMessagePB* tuple = nullptr;

  if (row_message->op() == RowMessage_Op_DELETE) {
    tuple = row_message->add_old_tuple();
    row_message->add_new_tuple();
  } else {
    tuple = row_message->add_new_tuple();
    if ((row_message->op() == RowMessage_Op_INSERT) ||
        !AddBothOldAndNewValues(metadata.GetRecordType()))
      row_message->add_old_tuple();
  }
  return tuple;
}

Status AddPrimaryKey(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const dockv::SubDocKey& decoded_key,
    const Schema& tablet_schema, const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map, RowMessage* row_message,
    const StreamMetadata& metadata, std::unordered_set<std::string>* modified_columns,
    bool add_to_record) {
  size_t i = 0;
  for (const auto& col : decoded_key.doc_key().hashed_group()) {
    modified_columns->insert(tablet_schema.column(i).name());
    if (add_to_record) {
      DatumMessagePB* tuple = AddTuple(row_message, metadata);
      RETURN_NOT_OK(AddColumnToMap(
          tablet_peer, tablet_schema.column(i), col, enum_oid_label_map, composite_atts_map, tuple,
          nullptr));
    }
    i++;
  }

  for (const auto& col : decoded_key.doc_key().range_group()) {
    modified_columns->insert(tablet_schema.column(i).name());
    if (add_to_record) {
      DatumMessagePB* tuple = AddTuple(row_message, metadata);
      RETURN_NOT_OK(AddColumnToMap(
          tablet_peer, tablet_schema.column(i), col, enum_oid_label_map, composite_atts_map, tuple,
          nullptr));
    }
    i++;
  }
  return Status::OK();
}

void SetCDCSDKOpId(
    int64_t term, int64_t index, uint32_t write_id, const std::string& key,
    CDCSDKOpIdPB* cdc_sdk_op_id_pb) {
  cdc_sdk_op_id_pb->set_term(term);
  cdc_sdk_op_id_pb->set_index(index);
  cdc_sdk_op_id_pb->set_write_id(write_id);
  cdc_sdk_op_id_pb->set_write_id_key(key);
}

void SetCheckpoint(
    int64_t term, int64_t index, int32 write_id, const std::string& key, uint64 time,
    CDCSDKCheckpointPB* cdc_sdk_checkpoint_pb, OpId* last_streamed_op_id) {
  cdc_sdk_checkpoint_pb->set_term(term);
  cdc_sdk_checkpoint_pb->set_index(index);
  cdc_sdk_checkpoint_pb->set_write_id(write_id);
  cdc_sdk_checkpoint_pb->set_key(key);
  cdc_sdk_checkpoint_pb->set_snapshot_time(time);
  if (last_streamed_op_id) {
    last_streamed_op_id->term = term;
    last_streamed_op_id->index = index;
  }
}

bool IsInsertOperation(const RowMessage& row_message) {
  return row_message.op() == RowMessage_Op_INSERT;
}

bool IsInsertOrUpdate(const RowMessage& row_message) {
  return row_message.IsInitialized() &&
         (row_message.op() == RowMessage_Op_INSERT || row_message.op() == RowMessage_Op_UPDATE);
}

Result<bool> ShouldPopulateNewInsertRecord(
    const bool& end_of_intents, const docdb::IntentKeyValueForCDC& next_intent,
    const Slice& current_primary_key, const HybridTime& current_hybrid_time,
    const bool& end_of_transaction) {
  if (!end_of_intents) {
    Slice next_key(next_intent.key_buf);
    const auto next_key_size =
        VERIFY_RESULT(dockv::DocKey::EncodedSize(next_key, dockv::DocKeyPart::kWholeDocKey));

    dockv::KeyEntryValue next_column_id;
    boost::optional<dockv::KeyEntryValue> next_column_id_opt;
    Slice next_key_column = next_key.WithoutPrefix(next_key_size);
    if (!next_key_column.empty()) {
      RETURN_NOT_OK(dockv::KeyEntryValue::DecodeKey(&next_key_column, &next_column_id));
      next_column_id_opt = next_column_id;
    }

    dockv::SubDocKey next_decoded_key;
    Slice next_sub_doc_key = next_key;
    RETURN_NOT_OK(
        next_decoded_key.DecodeFrom(&next_sub_doc_key, dockv::HybridTimeRequired::kFalse));

    Slice next_primary_key(next_key.data(), next_key_size);

    return (current_primary_key != next_primary_key) ||
           (current_hybrid_time != next_intent.intent_ht.hybrid_time());
  } else {
    return end_of_transaction;
  }
}

Result<bool> ShouldPopulateNewWriteRecord(
    const bool& end_of_write_batch, const yb::docdb::LWKeyValuePairPB& next_write_pair,
    const Slice& current_primary_key) {
  if (end_of_write_batch) {
    return true;
  }
  Slice key = next_write_pair.key();
  const auto key_size =
      VERIFY_RESULT(dockv::DocKey::EncodedSize(key, dockv::DocKeyPart::kWholeDocKey));

  Slice sub_doc_key = key;
  dockv::SubDocKey decoded_key;
  RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, dockv::HybridTimeRequired::kFalse));
  Slice primary_key(key.data(), key_size);

  return (primary_key != current_primary_key);
}

void MakeNewProtoRecord(
    const docdb::IntentKeyValueForCDC& intent, const OpId& op_id, const RowMessage& row_message,
    const Schema& schema, size_t col_count, CDCSDKProtoRecordPB* proto_record,
    GetChangesResponsePB* resp, IntraTxnWriteId* write_id, std::string* reverse_index_key,
    const uint64_t& commit_time) {
  CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
  SetCDCSDKOpId(
      op_id.term, op_id.index, intent.write_id, intent.reverse_index_key, cdc_sdk_op_id_pb);

  CDCSDKProtoRecordPB* record_to_be_added = resp->add_cdc_sdk_proto_records();
  record_to_be_added->CopyFrom(*proto_record);
  record_to_be_added->mutable_row_message()->CopyFrom(row_message);

  if (!record_to_be_added->row_message().has_commit_time()) {
    record_to_be_added->mutable_row_message()->set_commit_time(commit_time);
  }
  if (!record_to_be_added->row_message().has_record_time()) {
    Slice doc_ht(intent.ht_buf);
    auto result = DocHybridTime::DecodeFromEnd(&doc_ht);
    if (result.ok()) {
      record_to_be_added->mutable_row_message()->set_record_time((*result).hybrid_time().value());
    } else {
      LOG(WARNING) << "Failed to get commit hybrid time for intent key: " << intent.key_buf.c_str();
    }
  }

  *write_id = intent.write_id;
  *reverse_index_key = intent.reverse_index_key;
}

void EquateOldAndNewTuple(RowMessage* row_message) {
  if (row_message->new_tuple_size() > row_message->old_tuple_size()) {
    for (int i = 0; i < (row_message->new_tuple_size() - row_message->old_tuple_size()); i++) {
      row_message->add_old_tuple();
    }
  } else {
    for (int i = 0; i < (row_message->old_tuple_size() - row_message->new_tuple_size()); i++) {
      row_message->add_new_tuple();
    }
  }
}

Status PopulateBeforeImageForDeleteOp(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, RowMessage* row_message,
    const EnumOidLabelMap& enum_oid_label_map, const CompositeAttsMap& composite_atts_map,
    const Schema& schema, const std::vector<ColumnSchema>& columns, const qlexpr::QLTableRow& row,
    const cdc::CDCRecordType& record_type) {
  if (IsOldRowNeededOnDelete(record_type)) {
    QLValue ql_value;
    if (row.ColumnCount() == columns.size()) {
      for (size_t index = 0; index < row.ColumnCount(); ++index) {
        RETURN_NOT_OK(row.GetValue(schema.column_id(index), &ql_value));
        if (!ql_value.IsNull()) {
          RETURN_NOT_OK(AddColumnToMap(
              tablet_peer, columns[index], PrimitiveValue(), enum_oid_label_map, composite_atts_map,
              row_message->add_old_tuple(), &ql_value.value()));
        }
      }
    }

    for (int i = 0; i < row_message->old_tuple_size(); i++) {
      row_message->add_new_tuple();
    }
  }

  return Status::OK();
}

Status PopulateBeforeImageForUpdateOp(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, RowMessage* row_message,
    const EnumOidLabelMap& enum_oid_label_map, const CompositeAttsMap& composite_atts_map,
    const Schema& schema, const std::vector<ColumnSchema>& columns, const qlexpr::QLTableRow& row,
    const std::unordered_set<std::string>& modified_columns,
    const cdc::CDCRecordType& record_type) {
  QLValue ql_value;
  size_t found_columns = 0;
  if (row.ColumnCount() == columns.size()) {
    for (size_t index = 0; index < row.ColumnCount(); ++index) {
      RETURN_NOT_OK(row.GetValue(schema.column_id(index), &ql_value));
      bool shouldAddColumn = ContainsKey(modified_columns, columns[index].name());
      switch (record_type) {
        case CDCRecordType::MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES: {
          if (!ql_value.IsNull() && shouldAddColumn) {
            RETURN_NOT_OK(AddColumnToMap(
                tablet_peer, columns[index], PrimitiveValue(), enum_oid_label_map,
                composite_atts_map, row_message->add_old_tuple(), &ql_value.value()));
          }
          break;
        }
        case CDCRecordType::FULL_ROW_NEW_IMAGE: {
          if (!ql_value.IsNull() && !shouldAddColumn) {
            RETURN_NOT_OK(AddColumnToMap(
                tablet_peer, columns[index], PrimitiveValue(), enum_oid_label_map,
                composite_atts_map, row_message->add_new_tuple(), &ql_value.value()));
          }
          break;
        }
        case CDCRecordType::ALL: {
          if (!ql_value.IsNull()) {
            RETURN_NOT_OK(AddColumnToMap(
                tablet_peer, columns[index], PrimitiveValue(), enum_oid_label_map,
                composite_atts_map, row_message->add_old_tuple(), &ql_value.value()));
            if (!shouldAddColumn) {
              auto new_tuple_pb = row_message->mutable_new_tuple()->Add();
              new_tuple_pb->CopyFrom(row_message->old_tuple(static_cast<int>(found_columns)));
            }
            found_columns++;
          }
          break;
        }
        default:
          break;
      }
    }
  }
  EquateOldAndNewTuple(row_message);
  return Status::OK();
}

Status PopulateBeforeImage(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const ReadHybridTime& read_time,
    RowMessage* row_message, const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map, const dockv::SubDocKey& decoded_primary_key,
    const Schema& schema, const SchemaVersion schema_version, const ColocationId& colocation_id,
    const std::unordered_set<std::string>& modified_columns,
    const cdc::CDCRecordType& record_type) {
  if (record_type == cdc::CDCRecordType::CHANGE || row_message->op() == RowMessage_Op_INSERT) {
    return Status::OK();
  }

  auto tablet = tablet_peer->shared_tablet();
  auto docdb = tablet->doc_db();
  auto pending_op = tablet->CreateScopedRWOperationNotBlockingRocksDbShutdownStart();

  const auto log_prefix = tablet->LogPrefix();
  auto doc_read_context = VERIFY_RESULT(
      tablet_peer->tablet_metadata()->GetTableInfo(colocation_id))->doc_read_context;
  dockv::ReaderProjection projection(schema);
  docdb::DocRowwiseIterator iter(
      projection, *doc_read_context, TransactionOperationContext(), docdb,
      docdb::ReadOperationData::FromReadTime(read_time), pending_op);
  iter.SetSchema(schema);

  const dockv::DocKey& doc_key = decoded_primary_key.doc_key();
  docdb::DocQLScanSpec spec(schema, doc_key, rocksdb::kDefaultQueryId);
  RETURN_NOT_OK(iter.Init(spec));

  qlexpr::QLTableRow row;
  QLValue ql_value;
  // If CDC is failed to get the before image row, skip adding before image columns.
  auto result = VERIFY_RESULT(iter.FetchNext(&row));
  if (!result) {
    return STATUS_FORMAT(
        InternalError, "Failed to get the beforeimage for tablet_id: $0", tablet_peer->tablet_id());
  }

  std::vector<ColumnSchema> columns(schema.columns());

  switch (row_message->op()) {
    case RowMessage_Op_DELETE: {
      return PopulateBeforeImageForDeleteOp(
          tablet_peer, row_message, enum_oid_label_map, composite_atts_map, schema, columns, row,
          record_type);
    }
    case RowMessage_Op_UPDATE: {
      return PopulateBeforeImageForUpdateOp(
          tablet_peer, row_message, enum_oid_label_map, composite_atts_map, schema, columns, row,
          modified_columns, record_type);
    }
    default: {
      return Status::OK();
    }
  }

  return Status::OK();
}

template <class Decoder>
Result<size_t> DoPopulatePackedRows(
    const SchemaPackingStorage& schema_packing_storage, const Schema& schema,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map, const CompositeAttsMap& composite_atts_map,
    Slice* value_slice, RowMessage* row_message, std::unordered_set<std::string>* modified_columns,
    const cdc::CDCRecordType& record_type, std::unordered_set<std::string>* null_value_columns) {

  const dockv::SchemaPacking& packing =
      VERIFY_RESULT(schema_packing_storage.GetPacking(value_slice));
  Decoder decoder(packing, value_slice->data());
  for (size_t i = 0; i != packing.columns(); ++i) {
    auto column_value = decoder.FetchValue(i);
    const auto& column_data = packing.column_packing_data(i);

    auto pv = VERIFY_RESULT(UnpackPrimitiveValue(column_value, column_data.data_type));
    const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_data.id));
    modified_columns->insert(col.name());

    if (column_value.IsNull()) {
      null_value_columns->insert(col.name());
      continue;
    }

    RETURN_NOT_OK(AddColumnToMap(
        tablet_peer, col, pv, enum_oid_label_map, composite_atts_map,
        row_message->add_new_tuple(), nullptr));
    row_message->add_old_tuple();
  }

  return packing.columns();
}

template <class... Args>
Result<size_t> PopulatePackedRows(
    dockv::PackedRowVersion version, Args&&... args) {
  switch (version) {
    case dockv::PackedRowVersion::kV1:
      return DoPopulatePackedRows<dockv::PackedRowDecoderV1>(std::forward<Args>(args)...);
    case dockv::PackedRowVersion::kV2:
      return DoPopulatePackedRows<dockv::PackedRowDecoderV2>(std::forward<Args>(args)...);
  }
  return UnexpectedPackedRowVersionStatus(version);
}

HybridTime GetCDCSDKSafeTimeForTarget(
    const HybridTime leader_safe_time, HybridTime safe_hybrid_time_resp,
    HaveMoreMessages have_more_messages, const uint64_t& consistent_stream_safe_time,
    const bool& is_snapshot_op) {
  if (FLAGS_cdc_enable_consistent_records || is_snapshot_op) {
    return safe_hybrid_time_resp.is_valid() ? safe_hybrid_time_resp
                                            : HybridTime(consistent_stream_safe_time);
  }

  if (have_more_messages) {
    return safe_hybrid_time_resp;
  }

  if (safe_hybrid_time_resp.is_valid()) {
    if (!leader_safe_time.is_valid() || safe_hybrid_time_resp > leader_safe_time) {
      return safe_hybrid_time_resp;
    }
  }

  return leader_safe_time;
}

void SetTableProperties(
    const TablePropertiesPB& table_properties,
    CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb) {
  cdc_sdk_table_properties_pb->set_default_time_to_live(table_properties.default_time_to_live());
  cdc_sdk_table_properties_pb->set_num_tablets(table_properties.num_tablets());
  cdc_sdk_table_properties_pb->set_is_ysql_catalog_table(table_properties.is_ysql_catalog_table());
}

void SetColumnInfo(const ColumnSchemaPB& column, CDCSDKColumnInfoPB* column_info) {
  column_info->set_name(column.name());
  column_info->mutable_type()->CopyFrom(column.type());
  column_info->set_is_key(column.is_key());
  column_info->set_is_hash_key(column.is_hash_key());
  column_info->set_is_nullable(column.is_nullable());
  column_info->set_oid(column.pg_type_oid());
}

void FillDDLInfo(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const SchemaDetails current_schema_details,
    const TableName table_name,
    GetChangesResponsePB* resp) {
  const SchemaVersion& schema_version = current_schema_details.schema_version;
  SchemaPB schema_pb;
  SchemaToPB(*current_schema_details.schema, &schema_pb);
  CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
  RowMessage* row_message = proto_record->mutable_row_message();
  row_message->set_op(RowMessage_Op_DDL);
  row_message->set_table(table_name);
  for (const auto& column : schema_pb.columns()) {
    CDCSDKColumnInfoPB* column_info;
    column_info = row_message->mutable_schema()->add_column_info();
    SetColumnInfo(column, column_info);
  }

  row_message->set_schema_version(schema_version);
  row_message->set_pgschema_name(schema_pb.pgschema_name());
  CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb =
      row_message->mutable_schema()->mutable_tab_info();

  SetTableProperties(schema_pb.table_properties(), cdc_sdk_table_properties_pb);
}

Result<TableName> GetColocatedTableName(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const TableId& req_table_id) {
  for (auto const& cur_table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    if (cur_table_id != req_table_id) {
      continue;
    }

    const auto& tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
    return tablet->metadata()->table_name(cur_table_id);
  }

  return STATUS_FORMAT(InternalError, "Could not find name for table with id: ", req_table_id);
}

Result<SchemaDetails> GetOrPopulateRequiredSchemaDetails(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, uint64 read_hybrid_time,
    SchemaDetailsMap* cached_schema_details, client::YBClient* client, const TableId& req_table_id,
    GetChangesResponsePB* resp) {
  auto iter = cached_schema_details->find(req_table_id);
  if (iter != cached_schema_details->end()) {
    return iter->second;
  }

  for (auto const& cur_table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    if (cur_table_id != req_table_id) {
      continue;
    }

    auto tablet_result = tablet_peer->shared_tablet_safe();
    RETURN_NOT_OK(tablet_result);

    auto tablet = *tablet_result;
    auto table_name = tablet->metadata()->table_name(cur_table_id);
    // Ignore the DDL information of the parent table.
    if (tablet->metadata()->colocated() &&
        (boost::ends_with(table_name, kTablegroupParentTableNameSuffix) ||
         boost::ends_with(table_name, kColocationParentTableNameSuffix))) {
      continue;
    }

    auto result = client->GetTableSchemaFromSysCatalog(cur_table_id, read_hybrid_time);
    // Failed to get specific schema version from the system catalog, use the latest
    // schema version for the key-value decoding.
    if (!result.ok()) {
      (*cached_schema_details)[cur_table_id] = SchemaDetails{
          .schema_version = tablet->metadata()->schema_version(cur_table_id),
          .schema = tablet->metadata()->schema(cur_table_id)};
      LOG(WARNING) << "Failed to get the specific schema version from system catalog for table: "
                   << table_name << " with read hybrid time: " << read_hybrid_time;
    } else {
      (*cached_schema_details)[cur_table_id] = SchemaDetails{
          .schema_version = result->second, .schema = std::make_shared<Schema>(result->first)};
      VLOG(1) << "Found schema version:" << result->second << " for table : " << table_name
              << " from system catalog table with read hybrid time: " << read_hybrid_time;
    }

    VLOG(1) << "Populating schema details for table " << req_table_id
            << " tablet " << tablet->tablet_id();

    const auto& schema_details = (*cached_schema_details)[cur_table_id];
    FillDDLInfo(tablet_peer, schema_details, table_name, resp);

    return schema_details;
  }

  return STATUS_FORMAT(InternalError, "Did not find schema for table: ", req_table_id);
}

// Populate CDC record corresponding to WAL batch in ReplicateMsg.
Status PopulateCDCSDKIntentRecord(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const std::vector<docdb::IntentKeyValueForCDC>& intents,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    SchemaDetailsMap* cached_schema_details,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    IntraTxnWriteId* write_id,
    std::string* reverse_index_key,
    const uint64_t& commit_time,
    client::YBClient* client,
    const bool& end_of_transaction) {
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());

  bool colocated = tablet->metadata()->colocated();
  Schema schema = Schema();
  SchemaVersion schema_version = std::numeric_limits<uint32_t>::max();
  SchemaPackingStorage schema_packing_storage(tablet->table_type());
  ColocationId colocation_id = kColocationIdNotSet;

  if (!colocated) {
    const auto& schema_details = VERIFY_RESULT(GetOrPopulateRequiredSchemaDetails(
        tablet_peer, intents.begin()->intent_ht.hybrid_time().ToUint64(), cached_schema_details,
        client, tablet->metadata()->table_id(), resp));
    schema = *schema_details.schema;
    schema_version = schema_details.schema_version;
    schema_packing_storage.AddSchema(schema_version, schema);
  }

  std::string table_name = tablet->metadata()->table_name();
  Slice prev_key;
  CDCSDKProtoRecordPB proto_record;
  RowMessage* row_message = proto_record.mutable_row_message();
  std::unordered_set<std::string> modified_columns;
  size_t col_count = 0;
  docdb::IntentKeyValueForCDC prev_intent;
  MicrosTime prev_intent_phy_time = 0;
  bool new_cdc_record_needed = false;
  dockv::SubDocKey prev_decoded_key;
  std::unordered_set<std::string> null_value_columns;
  bool is_packed_row_record = false;

  for (size_t i = 0; i < intents.size(); i++) {
    const docdb::IntentKeyValueForCDC& intent = intents[i];
    Slice key(intent.key_buf);
    const auto key_size =
        VERIFY_RESULT(dockv::DocKey::EncodedSize(key, dockv::DocKeyPart::kWholeDocKey));

    dockv::KeyEntryValue column_id;
    boost::optional<dockv::KeyEntryValue> column_id_opt;
    Slice key_column = key.WithoutPrefix(key_size);
    if (!key_column.empty()) {
      RETURN_NOT_OK(dockv::KeyEntryValue::DecodeKey(&key_column, &column_id));
      column_id_opt = column_id;
    }

    dockv::SubDocKey decoded_key;
    Slice sub_doc_key = key;
    RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, dockv::HybridTimeRequired::kFalse));

    Slice value_slice = intent.value_buf;
    RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value_slice));
    auto value_type = dockv::DecodeValueEntryType(value_slice);
    value_slice.consume_byte();

    if (column_id_opt && column_id_opt->type() == dockv::KeyEntryType::kColumnId &&
        schema.is_key_column(column_id_opt->GetColumnId())) {
      *write_id = intent.write_id;
      *reverse_index_key = intent.reverse_index_key;
      continue;
    }

    if (*consumption) {
      consumption->Add(key.size());
    }

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice primary_key(key.data(), key_size);
    if (GetAtomicFlag(&FLAGS_enable_single_record_update)) {
      new_cdc_record_needed =
          (prev_key != primary_key) ||
          (value_type == dockv::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) ||
          prev_intent_phy_time != intent.intent_ht.hybrid_time().GetPhysicalValueMicros();
    } else {
      new_cdc_record_needed = (prev_key != primary_key) || (col_count >= schema.num_columns());
    }

    if (new_cdc_record_needed) {
      if (FLAGS_enable_single_record_update) {
        col_count = 0;

        if (proto_record.IsInitialized() && row_message->IsInitialized() &&
            row_message->op() == RowMessage_Op_UPDATE) {
          if (metadata.GetRecordType() != cdc::CDCRecordType::CHANGE) {
            VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                    << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                    << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                    << " for change record type: " << row_message->op();
            if (commit_time > 0) {
              auto hybrid_time = commit_time - 1;
              auto result = PopulateBeforeImage(
                  tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message,
                  enum_oid_label_map, composite_atts_map, prev_decoded_key, schema, schema_version,
                  colocation_id, modified_columns, metadata.GetRecordType());
              if (!result.ok()) {
                LOG(ERROR) << "Failed to get the Beforeimage for tablet: "
                           << tablet_peer->tablet_id()
                           << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                           << " for change record type: " << row_message->op()
                           << " row_message: " << row_message->DebugString()
                           << " with error status: " << result;
                return result;
              }
              VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
                      << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                      << " for change record type: " << row_message->op()
                      << " row_message: " << row_message->DebugString();
            } else {
              for (size_t index = 0; index < schema.num_columns(); ++index) {
                row_message->add_old_tuple();
              }
            }
          } else {
            for (int index = 0; index < row_message->new_tuple_size(); ++index) {
              row_message->add_old_tuple();
            }
          }

          MakeNewProtoRecord(
              prev_intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
              reverse_index_key, commit_time);
        }
      }

      proto_record.Clear();
      row_message->Clear();
      modified_columns.clear();
      null_value_columns.clear();
      is_packed_row_record = false;

      if (colocated) {
        colocation_id = decoded_key.doc_key().colocation_id();
        auto table_info = CHECK_RESULT(tablet->metadata()->GetTableInfo(colocation_id));

        const auto& schema_details = VERIFY_RESULT(GetOrPopulateRequiredSchemaDetails(
            tablet_peer, intents.begin()->intent_ht.hybrid_time().ToUint64(), cached_schema_details,
            client, table_info->table_id, resp));

        schema = *schema_details.schema;
        schema_version = schema_details.schema_version;
        table_name = table_info->table_name;
        schema_packing_storage = SchemaPackingStorage(tablet->table_type());
        schema_packing_storage.AddSchema(schema_version, schema);
      }

      // Check whether operation is WRITE or DELETE.
      if (value_type == dockv::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        SetOperation(row_message, OpType::DELETE, schema);
        if (!FLAGS_enable_single_record_update) {
          col_count = schema.num_columns();
        }
      } else if (IsPackedRow(value_type)) {
        is_packed_row_record = true;
        SetOperation(row_message, OpType::INSERT, schema);
        col_count = schema.num_key_columns();
      } else {
        if (column_id_opt && column_id_opt->type() == dockv::KeyEntryType::kSystemColumnId &&
            value_type == dockv::ValueEntryType::kNullLow) {
          SetOperation(row_message, OpType::INSERT, schema);
          col_count = schema.num_key_columns() - 1;
        } else {
          SetOperation(row_message, OpType::UPDATE, schema);
          if (!FLAGS_enable_single_record_update) {
            col_count = schema.num_columns();
          }
          *write_id = intent.write_id;
        }
      }

      // Write pair contains record for different row. Create a new CDCRecord in this case.
      row_message->set_transaction_id(transaction_id.ToString());
      row_message->set_commit_time(commit_time);
      row_message->set_record_time(intent.intent_ht.hybrid_time().ToUint64());

      if (IsOldRowNeededOnDelete(metadata.GetRecordType()) &&
         (row_message->op() == RowMessage_Op_DELETE)) {
        VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                << "  cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                << "  for change record type: " << row_message->op();
        if (commit_time > 0) {
          auto hybrid_time = commit_time - 1;
          auto result = PopulateBeforeImage(
              tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message, enum_oid_label_map,
              composite_atts_map, decoded_key, schema, schema_version, colocation_id,
              modified_columns, metadata.GetRecordType());
          if (!result.ok()) {
            LOG(ERROR) << "Failed to get the Beforeimage for tablet: " << tablet_peer->tablet_id()
                       << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                       << " for change record type: " << row_message->op()
                       << " row_message: " << row_message->DebugString()
                       << " with error status: " << result;
            return result;
          }
          VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
                  << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                  << " for change record type: " << row_message->op()
                  << " row_message: " << row_message->DebugString();
        }

        if (row_message->old_tuple_size() == 0) {
          RETURN_NOT_OK(AddPrimaryKey(
              tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
              metadata, &modified_columns, true));
        }
      } else {
        if (row_message->op() != RowMessage_Op_UPDATE) {
          RETURN_NOT_OK(AddPrimaryKey(
              tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
              metadata, &modified_columns, true));
        } else {
          RETURN_NOT_OK(AddPrimaryKey(
              tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
              metadata, &modified_columns, true));
        }
      }
    }

    prev_key = primary_key;
    prev_intent_phy_time = intent.intent_ht.hybrid_time().GetPhysicalValueMicros();
    if (IsInsertOrUpdate(*row_message)) {
      if (auto packed_row_version = GetPackedRowVersion(value_type)) {
        col_count += VERIFY_RESULT(PopulatePackedRows(
            *packed_row_version, schema_packing_storage, schema, tablet_peer, enum_oid_label_map,
            composite_atts_map, &value_slice, row_message, &modified_columns,
            metadata.GetRecordType(), &null_value_columns));
      } else {
        if (FLAGS_enable_single_record_update) {
          ++col_count;
        } else {
          if (IsInsertOperation(*row_message)) {
            ++col_count;
          }
        }

        dockv::Value decoded_value;
        RETURN_NOT_OK(decoded_value.Decode(intent.value_buf));

        if (column_id_opt && column_id_opt->type() == dockv::KeyEntryType::kColumnId) {
          const ColumnSchema& col =
              VERIFY_RESULT(schema.column_by_id(column_id_opt->GetColumnId()));
          modified_columns.insert(col.name());

          auto it = null_value_columns.find(col.name());
          if (it != null_value_columns.end()) {
            null_value_columns.erase(it);
          }
          RETURN_NOT_OK(AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), enum_oid_label_map,
              composite_atts_map, row_message->add_new_tuple(), nullptr));
          if (row_message->op() == RowMessage_Op_INSERT) {
            row_message->add_old_tuple();
          }

        } else if (column_id_opt && column_id_opt->type() != dockv::KeyEntryType::kSystemColumnId) {
          LOG(DFATAL) << "Unexpected value type in key: " << column_id_opt->type()
                      << " key: " << decoded_key.ToString()
                      << " value: " << decoded_value.primitive_value();
        }
      }
    }
    row_message->set_table(table_name);

    // Get the next intent to see if it should go into a new record.
    bool is_last_intent = (i == intents.size() -1);
    docdb::IntentKeyValueForCDC next_intent;
    if (!is_last_intent) {
      next_intent = intents[i + 1];
    }
    bool populate_new_record = VERIFY_RESULT(ShouldPopulateNewInsertRecord(
        is_last_intent, next_intent, primary_key, intent.intent_ht.hybrid_time(),
        end_of_transaction));

    if (FLAGS_enable_single_record_update) {
      if ((row_message->op() == RowMessage_Op_INSERT && populate_new_record) ||
          (row_message->op() == RowMessage_Op_DELETE)) {
        if (is_packed_row_record) {
          for (auto column_name : null_value_columns) {
            ColumnId column_id = VERIFY_RESULT(schema.ColumnIdByName(column_name));
            ColumnSchema col = VERIFY_RESULT(schema.column_by_id(column_id));
            RETURN_NOT_OK(AddColumnToMap(
                tablet_peer, col, PrimitiveValue(), enum_oid_label_map, composite_atts_map,
                row_message->add_new_tuple(), nullptr));
            row_message->add_old_tuple();
          }
        }
        MakeNewProtoRecord(
            intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
            reverse_index_key, commit_time);
        col_count = schema.num_columns();
      } else if (row_message->op() == RowMessage_Op_UPDATE) {
        prev_intent = intent;
        prev_decoded_key = decoded_key;
      }
    } else {
      if ((row_message->op() == RowMessage_Op_INSERT && populate_new_record) ||
          (row_message->op() == RowMessage_Op_UPDATE ||
           row_message->op() == RowMessage_Op_DELETE)) {
        if ((metadata.GetRecordType() != cdc::CDCRecordType::CHANGE) &&
            (row_message->op() == RowMessage_Op_UPDATE)) {
          VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                  << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                  << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                  << " for change record type: " << row_message->op();
          if (commit_time > 0) {
            auto hybrid_time = commit_time - 1;
            auto result = PopulateBeforeImage(
                tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message,
                enum_oid_label_map, composite_atts_map, decoded_key, schema, schema_version,
                colocation_id, modified_columns, metadata.GetRecordType());
            if (!result.ok()) {
              LOG(ERROR) << "Failed to get the Beforeimage for tablet: " << tablet_peer->tablet_id()
                         << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                         << " for change record type: " << row_message->op()
                         << " row_message: " << row_message->DebugString()
                         << " with error status: " << result;
              return result;
            }
            VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
                    << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                    << " for change record type: " << row_message->op()
                    << " row_message: " << row_message->DebugString();
          } else {
            for (size_t index = 0; index < schema.num_columns(); ++index) {
              row_message->add_old_tuple();
            }
          }
        } else {
          row_message->add_old_tuple();
        }
        MakeNewProtoRecord(
            intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
            reverse_index_key, commit_time);
      }
    }
  }

  if (FLAGS_enable_single_record_update && proto_record.IsInitialized() &&
      row_message->IsInitialized() && row_message->op() == RowMessage_Op_UPDATE &&
      end_of_transaction) {
    row_message->set_table(table_name);
    if (metadata.GetRecordType() != cdc::CDCRecordType::CHANGE) {
      VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
              << " with read time: " << ReadHybridTime::FromUint64(commit_time)
              << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
              << " for change record type: " << row_message->op();
      if (commit_time > 0) {
        auto hybrid_time = commit_time - 1;
        auto result = PopulateBeforeImage(
            tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message, enum_oid_label_map,
            composite_atts_map, prev_decoded_key, schema, schema_version, colocation_id,
            modified_columns, metadata.GetRecordType());
        if (!result.ok()) {
          LOG(ERROR) << "Failed to get the Beforeimage for tablet: " << tablet_peer->tablet_id()
                     << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                     << " for change record type: " << row_message->op()
                     << " row_message: " << row_message->DebugString()
                     << " with error status: " << result;
          return result;
        }
        VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
                << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                << " for change record type: " << row_message->op()
                << " row_message: " << row_message->DebugString();
      } else {
        for (size_t index = 0; index < schema.num_columns(); ++index) {
          row_message->add_old_tuple();
        }
      }
    } else {
      for (int index = 0; index < row_message->new_tuple_size(); ++index) {
        row_message->add_old_tuple();
      }
    }
    MakeNewProtoRecord(
        prev_intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
        reverse_index_key, commit_time);
  }

  return Status::OK();
}

void FillBeginRecordForSingleShardTransaction(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, GetChangesResponsePB* resp,
    const uint64_t& commit_timestamp) {
  for (auto const& table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    auto tablet_result = tablet_peer->shared_tablet_safe();
    if (!tablet_result.ok()) {
      LOG(WARNING) << tablet_result.status();
      continue;
    }
    auto tablet = *tablet_result;
    auto table_name = tablet->metadata()->table_name(table_id);
    // Ignore the DDL information of the parent table.
    if (tablet->metadata()->colocated() &&
        (boost::ends_with(table_name, kTablegroupParentTableNameSuffix) ||
         boost::ends_with(table_name, kColocationParentTableNameSuffix))) {
      continue;
    }
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();
    row_message->set_op(RowMessage_Op_BEGIN);
    row_message->set_table(table_name);
    row_message->set_commit_time(commit_timestamp);
    // No need to add record_time to the Begin record since it does not have any intent associated
    // with it.
  }
}

void FillCommitRecordForSingleShardTransaction(
    const OpId& op_id, const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    GetChangesResponsePB* resp, const uint64_t& commit_timestamp) {
  for (auto const& table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    auto tablet_result = tablet_peer->shared_tablet_safe();
    if (!tablet_result.ok()) {
      LOG(WARNING) << tablet_result.status();
      continue;
    }
    auto tablet = *tablet_result;
    auto table_name = tablet->metadata()->table_name(table_id);
    // Ignore the DDL information of the parent table.
    if (tablet->metadata()->colocated() &&
        (boost::ends_with(table_name, kTablegroupParentTableNameSuffix) ||
         boost::ends_with(table_name, kColocationParentTableNameSuffix))) {
      continue;
    }
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();

    row_message->set_op(RowMessage_Op_COMMIT);
    row_message->set_table(table_name);
    row_message->set_commit_time(commit_timestamp);
    // No need to add record_time to the Commit record since it does not have any intent associated
    // with it.

    CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
    SetCDCSDKOpId(op_id.term, op_id.index, 0, "", cdc_sdk_op_id_pb);
  }
}

// Populate CDC record corresponding to WAL batch in ReplicateMsg.
Status PopulateCDCSDKWriteRecord(
    const ReplicateMsgPtr& msg,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    SchemaDetailsMap* cached_schema_details,
    GetChangesResponsePB* resp,
    client::YBClient* client) {
  if (FLAGS_cdc_populate_end_markers_transactions) {
    FillBeginRecordForSingleShardTransaction(tablet_peer, resp, msg->hybrid_time());
  }

  auto tablet_ptr = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  const auto& batch = msg->write().write_batch();
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;
  dockv::SubDocKey prev_decoded_key;
  std::unordered_set<std::string> modified_columns;
  std::unordered_set<std::string> null_value_columns;
  bool is_packed_row_record = false;
  // Write batch may contain records from different rows.
  // For CDC, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;

  uint32_t records_added = 0;

  bool colocated = tablet_ptr->metadata()->colocated();
  Schema schema = Schema();
  SchemaVersion schema_version = std::numeric_limits<uint32_t>::max();
  auto colocation_id = kColocationIdNotSet;
  if (!colocated) {
    const auto& schema_details = VERIFY_RESULT(GetOrPopulateRequiredSchemaDetails(
        tablet_peer, msg->hybrid_time(), cached_schema_details, client,
        tablet_ptr->metadata()->table_id(), resp));
    schema = *schema_details.schema;
    schema_version = schema_details.schema_version;
  }

  std::string table_name = tablet_ptr->metadata()->table_name();
  SchemaPackingStorage schema_packing_storage(tablet_ptr->table_type());
  schema_packing_storage.AddSchema(schema_version, schema);
  // TODO: This function and PopulateCDCSDKIntentRecord have a lot of code in common. They should
  // be refactored to use some common row-column iterator.
  for (auto it = batch.write_pairs().cbegin(); it != batch.write_pairs().cend(); ++it) {
    const yb::docdb::LWKeyValuePairPB& write_pair = *it;
    Slice key = write_pair.key();
    const auto key_size =
        VERIFY_RESULT(dockv::DocKey::EncodedSize(key, dockv::DocKeyPart::kWholeDocKey));

    Slice value_slice = write_pair.value();
    RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value_slice));
    auto value_type = dockv::DecodeValueEntryType(value_slice);
    value_slice.consume_byte();

    Slice sub_doc_key = key;
    dockv::SubDocKey decoded_key;
    RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, dockv::HybridTimeRequired::kFalse));

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice primary_key(key.data(), key_size);
    if (prev_key != primary_key || (!FLAGS_enable_single_record_update && row_message &&
                                    row_message->op() == RowMessage_Op_UPDATE)) {
      Slice sub_doc_key = key;
      dockv::SubDocKey decoded_key;

      // With tablet splits we will end up reading records from this tablet's ancestors -
      // only process records that are in this tablet's key range.
      const auto& key_bounds = tablet_ptr->key_bounds();
      if (!key_bounds.IsWithinBounds(key)) {
        VLOG(1) << "Key for the read record is not within tablet bounds, skipping the key: "
                << primary_key.data();
        continue;
      }

      RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, dockv::HybridTimeRequired::kFalse));
      if (colocated) {
        colocation_id = decoded_key.doc_key().colocation_id();
        auto table_info = CHECK_RESULT(tablet_ptr->metadata()->GetTableInfo(colocation_id));

        const auto& schema_details = VERIFY_RESULT(GetOrPopulateRequiredSchemaDetails(
            tablet_peer, msg->hybrid_time(), cached_schema_details, client, table_info->table_id,
            resp));
        schema = *schema_details.schema;
        schema_version = schema_details.schema_version;
        table_name = table_info->table_name;
        schema_packing_storage = SchemaPackingStorage(tablet_ptr->table_type());
        schema_packing_storage.AddSchema(schema_version, schema);
      }

      if (row_message != nullptr && row_message->op() == RowMessage_Op_UPDATE) {
        if (metadata.GetRecordType() != cdc::CDCRecordType::CHANGE) {
          VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                  << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                  << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                  << " for change record type: " << row_message->op();
          auto result = PopulateBeforeImage(
              tablet_peer, ReadHybridTime::FromUint64(msg->hybrid_time() - 1), row_message,
              enum_oid_label_map, composite_atts_map, prev_decoded_key, schema, schema_version,
              colocation_id, modified_columns, metadata.GetRecordType());
          if (!result.ok()) {
            LOG(ERROR) << "Failed to get the Beforeimage for tablet: " << tablet_peer->tablet_id()
                       << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                       << " for change record type: " << row_message->op()
                       << " row_message: " << row_message->DebugString()
                       << " with error status: " << result;
            return result;
          }
          VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
                  << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                  << " for change record type: " << row_message->op()
                  << " row_message: " << row_message->DebugString();
        } else {
          for (int new_tuple_index = 0; new_tuple_index < row_message->new_tuple_size();
               ++new_tuple_index) {
            row_message->add_old_tuple();
          }
        }
      }

      // Write pair contains record for different row. Create a new CDCRecord in this case.
      proto_record = resp->add_cdc_sdk_proto_records();
      ++records_added;
      row_message = proto_record->mutable_row_message();
      modified_columns.clear();
      null_value_columns.clear();
      row_message->set_pgschema_name(schema.SchemaName());
      row_message->set_table(table_name);
      CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
      SetCDCSDKOpId(msg->id().term(), msg->id().index(), 0, "", cdc_sdk_op_id_pb);
      is_packed_row_record = false;

      // Check whether operation is WRITE or DELETE.
      if (value_type == dockv::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        SetOperation(row_message, OpType::DELETE, schema);
      } else if (IsPackedRow(value_type)) {
        SetOperation(row_message, OpType::INSERT, schema);
        is_packed_row_record = true;
      } else {
        dockv::KeyEntryValue column_id;
        Slice key_column(key.WithoutPrefix(key_size));
        RETURN_NOT_OK(dockv::KeyEntryValue::DecodeKey(&key_column, &column_id));

        if (column_id.type() == dockv::KeyEntryType::kSystemColumnId &&
            value_type == dockv::ValueEntryType::kNullLow) {
          SetOperation(row_message, OpType::INSERT, schema);
        } else {
          SetOperation(row_message, OpType::UPDATE, schema);
        }
      }

      if (IsOldRowNeededOnDelete(metadata.GetRecordType()) &&
          (row_message->op() == RowMessage_Op_DELETE)) {
        VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                << " for change record type: " << row_message->op();
        auto result = PopulateBeforeImage(
            tablet_peer, ReadHybridTime::FromUint64(msg->hybrid_time() - 1), row_message,
            enum_oid_label_map, composite_atts_map, decoded_key, schema, schema_version,
            colocation_id, modified_columns, metadata.GetRecordType());
        if (!result.ok()) {
          LOG(ERROR) << "Failed to get the Beforeimage for tablet: " << tablet_peer->tablet_id()
                     << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                     << " for change record type: " << row_message->op()
                     << " row_message: " << row_message->DebugString()
                     << " with error status: " << result;
          return result;
        }
        VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
                << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                << " for change record type: " << row_message->op()
                << " row_message: " << row_message->DebugString();

        if (row_message->old_tuple_size() == 0) {
          RETURN_NOT_OK(AddPrimaryKey(
              tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
              metadata, &modified_columns, true));
        }
      } else {
        if (row_message->op() != RowMessage_Op_UPDATE) {
          RETURN_NOT_OK(AddPrimaryKey(
              tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
              metadata, &modified_columns, true));
        } else {
          RETURN_NOT_OK(AddPrimaryKey(
            tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
            metadata, &modified_columns, true));
        }
      }
      // Process intent records.
      row_message->set_commit_time(msg->hybrid_time());
      row_message->set_record_time(msg->hybrid_time());

      prev_decoded_key = decoded_key;
    }
    prev_key = primary_key;
    DCHECK(proto_record);

    if (IsInsertOrUpdate(*row_message)) {
      const yb::docdb::LWKeyValuePairPB& next_write_pair = *(std::next(it, 1));
      bool end_of_write_batch = (std::next(it, 1) == batch.write_pairs().cend());
      bool populate_new_record = VERIFY_RESULT(
          ShouldPopulateNewWriteRecord(end_of_write_batch, next_write_pair, primary_key));

      if (auto version = GetPackedRowVersion(value_type)) {
        RETURN_NOT_OK(PopulatePackedRows(
            *version, schema_packing_storage, schema, tablet_peer, enum_oid_label_map,
            composite_atts_map, &value_slice, row_message, &modified_columns,
            metadata.GetRecordType(), &null_value_columns));
      } else {
        dockv::KeyEntryValue column_id;
        Slice key_column = key.WithoutPrefix(key_size);
        RETURN_NOT_OK(dockv::KeyEntryValue::DecodeKey(&key_column, &column_id));
        if (column_id.type() == dockv::KeyEntryType::kColumnId) {
          const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_id.GetColumnId()));
          modified_columns.insert(col.name());
          dockv::Value decoded_value;
          RETURN_NOT_OK(decoded_value.Decode(write_pair.value()));

          auto col_name = null_value_columns.find(col.name());
          if (col_name != null_value_columns.end()) {
            null_value_columns.erase(col_name);
          }
          RETURN_NOT_OK(AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), enum_oid_label_map,
              composite_atts_map, row_message->add_new_tuple(), nullptr));
          if (row_message->op() == RowMessage_Op_INSERT) {
            row_message->add_old_tuple();
          }
        } else if (column_id.type() != dockv::KeyEntryType::kSystemColumnId) {
          LOG(DFATAL) << "Unexpected value type in key: " << column_id.type();
        }
      }
      if (row_message->op() == RowMessage_Op_INSERT && populate_new_record &&
          is_packed_row_record) {
        for (auto column_name : null_value_columns) {
          ColumnId column_id = VERIFY_RESULT(schema.ColumnIdByName(column_name));
          ColumnSchema col = VERIFY_RESULT(schema.column_by_id(column_id));
          RETURN_NOT_OK(AddColumnToMap(
              tablet_peer, col, PrimitiveValue(), enum_oid_label_map, composite_atts_map,
              row_message->add_new_tuple(), nullptr));
          row_message->add_old_tuple();
        }
      }
    }
  }

  if (row_message && row_message->op() == RowMessage_Op_UPDATE) {
    if (metadata.GetRecordType() != cdc::CDCRecordType::CHANGE) {
      VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
              << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
              << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
              << " for change record type: " << row_message->op();
      auto result = PopulateBeforeImage(
          tablet_peer, ReadHybridTime::FromUint64(msg->hybrid_time() - 1), row_message,
          enum_oid_label_map, composite_atts_map, prev_decoded_key, schema, schema_version,
          colocation_id, modified_columns, metadata.GetRecordType());
      if (!result.ok()) {
        LOG(ERROR) << "Failed to get the Beforeimage for tablet: " << tablet_peer->tablet_id()
                   << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                   << " for change record type: " << row_message->op()
                   << " row_message: " << row_message->DebugString()
                   << " with error status: " << result;
        return result;
      }
      VLOG(2) << "Successfully got the Beforeimage for tablet: " << tablet_peer->tablet_id()
              << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
              << " for change record type: " << row_message->op()
              << " row_message: " << row_message->DebugString();
    } else {
      for (int index = 0; index < row_message->new_tuple_size(); ++index) {
        row_message->add_old_tuple();
      }
    }
  }

  if (FLAGS_cdc_populate_end_markers_transactions) {
    // If there are no records added, we do not need to populate the begin-commit block
    // and we should return from here.
    if (records_added == 0 && !resp->mutable_cdc_sdk_proto_records()->empty()) {
      VLOG(2) << "Removing the added BEGIN record because there are no other records to add";
      // Only remove the BEGIN record if it happens to be the last added record in the response. If
      // its not the last record, skip removing it and instead add a commit record to the response.
      auto size = resp->cdc_sdk_proto_records_size();
      auto last_record = resp->cdc_sdk_proto_records().Get(size - 1);
      if (last_record.has_row_message() && last_record.row_message().op() == RowMessage::BEGIN) {
        resp->mutable_cdc_sdk_proto_records()->RemoveLast();
        return Status::OK();
      }
    }

    FillCommitRecordForSingleShardTransaction(
        OpId(msg->id().term(), msg->id().index()), tablet_peer, resp, msg->hybrid_time());
  }

  return Status::OK();
}

Status PopulateCDCSDKWriteRecordWithInvalidSchemaRetry(
    const ReplicateMsgPtr& msg,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    SchemaDetailsMap* cached_schema_details,
    GetChangesResponsePB* resp,
    client::YBClient* client) {
  const auto& records_size_before = resp->cdc_sdk_proto_records_size();

  auto status = PopulateCDCSDKWriteRecord(
      msg, metadata, tablet_peer, enum_oid_label_map, composite_atts_map, cached_schema_details,
      resp, client);

  if (!status.ok()) {
    VLOG_WITH_FUNC(1) << "Received error status: " << status.ToString()
                      << ", while processing WRITE_OP, with op_id: " << msg->id().ShortDebugString()
                      << ", on tablet: " << tablet_peer->tablet_id();
    // Remove partial remnants created while processing the write record
    while (resp->cdc_sdk_proto_records_size() > records_size_before) {
      resp->mutable_cdc_sdk_proto_records()->RemoveLast();
    }

    // Clear the scheam for all the colocated tables assocaited with the tablet
    auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
    for (auto const& cur_table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
      auto it = cached_schema_details->find(cur_table_id);
      if (it != cached_schema_details->end()) {
        (*cached_schema_details).erase(it);
      }
    }

    auto status = PopulateCDCSDKWriteRecord(
        msg, metadata, tablet_peer, enum_oid_label_map, composite_atts_map, cached_schema_details,
        resp, client);
  }

  return status;
}

Status PopulateCDCSDKDDLRecord(
    const ReplicateMsgPtr& msg, CDCSDKProtoRecordPB* proto_record, const string& table_name,
    const Schema& schema) {
  SCHECK(
      msg->has_change_metadata_request(), InvalidArgument,
      Format(
          "Change metadata (DDL) message requires metadata information: $0",
          msg->ShortDebugString()));

  RowMessage* row_message = nullptr;

  row_message = proto_record->mutable_row_message();
  row_message->set_op(RowMessage_Op_DDL);
  row_message->set_table(table_name);
  row_message->set_commit_time(msg->hybrid_time());

  CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
  SetCDCSDKOpId(msg->id().term(), msg->id().index(), 0, "", cdc_sdk_op_id_pb);

  for (const auto& column : msg->change_metadata_request().schema().columns()) {
    SetColumnInfo(column.ToGoogleProtobuf(), row_message->mutable_schema()->add_column_info());
  }

  CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb;
  const auto* table_properties = &msg->change_metadata_request().schema().table_properties();

  cdc_sdk_table_properties_pb = row_message->mutable_schema()->mutable_tab_info();
  row_message->set_schema_version(msg->change_metadata_request().schema_version());
  row_message->set_new_table_name(msg->change_metadata_request().new_table_name().ToBuffer());
  row_message->set_pgschema_name(schema.SchemaName());
  row_message->set_commit_time(msg->hybrid_time());
  SetTableProperties(table_properties->ToGoogleProtobuf(), cdc_sdk_table_properties_pb);

  return Status::OK();
}

Status PopulateCDCSDKTruncateRecord(
    const ReplicateMsgPtr& msg, CDCSDKProtoRecordPB* proto_record, const Schema& schema) {
  SCHECK(
      msg->has_truncate(), InvalidArgument,
      Format(
          "Truncate message requires truncate request information: $0", msg->ShortDebugString()));

  RowMessage* row_message = nullptr;

  row_message = proto_record->mutable_row_message();
  row_message->set_op(RowMessage_Op_TRUNCATE);
  row_message->set_pgschema_name(schema.SchemaName());

  CDCSDKOpIdPB* cdc_sdk_op_id_pb;

  cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
  SetCDCSDKOpId(msg->id().term(), msg->id().index(), 0, "", cdc_sdk_op_id_pb);

  return Status::OK();
}

void SetTermIndex(int64_t term, int64_t index, CDCSDKCheckpointPB* checkpoint) {
  checkpoint->set_term(term);
  checkpoint->set_index(index);
}

void SetKeyWriteId(string key, int32_t write_id, CDCSDKCheckpointPB* checkpoint) {
  checkpoint->set_key(key);
  checkpoint->set_write_id(write_id);
}

void FillBeginRecord(
    const TransactionId& transaction_id, const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    GetChangesResponsePB* resp, const uint64_t& commit_timestamp) {
  for (auto const& table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    auto tablet_result = tablet_peer->shared_tablet_safe();
    if (!tablet_result.ok()) {
      LOG(WARNING) << tablet_result.status();
      continue;
    }
    auto tablet = *tablet_result;
    auto table_name = tablet->metadata()->table_name(table_id);
    // Ignore the DDL information of the parent table.
    if (tablet->metadata()->colocated() &&
        (boost::ends_with(table_name, kTablegroupParentTableNameSuffix) ||
         boost::ends_with(table_name, kColocationParentTableNameSuffix))) {
      continue;
    }
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();
    row_message->set_op(RowMessage_Op_BEGIN);
    row_message->set_transaction_id(transaction_id.ToString());
    row_message->set_table(table_name);
    row_message->set_commit_time(commit_timestamp);
    // No need to add record_time to the Begin record since it does not have any intent associated
    // with it.
  }
}

void FillCommitRecord(
    const OpId& op_id, const TransactionId& transaction_id,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, CDCSDKCheckpointPB* checkpoint,
    GetChangesResponsePB* resp, const uint64_t& commit_timestamp) {
  for (auto const& table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    auto tablet_result = tablet_peer->shared_tablet_safe();
    if (!tablet_result.ok()) {
      LOG(WARNING) << tablet_result.status();
      continue;
    }
    auto tablet = *tablet_result;
    auto table_name = tablet->metadata()->table_name(table_id);
    // Ignore the DDL information of the parent table.
    if (tablet->metadata()->colocated() &&
        (boost::ends_with(table_name, kTablegroupParentTableNameSuffix) ||
         boost::ends_with(table_name, kColocationParentTableNameSuffix))) {
      continue;
    }
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();

    row_message->set_op(RowMessage_Op_COMMIT);
    row_message->set_transaction_id(transaction_id.ToString());
    row_message->set_table(table_name);
    row_message->set_commit_time(commit_timestamp);
    // No need to add record_time to the Commit record since it does not have any intent associated
    // with it.

    CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
    SetCDCSDKOpId(op_id.term, op_id.index, 0, "", cdc_sdk_op_id_pb);
    SetKeyWriteId("", 0, checkpoint);
  }
}

Status ProcessIntents(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const StreamMetadata& metadata,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    CDCSDKCheckpointPB* checkpoint,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    std::vector<docdb::IntentKeyValueForCDC>* keyValueIntents,
    docdb::ApplyTransactionState* stream_state,
    client::YBClient* client,
    SchemaDetailsMap* cached_schema_details,
    const uint64_t& commit_time) {
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  if (stream_state->key.empty() && stream_state->write_id == 0 &&
      FLAGS_cdc_populate_end_markers_transactions) {
    FillBeginRecord(transaction_id, tablet_peer, resp, commit_time);
  }

  RETURN_NOT_OK(tablet->GetIntents(transaction_id, keyValueIntents, stream_state));
  VLOG(1) << "The size of intentKeyValues for transaction id: " << transaction_id
          << ", with apply record op_id : " << op_id << ", is: " << (*keyValueIntents).size();

  const OpId& checkpoint_op_id = tablet_peer->GetLatestCheckPoint();
  if ((*keyValueIntents).size() == 0 && op_id <= checkpoint_op_id) {
    LOG(ERROR) << "CDCSDK is trying to get intents for a transaction: " << transaction_id
               << ", whose Apply record's OpId " << op_id
               << "is lesser than the checkpoint in the tablet peer: " << checkpoint_op_id
               << ", on tablet: " << tablet_peer->tablet_id()
               << ". The intents would have already been removed from IntentsDB.";
    return STATUS_FORMAT(
        InternalError, "CDCSDK Trying to fetch already GCed intents for transaction $0",
        transaction_id);
  }

  for (auto& keyValue : *keyValueIntents) {
    dockv::SubDocKey sub_doc_key;
    CHECK_OK(
        sub_doc_key.FullyDecodeFrom(Slice(keyValue.key_buf), dockv::HybridTimeRequired::kFalse));
    Slice value_slice = keyValue.value_buf;
    RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value_slice));
    auto value_type = dockv::DecodeValueEntryType(value_slice);
    if (!IsPackedRow(value_type)) {
      dockv::Value decoded_value;
      RETURN_NOT_OK(decoded_value.Decode(Slice(keyValue.value_buf)));
    }
  }

  std::string reverse_index_key;
  IntraTxnWriteId write_id = 0;

  bool end_of_transaction = (stream_state->key.empty()) && (stream_state->write_id == 0);

  // Need to populate the CDCSDKRecords
  if (!keyValueIntents->empty()) {
    RETURN_NOT_OK(PopulateCDCSDKIntentRecord(
        op_id, transaction_id, *keyValueIntents, metadata, tablet_peer, enum_oid_label_map,
        composite_atts_map, cached_schema_details, resp, consumption, &write_id, &reverse_index_key,
        commit_time, client, end_of_transaction));
  }

  if (end_of_transaction) {
    if (FLAGS_cdc_populate_end_markers_transactions) {
      FillCommitRecord(op_id, transaction_id, tablet_peer, checkpoint, resp, commit_time);
    }
  } else {
    SetKeyWriteId(reverse_index_key, write_id, checkpoint);
  }

  return Status::OK();
}

Status PrcoessIntentsWithInvalidSchemaRetry(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const StreamMetadata& metadata,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    CDCSDKCheckpointPB* checkpoint,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    std::vector<docdb::IntentKeyValueForCDC>* keyValueIntents,
    docdb::ApplyTransactionState* stream_state,
    client::YBClient* client,
    SchemaDetailsMap* cached_schema_details,
    const uint64_t& commit_time) {
  const auto& records_size_before = resp->cdc_sdk_proto_records_size();

  auto status = ProcessIntents(
      op_id, transaction_id, metadata, enum_oid_label_map, composite_atts_map, resp, consumption,
      checkpoint, tablet_peer, keyValueIntents, stream_state, client, cached_schema_details,
      commit_time);

  if (!status.ok()) {
    VLOG_WITH_FUNC(1) << "Received error status: " << status.ToString()
                      << ", while processing intents for transaction: " << transaction_id
                      << ", with APPLY op_id: " << op_id
                      << ", on tablet: " << tablet_peer->tablet_id();
    // Remove partial remnants created while processing intents
    while (resp->cdc_sdk_proto_records_size() > records_size_before) {
      resp->mutable_cdc_sdk_proto_records()->RemoveLast();
    }

    // Clear the scheam for all the colocated tables assocaited with the tablet
    auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
    for (auto const& cur_table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
      auto it = cached_schema_details->find(cur_table_id);
      if (it != cached_schema_details->end()) {
        (*cached_schema_details).erase(it);
      }
    }

    status = ProcessIntents(
        op_id, transaction_id, metadata, enum_oid_label_map, composite_atts_map, resp, consumption,
        checkpoint, tablet_peer, keyValueIntents, stream_state, client, cached_schema_details,
        commit_time);
  }

  return status;
}

Status PopulateCDCSDKSnapshotRecord(
    GetChangesResponsePB* resp,
    const qlexpr::QLTableRow* row,
    const Schema& schema,
    const TableName& table_name,
    ReadHybridTime time,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    const CDCSDKCheckpointPB& snapshot_op_id,
    const std::string& snapshot_record_key,
    bool is_ysql_table) {
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;

  proto_record = resp->add_cdc_sdk_proto_records();
  row_message = proto_record->mutable_row_message();
  row_message->set_table(table_name);
  row_message->set_op(RowMessage_Op_READ);
  row_message->set_pgschema_name(schema.SchemaName());
  row_message->set_commit_time(time.read.ToUint64());
  row_message->set_record_time(time.read.ToUint64());

  proto_record->mutable_cdc_sdk_op_id()->set_term(snapshot_op_id.term());
  proto_record->mutable_cdc_sdk_op_id()->set_index(snapshot_op_id.index());
  proto_record->mutable_cdc_sdk_op_id()->set_write_id_key(snapshot_record_key);

  DatumMessagePB* cdc_datum_message = nullptr;

  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    ColumnId col_id = schema.column_id(col_idx);
    const auto* value = row->GetColumn(col_id);
    const ColumnSchema& col_schema = VERIFY_RESULT(schema.column_by_id(col_id));

    cdc_datum_message = row_message->add_new_tuple();
    cdc_datum_message->set_column_name(col_schema.name());

    if (value && value->value_case() != QLValuePB::VALUE_NOT_SET) {
      if (is_ysql_table) {
        if (col_schema.pg_type_oid() != 0 /*kInvalidOid*/) {
          RETURN_NOT_OK(docdb::SetValueFromQLBinaryWrapper(
              *value, col_schema.pg_type_oid(), enum_oid_label_map, composite_atts_map,
              cdc_datum_message));
        } else {
          cdc_datum_message->set_column_type(col_schema.pg_type_oid());
          cdc_datum_message->set_pg_type(col_schema.pg_type_oid());
        }
      } else {
        cdc_datum_message->mutable_cql_value()->CopyFrom(*value);
        col_schema.type()->ToQLTypePB(cdc_datum_message->mutable_cql_type());
      }
    } else {
      if (is_ysql_table) {
        cdc_datum_message->set_column_type(col_schema.pg_type_oid());
        cdc_datum_message->set_pg_type(col_schema.pg_type_oid());
      } else {
        col_schema.type()->ToQLTypePB(cdc_datum_message->mutable_cql_type());
      }
    }

    row_message->add_old_tuple();
  }

  return Status::OK();
}

Status PopulateCDCSDKSafepointOpRecord(
    const uint64_t timestamp, const string& table_name, CDCSDKProtoRecordPB* proto_record,
    const Schema& schema) {
  RowMessage* row_message = nullptr;

  row_message = proto_record->mutable_row_message();
  row_message->set_op(RowMessage_Op_SAFEPOINT);
  row_message->set_pgschema_name(schema.SchemaName());
  row_message->set_commit_time(timestamp);
  row_message->set_table(table_name);

  return Status::OK();
}

bool VerifyTabletSplitOnParentTablet(
    const TableId& table_id, const TabletId& tablet_id, client::YBClient* client) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  client::YBTableName table_name;
  table_name.set_table_id(table_id);
  RETURN_NOT_OK_RET(
      client->GetTablets(
          table_name, 0, &tablets, /* partition_list_version =*/nullptr,
          RequireTabletsRunning::kFalse, master::IncludeInactive::kTrue),
      false);

  uint children_tablet_count = 0;
  for (const auto& tablet : tablets) {
    if (tablet.has_split_parent_tablet_id() && tablet.split_parent_tablet_id() == tablet_id) {
      children_tablet_count += 1;
    }
  }

  return (children_tablet_count == 2);
}
}  // namespace

bool IsWriteOp(const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg) {
  return msg->op_type() == consensus::OperationType::WRITE_OP;
}

bool IsIntent(const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg) {
  return IsWriteOp(msg) && msg->write().write_batch().has_transaction();
}

bool IsUpdateTransactionOp(const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg) {
  return msg->op_type() == consensus::OperationType::UPDATE_TRANSACTION_OP;
}

// Returns the transaction commit time in case of a multi shard transaction, else returns the
// message hybrid time.
uint64_t GetTransactionCommitTime(const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg) {
  return IsUpdateTransactionOp(msg) ? msg->transaction_state().commit_hybrid_time()
                                    : msg->hybrid_time();
}

void SortConsistentWALRecords(
    std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>* consistent_wal_records) {
  std::sort(
      (*consistent_wal_records).begin(), (*consistent_wal_records).end(),
      [](const std::shared_ptr<yb::consensus::LWReplicateMsg>& lhs,
         const std::shared_ptr<yb::consensus::LWReplicateMsg>& rhs) -> bool {
        const auto& lhs_commit_time = GetTransactionCommitTime(lhs);
        const auto& rhs_commit_time = GetTransactionCommitTime(rhs);
        return lhs_commit_time == rhs_commit_time ? lhs->id().index() < rhs->id().index()
                                                  : lhs_commit_time < rhs_commit_time;
      });
}

Status GetConsistentWALRecords(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const MemTrackerPtr& mem_tracker,
    consensus::ReplicateMsgsHolder* msgs_holder, ScopedTrackedConsumption* consumption,
    const uint64_t& consistent_safe_time, const OpId& historical_max_op_id,
    bool* wait_for_wal_update, OpId* last_seen_op_id, int64_t** last_readable_opid_index,
    const int64_t& safe_hybrid_time_req, const CoarseTimePoint& deadline,
    std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>* consistent_wal_records,
    std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>* all_checkpoints) {
  VLOG(2) << "Getting consistent WAL records. safe_hybrid_time_req: " << safe_hybrid_time_req
          << ", consistent_safe_time: " << consistent_safe_time
          << ", last_seen_op_id: " << last_seen_op_id->ToString()
          << ", historical_max_op_id: " << historical_max_op_id;
  auto consensus = VERIFY_RESULT(tablet_peer->GetConsensus());
  // Read the committed WAL messages with hybrid time <= consistent_stream_safe_time. If there exist
  // messages in the WAL which are replicated but not yet committed,
  // ReadReplicatedMessagesForConsistentCDC waits for them to get committed and eventually includes
  // them in the result.
  auto read_ops = VERIFY_RESULT(consensus->ReadReplicatedMessagesForConsistentCDC(
      *last_seen_op_id, consistent_safe_time, deadline, false, *last_readable_opid_index));

  if (read_ops.read_from_disk_size && mem_tracker) {
    (*consumption) = ScopedTrackedConsumption(mem_tracker, read_ops.read_from_disk_size);
  }

  for (const auto& msg : read_ops.messages) {
    last_seen_op_id->term = msg->id().term();
    last_seen_op_id->index = msg->id().index();

    if (IsIntent(msg) || (IsUpdateTransactionOp(msg) &&
                          msg->transaction_state().status() != TransactionStatus::APPLYING)) {
      continue;
    }

    if (VLOG_IS_ON(3) && IsUpdateTransactionOp(msg) &&
        msg->transaction_state().status() == TransactionStatus::APPLYING) {
      auto txn_id =
          VERIFY_RESULT(FullyDecodeTransactionId(msg->transaction_state().transaction_id()));
      VLOG(3) << "Read transaction in WAL on "
              << "tablet_id: " << tablet_peer->tablet_id() << ", transaction_id: " << txn_id
              << ", OpId: " << msg->id().term() << "." << msg->id().index()
              << ", commit_time: " << GetTransactionCommitTime(msg)
              << ", consistent safe_time: " << consistent_safe_time
              << ", safe_hybrid_time_req: " << safe_hybrid_time_req;
    } else if (VLOG_IS_ON(3)) {
      VLOG(3) << "Read WAL msg on "
              << "tablet_id: " << tablet_peer->tablet_id() << ", op_type: " << msg->op_type()
              << ", OpId: " << msg->id().term() << "." << msg->id().index()
              << ", commit_time: " << GetTransactionCommitTime(msg)
              << ", consistent safe_time: " << consistent_safe_time
              << ", safe_hybrid_time_req: " << safe_hybrid_time_req;
    }

    all_checkpoints->push_back(msg);
    consistent_wal_records->push_back(msg);
  }

  if (read_ops.messages.size() > 0) {
    *msgs_holder = consensus::ReplicateMsgsHolder(
        nullptr, std::move(read_ops.messages), std::move((*consumption)));
  }

  // Handle the case where WAL doesn't have the apply record for all the committed transactions.
  if (historical_max_op_id.valid() && historical_max_op_id > *last_seen_op_id) {
    (*wait_for_wal_update) = true;
  }

  if (consistent_wal_records->empty() && read_ops.have_more_messages) {
    VLOG(1) << "Received empty read_ops with have_more_messages set to true, indicating presence "
               "of replicated but not committed records in the WAL";
    *wait_for_wal_update = true;
  }

  SortConsistentWALRecords(consistent_wal_records);
  VLOG_WITH_FUNC(1) << "Got a total of " << consistent_wal_records->size() << " WAL records "
                    << "in the current segment";
  return Status::OK();
}

Status GetWALRecords(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const MemTrackerPtr& mem_tracker,
    consensus::ReplicateMsgsHolder* msgs_holder, ScopedTrackedConsumption* consumption,
    const uint64_t& consistent_safe_time, OpId* last_seen_op_id, int64_t** last_readable_opid_index,
    const int64_t& safe_hybrid_time, const CoarseTimePoint& deadline, bool skip_intents,
    std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>* wal_records,
    std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>* all_checkpoints) {
  auto consensus = VERIFY_RESULT(tablet_peer->GetConsensus());
  auto read_ops = VERIFY_RESULT(consensus->ReadReplicatedMessagesForCDC(
      *last_seen_op_id, *last_readable_opid_index, deadline));

  if (read_ops.messages.empty()) {
    VLOG_WITH_FUNC(1) << "Did not get any messages with current batch of 'read_ops'."
                      << "last_seen_op_id: " << last_seen_op_id << ", last_readable_opid_index "
                      << *last_readable_opid_index;
    return Status::OK();
  }

  if (read_ops.read_from_disk_size && mem_tracker) {
    (*consumption) = ScopedTrackedConsumption(mem_tracker, read_ops.read_from_disk_size);
  }

  for (const auto& msg : read_ops.messages) {
    last_seen_op_id->term = msg->id().term();
    last_seen_op_id->index = msg->id().index();

    bool is_intent_or_invalid_transaction_op =
        IsIntent(msg) || (IsUpdateTransactionOp(msg) &&
                          msg->transaction_state().status() != TransactionStatus::APPLYING);

    if (skip_intents && is_intent_or_invalid_transaction_op) {
      continue;
    }

    if (!is_intent_or_invalid_transaction_op) {
      all_checkpoints->push_back(msg);
    }

    wal_records->push_back(msg);
  }

  if (read_ops.messages.size() > 0) {
    *msgs_holder = consensus::ReplicateMsgsHolder(
        nullptr, std::move(read_ops.messages), std::move((*consumption)));
  }

  return Status::OK();
}

// Basic sanity checks on the wal_segment_index recieved from the request.
int GetWalSegmentIndex(const int& wal_segment_index_req) {
  if (!FLAGS_cdc_enable_consistent_records) return 0;
  return wal_segment_index_req >= 0 ? wal_segment_index_req : 0;
}

// Returns 'true' if we should update the response safe time to the record's commit time.
uint64_t ShouldUpdateSafeTime(
    const std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>& wal_records,
    const size_t& current_index) {
  const auto& msg = wal_records[current_index];

  if (IsUpdateTransactionOp(msg)) {
    const auto& txn_id = msg->transaction_state().transaction_id();
    const auto& commit_time = GetTransactionCommitTime(msg);

    size_t index = current_index + 1;
    while ((index < wal_records.size()) &&
           (GetTransactionCommitTime(wal_records[index]) == commit_time)) {
      // Return false if we find single shard txn, or multi-shard txn with different txn_id.
      if (!IsUpdateTransactionOp(wal_records[index]) ||
          wal_records[index]->transaction_state().transaction_id() != txn_id) {
        return false;
      }
      index++;
    }
  } else {
    if (wal_records.size() > (current_index + 1)) {
      return GetTransactionCommitTime(wal_records[current_index + 1]) !=
             GetTransactionCommitTime(wal_records[current_index]);
    }
  }

  return true;
}

// Returns 'true' if we know for sure that the split corresponding to the 'split_op_index'
// had failed.
bool HasSplitFailed(
    const std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>& wal_records,
    const size_t& split_op_index) {
  // If there is a wal record that can't exist after a successful split we know
  // that the split_op corresponds to an unsuccesful split attempt.
  for (size_t index = split_op_index + 1; index < wal_records.size(); ++index) {
    const auto& msg = wal_records[index];
    if (msg->op_type() == consensus::OperationType::UPDATE_TRANSACTION_OP ||
        msg->op_type() == consensus::OperationType::WRITE_OP ||
        msg->op_type() == consensus::OperationType::CHANGE_METADATA_OP ||
        msg->op_type() == consensus::OperationType::TRUNCATE_OP ||
        msg->op_type() == consensus::OperationType::SPLIT_OP) {
      return true;
    }
  }

  return false;
}

// Checks if based on the order of records and wal msg that we streamed, it is possible to increment
// the checkpoint op id. It also updates the wal_segment_index accordingly.
bool CanUpdateCheckpointOpId(
    const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg, size_t* next_checkpoint_index,
    const std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>& all_checkpoints,
    int* wal_segment_index) {
  bool update_checkpoint = false;

  if (!FLAGS_cdc_enable_consistent_records) {
    --(*wal_segment_index);
    ++(*next_checkpoint_index);
    return true;
  }

  while ((*next_checkpoint_index) < all_checkpoints.size() &&
         ((GetTransactionCommitTime(all_checkpoints[*next_checkpoint_index]) <
           GetTransactionCommitTime(msg)) ||
          (all_checkpoints[*next_checkpoint_index]->id().index() == msg->id().index()))) {
    --(*wal_segment_index);
    ++(*next_checkpoint_index);
    update_checkpoint = true;
  }
  return update_checkpoint;
}

Result<uint64_t> GetConsistentStreamSafeTime(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const tablet::TabletPtr& tablet_ptr,
    const HybridTime& leader_safe_time, const int64_t& safe_hybrid_time_req,
    const CoarseTimePoint& deadline) {
  HybridTime consistent_stream_safe_time = HybridTime::kInvalid;
  if (tablet_ptr->transaction_participant()) {
    consistent_stream_safe_time =
        tablet_ptr->transaction_participant()->GetMinStartTimeAmongAllRunningTransactions();
  }
  consistent_stream_safe_time = consistent_stream_safe_time == HybridTime::kInvalid
                                    ? leader_safe_time
                                    : consistent_stream_safe_time;

  VLOG_WITH_FUNC(3) << "Getting consistent_stream_safe_time. consistent_stream_safe_time: "
                    << consistent_stream_safe_time.ToUint64()
                    << ", safe_hybrid_time_req: " << safe_hybrid_time_req
                    << ", leader_safe_time: " << leader_safe_time.ToUint64()
                    << ", tablet_id: " << tablet_peer->tablet_id();

  if (!consistent_stream_safe_time.is_valid()) {
    VLOG_WITH_FUNC(3) << "We'll use the leader_safe_time as the consistent_stream_safe_time, since "
                         "GetMinStartTimeAmongAllRunningTransactions returned an invalid "
                         "value";
    return leader_safe_time.ToUint64();
  } else if (
      (safe_hybrid_time_req > 0 &&
       consistent_stream_safe_time.ToUint64() < (uint64_t)safe_hybrid_time_req) ||
      (int64_t)leader_safe_time.GetPhysicalValueMillis() -
              (int64_t)consistent_stream_safe_time.GetPhysicalValueMillis() >
          FLAGS_cdc_resolve_intent_lag_threshold_ms) {
    VLOG_WITH_FUNC(3)
        << "Calling 'ResolveIntents' since the lag between consistent_stream_safe_time: "
        << consistent_stream_safe_time << ", and leader_safe_time: " << leader_safe_time
        << ", is greater than: FLAGS_cdc_resolve_intent_lag_threshold_ms: "
        << FLAGS_cdc_resolve_intent_lag_threshold_ms;

    RETURN_NOT_OK(
        tablet_ptr->transaction_participant()->ResolveIntents(leader_safe_time, deadline));

    return leader_safe_time.ToUint64();
  }

  return safe_hybrid_time_req > 0
             // It is possible for us to receive a transaction with begin time lower than
             // a previously fetched leader_safe_time. So, we need a max of safe time from
             // request and consistent_stream_safe_time here.
             ? std::max(consistent_stream_safe_time.ToUint64(), (uint64_t)safe_hybrid_time_req)
             : consistent_stream_safe_time.ToUint64();
}

void SetSafetimeFromRequestIfInvalid(
    const int64_t& safe_hybrid_time_req, HybridTime* safe_hybrid_time_resp) {
  if (!safe_hybrid_time_resp->is_valid()) {
    *safe_hybrid_time_resp = HybridTime((safe_hybrid_time_req > 0) ? safe_hybrid_time_req : 0);
  }
}

void UpdateSafetimeForResponse(
    const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg, const bool& update_safe_time,
    const int64_t& safe_hybrid_time_req, HybridTime* safe_hybrid_time_resp) {
  if (!FLAGS_cdc_enable_consistent_records) {
    *safe_hybrid_time_resp = HybridTime(GetTransactionCommitTime(msg));
    return;
  }

  if (update_safe_time) {
    const auto& commit_time = GetTransactionCommitTime(msg);
    if ((int64_t)commit_time >= safe_hybrid_time_req &&
        (!safe_hybrid_time_resp->is_valid() || safe_hybrid_time_resp->ToUint64() < commit_time)) {
      *safe_hybrid_time_resp = HybridTime(GetTransactionCommitTime(msg));
      return;
    }
  }

  SetSafetimeFromRequestIfInvalid(safe_hybrid_time_req, safe_hybrid_time_resp);
}

// Update the response safetime, wal_segmemt_index and checkpoint op id based on the WAL msg
// that was streamed.
void AcknowledgeStreamedMsg(
    const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg, const bool& update_safe_time,
    const int64_t& safe_hybrid_time_req, size_t* next_checkpoint_index,
    const std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>& all_checkpoints,
    CDCSDKCheckpointPB* checkpoint, OpId* last_streamed_op_id, HybridTime* safe_hybrid_time_resp,
    int* wal_segment_index) {
  UpdateSafetimeForResponse(msg, update_safe_time, safe_hybrid_time_req, safe_hybrid_time_resp);
  ++(*wal_segment_index);
  if (CanUpdateCheckpointOpId(msg, next_checkpoint_index, all_checkpoints, wal_segment_index)) {
    auto msg = all_checkpoints[(*next_checkpoint_index) - 1];
    SetCheckpoint(msg->id().term(), msg->id().index(), 0, "", 0, checkpoint, last_streamed_op_id);
  }
}

// Update the response safetime, wal_segmemt_index and checkpoint op id considering we streamed
// a multi-shard transaction.
void AcknowledgeStreamedMultiShardTxn(
    const std::shared_ptr<yb::consensus::LWReplicateMsg>& msg, const bool& update_safe_time,
    const int64_t& safe_hybrid_time_req, size_t* next_checkpoint_index,
    const std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>>& all_checkpoints,
    CDCSDKCheckpointPB* checkpoint, OpId* last_streamed_op_id, HybridTime* safe_hybrid_time_resp,
    int* wal_segment_index) {
  UpdateSafetimeForResponse(msg, update_safe_time, safe_hybrid_time_req, safe_hybrid_time_resp);
  ++(*wal_segment_index);
  if (CanUpdateCheckpointOpId(msg, next_checkpoint_index, all_checkpoints, wal_segment_index)) {
    const auto& msg = all_checkpoints[(*next_checkpoint_index) - 1];
    const int64_t& term = msg->id().term();
    const int64_t& index = msg->id().index();
    SetTermIndex(term, index, checkpoint);
    last_streamed_op_id->term = term;
    last_streamed_op_id->index = index;
  }
}

Status HandleGetChangesForSnapshotRequest(
    const xrepl::StreamId& stream_id, const TabletId& tablet_id,
    const CDCSDKCheckpointPB& from_op_id, const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map, const CompositeAttsMap& composite_atts_map,
    client::YBClient* client, GetChangesResponsePB* resp, SchemaDetailsMap* cached_schema_details,
    const TableId& colocated_table_id, const tablet::TabletPtr& tablet_ptr, string* table_name,
    CDCSDKCheckpointPB* checkpoint, bool* checkpoint_updated, HybridTime* safe_hybrid_time_resp,
    CoarseTimePoint deadline) {

  ReadHybridTime time;

  // It is first call in snapshot then take snapshot.
  if ((from_op_id.key().empty()) && (from_op_id.snapshot_time() == 0)) {
    tablet::RemoveIntentsData data;
    RETURN_NOT_OK(tablet_peer->GetLastReplicatedData(&data));

    // Set the checkpoint and communicate to the follower.
    VLOG(1) << "The first snapshot term " << data.op_id.term << "index  " << data.op_id.index
            << "time " << data.log_ht.ToUint64();
    // Update the CDCConsumerOpId.
    VERIFY_RESULT(tablet_peer->GetConsensus())->UpdateCDCConsumerOpId(data.op_id);

    LOG(INFO) << "CDC snapshot initialization is started, by setting checkpoint as: " << data.op_id
              << ", for tablet_id: " << tablet_id << " stream_id: " << stream_id;
    RETURN_NOT_OK(tablet_peer->SetAllInitialCDCSDKRetentionBarriers(
        data.op_id, data.log_ht, true /* require_history_cutoff */));

    RETURN_NOT_OK(tablet_peer->GetLastReplicatedData(&data));
    time = ReadHybridTime::SingleTime(data.log_ht);
    // Use the last replicated hybrid time as a safe time for snapshot operation. so that
    // compaction can be restricted during snapshot operation.

    if (time.read.ToUint64() == 0) {
      // This means there is no data from the sansphot.
      SetCheckpoint(data.op_id.term, data.op_id.index, 0, "", 0, checkpoint, nullptr);
    } else {
      *safe_hybrid_time_resp = data.log_ht;
      // This should go to cdc_state table.
      // Below condition update the checkpoint in cdc_state table.
      SetCheckpoint(
          data.op_id.term, data.op_id.index, -1, "", time.read.ToUint64(), checkpoint, nullptr);
    }

    *checkpoint_updated = true;
  } else {
    // Snapshot is already taken.
    HybridTime ht;
    time = ReadHybridTime::FromUint64(from_op_id.snapshot_time());
    *safe_hybrid_time_resp = HybridTime(from_op_id.snapshot_time());
    const auto& next_key = from_op_id.key();
    VLOG(1) << "The after snapshot term " << from_op_id.term() << "index  " << from_op_id.index()
            << "key " << from_op_id.key() << "snapshot time " << from_op_id.snapshot_time();

    // This is for test purposes only, to create a snapshot failure scenario from the server.
    if (PREDICT_FALSE(FLAGS_TEST_cdc_snapshot_failure)) {
      return STATUS_FORMAT(ServiceUnavailable, "CDC snapshot is failed for tablet: $0 ", tablet_id);
    }

    const auto& schema_details = VERIFY_RESULT(GetOrPopulateRequiredSchemaDetails(
        tablet_peer, std::numeric_limits<uint64_t>::max(), cached_schema_details, client,
        colocated_table_id.empty() ? tablet_ptr->metadata()->table_id() : colocated_table_id,
        resp));

    if (!colocated_table_id.empty()) {
      *table_name = VERIFY_RESULT(GetColocatedTableName(tablet_peer, colocated_table_id));
    }

    int limit = FLAGS_cdc_snapshot_batch_size;
    int fetched = 0;
    std::vector<qlexpr::QLTableRow> rows;
    qlexpr::QLTableRow row;
    dockv::ReaderProjection projection(*schema_details.schema);

    // A consistent view of data across tablets is required. The consistent snapshot time
    // has been picked by Master. Thus, there is a need to wait for that timestamp to become
    // safe to read at on this tablet.
    RETURN_NOT_OK(tablet_ptr->SafeTime(tablet::RequireLease::kTrue, time.read, deadline));
    auto iter = VERIFY_RESULT(
        tablet_ptr->CreateCDCSnapshotIterator(projection, time, next_key, colocated_table_id));
    while (fetched < limit && VERIFY_RESULT(iter->FetchNext(&row))) {
      RETURN_NOT_OK(PopulateCDCSDKSnapshotRecord(
          resp, &row, *schema_details.schema, *table_name, time, enum_oid_label_map,
          composite_atts_map, from_op_id, next_key, tablet_ptr->table_type() == PGSQL_TABLE_TYPE));
      fetched++;
    }
    dockv::SubDocKey sub_doc_key;
    RETURN_NOT_OK(iter->GetNextReadSubDocKey(&sub_doc_key));

    // Snapshot ends when next key is empty.
    if (sub_doc_key.doc_key().empty()) {
      VLOG(1) << "Setting next sub doc key empty ";
      LOG(INFO) << "Done with snapshot operation for tablet_id: " << tablet_id
                << " stream_id: " << stream_id << ", from_op_id: " << from_op_id.DebugString();
      // Get the checkpoint or read the checkpoint from the table/cache.
      SetCheckpoint(from_op_id.term(), from_op_id.index(), 0, "", 0, checkpoint, nullptr);
      *checkpoint_updated = true;
    } else {
      VLOG(1) << "Setting next sub doc key is " << sub_doc_key.Encode().ToStringBuffer();

      checkpoint->set_write_id(-1);
      SetCheckpoint(
          from_op_id.term(), from_op_id.index(), -1, sub_doc_key.Encode().ToStringBuffer(),
          time.read.ToUint64(), checkpoint, nullptr);
      *checkpoint_updated = true;
    }
  }

  return Status::OK();
}

// CDC get changes is different from xCluster as it doesn't need
// to read intents from WAL.

Status GetChangesForCDCSDK(
    const xrepl::StreamId& stream_id,
    const TabletId& tablet_id,
    const CDCSDKCheckpointPB& from_op_id,
    const StreamMetadata& stream_metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const MemTrackerPtr& mem_tracker,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map,
    client::YBClient* client,
    consensus::ReplicateMsgsHolder* msgs_holder,
    GetChangesResponsePB* resp,
    uint64_t* commit_timestamp,
    SchemaDetailsMap* cached_schema_details,
    OpId* last_streamed_op_id,
    const int64_t& safe_hybrid_time_req,
    const std::optional<uint64_t> consistent_snapshot_time,
    const int& wal_segment_index_req,
    int64_t* last_readable_opid_index,
    const TableId& colocated_table_id,
    const CoarseTimePoint deadline) {
  // Delete the memory context if it was created for decoding the QLValuePB.
  auto scope_exit = ScopeExit([&] { docdb::DeleteMemoryContextForCDCWrapper(); });

  OpId op_id{from_op_id.term(), from_op_id.index()};
  VLOG(1) << "GetChanges request has from_op_id: " << from_op_id.DebugString()
          << ", safe_hybrid_time: " << safe_hybrid_time_req
          << ", wal_segment_index: " << wal_segment_index_req << " for tablet_id: " << tablet_id;
  ScopedTrackedConsumption consumption;
  CDCSDKCheckpointPB checkpoint;
  // 'checkpoint_updated' decides if the response checkpoint should be copied from
  // previously declared 'checkpoint' or the 'from_op_id'.
  bool checkpoint_updated = false;
  bool report_tablet_split = false;
  bool snapshot_operation = false;
  bool pending_intents = false;
  int wal_segment_index = GetWalSegmentIndex(wal_segment_index_req);
  bool wait_for_wal_update = false;

  auto tablet_ptr = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  auto leader_safe_time = tablet_ptr->SafeTime();
  if (!leader_safe_time.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << "Could not compute safe time: " << leader_safe_time.status();
    leader_safe_time = HybridTime::kInvalid;
  }
  uint64_t consistent_stream_safe_time = VERIFY_RESULT(GetConsistentStreamSafeTime(
      tablet_peer, tablet_ptr, leader_safe_time.get(), safe_hybrid_time_req, deadline));
  OpId historical_max_op_id = tablet_ptr->transaction_participant()
                                  ? tablet_ptr->transaction_participant()->GetHistoricalMaxOpId()
                                  : OpId::Invalid();
  auto table_name = tablet_ptr->metadata()->table_name();

  auto safe_hybrid_time_resp = HybridTime::kInvalid;
  HaveMoreMessages have_more_messages(false);
  // It is snapshot call.
  if (from_op_id.write_id() == -1) {
    snapshot_operation = true;
    RETURN_NOT_OK(HandleGetChangesForSnapshotRequest(
        stream_id, tablet_id, from_op_id, tablet_peer, enum_oid_label_map, composite_atts_map,
        client, resp, cached_schema_details, colocated_table_id, tablet_ptr, &table_name,
        &checkpoint, &checkpoint_updated, &safe_hybrid_time_resp, deadline));
  } else if (!from_op_id.key().empty() && from_op_id.write_id() != 0) {
    std::string reverse_index_key = from_op_id.key();
    Slice reverse_index_key_slice(reverse_index_key);
    std::vector<docdb::IntentKeyValueForCDC> keyValueIntents;
    docdb::ApplyTransactionState stream_state;
    stream_state.key = from_op_id.key();
    stream_state.write_id = from_op_id.write_id();
    OpId last_seen_op_id;
    last_seen_op_id.term = from_op_id.term();
    last_seen_op_id.index = from_op_id.index();
    uint64_t commit_timestamp = 0;

    size_t next_checkpoint_index = 0;
    std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>> wal_records, all_checkpoints;

    if (FLAGS_cdc_enable_consistent_records)
      RETURN_NOT_OK(GetConsistentWALRecords(
          tablet_peer, mem_tracker, msgs_holder, &consumption, consistent_stream_safe_time,
          historical_max_op_id, &wait_for_wal_update, &last_seen_op_id, &last_readable_opid_index,
          safe_hybrid_time_req, deadline, &wal_records, &all_checkpoints));
    else
      // 'skip_intents' is true here because we want the first transaction to be the partially
      // streamed transaction.
      RETURN_NOT_OK(GetWALRecords(
          tablet_peer, mem_tracker, msgs_holder, &consumption, consistent_stream_safe_time,
          &last_seen_op_id, &last_readable_opid_index, safe_hybrid_time_req, deadline, true,
          &wal_records, &all_checkpoints));

    // We don't need to wait for wal to get updated in this case because we will anyways stream
    // only until we complete this transaction.
    wait_for_wal_update = false;

    have_more_messages = HaveMoreMessages(true);

    if (wal_records.size() > (size_t)wal_segment_index &&
        wal_records[wal_segment_index]->op_type() ==
            consensus::OperationType::UPDATE_TRANSACTION_OP &&
        wal_records[wal_segment_index]->transaction_state().has_commit_hybrid_time()) {
      const auto& msg = wal_records[wal_segment_index];
      auto txn_id =
          VERIFY_RESULT(FullyDecodeTransactionId(msg->transaction_state().transaction_id()));
      VLOG(3) << "Will stream remaining records for a partially streamed transaction. op_id: "
              << msg->id().ShortDebugString() << ", tablet_id: " << tablet_id
              << ", transaction_id: " << txn_id;
      commit_timestamp = msg->transaction_state().commit_hybrid_time();

      op_id.term = msg->id().term();
      op_id.index = msg->id().index();
    } else {
      LOG(DFATAL) << "Unable to read the transaction commit time for tablet_id: " << tablet_id
                  << " with stream_id: " << stream_id
                  << " because there is no RAFT log message read from WAL with from_op_id: "
                  << OpId::FromPB(from_op_id) << ", which can impact the safe time.";
      if (wal_records.size() > 0) {
        VLOG(1) << "Expected message with UPDATE_TRANSACTION_OP but instead received a message"
                << "with op: " << wal_records[wal_segment_index]->op_type();
      }
    }

    RETURN_NOT_OK(reverse_index_key_slice.consume_byte(dockv::KeyEntryTypeAsChar::kTransactionId));
    auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&reverse_index_key_slice));

    RETURN_NOT_OK(PrcoessIntentsWithInvalidSchemaRetry(
        op_id, transaction_id, stream_metadata, enum_oid_label_map, composite_atts_map, resp,
        &consumption, &checkpoint, tablet_peer, &keyValueIntents, &stream_state, client,
        cached_schema_details, commit_timestamp));

    if (checkpoint.write_id() == 0 && checkpoint.key().empty() && wal_records.size()) {
      AcknowledgeStreamedMultiShardTxn(
          wal_records[wal_segment_index], ShouldUpdateSafeTime(wal_records, wal_segment_index),
          safe_hybrid_time_req, &next_checkpoint_index, all_checkpoints, &checkpoint,
          last_streamed_op_id, &safe_hybrid_time_resp, &wal_segment_index);
    } else {
      pending_intents = true;
      VLOG(1) << "Couldn't stream all records with this GetChanges call for tablet_id: "
              << tablet_id << ", transaction_id: " << transaction_id.ToString()
              << ", commit_time: " << commit_timestamp
              << ". The remaining records will be streamed in susequent GetChanges calls.";
      SetSafetimeFromRequestIfInvalid(safe_hybrid_time_req, &safe_hybrid_time_resp);
    }
    checkpoint_updated = true;
  } else {
    RequestScope request_scope;
    OpId last_seen_op_id = op_id;
    bool saw_non_actionable_message = false;
    std::unordered_set<std::string> streamed_txns;

    if (tablet_ptr->metadata()->tablet_data_state() == tablet::TABLET_DATA_SPLIT_COMPLETED) {
      // This indicates that the tablet being polled has been split and in this case we should
      // tell the client immediately about the split.
      LOG(INFO) << "Tablet split detected for tablet " << tablet_id
                << ", moving to children tablets immediately";

      return STATUS_FORMAT(
        TabletSplit, "Tablet split detected on $0", tablet_id);
    }

    // It's possible that a batch of messages in read_ops after fetching from
    // 'ReadReplicatedMessagesForCDC' , will not have any actionable messages. In which case we
    // keep retrying by fetching the next batch, until either we get an actionable message or reach
    // the 'last_readable_opid_index'.
    do {
      size_t next_checkpoint_index = 0;
      std::vector<std::shared_ptr<yb::consensus::LWReplicateMsg>> wal_records, all_checkpoints;

      if (FLAGS_cdc_enable_consistent_records)
        RETURN_NOT_OK(GetConsistentWALRecords(
            tablet_peer, mem_tracker, msgs_holder, &consumption, consistent_stream_safe_time,
            historical_max_op_id, &wait_for_wal_update, &last_seen_op_id, &last_readable_opid_index,
            safe_hybrid_time_req, deadline, &wal_records, &all_checkpoints));
      else
        // 'skip_intents' is false otherwise in case the complete wal segment is filled with
        // intents we will break the loop thinking that WAL has no more records.
        RETURN_NOT_OK(GetWALRecords(
            tablet_peer, mem_tracker, msgs_holder, &consumption, consistent_stream_safe_time,
            &last_seen_op_id, &last_readable_opid_index, safe_hybrid_time_req, deadline, false,
            &wal_records, &all_checkpoints));

      if (wait_for_wal_update) {
        VLOG_WITH_FUNC(1)
            << "Returning an empty response because WAL is not up to date with apply records "
               "of all comitted transactions. historical_max_op_id: "
            << historical_max_op_id.ToString()
            << ", last_seen_op_id: " << last_seen_op_id.ToString();
        break;
      }

      if (wal_records.empty()) {
        VLOG_WITH_FUNC(1) << "Did not get any messages with current batch of 'wal_records'."
                          << "last_seen_op_id: " << last_seen_op_id << ", last_readable_opid_index "
                          << *last_readable_opid_index << ", safe_hybrid_time "
                          << safe_hybrid_time_req << ", consistent_safe_time "
                          << consistent_stream_safe_time;
        break;
      }

      auto txn_participant = tablet_ptr->transaction_participant();
      if (txn_participant) {
        request_scope = VERIFY_RESULT(RequestScope::Create(txn_participant));
      }
      have_more_messages = HaveMoreMessages(true);

      Schema current_schema = *tablet_ptr->metadata()->schema();
      bool saw_split_op = false;

      int resp_num_records = 0;
      uint64_t resp_records_size = 0;

      for (size_t index = wal_segment_index; index < wal_records.size(); ++index) {
        for (; resp_num_records < resp->cdc_sdk_proto_records_size(); ++resp_num_records) {
          resp_records_size += resp->cdc_sdk_proto_records(resp_num_records).ByteSizeLong();
        }

        if (resp_records_size >= FLAGS_cdc_stream_records_threshold_size_bytes) {
          VLOG(1) << "Response records size crossed the thresold size. Will stream rest of the "
                     "records in next GetChanges Call. resp_records_size: "
                  << resp_records_size
                  << ", threshold: " << FLAGS_cdc_stream_records_threshold_size_bytes
                  << ", resp_num_records: " << resp_num_records << ", tablet_id: " << tablet_id;
          break;
        }

        const auto& msg = wal_records[index];

        // In case of a connector failure we may get a wal_segment_index that is obsolete.
        // We should not stream messages we have already streamed again in this case,
        // except for "SPLIT_OP" messages which can appear with a hybrid_time lower than
        // safe_hybrid_time_req.

        uint64_t commit_time_threshold = 0;
        if (consistent_snapshot_time.has_value()) {
          if (safe_hybrid_time_req >= 0) {
            commit_time_threshold = std::max((uint64_t)safe_hybrid_time_req,
                                             *consistent_snapshot_time);
          } else {
            commit_time_threshold = *consistent_snapshot_time;
          }
        } else {
          if (safe_hybrid_time_req >= 0) {
            commit_time_threshold = (uint64_t)safe_hybrid_time_req;
          }
        }
        VLOG(3) << "Commit time Threshold = " << commit_time_threshold;
        VLOG(3) << "Txn commit time       = " << GetTransactionCommitTime(msg);

        if (FLAGS_cdc_enable_consistent_records &&
            GetTransactionCommitTime(msg) <= commit_time_threshold &&
            msg->op_type() != yb::consensus::OperationType::SPLIT_OP) {
          VLOG_WITH_FUNC(2)
              << "Received a message in wal_segment with commit_time <= request safe time."
                 " Will ignore this message. consistent_stream_safe_time: "
              << consistent_stream_safe_time << ", safe_hybrid_time_req: " << safe_hybrid_time_req
              << ", tablet_id: " << tablet_id << ", wal_msg: " << msg->ShortDebugString();
          saw_non_actionable_message = true;
          AcknowledgeStreamedMsg(
              msg, false, safe_hybrid_time_req, &next_checkpoint_index, all_checkpoints,
              &checkpoint, last_streamed_op_id, &safe_hybrid_time_resp, &wal_segment_index);
          continue;
        }

        // We should break if we have started seeing records with commit_time more than the
        // consistent_stream_safe_time.
        if (FLAGS_cdc_enable_consistent_records &&
            GetTransactionCommitTime(msg) > consistent_stream_safe_time) {
          VLOG_WITH_FUNC(2)
              << "Received a message in wal_segment with commit_time > consistent_safe_time."
                 " Will not process further messages in this GetChanges call. "
                 "consistent_safe_time: "
              << consistent_stream_safe_time << "safe_hybrid_time_req: " << safe_hybrid_time_req
              << ", tablet_id: " << tablet_id << ", wal_msg: " << msg->ShortDebugString();
          break;
        }

        switch (msg->op_type()) {
          case consensus::OperationType::UPDATE_TRANSACTION_OP:
            // Ignore intents.
            // Read from IntentDB after they have been applied.
            if (msg->transaction_state().status() == TransactionStatus::APPLYING) {
              auto txn_id = VERIFY_RESULT(
                  FullyDecodeTransactionId(msg->transaction_state().transaction_id()));
              auto result = GetTransactionStatus(txn_id, tablet_peer->Now(), txn_participant);

              // It is possible for a transaction to have two APPLYs in WAL. This check
              // prevents us from streaming the same transaction twice in the same GetChanges
              // call.
              if (streamed_txns.find(txn_id.ToString()) != streamed_txns.end()) {
                saw_non_actionable_message = true;
                AcknowledgeStreamedMultiShardTxn(
                    msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                    &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                    &safe_hybrid_time_resp, &wal_segment_index);
                break;
              }

              std::vector<docdb::IntentKeyValueForCDC> intents;
              docdb::ApplyTransactionState new_stream_state;

              *commit_timestamp = msg->transaction_state().commit_hybrid_time();
              op_id.term = msg->id().term();
              op_id.index = msg->id().index();

              VLOG(3) << "Will stream records for a multi-shard transaction. op_id: "
                      << msg->id().ShortDebugString() << ", tablet_id: " << tablet_id
                      << ", transaction_id: " << txn_id << ", commit_time: " << *commit_timestamp;

              RETURN_NOT_OK(PrcoessIntentsWithInvalidSchemaRetry(
                  op_id, txn_id, stream_metadata, enum_oid_label_map, composite_atts_map, resp,
                  &consumption, &checkpoint, tablet_peer, &intents, &new_stream_state, client,
                  cached_schema_details, msg->transaction_state().commit_hybrid_time()));
              streamed_txns.insert(txn_id.ToString());

              if (new_stream_state.write_id != 0 && !new_stream_state.key.empty()) {
                pending_intents = true;
                VLOG(1)
                    << "Couldn't stream all records with this GetChanges call for tablet_id: "
                    << tablet_id << ", transaction_id: " << txn_id.ToString()
                    << ", op_id: " << op_id << ", commit_time: " << *commit_timestamp
                    << ". The remaining records will be streamed in susequent GetChanges calls.";
                SetSafetimeFromRequestIfInvalid(safe_hybrid_time_req, &safe_hybrid_time_resp);
              } else {
                AcknowledgeStreamedMultiShardTxn(
                    msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                    &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                    &safe_hybrid_time_resp, &wal_segment_index);
              }
              checkpoint_updated = true;
            }
            break;

          case consensus::OperationType::WRITE_OP: {
            const auto& batch = msg->write().write_batch();
            *commit_timestamp = msg->hybrid_time();

            VLOG(3) << "Will stream a single-shard transaction. op_id: "
                    << msg->id().ShortDebugString() << ", tablet_id: " << tablet_id
                    << ", hybrid_time: " << *commit_timestamp;

            if (!batch.has_transaction()) {
              RETURN_NOT_OK(PopulateCDCSDKWriteRecordWithInvalidSchemaRetry(
                  msg, stream_metadata, tablet_peer, enum_oid_label_map, composite_atts_map,
                  cached_schema_details, resp, client));

              AcknowledgeStreamedMsg(
                  msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                  &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                  &safe_hybrid_time_resp, &wal_segment_index);
              checkpoint_updated = true;
            }
          } break;

          case consensus::OperationType::CHANGE_METADATA_OP: {
            RETURN_NOT_OK(SchemaFromPB(
                msg->change_metadata_request().schema().ToGoogleProtobuf(), &current_schema));
            TabletId table_id = tablet_ptr->metadata()->table_id();
            if (tablet_ptr->metadata()->colocated()) {
              auto table_info = CHECK_RESULT(tablet_ptr->metadata()->GetTableInfo(
                  msg->change_metadata_request().alter_table_id().ToBuffer()));
              table_id = table_info->table_id;
              table_name = table_info->table_name;
            }

            // We cross-verify the scheam details from the replicated message with the schema
            // details from the SysCatalog table.
            auto previous_schema_version = std::numeric_limits<uint32_t>::max();
            uint32_t changed_schema_version;
            {
              auto iter = cached_schema_details->find(table_id);
              if (iter != cached_schema_details->end()) {
                previous_schema_version = iter->second.schema_version;
              }
            }

            (*cached_schema_details)[table_id] = SchemaDetails{
                .schema_version = msg->change_metadata_request().schema_version(),
                .schema = std::make_shared<Schema>(std::move(current_schema))};
            changed_schema_version = msg->change_metadata_request().schema_version();
            auto result = client->GetTableSchemaFromSysCatalog(table_id, msg->hybrid_time());
            if (!result.ok()) {
              LOG(WARNING)
                  << "Failed to get the specific schema version from system catalog for table: "
                  << table_name
                  << " proceedings with the table schema version got with CHANGE_METADATA_OP.";
            } else if ((*cached_schema_details)[table_id].schema_version != result->second) {
              current_schema = result->first;
              (*cached_schema_details)[table_id] = SchemaDetails{
                  .schema_version = result->second,
                  .schema = std::make_shared<Schema>(result->first)};
              changed_schema_version = result->second;
            }

            if (previous_schema_version != changed_schema_version &&
                !boost::ends_with(table_name, kTablegroupParentTableNameSuffix) &&
                !boost::ends_with(table_name, kColocationParentTableNameSuffix)) {
              RETURN_NOT_OK(PopulateCDCSDKDDLRecord(
                  msg, resp->add_cdc_sdk_proto_records(), table_name, current_schema));
            }

            AcknowledgeStreamedMsg(
                msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                &safe_hybrid_time_resp, &wal_segment_index);
            checkpoint_updated = true;
          } break;

          case consensus::OperationType::TRUNCATE_OP: {
            if (FLAGS_stream_truncate_record) {
              RETURN_NOT_OK(PopulateCDCSDKTruncateRecord(
                  msg, resp->add_cdc_sdk_proto_records(), current_schema));
              checkpoint_updated = true;
            } else {
              saw_non_actionable_message = true;
            }

            AcknowledgeStreamedMsg(
                msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                &safe_hybrid_time_resp, &wal_segment_index);
          } break;

          case yb::consensus::OperationType::SPLIT_OP: {
            const TableId& table_id = tablet_ptr->metadata()->table_id();
            auto op_id = OpId::FromPB(msg->id());

            // Handle if SPLIT_OP corresponds to the parent tablet or we know that the split_op was
            // unsuccessful.
            if (msg->split_request().tablet_id() != tablet_id ||
                HasSplitFailed(wal_records, index)) {
              saw_non_actionable_message = true;
              AcknowledgeStreamedMsg(
                  msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                  &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                  &safe_hybrid_time_resp, &wal_segment_index);
              break;
            }

            // Set 'saw_split_op' to true only if the split op is for the current tablet.
            saw_split_op = true;

            // We first verify if a split has indeed occured succesfully by checking if there are
            // two children tablets for the tablet. This check also verifies if the SPLIT_OP
            // belongs to the current tablet
            if (!(VerifyTabletSplitOnParentTablet(table_id, tablet_id, client))) {
              // We could verify the tablet split succeeded. This is possible when the child tablets
              // of a split are not running yet.
              LOG(INFO) << "Found SPLIT_OP record with index: " << op_id
                        << ", but did not find any children tablets for the tablet: " << tablet_id
                        << ". This is possible when the child tablets are not up and running yet.";
              SetSafetimeFromRequestIfInvalid(safe_hybrid_time_req, &safe_hybrid_time_resp);
            } else {
              if (checkpoint_updated) {
                // If we have records which are yet to be streamed which we discovered in the same
                // 'GetChangesForCDCSDK' call, we will not update the checkpoint to the SplitOp
                // record's OpId and return the records seen till now. Next time the client will
                // call 'GetChangesForCDCSDK' with the OpId just before the SplitOp's record.
                //
                // NOTE: It is fine to not update the checkpoint in this case because this should
                // be the last actionable record in the WAL.
                LOG(INFO) << "Found SPLIT_OP record with OpId: " << op_id
                          << ", for parent tablet: " << tablet_id
                          << ", will stream all seen records until now.";
              } else {
                // If 'GetChangesForCDCSDK' was called with the OpId just before the SplitOp's
                // record, and if there is no more data to stream and we can notify the client
                // about the split and update the checkpoint.
                LOG(INFO) << "Found SPLIT_OP record with OpId: " << op_id
                          << ", for parent tablet: " << tablet_id
                          << ", and if we did not see any other records we will report the tablet "
                             "split to the client";

                AcknowledgeStreamedMsg(
                    msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                    &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                    &safe_hybrid_time_resp, &wal_segment_index);
                checkpoint_updated = true;
                report_tablet_split = true;
              }
            }
          } break;

          default:
            // Nothing to do for other operation types.
            saw_non_actionable_message = true;
            AcknowledgeStreamedMsg(
                msg, ShouldUpdateSafeTime(wal_records, index), safe_hybrid_time_req,
                &next_checkpoint_index, all_checkpoints, &checkpoint, last_streamed_op_id,
                &safe_hybrid_time_resp, &wal_segment_index);
            VLOG_WITH_FUNC(2) << "Found message of Op type: " << msg->op_type()
                              << ", on tablet: " << tablet_id
                              << ", with OpId: " << msg->id().ShortDebugString();
            break;
        }

        // There can be NO_OP messages after a SPLIT_OP. Ignore them.
        if (pending_intents || saw_split_op) {
          break;
        }
      }

      if (!checkpoint_updated && VLOG_IS_ON(1)) {
        VLOG_WITH_FUNC(1)
            << "The current batch of 'wal_records' had no actionable message. last_see_op_id: "
            << last_seen_op_id << ", last_readable_opid_index: " << *last_readable_opid_index
            << ". Will retry and get another batch";
      }

    } while (!checkpoint_updated && last_readable_opid_index &&
             last_seen_op_id.index < *last_readable_opid_index);

    // In case the checkpoint was not updated at-all, we will update the checkpoint using the last
    // seen non-actionable message.
    if (saw_non_actionable_message && !checkpoint_updated) {
      have_more_messages = HaveMoreMessages(false);
      checkpoint_updated = true;
      VLOG_WITH_FUNC(2) << "The last batch of 'wal_records' had no actionable message"
                        << ", on tablet: " << tablet_id << ".";
    }
  }

  // If the GetChanges call is not for snapshot and then we know that a split has indeed been
  // successful then we should report the split to the client.
  if (!snapshot_operation && report_tablet_split) {
    LOG(INFO) << "Tablet split detected for tablet " << tablet_id
              << ", moving to children tablets immediately";
    return STATUS_FORMAT(
      TabletSplit, "Tablet split detected on $0", tablet_id
    );
  }

  if (consumption) {
    consumption.Add(resp->SpaceUsedLong());
  }

  // If we need to wait for WAL to get up to date with all committed transactions, we will send the
  // request safe in the response as well.
  auto safe_time = wait_for_wal_update
                       ? HybridTime((safe_hybrid_time_req > 0) ? safe_hybrid_time_req : 0)
                       : GetCDCSDKSafeTimeForTarget(
                             leader_safe_time.get(), safe_hybrid_time_resp, have_more_messages,
                             consistent_stream_safe_time, snapshot_operation);
  resp->set_safe_hybrid_time(safe_time.ToUint64());

  // It is possible in case of a partially streamed transaction.
  if (checkpoint_updated && !(checkpoint.has_term() && checkpoint.has_index())) {
    checkpoint.set_term(from_op_id.term());
    checkpoint.set_index(from_op_id.index());
  }

  checkpoint_updated ? resp->mutable_cdc_sdk_checkpoint()->CopyFrom(checkpoint)
                     : resp->mutable_cdc_sdk_checkpoint()->CopyFrom(from_op_id);
  resp->set_wal_segment_index(wal_segment_index);

  if (last_streamed_op_id->index > 0) {
    last_streamed_op_id->ToPB(resp->mutable_checkpoint()->mutable_op_id());
  }

  // We do not populate SAFEPOINT records in two scenarios:
  // 1. When we are streaming batches of a large transaction
  // 2. When we are streaming snapshot records
  if (FLAGS_cdc_populate_safepoint_record && !pending_intents && from_op_id.write_id() != -1) {
    RETURN_NOT_OK(PopulateCDCSDKSafepointOpRecord(
        safe_time.ToUint64(),
        tablet_peer->tablet()->metadata()->table_name(),
        resp->add_cdc_sdk_proto_records(),
        *tablet_peer->tablet()->schema().get()));
    VLOG(2) << "Added Safepoint Record";
  }

  // Populate from_op_id in all cdcsdk records
  for (auto& record : (*resp->mutable_cdc_sdk_proto_records())) {
    auto record_from_op_id = record.mutable_from_op_id();
    SetCDCSDKOpId(
        from_op_id.term(), from_op_id.index(), from_op_id.write_id(), from_op_id.key(),
        record_from_op_id);
  }

  VLOG(1) << "Sending GetChanges response. cdcsdk_checkpoint: "
          << resp->cdc_sdk_checkpoint().ShortDebugString()
          << ", safe_hybrid_time: " << resp->safe_hybrid_time()
          << ", wal_segment_index: " << resp->wal_segment_index()
          << ", num_records: " << resp->cdc_sdk_proto_records_size()
          << ", tablet_id: " << tablet_id;

  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
