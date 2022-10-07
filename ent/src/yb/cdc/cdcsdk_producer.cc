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

#include "yb/cdc/cdc_common_util.h"

#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ql_expr.h"

#include "yb/docdb/docdb_util.h"
#include "yb/docdb/doc_key.h"

#include "yb/master/master_client.pb.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"

DEFINE_int32(cdc_snapshot_batch_size, 250, "Batch size for the snapshot operation in CDC");
TAG_FLAG(cdc_snapshot_batch_size, runtime);

DEFINE_bool(stream_truncate_record, false, "Enable streaming of TRUNCATE record");
TAG_FLAG(stream_truncate_record, runtime);

DECLARE_int64(cdc_intent_retention_ms);

DEFINE_bool(
    enable_single_record_update, true,
    "Enable packing updates corresponding to a row in single CDC record");
TAG_FLAG(enable_single_record_update, runtime);

namespace yb {
namespace cdc {

using consensus::ReplicateMsgPtr;
using consensus::ReplicateMsgs;
using docdb::PrimitiveValue;
using docdb::SchemaPackingStorage;
using tablet::TransactionParticipant;
using yb::QLTableRow;

YB_DEFINE_ENUM(OpType, (INSERT)(UPDATE)(DELETE));

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
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const ColumnSchema& col_schema,
    const Value& col,
    const EnumOidLabelMap& enum_oid_label_map,
    DatumMessagePB* cdc_datum_message) {
  cdc_datum_message->set_column_name(col_schema.name());
  QLValuePB ql_value;
  if (tablet_peer->tablet()->table_type() == PGSQL_TABLE_TYPE) {
    col.ToQLValuePB(col_schema.type(), &ql_value);
    if (!IsNull(ql_value) && col_schema.pg_type_oid() != 0 /*kInvalidOid*/) {
      RETURN_NOT_OK(docdb::SetValueFromQLBinaryWrapper(
          ql_value, col_schema.pg_type_oid(), enum_oid_label_map, cdc_datum_message));
    } else {
      cdc_datum_message->set_column_type(col_schema.pg_type_oid());
    }
  }
  return Status::OK();
}

DatumMessagePB* AddTuple(RowMessage* row_message) {
  if (!row_message) {
    return nullptr;
  }
  DatumMessagePB* tuple = nullptr;

  if (row_message->op() == RowMessage_Op_DELETE) {
    tuple = row_message->add_old_tuple();
    row_message->add_new_tuple();
  } else {
    tuple = row_message->add_new_tuple();
    row_message->add_old_tuple();
  }
  return tuple;
}

Status AddPrimaryKey(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const docdb::SubDocKey& decoded_key,
    const Schema& tablet_schema, const EnumOidLabelMap& enum_oid_label_map,
    RowMessage* row_message) {
  size_t i = 0;
  for (const auto& col : decoded_key.doc_key().hashed_group()) {
    DatumMessagePB* tuple = AddTuple(row_message);
    RETURN_NOT_OK(
        AddColumnToMap(tablet_peer, tablet_schema.column(i), col, enum_oid_label_map, tuple));
    i++;
  }

  for (const auto& col : decoded_key.doc_key().range_group()) {
    DatumMessagePB* tuple = AddTuple(row_message);
    RETURN_NOT_OK(
        AddColumnToMap(tablet_peer, tablet_schema.column(i), col, enum_oid_label_map, tuple));
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
  return row_message.IsInitialized()  &&
         (row_message.op() == RowMessage_Op_INSERT
          || row_message.op() == RowMessage_Op_UPDATE);
}

void MakeNewProtoRecord(
    const docdb::IntentKeyValueForCDC& intent, const OpId& op_id, const RowMessage& row_message,
    const Schema& schema, size_t col_count, CDCSDKProtoRecordPB* proto_record,
    GetChangesResponsePB* resp, IntraTxnWriteId* write_id, std::string* reverse_index_key) {
  CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
  SetCDCSDKOpId(
      op_id.term, op_id.index, intent.write_id, intent.reverse_index_key, cdc_sdk_op_id_pb);

  Slice doc_ht(intent.ht_buf);

  CDCSDKProtoRecordPB* record_to_be_added = resp->add_cdc_sdk_proto_records();
  record_to_be_added->CopyFrom(*proto_record);
  record_to_be_added->mutable_row_message()->CopyFrom(row_message);
  auto result = DocHybridTime::DecodeFromEnd(&doc_ht);
  if (result.ok()) {
    record_to_be_added->mutable_row_message()->set_commit_time((*result).hybrid_time().value());
  } else {
    LOG(WARNING) << "Failed to get commit hybrid time for intent key: " << intent.key_buf.c_str();
  }
  *write_id = intent.write_id;
  *reverse_index_key = intent.reverse_index_key;
}

Result<size_t> PopulatePackedRows(
    const SchemaPackingStorage& schema_packing_storage, const Schema& schema,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map, Slice* value_slice, RowMessage* row_message) {
  const docdb::SchemaPacking& packing =
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
    RETURN_NOT_OK(
        AddColumnToMap(tablet_peer, col, pv, enum_oid_label_map, row_message->add_new_tuple()));
    row_message->add_old_tuple();
  }

  return packing.columns();
}

// Populate CDC record corresponding to WAL batch in ReplicateMsg.
Status PopulateCDCSDKIntentRecord(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const std::vector<docdb::IntentKeyValueForCDC>& intents,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    IntraTxnWriteId* write_id,
    std::string* reverse_index_key,
    Schema* old_schema) {
  bool colocated = tablet_peer->tablet()->metadata()->colocated();
  Schema& schema = old_schema ? *old_schema : *tablet_peer->tablet()->schema();
  SchemaVersion schema_version = tablet_peer->tablet()->metadata()->schema_version();
  std::string table_name = tablet_peer->tablet()->metadata()->table_name();
  SchemaPackingStorage schema_packing_storage;
  schema_packing_storage.AddSchema(schema_version, schema);
  Slice prev_key;
  CDCSDKProtoRecordPB proto_record;
  RowMessage* row_message = proto_record.mutable_row_message();
  size_t col_count = 0;
  docdb::IntentKeyValueForCDC prev_intent;
  MicrosTime prev_intent_phy_time = 0;
  bool new_cdc_record_needed = false;

  for (const auto& intent : intents) {
    Slice key(intent.key_buf);
    const auto key_size =
        VERIFY_RESULT(docdb::DocKey::EncodedSize(key, docdb::DocKeyPart::kWholeDocKey));

    docdb::KeyEntryValue column_id;
    boost::optional<docdb::KeyEntryValue> column_id_opt;
    Slice key_column = key.WithoutPrefix(key_size);
    if (!key_column.empty()) {
      RETURN_NOT_OK(docdb::KeyEntryValue::DecodeKey(&key_column, &column_id));
      column_id_opt = column_id;
    }

    Slice sub_doc_key = key;
    docdb::SubDocKey decoded_key;
    RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, docdb::HybridTimeRequired::kFalse));

