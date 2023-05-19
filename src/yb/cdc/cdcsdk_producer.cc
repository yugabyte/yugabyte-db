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

#include "yb/master/master_client.pb.h"
#include "yb/master/master_util.h"

#include "yb/qlexpr/ql_expr.h"
#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"

using std::string;

DEFINE_RUNTIME_int32(cdc_snapshot_batch_size, 250, "Batch size for the snapshot operation in CDC");

DEFINE_RUNTIME_bool(stream_truncate_record, false, "Enable streaming of TRUNCATE record");

DECLARE_int64(cdc_intent_retention_ms);

DEFINE_RUNTIME_bool(
    enable_single_record_update, true,
    "Enable packing updates corresponding to a row in single CDC record");

DEFINE_RUNTIME_bool(
    cdc_populate_safepoint_record, false,
    "If 'true' we will also send a 'SAFEPOINT' record at the end of each GetChanges call.");

DEFINE_test_flag(
    bool, cdc_snapshot_failure, false,
    "For testing only, When it is set to true, the CDC snapshot operation will fail.");

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

template <class Value>
Status AddColumnToMap(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const ColumnSchema& col_schema,
    const Value& col, const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map, DatumMessagePB* cdc_datum_message,
    const QLValuePB* old_ql_value_passed) {
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  cdc_datum_message->set_column_name(col_schema.name());
  QLValuePB ql_value;
  if (tablet->table_type() == PGSQL_TABLE_TYPE) {
    if (old_ql_value_passed) {
      ql_value = *old_ql_value_passed;
    } else {
      col.ToQLValuePB(col_schema.type(), &ql_value);
    }
    if (!IsNull(ql_value) && col_schema.pg_type_oid() != 0 /*kInvalidOid*/) {
      RETURN_NOT_OK(docdb::SetValueFromQLBinaryWrapper(
          ql_value, col_schema.pg_type_oid(), enum_oid_label_map, composite_atts_map,
          cdc_datum_message));
    } else {
      cdc_datum_message->set_column_type(col_schema.pg_type_oid());
    }
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
    if ((metadata.GetRecordType() == cdc::CDCRecordType::CHANGE) ||
        ((metadata.GetRecordType() == cdc::CDCRecordType::ALL) &&
         (row_message->op() == RowMessage_Op_INSERT)))
      row_message->add_old_tuple();
  }
  return tuple;
}

Status AddPrimaryKey(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const dockv::SubDocKey& decoded_key,
    const Schema& tablet_schema, const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map, RowMessage* row_message,
    const StreamMetadata& metadata) {
  size_t i = 0;
  for (const auto& col : decoded_key.doc_key().hashed_group()) {
    DatumMessagePB* tuple = AddTuple(row_message, metadata);
    RETURN_NOT_OK(AddColumnToMap(
        tablet_peer, tablet_schema.column(i), col, enum_oid_label_map, composite_atts_map, tuple,
        nullptr));
    i++;
  }

  for (const auto& col : decoded_key.doc_key().range_group()) {
    DatumMessagePB* tuple = AddTuple(row_message, metadata);
    RETURN_NOT_OK(AddColumnToMap(
        tablet_peer, tablet_schema.column(i), col, enum_oid_label_map, composite_atts_map, tuple,
        nullptr));
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

Status PopulateBeforeImage(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const ReadHybridTime& read_time,
    RowMessage* row_message, const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map, const dockv::SubDocKey& decoded_primary_key,
    const Schema& schema, ColocationId colocation_id) {
  auto tablet = tablet_peer->shared_tablet();
  auto docdb = tablet->doc_db();
  auto pending_op = tablet->CreateScopedRWOperationNotBlockingRocksDbShutdownStart();

  const auto log_prefix = tablet->LogPrefix();
  auto doc_read_context = VERIFY_RESULT(
      tablet_peer->tablet_metadata()->GetTableInfo(colocation_id))->doc_read_context;
  dockv::ReaderProjection projection(schema);
  docdb::DocRowwiseIterator iter(
      projection,
      *doc_read_context,
      TransactionOperationContext(), docdb, CoarseTimePoint::max() /* deadline */, read_time,
      pending_op);
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

  size_t found_columns = 0;
  if (row.ColumnCount() == columns.size()) {
    for (size_t index = 0; index < row.ColumnCount(); ++index) {
      bool column_updated = false;
      RETURN_NOT_OK(row.GetValue(schema.column_id(index), &ql_value));
      if (!ql_value.IsNull()) {
        RETURN_NOT_OK(AddColumnToMap(
            tablet_peer, columns[index], PrimitiveValue(), enum_oid_label_map, composite_atts_map,
            row_message->add_old_tuple(), &ql_value.value()));
        if (row_message->op() == RowMessage_Op_UPDATE) {
          const auto& old_tuple_column_name =
              row_message->old_tuple(static_cast<int>(found_columns)).column_name();
          for (int new_tuple_index = 0; new_tuple_index < row_message->new_tuple_size();
               ++new_tuple_index) {
            if (row_message->new_tuple(static_cast<int>(new_tuple_index)).column_name() ==
                old_tuple_column_name) {
              column_updated = true;
              break;
            }
          }
          if (!column_updated) {
            auto new_tuple_pb = row_message->mutable_new_tuple()->Add();
            new_tuple_pb->CopyFrom(row_message->old_tuple(static_cast<int>(found_columns)));
          }
        }
        found_columns += 1;
      }
    }
  } else if (row_message->op() != RowMessage_Op_DELETE) {
    for (size_t index = 0; index < schema.num_columns(); ++index) {
      row_message->add_old_tuple();
    }
  }
  return Status::OK();
}

Result<size_t> PopulatePackedRows(
    const SchemaPackingStorage& schema_packing_storage, const Schema& schema,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map, const CompositeAttsMap& composite_atts_map,
    Slice* value_slice, RowMessage* row_message) {
  const dockv::SchemaPacking& packing =
      VERIFY_RESULT(schema_packing_storage.GetPacking(value_slice));
  for (size_t i = 0; i != packing.columns(); ++i) {
    auto slice = packing.GetValue(i, *value_slice);
    const auto& column_data = packing.column_packing_data(i);

    PrimitiveValue pv;
    // Empty slice represent NULL value and is valid.
    if (!slice.empty()) {
      RETURN_NOT_OK(pv.DecodeFromValue(slice));
    }
    const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_data.id));
    RETURN_NOT_OK(AddColumnToMap(
        tablet_peer, col, pv, enum_oid_label_map, composite_atts_map, row_message->add_new_tuple(),
        nullptr));
    row_message->add_old_tuple();
  }

  return packing.columns();
}