    Slice value_slice = intent.value_buf;
    RETURN_NOT_OK(docdb::ValueControlFields::Decode(&value_slice));
    auto value_type = docdb::DecodeValueEntryType(value_slice);
    value_slice.consume_byte();

    if (column_id_opt && column_id_opt->type() == docdb::KeyEntryType::kColumnId &&
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
          (value_type == docdb::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) ||
          prev_intent_phy_time != intent.intent_ht.hybrid_time().GetPhysicalValueMicros();
    } else {
      new_cdc_record_needed = (prev_key != primary_key) || (col_count >= schema.num_columns());
    }

    if (new_cdc_record_needed) {
      if (FLAGS_enable_single_record_update) {
        if (col_count > 0) col_count = 0;

        if (proto_record.IsInitialized() && row_message->IsInitialized() &&
            row_message->op() == RowMessage_Op_UPDATE) {
          MakeNewProtoRecord(
              prev_intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
              reverse_index_key);
        }
      }

      proto_record.Clear();
      row_message->Clear();

      if (colocated) {
        auto colocation_id = decoded_key.doc_key().colocation_id();
        schema = *tablet_peer->tablet()->metadata()->schema("", colocation_id);
        schema_version = tablet_peer->tablet()->metadata()->schema_version("", colocation_id);
        table_name = tablet_peer->tablet()->metadata()->table_name("", colocation_id);
        schema_packing_storage = SchemaPackingStorage();
        schema_packing_storage.AddSchema(schema_version, schema);
      }

      // Check whether operation is WRITE or DELETE.
      if (value_type == docdb::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        SetOperation(row_message, OpType::DELETE, schema);
        if (!FLAGS_enable_single_record_update) {
          col_count = schema.num_columns();
        }
      } else if (value_type == docdb::ValueEntryType::kPackedRow) {
        SetOperation(row_message, OpType::INSERT, schema);
        col_count = schema.num_key_columns();
      } else {
        if (column_id_opt && column_id_opt->type() == docdb::KeyEntryType::kSystemColumnId &&
            value_type == docdb::ValueEntryType::kNullLow) {
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
      RETURN_NOT_OK(
          AddPrimaryKey(tablet_peer, decoded_key, schema, enum_oid_label_map, row_message));
    }

    prev_key = primary_key;
    prev_intent_phy_time = intent.intent_ht.hybrid_time().GetPhysicalValueMicros();
    if (IsInsertOrUpdate(*row_message)) {
      if (value_type == docdb::ValueEntryType::kPackedRow) {
        col_count += VERIFY_RESULT(PopulatePackedRows(
            schema_packing_storage, schema, tablet_peer, enum_oid_label_map, &value_slice,
            row_message));
      } else {
        if (FLAGS_enable_single_record_update) {
          ++col_count;
        } else {
          if (IsInsertOperation(*row_message)) {
            ++col_count;
          }
        }

        docdb::Value decoded_value;
        RETURN_NOT_OK(decoded_value.Decode(intent.value_buf));

        if (column_id_opt && column_id_opt->type() == docdb::KeyEntryType::kColumnId) {
          const ColumnSchema& col =
              VERIFY_RESULT(schema.column_by_id(column_id_opt->GetColumnId()));

          RETURN_NOT_OK(AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), enum_oid_label_map,
              row_message->add_new_tuple()));
          row_message->add_old_tuple();

        } else if (column_id_opt && column_id_opt->type() != docdb::KeyEntryType::kSystemColumnId) {
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
            reverse_index_key);
        col_count = schema.num_columns();
      } else if (row_message->op() == RowMessage_Op_UPDATE) {
        prev_intent = intent;
      }
    } else {
      if ((row_message->op() == RowMessage_Op_INSERT && col_count == schema.num_columns()) ||
          (row_message->op() == RowMessage_Op_UPDATE ||
           row_message->op() == RowMessage_Op_DELETE)) {
        MakeNewProtoRecord(
            intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
            reverse_index_key);
      }
    }
  }

  if (FLAGS_enable_single_record_update && proto_record.IsInitialized() &&
      row_message->IsInitialized() && row_message->op() == RowMessage_Op_UPDATE) {
    row_message->set_table(table_name);
    MakeNewProtoRecord(
        prev_intent, op_id, *row_message, schema, col_count, &proto_record, resp, write_id,
        reverse_index_key);
  }

  return Status::OK();
}

// Populate CDC record corresponding to WAL batch in ReplicateMsg.
Status PopulateCDCSDKWriteRecord(
    const ReplicateMsgPtr& msg,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const EnumOidLabelMap& enum_oid_label_map,
    GetChangesResponsePB* resp,
    const Schema& current_schema) {
  const auto& batch = msg->write().write_batch();
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;
  bool colocated = tablet_peer->tablet()->metadata()->colocated();
  // Write batch may contain records from different rows.
  // For CDC, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;
  Schema schema = current_schema;
  SchemaVersion schema_version = tablet_peer->tablet()->metadata()->schema_version();
  std::string table_name = tablet_peer->tablet()->metadata()->table_name();
  SchemaPackingStorage schema_packing_storage;
  schema_packing_storage.AddSchema(schema_version, schema);
  // TODO: This function and PopulateCDCSDKIntentRecord have a lot of code in common. They should
  // be refactored to use some common row-column iterator.
  for (const auto& write_pair : batch.write_pairs()) {
    Slice key = write_pair.key();
    const auto key_size =
        VERIFY_RESULT(docdb::DocKey::EncodedSize(key, docdb::DocKeyPart::kWholeDocKey));

    Slice value_slice = write_pair.value();
    RETURN_NOT_OK(docdb::ValueControlFields::Decode(&value_slice));
    auto value_type = docdb::DecodeValueEntryType(value_slice);
    value_slice.consume_byte();

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice primary_key(key.data(), key_size);
    if (prev_key != primary_key) {
      Slice sub_doc_key = key;
      docdb::SubDocKey decoded_key;
      RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, docdb::HybridTimeRequired::kFalse));
      if (colocated) {
        auto colocation_id = decoded_key.doc_key().colocation_id();
        schema = *tablet_peer->tablet()->metadata()->schema("", colocation_id);
        schema_version = tablet_peer->tablet()->metadata()->schema_version("", colocation_id);
        table_name = tablet_peer->tablet()->metadata()->table_name("", colocation_id);
        schema_packing_storage = SchemaPackingStorage();
        schema_packing_storage.AddSchema(schema_version, schema);
      }

      // Write pair contains record for different row. Create a new CDCRecord in this case.
      proto_record = resp->add_cdc_sdk_proto_records();
      row_message = proto_record->mutable_row_message();
      row_message->set_pgschema_name(schema.SchemaName());
      row_message->set_table(table_name);
      CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
      SetCDCSDKOpId(msg->id().term(), msg->id().index(), 0, "", cdc_sdk_op_id_pb);
      // Check whether operation is WRITE or DELETE.
      if (value_type == docdb::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        SetOperation(row_message, OpType::DELETE, schema);
      } else if (value_type == docdb::ValueEntryType::kPackedRow) {
        SetOperation(row_message, OpType::INSERT, schema);
      } else {
        docdb::KeyEntryValue column_id;
        Slice key_column(key.WithoutPrefix(key_size));
        RETURN_NOT_OK(docdb::KeyEntryValue::DecodeKey(&key_column, &column_id));

        if (column_id.type() == docdb::KeyEntryType::kSystemColumnId &&
            value_type == docdb::ValueEntryType::kNullLow) {
          SetOperation(row_message, OpType::INSERT, schema);
        } else {
          SetOperation(row_message, OpType::UPDATE, schema);
        }
      }

      RETURN_NOT_OK(
          AddPrimaryKey(tablet_peer, decoded_key, schema, enum_oid_label_map, row_message));
      // Process intent records.
      row_message->set_commit_time(msg->hybrid_time());
    }
    prev_key = primary_key;
    DCHECK(proto_record);

    if (IsInsertOrUpdate(*row_message)) {
      if (value_type == docdb::ValueEntryType::kPackedRow) {
        RETURN_NOT_OK(PopulatePackedRows(
            schema_packing_storage, schema, tablet_peer, enum_oid_label_map, &value_slice,
            row_message));
      } else {
        docdb::KeyEntryValue column_id;
        Slice key_column = key.WithoutPrefix(key_size);
        RETURN_NOT_OK(docdb::KeyEntryValue::DecodeKey(&key_column, &column_id));
        if (column_id.type() == docdb::KeyEntryType::kColumnId) {
          const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_id.GetColumnId()));
          docdb::Value decoded_value;
          RETURN_NOT_OK(decoded_value.Decode(write_pair.value()));

          RETURN_NOT_OK(AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), enum_oid_label_map,
              row_message->add_new_tuple()));
          row_message->add_old_tuple();

        } else if (column_id.type() != docdb::KeyEntryType::kSystemColumnId) {
          LOG(DFATAL) << "Unexpected value type in key: " << column_id.type();
        }
      }
    }
  }

  return Status::OK();
}