HybridTime GetCDCSDKSafeTimeForTarget(
    const HybridTime leader_safe_time,
    HybridTime ht_of_last_returned_message,
    HaveMoreMessages have_more_messages) {
  if (have_more_messages) {
    return ht_of_last_returned_message;
  }

  if (ht_of_last_returned_message.is_valid()) {
    if (!leader_safe_time.is_valid() || ht_of_last_returned_message > leader_safe_time) {
      return ht_of_last_returned_message;
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
    client::YBClient* client) {
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
  size_t col_count = 0;
  docdb::IntentKeyValueForCDC prev_intent;
  MicrosTime prev_intent_phy_time = 0;
  bool new_cdc_record_needed = false;
  dockv::SubDocKey prev_decoded_key;

  for (const auto& intent : intents) {
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
          (prev_key != primary_key) || (col_count >= schema.num_columns()) ||
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
          if (metadata.GetRecordType() == cdc::CDCRecordType::ALL) {
            VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                    << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                    << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                    << " for change record type: " << row_message->op();
            if (commit_time > 0) {
              auto hybrid_time = commit_time - 1;
              auto result = PopulateBeforeImage(
                  tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message,
                  enum_oid_label_map, composite_atts_map, prev_decoded_key, schema, colocation_id);
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
      } else if (value_type == dockv::ValueEntryType::kPackedRow) {
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

      if ((metadata.GetRecordType() == cdc::CDCRecordType::ALL) &&
          (row_message->op() == RowMessage_Op_DELETE)) {
        VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                << "  cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                << "  for change record type: " << row_message->op();
        if (commit_time > 0) {
          auto hybrid_time = commit_time - 1;
          auto result = PopulateBeforeImage(
              tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message, enum_oid_label_map,
              composite_atts_map, decoded_key, schema, colocation_id);
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
              metadata));
        } else {
          for (size_t index = 0; index < schema.num_columns(); ++index) {
            row_message->add_new_tuple();
          }
        }
      } else {
        RETURN_NOT_OK(AddPrimaryKey(
            tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
            metadata));
      }
    }

    prev_key = primary_key;
    prev_intent_phy_time = intent.intent_ht.hybrid_time().GetPhysicalValueMicros();
    if (IsInsertOrUpdate(*row_message)) {
      if (value_type == dockv::ValueEntryType::kPackedRow) {
        col_count += VERIFY_RESULT(PopulatePackedRows(
            schema_packing_storage, schema, tablet_peer, enum_oid_label_map, composite_atts_map,
            &value_slice, row_message));
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
    if (FLAGS_enable_single_record_update) {
      if ((row_message->op() == RowMessage_Op_INSERT && col_count == schema.num_columns()) ||
          (row_message->op() == RowMessage_Op_DELETE)) {
        MakeNewProtoRecord(
            intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
            reverse_index_key, commit_time);
        col_count = schema.num_columns();
      } else if (row_message->op() == RowMessage_Op_UPDATE) {
        prev_intent = intent;
        prev_decoded_key = decoded_key;
      }
    } else {
      if ((row_message->op() == RowMessage_Op_INSERT && col_count == schema.num_columns()) ||
          (row_message->op() == RowMessage_Op_UPDATE ||
           row_message->op() == RowMessage_Op_DELETE)) {
        if ((metadata.GetRecordType() == cdc::CDCRecordType::ALL) &&
            (row_message->op() == RowMessage_Op_UPDATE)) {
          VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                  << " with read time: " << ReadHybridTime::FromUint64(commit_time)
                  << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                  << " for change record type: " << row_message->op();
          if (commit_time > 0) {
            auto hybrid_time = commit_time - 1;
            auto result = PopulateBeforeImage(
                tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message,
                enum_oid_label_map, composite_atts_map, decoded_key, schema, colocation_id);
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
      row_message->IsInitialized() && row_message->op() == RowMessage_Op_UPDATE) {
    row_message->set_table(table_name);
    if (metadata.GetRecordType() == cdc::CDCRecordType::ALL) {
      VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
              << " with read time: " << ReadHybridTime::FromUint64(commit_time)
              << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
              << " for change record type: " << row_message->op();
      if (commit_time > 0) {
        auto hybrid_time = commit_time - 1;
        auto result = PopulateBeforeImage(
            tablet_peer, ReadHybridTime::FromUint64(hybrid_time), row_message, enum_oid_label_map,
            composite_atts_map, prev_decoded_key, schema, colocation_id);
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
  auto tablet_ptr = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  const auto& batch = msg->write().write_batch();
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;
  dockv::SubDocKey prev_decoded_key;
  // Write batch may contain records from different rows.
  // For CDC, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;

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
  for (const auto& write_pair : batch.write_pairs()) {
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
        if (metadata.GetRecordType() == cdc::CDCRecordType::ALL) {
          VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                  << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                  << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                  << " for change record type: " << row_message->op();
          auto result = PopulateBeforeImage(
              tablet_peer, ReadHybridTime::FromUint64(msg->hybrid_time() - 1), row_message,
              enum_oid_label_map, composite_atts_map, prev_decoded_key, schema, colocation_id);
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
      row_message = proto_record->mutable_row_message();
      row_message->set_pgschema_name(schema.SchemaName());
      row_message->set_table(table_name);
      CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
      SetCDCSDKOpId(msg->id().term(), msg->id().index(), 0, "", cdc_sdk_op_id_pb);

      // Check whether operation is WRITE or DELETE.
      if (value_type == dockv::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        SetOperation(row_message, OpType::DELETE, schema);
      } else if (value_type == dockv::ValueEntryType::kPackedRow) {
        SetOperation(row_message, OpType::INSERT, schema);
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

      if ((metadata.GetRecordType() == cdc::CDCRecordType::ALL) &&
          (row_message->op() == RowMessage_Op_DELETE)) {
        VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
                << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
                << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
                << " for change record type: " << row_message->op();
        auto result = PopulateBeforeImage(
            tablet_peer, ReadHybridTime::FromUint64(msg->hybrid_time() - 1), row_message,
            enum_oid_label_map, composite_atts_map, decoded_key, schema, colocation_id);
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
              metadata));
        } else {
          for (size_t index = 0; index < schema.num_columns(); ++index) {
            row_message->add_new_tuple();
          }
        }
      } else {
        RETURN_NOT_OK(AddPrimaryKey(
            tablet_peer, decoded_key, schema, enum_oid_label_map, composite_atts_map, row_message,
            metadata));
      }
      // Process intent records.
      row_message->set_commit_time(msg->hybrid_time());
      row_message->set_record_time(msg->hybrid_time());

      prev_decoded_key = decoded_key;
    }
    prev_key = primary_key;
    DCHECK(proto_record);

    if (IsInsertOrUpdate(*row_message)) {
      if (value_type == dockv::ValueEntryType::kPackedRow) {
        RETURN_NOT_OK(PopulatePackedRows(
            schema_packing_storage, schema, tablet_peer, enum_oid_label_map, composite_atts_map,
            &value_slice, row_message));
      } else {
        dockv::KeyEntryValue column_id;
        Slice key_column = key.WithoutPrefix(key_size);
        RETURN_NOT_OK(dockv::KeyEntryValue::DecodeKey(&key_column, &column_id));
        if (column_id.type() == dockv::KeyEntryType::kColumnId) {
          const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_id.GetColumnId()));
          dockv::Value decoded_value;
          RETURN_NOT_OK(decoded_value.Decode(write_pair.value()));

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
    }
  }

  if (row_message && row_message->op() == RowMessage_Op_UPDATE) {
    if (metadata.GetRecordType() == cdc::CDCRecordType::ALL) {
      VLOG(2) << "Get Beforeimage for tablet: " << tablet_peer->tablet_id()
              << " with read time: " << ReadHybridTime::FromUint64(msg->hybrid_time())
              << " cdcsdk_safe_time: " << tablet_peer->get_cdc_sdk_safe_time()
              << " for change record type: " << row_message->op();
      auto result = PopulateBeforeImage(
          tablet_peer, ReadHybridTime::FromUint64(msg->hybrid_time() - 1), row_message,
          enum_oid_label_map, composite_atts_map, prev_decoded_key, schema, colocation_id);
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

  return Status::OK();
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
  if (stream_state->key.empty() && stream_state->write_id == 0) {
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
    if (value_type != dockv::ValueEntryType::kPackedRow) {
      dockv::Value decoded_value;
      RETURN_NOT_OK(decoded_value.Decode(Slice(keyValue.value_buf)));
    }
  }

  std::string reverse_index_key;
  IntraTxnWriteId write_id = 0;

  // Need to populate the CDCSDKRecords
  if (!keyValueIntents->empty()) {
    RETURN_NOT_OK(PopulateCDCSDKIntentRecord(
        op_id, transaction_id, *keyValueIntents, metadata, tablet_peer, enum_oid_label_map,
        composite_atts_map, cached_schema_details, resp, consumption, &write_id, &reverse_index_key,
        commit_time, client));
  }

  SetTermIndex(op_id.term, op_id.index, checkpoint);

  if (stream_state->key.empty() && stream_state->write_id == 0) {
    FillCommitRecord(op_id, transaction_id, tablet_peer, checkpoint, resp, commit_time);
  } else {
    SetKeyWriteId(reverse_index_key, write_id, checkpoint);
  }

  return Status::OK();
}

Status PopulateCDCSDKSnapshotRecord(
    GetChangesResponsePB* resp,
    const qlexpr::QLTableRow* row,
    const Schema& schema,
    const TableName& table_name,
    ReadHybridTime time,
    const EnumOidLabelMap& enum_oid_label_map,
    const CompositeAttsMap& composite_atts_map) {
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;

  proto_record = resp->add_cdc_sdk_proto_records();
  row_message = proto_record->mutable_row_message();
  row_message->set_table(table_name);
  row_message->set_op(RowMessage_Op_READ);
  row_message->set_pgschema_name(schema.SchemaName());
  row_message->set_commit_time(time.read.ToUint64());
  row_message->set_record_time(time.read.ToUint64());

  DatumMessagePB* cdc_datum_message = nullptr;

  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    ColumnId col_id = schema.column_id(col_idx);
    const auto* value = row->GetColumn(col_id);
    const ColumnSchema& col_schema = VERIFY_RESULT(schema.column_by_id(col_id));

    cdc_datum_message = row_message->add_new_tuple();
    cdc_datum_message->set_column_name(col_schema.name());

    if (value && value->value_case() != QLValuePB::VALUE_NOT_SET &&
        col_schema.pg_type_oid() != 0 /*kInvalidOid*/) {
      RETURN_NOT_OK(docdb::SetValueFromQLBinaryWrapper(
          *value, col_schema.pg_type_oid(), enum_oid_label_map, composite_atts_map,
          cdc_datum_message));
    } else {
      cdc_datum_message->set_column_type(col_schema.pg_type_oid());
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

// CDC get changes is different from xCluster as it doesn't need
// to read intents from WAL.

Status GetChangesForCDCSDK(
    const CDCStreamId& stream_id,
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
    int64_t* last_readable_opid_index,
    const TableId& colocated_table_id,
    const CoarseTimePoint deadline) {
  OpId op_id{from_op_id.term(), from_op_id.index()};
  VLOG(1) << "The from_op_id from GetChanges is  " << op_id << " for tablet_id: " << tablet_id;
  ScopedTrackedConsumption consumption;
  CDCSDKCheckpointPB checkpoint;
  bool checkpoint_updated = false;
  bool report_tablet_split = false;
  OpId split_op_id = OpId::Invalid();
  bool snapshot_operation = false;

  auto tablet_ptr = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  auto leader_safe_time = tablet_ptr->SafeTime();
  if (!leader_safe_time.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << "Could not compute safe time: " << leader_safe_time.status();
    leader_safe_time = HybridTime::kInvalid;
  }
  auto table_name = tablet_ptr->metadata()->table_name();

  auto ht_of_last_returned_message = HybridTime::kInvalid;
  HaveMoreMessages have_more_messages(false);
  // It is snapshot call.
  if (from_op_id.write_id() == -1) {
    snapshot_operation = true;
    auto txn_participant = tablet_ptr->transaction_participant();
    ReadHybridTime time;
    std::string nextKey;
    // It is first call in snapshot then take snapshot.
    if ((from_op_id.key().empty()) && (from_op_id.snapshot_time() == 0)) {
      if (txn_participant == nullptr || txn_participant->context() == nullptr)
        return STATUS_SUBSTITUTE(
            Corruption, "Cannot read data as the transaction participant context is null");
      tablet::RemoveIntentsData data;
      RETURN_NOT_OK(txn_participant->context()->GetLastReplicatedData(&data));
      // Set the checkpoint and communicate to the follower.
      VLOG(1) << "The first snapshot term " << data.op_id.term << "index  " << data.op_id.index
              << "time " << data.log_ht.ToUint64();
      // Update the CDCConsumerOpId.
      std::shared_ptr<consensus::Consensus> shared_consensus = tablet_peer->shared_consensus();
      shared_consensus->UpdateCDCConsumerOpId(data.op_id);

      if (txn_participant == nullptr || txn_participant->context() == nullptr) {
        return STATUS_SUBSTITUTE(
            Corruption, "Cannot read data as the transaction participant context is null");
      }
      LOG(INFO) << "CDC snapshot initialization is started, by setting checkpoint as: "
                << data.op_id << ", for tablet_id: " << tablet_id << " stream_id: " << stream_id;
      txn_participant->SetIntentRetainOpIdAndTime(
          data.op_id, MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)));
      RETURN_NOT_OK(txn_participant->context()->GetLastReplicatedData(&data));
      time = ReadHybridTime::SingleTime(data.log_ht);
      // Use the last replicated hybrid time as a safe time for snapshot operation. so that
      // compaction can be restricted during snapshot operation.

      if (time.read.ToUint64() == 0) {
        // This means there is no data from the sansphot.
        SetCheckpoint(data.op_id.term, data.op_id.index, 0, "", 0, &checkpoint, nullptr);
      } else {
        *leader_safe_time = data.log_ht;
        // This should go to cdc_state table.
        // Below condition update the checkpoint in cdc_state table.
        SetCheckpoint(
            data.op_id.term, data.op_id.index, -1, "", time.read.ToUint64(), &checkpoint, nullptr);
      }

      checkpoint_updated = true;
    } else {
      // Snapshot is already taken.
      HybridTime ht;
      time = ReadHybridTime::FromUint64(from_op_id.snapshot_time());
      *leader_safe_time = HybridTime(from_op_id.snapshot_time());
      nextKey = from_op_id.key();
      VLOG(1) << "The after snapshot term " << from_op_id.term() << "index  " << from_op_id.index()
              << "key " << from_op_id.key() << "snapshot time " << from_op_id.snapshot_time();

      // This is for test purposes only, to create a snapshot failure scenario from the server.
      if (PREDICT_FALSE(FLAGS_TEST_cdc_snapshot_failure)) {
        return STATUS_FORMAT(
            ServiceUnavailable, "CDC snapshot is failed for tablet: $0 ", tablet_id);
      }

      const auto& schema_details = VERIFY_RESULT(GetOrPopulateRequiredSchemaDetails(
          tablet_peer, std::numeric_limits<uint64_t>::max(), cached_schema_details, client,
          colocated_table_id.empty() ? tablet_ptr->metadata()->table_id() : colocated_table_id,
          resp));

      if (!colocated_table_id.empty()) {
        table_name = VERIFY_RESULT(GetColocatedTableName(tablet_peer, colocated_table_id));
      }

      int limit = FLAGS_cdc_snapshot_batch_size;
      int fetched = 0;
      std::vector<qlexpr::QLTableRow> rows;
      qlexpr::QLTableRow row;
      dockv::ReaderProjection projection(*schema_details.schema);
      auto iter = VERIFY_RESULT(
          tablet_ptr->CreateCDCSnapshotIterator(projection, time, nextKey, colocated_table_id));
      while (fetched < limit && VERIFY_RESULT(iter->FetchNext(&row))) {
        RETURN_NOT_OK(PopulateCDCSDKSnapshotRecord(
            resp, &row, *schema_details.schema, table_name, time, enum_oid_label_map,
            composite_atts_map));
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
        SetCheckpoint(from_op_id.term(), from_op_id.index(), 0, "", 0, &checkpoint, nullptr);
        checkpoint_updated = true;
      } else {
        VLOG(1) << "Setting next sub doc key is " << sub_doc_key.Encode().ToStringBuffer();

        checkpoint.set_write_id(-1);
        SetCheckpoint(
            from_op_id.term(), from_op_id.index(), -1, sub_doc_key.Encode().ToStringBuffer(),
            time.read.ToUint64(), &checkpoint, nullptr);
        checkpoint_updated = true;
      }
    }
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
    consensus::ReadOpsResult read_ops;
    uint64_t commit_timestamp = 0;

    read_ops = VERIFY_RESULT(tablet_peer->consensus()->ReadReplicatedMessagesForCDC(
        last_seen_op_id, last_readable_opid_index, deadline, true));
    have_more_messages = HaveMoreMessages(true);

    if (read_ops.messages.size() != 1) {
      LOG(WARNING) << "Reading more or less than one raft log message while reading intents, read "
                   << read_ops.messages.size() << " instead";
    }

    if (read_ops.messages.size() > 0 &&
        read_ops.messages[0]->op_type() == consensus::OperationType::UPDATE_TRANSACTION_OP &&
        read_ops.messages[0]->transaction_state().has_commit_hybrid_time()) {
      commit_timestamp = read_ops.messages[0]->transaction_state().commit_hybrid_time();
    } else {
      LOG(WARNING) << "Unable to read the transaction commit time for tablet_id: " << tablet_id
                   << " with stream_id: " << stream_id
                   << " because there is no RAFT log message read from WAL with from_op_id: "
                   << OpId::FromPB(from_op_id) << ", which can impact the safe time.";
    }

    RETURN_NOT_OK(reverse_index_key_slice.consume_byte(dockv::KeyEntryTypeAsChar::kTransactionId));
    auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&reverse_index_key_slice));

    RETURN_NOT_OK(ProcessIntents(
        op_id, transaction_id, stream_metadata, enum_oid_label_map, composite_atts_map, resp,
        &consumption, &checkpoint, tablet_peer, &keyValueIntents, &stream_state, client,
        cached_schema_details, commit_timestamp));
    ht_of_last_returned_message =
        (resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1).row_message().op() ==
         RowMessage_Op_COMMIT)
            // Commit time won't be populated in the COMMIT record, in which case we will consider
            // the commit_time of the previous to last record.
            ? HybridTime(commit_timestamp)
            : HybridTime::FromPB(resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                                     .row_message()
                                     .record_time());

    if (checkpoint.write_id() == 0 && checkpoint.key().empty()) {
      last_streamed_op_id->term = checkpoint.term();
      last_streamed_op_id->index = checkpoint.index();
    }
    checkpoint_updated = true;
  } else {
    RequestScope request_scope;
    OpId last_seen_op_id = op_id;
    // Last seen OpId of a non-actionable message.
    OpId last_seen_default_message_op_id = OpId().Invalid();

    // It's possible that a batch of messages in read_ops after fetching from
    // 'ReadReplicatedMessagesForCDC' , will not have any actionable messages. In which case we
    // keep retrying by fetching the next batch, until either we get an actionable message or reach
    // the 'last_readable_opid_index'.
    consensus::ReadOpsResult read_ops;
    do {
      read_ops = VERIFY_RESULT(tablet_peer->consensus()->ReadReplicatedMessagesForCDC(
          last_seen_op_id, last_readable_opid_index, deadline));

      if (read_ops.read_from_disk_size && mem_tracker) {
        consumption = ScopedTrackedConsumption(mem_tracker, read_ops.read_from_disk_size);
      }

      auto txn_participant = tablet_ptr->transaction_participant();
      if (txn_participant) {
        request_scope = VERIFY_RESULT(RequestScope::Create(txn_participant));
      }
      have_more_messages = HaveMoreMessages(true);

      Schema current_schema = *tablet_ptr->metadata()->schema();
      bool pending_intents = false;

      if (read_ops.messages.empty()) {
        VLOG_WITH_FUNC(1) << "Did not get any messages with current batch of 'read_ops'."
                          << "last_seen_op_id: " << last_seen_op_id << ", last_readable_opid_index "
                          << *last_readable_opid_index;
        break;
      }

      for (const auto& msg : read_ops.messages) {
        last_seen_op_id.term = msg->id().term();
        last_seen_op_id.index = msg->id().index();

        switch (msg->op_type()) {
          case consensus::OperationType::UPDATE_TRANSACTION_OP:
            // Ignore intents.
            // Read from IntentDB after they have been applied.
            if (msg->transaction_state().status() == TransactionStatus::APPLYING) {
              auto txn_id = VERIFY_RESULT(
                  FullyDecodeTransactionId(msg->transaction_state().transaction_id()));
              auto result = GetTransactionStatus(txn_id, tablet_peer->Now(), txn_participant);
              std::vector<docdb::IntentKeyValueForCDC> intents;
              docdb::ApplyTransactionState new_stream_state;

              *commit_timestamp = msg->transaction_state().commit_hybrid_time();
              op_id.term = msg->id().term();
              op_id.index = msg->id().index();
              RETURN_NOT_OK(ProcessIntents(
                  op_id, txn_id, stream_metadata, enum_oid_label_map, composite_atts_map, resp,
                  &consumption, &checkpoint, tablet_peer, &intents, &new_stream_state, client,
                  cached_schema_details, msg->transaction_state().commit_hybrid_time()));

              if (new_stream_state.write_id != 0 && !new_stream_state.key.empty()) {
                pending_intents = true;
                VLOG(1) << "There are pending intents for the transaction id " << txn_id
                        << " with apply record OpId: " << op_id;
              } else {
                last_streamed_op_id->term = msg->id().term();
                last_streamed_op_id->index = msg->id().index();
              }
            }
            checkpoint_updated = true;
            break;

          case consensus::OperationType::WRITE_OP: {
            const auto& batch = msg->write().write_batch();

            *commit_timestamp = msg->hybrid_time();
            if (!batch.has_transaction()) {
              RETURN_NOT_OK(PopulateCDCSDKWriteRecord(
                  msg, stream_metadata, tablet_peer, enum_oid_label_map, composite_atts_map,
                  cached_schema_details, resp, client));

              SetCheckpoint(
                  msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
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

            SetCheckpoint(
                msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
            checkpoint_updated = true;
          } break;

          case consensus::OperationType::TRUNCATE_OP: {
            if (FLAGS_stream_truncate_record) {
              RETURN_NOT_OK(PopulateCDCSDKTruncateRecord(
                  msg, resp->add_cdc_sdk_proto_records(), current_schema));
              SetCheckpoint(
                  msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
              checkpoint_updated = true;
            }
          } break;

          case yb::consensus::OperationType::SPLIT_OP: {
            // It is possible that we found records corresponding to SPLIT_OP even when it failed.
            // We first verify if a split has indeed occured succesfully on the tablet by checking:
            // 1. There are two children tablets for the tablet
            // 2. The split op is the last operation on the tablet
            // If either of the conditions are false, we will know the splitOp is not succesfull.
            const TableId& table_id = tablet_ptr->metadata()->table_id();
            auto op_id = OpId::FromPB(msg->id());

            if (!(VerifyTabletSplitOnParentTablet(table_id, tablet_id, client))) {
              // We could verify the tablet split succeeded. This is possible when the child tablets
              // of a split are not running yet.
              LOG(INFO) << "Found SPLIT_OP record with index: " << op_id
                        << ", but did not find any children tablets for the tablet: " << tablet_id
                        << ". This is possible when the child tablets are not up and running yet.";
            } else {
              if (checkpoint_updated) {
                // If we have records which are yet to be streamed which we discovered in the same
                // 'GetChangesForCDCSDK' call, we will not update the checkpoint to the SplitOp
                // record's OpId and return the records seen till now. Next time the client will
                // call 'GetChangesForCDCSDK' with the OpId just before the SplitOp's record.
                LOG(INFO) << "Found SPLIT_OP record with OpId: " << op_id
                          << ", for parent tablet: " << tablet_id
                          << ", will stream all seen records until now.";
              } else {
                // If 'GetChangesForCDCSDK' was called with the OpId just before the SplitOp's
                // record, and if there is no more data to stream and we can notify the client
                // about the split and update the checkpoint. At this point, we will store the
                // split_op_id.
                LOG(INFO) << "Found SPLIT_OP record with OpId: " << op_id
                          << ", for parent tablet: " << tablet_id
                          << ", and if we did not see any other records we will report the tablet "
                             "split to the client";
                SetCheckpoint(op_id.term, op_id.index, 0, "", 0, &checkpoint, last_streamed_op_id);
                checkpoint_updated = true;
                split_op_id = op_id;
              }
            }
          } break;

          default:
            // Nothing to do for other operation types.
            last_seen_default_message_op_id = OpId(msg->id().term(), msg->id().index());
            VLOG_WITH_FUNC(2) << "Found message of Op type: " << msg->op_type()
                              << ", on tablet: " << tablet_id
                              << ", with OpId: " << msg->id().ShortDebugString();
            break;
        }

        if (pending_intents) {
          // Incase of pending intents use the last replicated intents commit time.
          ht_of_last_returned_message =
              HybridTime::FromPB(resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                                     .row_message()
                                     .record_time());
          break;
        } else {
          ht_of_last_returned_message = HybridTime(msg->hybrid_time());
        }
      }
      if (read_ops.messages.size() > 0) {
        *msgs_holder = consensus::ReplicateMsgsHolder(
            nullptr, std::move(read_ops.messages), std::move(consumption));
      }

      if (!checkpoint_updated && VLOG_IS_ON(1)) {
        VLOG_WITH_FUNC(1)
            << "The current batch of 'read_ops' had no actionable message. last_see_op_id: "
            << last_seen_op_id << ", last_readable_opid_index: " << *last_readable_opid_index
            << ". Will retry and get another batch";
      }

    } while (!checkpoint_updated && last_readable_opid_index &&
             last_seen_op_id.index < *last_readable_opid_index);

    // In case the checkpoint was not updated at-all, we will update the checkpoint using the last
    // seen non-actionable message.
    if (!checkpoint_updated) {
      have_more_messages = HaveMoreMessages(false);
      if (last_seen_default_message_op_id != OpId::Invalid()) {
        SetCheckpoint(
            last_seen_default_message_op_id.term, last_seen_default_message_op_id.index, 0, "", 0,
            &checkpoint, last_streamed_op_id);
        checkpoint_updated = true;
        VLOG_WITH_FUNC(2)
            << "The last batch of 'read_ops' had no actionable message"
            << ", on tablet: " << tablet_id
            << ". The checkpoint will be updated based on the last message's OpId to: "
            << last_seen_default_message_op_id;
      }
    }
  }

  // If the split_op_id is equal to the checkpoint i.e the OpId of the last actionable message, we
  // know that after the split there are no more actionable messages, and this confirms that the
  // SPLIT OP was succesfull.
  if (!snapshot_operation && split_op_id.term == checkpoint.term() &&
      split_op_id.index == checkpoint.index()) {
    report_tablet_split = true;
  }

  if (consumption) {
    consumption.Add(resp->SpaceUsedLong());
  }

  auto safe_time = GetCDCSDKSafeTimeForTarget(
      leader_safe_time.get(), ht_of_last_returned_message, have_more_messages);
  resp->set_safe_hybrid_time(safe_time.ToUint64());

  checkpoint_updated ? resp->mutable_cdc_sdk_checkpoint()->CopyFrom(checkpoint)
                     : resp->mutable_cdc_sdk_checkpoint()->CopyFrom(from_op_id);

  if (last_streamed_op_id->index > 0) {
    last_streamed_op_id->ToPB(resp->mutable_checkpoint()->mutable_op_id());
  }
  if (checkpoint_updated) {
    VLOG(1) << "The cdcsdk checkpoint is updated " << resp->cdc_sdk_checkpoint().ShortDebugString();
    VLOG(1) << "The checkpoint is updated " << resp->checkpoint().ShortDebugString();
  } else {
    VLOG(1) << "The cdcsdk checkpoint is not  updated "
            << resp->cdc_sdk_checkpoint().ShortDebugString();
    VLOG(1) << "The checkpoint is not updated " << resp->checkpoint().ShortDebugString();
  }

  if (report_tablet_split) {
    return STATUS_FORMAT(
        TabletSplit, "Tablet Split on tablet: $0, no more records to stream", tablet_id);
  }

  if (FLAGS_cdc_populate_safepoint_record) {
    RETURN_NOT_OK(PopulateCDCSDKSafepointOpRecord(
        safe_time.ToUint64(),
        tablet_peer->tablet()->metadata()->table_name(),
        resp->add_cdc_sdk_proto_records(),
        *tablet_peer->tablet()->schema().get()));
    VLOG(2) << "Added Safepoint Record";
  }

  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