void SetTableProperties(
    const TablePropertiesPB* table_properties,
    CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb) {
  cdc_sdk_table_properties_pb->set_default_time_to_live(table_properties->default_time_to_live());
  cdc_sdk_table_properties_pb->set_num_tablets(table_properties->num_tablets());
  cdc_sdk_table_properties_pb->set_is_ysql_catalog_table(table_properties->is_ysql_catalog_table());
}

void SetColumnInfo(const ColumnSchemaPB& column, CDCSDKColumnInfoPB* column_info) {
  column_info->set_name(column.name());
  column_info->mutable_type()->CopyFrom(column.type());
  column_info->set_is_key(column.is_key());
  column_info->set_is_hash_key(column.is_hash_key());
  column_info->set_is_nullable(column.is_nullable());
  column_info->set_oid(column.pg_type_oid());
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
    CDCSDKColumnInfoPB* column_info = nullptr;
    column_info = row_message->mutable_schema()->add_column_info();
    SetColumnInfo(column, column_info);
  }

  CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb;
  const TablePropertiesPB* table_properties =
      &(msg->change_metadata_request().schema().table_properties());

  cdc_sdk_table_properties_pb = row_message->mutable_schema()->mutable_tab_info();
  row_message->set_schema_version(msg->change_metadata_request().schema_version());
  row_message->set_new_table_name(msg->change_metadata_request().new_table_name());
  row_message->set_pgschema_name(schema.SchemaName());
  SetTableProperties(table_properties, cdc_sdk_table_properties_pb);

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

Status ProcessIntents(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const StreamMetadata& metadata,
    const EnumOidLabelMap& enum_oid_label_map,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    CDCSDKCheckpointPB* checkpoint,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    std::vector<docdb::IntentKeyValueForCDC>* keyValueIntents,
    docdb::ApplyTransactionState* stream_state,
    Schema* schema) {
  if (stream_state->key.empty() && stream_state->write_id == 0) {
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();
    row_message->set_op(RowMessage_Op_BEGIN);
    row_message->set_transaction_id(transaction_id.ToString());
    row_message->set_table(tablet_peer->tablet()->metadata()->table_name());
  }

  auto tablet = tablet_peer->shared_tablet();
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
    docdb::SubDocKey sub_doc_key;
    CHECK_OK(
        sub_doc_key.FullyDecodeFrom(Slice(keyValue.key_buf), docdb::HybridTimeRequired::kFalse));

    Slice value_slice = keyValue.value_buf;
    RETURN_NOT_OK(docdb::ValueControlFields::Decode(&value_slice));
    auto value_type = docdb::DecodeValueEntryType(value_slice);
    if (value_type != docdb::ValueEntryType::kPackedRow) {
      docdb::Value decoded_value;
      RETURN_NOT_OK(decoded_value.Decode(Slice(keyValue.value_buf)));
    }
  }

  std::string reverse_index_key;
  IntraTxnWriteId write_id = 0;

  // Need to populate the CDCSDKRecords
  RETURN_NOT_OK(PopulateCDCSDKIntentRecord(
      op_id, transaction_id, *keyValueIntents, metadata, tablet_peer, enum_oid_label_map, resp,
      consumption, &write_id, &reverse_index_key, schema));

  SetTermIndex(op_id.term, op_id.index, checkpoint);

  if (stream_state->key.empty() && stream_state->write_id == 0) {
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();

    row_message->set_op(RowMessage_Op_COMMIT);
    row_message->set_transaction_id(transaction_id.ToString());
    row_message->set_table(tablet_peer->tablet()->metadata()->table_name());

    CDCSDKOpIdPB* cdc_sdk_op_id_pb = proto_record->mutable_cdc_sdk_op_id();
    SetCDCSDKOpId(op_id.term, op_id.index, 0, "", cdc_sdk_op_id_pb);
    SetKeyWriteId("", 0, checkpoint);
  } else {
    SetKeyWriteId(reverse_index_key, write_id, checkpoint);
  }

  return Status::OK();
}

Status PopulateCDCSDKSnapshotRecord(
    GetChangesResponsePB* resp,
    const QLTableRow* row,
    const Schema& schema,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    ReadHybridTime time,
    const EnumOidLabelMap& enum_oid_label_map) {
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;
  const std::string& table_name = tablet_peer->tablet()->metadata()->table_name();

  proto_record = resp->add_cdc_sdk_proto_records();
  row_message = proto_record->mutable_row_message();
  row_message->set_table(table_name);
  row_message->set_op(RowMessage_Op_READ);
  row_message->set_pgschema_name(schema.SchemaName());
  row_message->set_commit_time(time.read.ToUint64());

  DatumMessagePB* cdc_datum_message = nullptr;

  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    ColumnId col_id = schema.column_id(col_idx);
    const auto* value = row->GetColumn(col_id);
    const ColumnSchema& col_schema = VERIFY_RESULT(schema.column_by_id(col_id));

    cdc_datum_message = row_message->add_new_tuple();
    cdc_datum_message->set_column_name(col_schema.name());

    if (value && value->value_case() != QLValuePB::VALUE_NOT_SET
        && col_schema.pg_type_oid() != 0 /*kInvalidOid*/) {
      RETURN_NOT_OK(docdb::SetValueFromQLBinaryWrapper(
          *value, col_schema.pg_type_oid(), enum_oid_label_map, cdc_datum_message));
    } else {
      cdc_datum_message->set_column_type(col_schema.pg_type_oid());
    }

    row_message->add_old_tuple();
  }

  return Status::OK();
}

void FillDDLInfo(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, GetChangesResponsePB* resp) {
  for (auto const& table_id : tablet_peer->tablet_metadata()->GetAllColocatedTables()) {
    auto table_name = tablet_peer->tablet()->metadata()->table_name(table_id);
    auto schema_version = tablet_peer->tablet()->metadata()->schema_version(table_id);
    Schema schema = *tablet_peer->tablet()->metadata()->schema(table_id).get();
    SchemaPB schema_pb;
    SchemaToPB(schema, &schema_pb);
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

    const TablePropertiesPB* table_properties = &(schema_pb.table_properties());
    SetTableProperties(table_properties, cdc_sdk_table_properties_pb);
  }
}

bool VerifyTabletSplitOnParentTablet(
    const TableId& table_id, const TabletId& tablet_id,
    const std::shared_ptr<yb::consensus::ReplicateMsg>& msg, client::YBClient* client) {
  if (!(msg->has_split_request() && msg->split_request().has_tablet_id() &&
        msg->split_request().tablet_id() == tablet_id)) {
    LOG(WARNING) << "The replicate message for split-op does not have the parent tablet_id set to: "
                 << tablet_id << ". Could not verify tablet-split for tablet: " << tablet_id;
    return false;
  }

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

// CDC get changes is different from 2DC as it doesn't need
// to read intents from WAL.

Status GetChangesForCDCSDK(
    const CDCStreamId& stream_id,
    const TabletId& tablet_id,
    const CDCSDKCheckpointPB& from_op_id,
    const StreamMetadata& stream_metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const MemTrackerPtr& mem_tracker,
    const EnumOidLabelMap& enum_oid_label_map,
    client::YBClient* client,
    consensus::ReplicateMsgsHolder* msgs_holder,
    GetChangesResponsePB* resp,
    std::string* commit_timestamp,
    std::shared_ptr<Schema>* cached_schema,
    OpId* last_streamed_op_id,
    int64_t* last_readable_opid_index,
    const CoarseTimePoint deadline) {
  OpId op_id{from_op_id.term(), from_op_id.index()};
  VLOG(1) << "The from_op_id from GetChanges is  " << op_id;
  ScopedTrackedConsumption consumption;
  CDCSDKCheckpointPB checkpoint;
  bool checkpoint_updated = false;
  bool report_tablet_split = false;
  OpId split_op_id = OpId::Invalid();

  // It is snapshot call.
  if (from_op_id.write_id() == -1) {
    auto txn_participant = tablet_peer->tablet()->transaction_participant();
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
      txn_participant->SetIntentRetainOpIdAndTime(
          data.op_id, MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms)));
      RETURN_NOT_OK(txn_participant->context()->GetLastReplicatedData(&data));
      time = ReadHybridTime::SingleTime(data.log_ht);

      // This should go to cdc_state table.
      // Below condition update the checkpoint in cdc_state table.
      SetCheckpoint(
          data.op_id.term, data.op_id.index, -1, "", time.read.ToUint64(), &checkpoint, nullptr);
      checkpoint_updated = true;
    } else {
      // Snapshot is already taken.
      HybridTime ht;
      time = ReadHybridTime::FromUint64(from_op_id.snapshot_time());
      nextKey = from_op_id.key();
      VLOG(1) << "The after snapshot term " << from_op_id.term() << "index  " << from_op_id.index()
              << "key " << from_op_id.key() << "snapshot time " << from_op_id.snapshot_time();

      FillDDLInfo(tablet_peer, resp);
      Schema schema = *tablet_peer->tablet()->metadata()->schema().get();

      int limit = FLAGS_cdc_snapshot_batch_size;
      int fetched = 0;
      std::vector<QLTableRow> rows;
      QLTableRow row;
      auto iter = VERIFY_RESULT(tablet_peer->tablet()->CreateCDCSnapshotIterator(
          schema.CopyWithoutColumnIds(), time, nextKey));
      while (VERIFY_RESULT(iter->HasNext()) && fetched < limit) {
        RETURN_NOT_OK(iter->NextRow(&row));
        RETURN_NOT_OK(PopulateCDCSDKSnapshotRecord(
            resp, &row, schema, tablet_peer, time, enum_oid_label_map));
        fetched++;
      }
      docdb::SubDocKey sub_doc_key;
      RETURN_NOT_OK(iter->GetNextReadSubDocKey(&sub_doc_key));

      // Snapshot ends when next key is empty.
      if (sub_doc_key.doc_key().empty()) {
        VLOG(1) << "Setting next sub doc key empty ";
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

    RETURN_NOT_OK(reverse_index_key_slice.consume_byte(docdb::KeyEntryTypeAsChar::kTransactionId));
    auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&reverse_index_key_slice));

    RETURN_NOT_OK(ProcessIntents(
        op_id, transaction_id, stream_metadata, enum_oid_label_map, resp, &consumption, &checkpoint,
        tablet_peer, &keyValueIntents, &stream_state, nullptr));

    if (checkpoint.write_id() == 0 && checkpoint.key().empty()) {
      last_streamed_op_id->term = checkpoint.term();
      last_streamed_op_id->index = checkpoint.index();
    }
    checkpoint_updated = true;
  } else {
    RequestScope request_scope;
    OpId last_seen_op_id = op_id;

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

      auto txn_participant = tablet_peer->tablet()->transaction_participant();
      if (txn_participant) {
        request_scope = VERIFY_RESULT(RequestScope::Create(txn_participant));
      }

      Schema current_schema;
      bool pending_intents = false;
      bool schema_streamed = false;

      if (read_ops.messages.empty()) {
        VLOG_WITH_FUNC(1) << "Did not get any messages with current batch of 'read_ops'."
                          << "last_seen_op_id: " << last_seen_op_id << ", last_readable_opid_index "
                          << *last_readable_opid_index;
        break;
      }

      for (const auto& msg : read_ops.messages) {
        last_seen_op_id.term = msg->id().term();
        last_seen_op_id.index = msg->id().index();

        if (!schema_streamed && !(**cached_schema).initialized()) {
          current_schema.CopyFrom(*tablet_peer->tablet()->schema().get());
          schema_streamed = true;
          *cached_schema = std::make_shared<Schema>(std::move(current_schema));
          FillDDLInfo(tablet_peer, resp);
        } else {
          current_schema = **cached_schema;
        }

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
                  op_id, txn_id, stream_metadata, enum_oid_label_map, resp, &consumption,
                  &checkpoint, tablet_peer, &intents, &new_stream_state, &current_schema));

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

            if (!batch.has_transaction()) {
              RETURN_NOT_OK(PopulateCDCSDKWriteRecord(
                  msg, stream_metadata, tablet_peer, enum_oid_label_map, resp, current_schema));

              SetCheckpoint(
                  msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
              checkpoint_updated = true;
            }
          }
          break;

          case consensus::OperationType::CHANGE_METADATA_OP: {
            RETURN_NOT_OK(SchemaFromPB(msg->change_metadata_request().schema(), &current_schema));
            const std::string& table_name = tablet_peer->tablet()->metadata()->table_name();
            *cached_schema = std::make_shared<Schema>(std::move(current_schema));
            if ((resp->cdc_sdk_proto_records_size() > 0 &&
                 resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                         .row_message()
                         .op() == RowMessage_Op_DDL)) {
              if ((resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                       .row_message()
                       .schema_version() != msg->change_metadata_request().schema_version())) {
                RETURN_NOT_OK(PopulateCDCSDKDDLRecord(
                    msg, resp->add_cdc_sdk_proto_records(), table_name, current_schema));
              }
            } else {
              RETURN_NOT_OK(PopulateCDCSDKDDLRecord(
                  msg, resp->add_cdc_sdk_proto_records(), table_name, current_schema));
            }
            SetCheckpoint(
                msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
            checkpoint_updated = true;
          }
          break;

          case consensus::OperationType::TRUNCATE_OP: {
            if (FLAGS_stream_truncate_record) {
              RETURN_NOT_OK(PopulateCDCSDKTruncateRecord(
                  msg, resp->add_cdc_sdk_proto_records(), current_schema));
              SetCheckpoint(
                  msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
              checkpoint_updated = true;
            }
          }
          break;

          case yb::consensus::OperationType::SPLIT_OP: {
            // It is possible that we found records corresponding to SPLIT_OP even when it failed.
            // We first verify if a split has indeed occured succesfully on the tablet by checking:
            // 1. There are two children tablets for the tablet
            // 2. The split op is the last operation on the tablet
            // If either of the conditions are false, we will know the splitOp is not succesfull.
            const TableId& table_id = tablet_peer->tablet()->metadata()->table_id();

            if (!(VerifyTabletSplitOnParentTablet(table_id, tablet_id, msg, client))) {
              SetCheckpoint(
                  msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint, last_streamed_op_id);
              checkpoint_updated = true;
              LOG_WITH_FUNC(WARNING)
                  << "Found SplitOp record with index: " << msg->id()
                  << ", but did not find any children tablets for the parent tablet: " << tablet_id;
            } else {
              if (checkpoint_updated) {
                // If we have records which are yet to be streamed which we discovered in the same
                // 'GetChangesForCDCSDK' call, we will not update the checkpoint to the SplitOp
                // record's OpId and return the records seen till now. Next time the client will
                // call 'GetChangesForCDCSDK' with the OpId just before the SplitOp's record.
                LOG(INFO) << "Found SPLIT_OP record with OpId: " << msg->id()
                          << ", will stream all seen records until now.";
              } else {
                // If 'GetChangesForCDCSDK' was called with the OpId just before the SplitOp's
                // record, and if there is no more data to stream and we can notify the client
                // about the split and update the checkpoint. At this point, we will store the
                // split_op_id.
                LOG(INFO) << "Found SPLIT_OP record with OpId: " << msg->id()
                          << ", and if we did not see any other records we will report the tablet "
                             "split to the client";
                SetCheckpoint(
                    msg->id().term(), msg->id().index(), 0, "", 0, &checkpoint,
                    last_streamed_op_id);
                checkpoint_updated = true;
                split_op_id = OpId::FromPB(msg->id());
              }
            }
          }
          break;

          default:
            // Nothing to do for other operation types.
            break;
        }

        if (pending_intents) break;
      }
      if (read_ops.messages.size() > 0) {
        *msgs_holder = consensus::ReplicateMsgsHolder(
            nullptr, std::move(read_ops.messages), std::move(consumption));
      }

      if (!checkpoint_updated) {
        LOG_WITH_FUNC(INFO)
            << "The last batch of 'read_ops' had no actionable message. last_see_op_id: "
            << last_seen_op_id << ", last_readable_opid_index: " << *last_readable_opid_index
            << ". Will retry and get another batch";
      }
    } while (!checkpoint_updated && last_readable_opid_index &&
             last_seen_op_id.index < *last_readable_opid_index);
  }

  // If the split_op_id is equal to the checkpoint i.e the OpId of the last actionable message, we
  // know that after the split there are no more actionable messages, and this confirms that the
  // SPLIT OP was succesfull.
  if (split_op_id.term == checkpoint.term() && split_op_id.index == checkpoint.index()) {
    report_tablet_split = true;
  }

  if (consumption) {
    consumption.Add(resp->SpaceUsedLong());
  }

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

  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
