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

#include "yb/common/wire_protocol.h"
#include "yb/common/ql_expr.h"

#include "yb/docdb/docdb_util.h"
#include "yb/docdb/doc_key.h"

DEFINE_int32(cdc_snapshot_batch_size, 250, "Batch size for the snapshot operation in CDC");

namespace yb {
namespace cdc {

using consensus::ReplicateMsgPtr;
using consensus::ReplicateMsgs;
using docdb::PrimitiveValue;
using tablet::TransactionParticipant;
using yb::QLTableRow;

enum OpType {
  INSERT,
  UPDATE,
  DELETE
};

void SetOperation(
    CDCSDKRecordPB* record, cdc::RowMessage* row_message, OpType type, bool is_proto_record) {
  switch (type) {
    case INSERT:
      if (!is_proto_record)
        record->set_operation(CDCSDKRecordPB::INSERT);
      else
        row_message->set_op(RowMessage_Op_INSERT);
      break;
    case UPDATE:
      if (!is_proto_record)
        record->set_operation(CDCSDKRecordPB::UPDATE);
      else
        row_message->set_op(RowMessage_Op_UPDATE);
      break;
    case DELETE:
      if (!is_proto_record)
        record->set_operation(CDCSDKRecordPB::DELETE);
      else
        row_message->set_op(RowMessage_Op_DELETE);
      break;
  }
}

void AddColumnToMap(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const ColumnSchema& col_schema,
    const docdb::PrimitiveValue& col,
    cdc::KeyValuePairPB* kv_pair,
    DatumMessagePB* cdc_datum_message) {
  bool is_proto_record = (kv_pair == nullptr);
  QLValuePB tempPB;
  (is_proto_record) ? cdc_datum_message->set_column_name(col_schema.name())
                  : kv_pair->set_key(col_schema.name());

  if (tablet_peer->tablet()->table_type() == PGSQL_TABLE_TYPE) {
    docdb::PrimitiveValue::ToQLValuePB(col, col_schema.type(), &tempPB);
    if (!IsNull(tempPB)) {
      docdb::SetValueFromQLBinaryWrapper(
          tempPB, col_schema.pg_type_oid(), is_proto_record,
          (!is_proto_record) ? kv_pair->mutable_value() : nullptr, cdc_datum_message);
    } else {
      if (is_proto_record) cdc_datum_message->set_column_type(col_schema.pg_type_oid());
    }
  } else {
    PrimitiveValue::ToQLValuePB(col, col_schema.type(), kv_pair->mutable_value());
  }
}

inline DatumMessagePB* AddTuple(cdc::RowMessage* row_message) {
  DatumMessagePB* tuple = nullptr;

  if (row_message) {
    if (row_message->op() == RowMessage_Op_DELETE) {
      tuple = row_message->add_old_tuple();
      row_message->add_new_tuple();
    } else {
      tuple = row_message->add_new_tuple();
      row_message->add_old_tuple();
    }
  }

  return tuple;
}

void AddPrimaryKey(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const docdb::SubDocKey& decoded_key,
    const Schema& tablet_schema, CDCSDKRecordPB* record, cdc::RowMessage* row_message) {
  size_t i = 0;

  for (const auto& col : decoded_key.doc_key().hashed_group()) {
    DatumMessagePB* tuple = AddTuple(row_message);

    AddColumnToMap(
        tablet_peer, tablet_schema.column(i), col, record ? record->add_key() : nullptr, tuple);
    i++;
  }
  for (const auto& col : decoded_key.doc_key().range_group()) {
    DatumMessagePB* tuple = AddTuple(row_message);

    AddColumnToMap(
        tablet_peer, tablet_schema.column(i), col, record ? record->add_key() : nullptr, tuple);
    i++;
  }
}

inline void SetCDCSDKOpId(
    CDCSDKOpIdPB* cdc_sdk_op_id_pb, int64_t term, int64_t index, uint32_t write_id,
    const std::string& key) {
  cdc_sdk_op_id_pb->set_term(term);
  cdc_sdk_op_id_pb->set_index(index);
  cdc_sdk_op_id_pb->set_write_id(write_id);
  cdc_sdk_op_id_pb->set_write_id_key(key);
}

inline void SetCheckpoint(
    CDCSDKCheckpointPB* cdc_sdk_checkpoint_pb, OpId* last_streamed_op_id, int64_t term,
    int64_t index, int32 write_id, const std::string& key, uint64 time = 0) {
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

bool CreateNewRecord(CDCSDKRecordPB* record, const Schema& schema, size_t col_count) {
  return (record->operation() == CDCSDKRecordPB::INSERT && col_count == schema.num_columns()) ||
         (record->operation() == CDCSDKRecordPB::UPDATE) ||
         (record->operation() == CDCSDKRecordPB::DELETE);
}

bool CreateNewProtoRecord(cdc::RowMessage* row_message, const Schema& schema, size_t col_count) {
  return (row_message->op() == RowMessage_Op_INSERT && col_count == schema.num_columns()) ||
         (row_message->op() == RowMessage_Op_UPDATE) ||
         (row_message->op() == RowMessage_Op_DELETE);
}

bool IsInsertOperation(CDCSDKRecordPB* record, RowMessage* row_message) {
  return (record->operation() == CDCSDKRecordPB::INSERT) ||
         (row_message->op() == RowMessage_Op_INSERT);
}

bool IsInsertOrUpdate(CDCSDKRecordPB* record, cdc::RowMessage* row_message, bool is_proto_record) {
  return (!is_proto_record && ((record->operation() == CDCSDKRecordPB::INSERT) ||
                               (record->operation() == CDCSDKRecordPB::UPDATE))) ||
         ((row_message->op() == RowMessage_Op_INSERT) ||
          (row_message->op() == RowMessage_Op_UPDATE));
}
// Populate CDC record corresponding to WAL batch in ReplicateMsg.

CHECKED_STATUS PopulateCDCSDKIntentRecord(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const std::vector<docdb::IntentKeyValueForCDC>* intents,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    IntraTxnWriteId* write_id,
    std::string* reverse_index_key,
    Schema* oldSchema,
    bool is_proto_record) {
  Schema schema = oldSchema ? (*oldSchema) : (*tablet_peer->tablet()->schema());
  Slice prev_key;
  CDCSDKRecordPB record;
  CDCSDKProtoRecordPB proto_record;
  cdc::RowMessage* row_message = proto_record.mutable_row_message();
  size_t col_count = 0;

  for (const auto& intent : *intents) {
    Slice key(intent.key_buf);
    Slice value(intent.value_buf);
    const auto key_size =
        VERIFY_RESULT(docdb::DocKey::EncodedSize(key, docdb::DocKeyPart::kWholeDocKey));

    docdb::PrimitiveValue column_id;
    bool isColumnIDInitialized = false;
    Slice key_column((const char*)(key.data() + key_size));
    if (!key_column.empty()) {
      isColumnIDInitialized = true;
      RETURN_NOT_OK(docdb::SubDocument::DecodeKey(&key_column, &column_id));
    }

    Slice sub_doc_key = key;
    docdb::SubDocKey decoded_key;
    RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, docdb::HybridTimeRequired::kFalse));

    docdb::Value decoded_value;
    RETURN_NOT_OK(decoded_value.Decode(value));

    if (isColumnIDInitialized && column_id.value_type() == docdb::ValueType::kColumnId &&
        schema.is_key_column(column_id.GetColumnId())) {
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
    if (prev_key != primary_key || col_count >= schema.num_columns()) {
      if (!is_proto_record) {
        record.Clear();
      } else {
        proto_record.Clear();
        row_message->Clear();
      }

      // Check whether operation is WRITE or DELETE.
      if (decoded_value.value_type() == docdb::ValueType::kTombstone &&
          decoded_key.num_subkeys() == 0) {
        SetOperation(&record, row_message, DELETE, is_proto_record);
        *write_id = intent.write_id;
      } else {
        if (isColumnIDInitialized && column_id.value_type() == docdb::ValueType::kSystemColumnId &&
            decoded_value.value_type() == docdb::ValueType::kNullLow) {
          SetOperation(&record, row_message, INSERT, is_proto_record);
          col_count = schema.num_key_columns() - 1;
        } else {
          SetOperation(&record, row_message, UPDATE, is_proto_record);
          col_count = schema.num_columns();
          *write_id = intent.write_id;
        }
      }

      // Write pair contains record for different row. Create a new CDCRecord in this case.
      if (is_proto_record) {
        row_message->set_transaction_id(transaction_id.ToString());
        AddPrimaryKey(tablet_peer, decoded_key, schema, nullptr, row_message);
      } else {
        record.mutable_transaction_state()->set_transaction_id(transaction_id.ToString());
        AddPrimaryKey(tablet_peer, decoded_key, schema, &record, nullptr);
      }
    }

    if (IsInsertOperation(&record, row_message)) {
      ++col_count;
    }

    prev_key = primary_key;

    if (!is_proto_record)
      DCHECK(&record);
    else
      DCHECK(&proto_record);

    if (IsInsertOrUpdate(&record, row_message, is_proto_record)) {
      if (isColumnIDInitialized && column_id.value_type() == docdb::ValueType::kColumnId) {
        const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_id.GetColumnId()));

        if (!is_proto_record) {
          AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), record.add_changes(), nullptr);
        } else {
          AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), nullptr,
              row_message->add_new_tuple());
          row_message->add_old_tuple();
        }
      } else if (
          isColumnIDInitialized && column_id.value_type() != docdb::ValueType::kSystemColumnId) {
        LOG(DFATAL) << "Unexpected value type in key: " << column_id.value_type()
                    << " key: " << decoded_key.ToString()
                    << "value: " << decoded_value.primitive_value();
      }
    }

    if (!is_proto_record) {
      if (CreateNewRecord(&record, schema, col_count)) {
        CDCSDKOpIdPB* cdcSdkOpIdPB = record.mutable_cdc_sdk_op_id();
        SetCDCSDKOpId(
            cdcSdkOpIdPB, op_id.term, op_id.index, intent.write_id, intent.reverse_index_key);

        resp->add_cdc_sdk_records()->CopyFrom(record);
        *write_id = intent.write_id;
        *reverse_index_key = intent.reverse_index_key;
      }
    } else {
      row_message->set_table(tablet_peer->tablet()->metadata()->table_name());
      if (CreateNewProtoRecord(row_message, schema, col_count)) {
        CDCSDKOpIdPB* cdcSdkOpIdPB = proto_record.mutable_cdc_sdk_op_id();
        SetCDCSDKOpId(
            cdcSdkOpIdPB, op_id.term, op_id.index, intent.write_id, intent.reverse_index_key);

        CDCSDKProtoRecordPB* record_to_be_added = resp->add_cdc_sdk_proto_records();
        record_to_be_added->CopyFrom(proto_record);
        record_to_be_added->mutable_row_message()->CopyFrom(*row_message);

        *write_id = intent.write_id;
        *reverse_index_key = intent.reverse_index_key;
      }
    }
  }

  return Status::OK();
}

// Populate CDC record corresponding to WAL batch in ReplicateMsg.
CHECKED_STATUS PopulateCDCSDKWriteRecord(
    const ReplicateMsgPtr& msg,
    const StreamMetadata& metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    GetChangesResponsePB* resp,
    const Schema& schema,
    bool is_proto_record) {
  const auto& batch = msg->write().write_batch();

  // Write batch may contain records from different rows.
  // For CDC, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;
  CDCSDKRecordPB* record = nullptr;
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;
  for (const auto& write_pair : batch.write_pairs()) {
    Slice key = write_pair.key();
    const auto key_size =
        VERIFY_RESULT(docdb::DocKey::EncodedSize(key, docdb::DocKeyPart::kWholeDocKey));

    Slice value = write_pair.value();
    docdb::Value decoded_value;
    RETURN_NOT_OK(decoded_value.Decode(value));

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice primary_key(key.data(), key_size);
    if (prev_key != primary_key) {
      // Write pair contains record for different row. Create a new CDCRecord in this case.
      CDCSDKOpIdPB* cdcSdkOpIdPB;
      if (!is_proto_record) {
        record = resp->add_cdc_sdk_records();
        cdcSdkOpIdPB = record->mutable_cdc_sdk_op_id();
      } else {
        proto_record = resp->add_cdc_sdk_proto_records();
        row_message = proto_record->mutable_row_message();
        row_message->set_table(tablet_peer->tablet()->metadata()->table_name());
        cdcSdkOpIdPB = proto_record->mutable_cdc_sdk_op_id();
      }
      SetCDCSDKOpId(cdcSdkOpIdPB, msg->id().term(), msg->id().index(), 0, "");

      Slice sub_doc_key = key;
      docdb::SubDocKey decoded_key;
      RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, docdb::HybridTimeRequired::kFalse));

      // Check whether operation is WRITE or DELETE.
      if (decoded_value.value_type() == docdb::ValueType::kTombstone &&
          decoded_key.num_subkeys() == 0) {
        if (!is_proto_record)
          record->set_operation(CDCSDKRecordPB::DELETE);
        else
          row_message->set_op(RowMessage_Op_DELETE);
      } else {
        docdb::PrimitiveValue column_id;
        Slice key_column((const char*)(key.data() + key_size));
        RETURN_NOT_OK(docdb::SubDocument::DecodeKey(&key_column, &column_id));

        if (column_id.value_type() == docdb::ValueType::kSystemColumnId &&
            decoded_value.value_type() == docdb::ValueType::kNullLow) {
          (!is_proto_record) ? (record->set_operation(CDCSDKRecordPB::INSERT))
                           : (row_message->set_op(RowMessage_Op_INSERT));
        } else {
          (!is_proto_record) ? (record->set_operation(CDCSDKRecordPB::UPDATE))
                           : (row_message->set_op(RowMessage_Op_UPDATE));
        }
      }

      if (!is_proto_record) {
        AddPrimaryKey(tablet_peer, decoded_key, schema, record, nullptr);
      } else {
        AddPrimaryKey(tablet_peer, decoded_key, schema, nullptr, row_message);
      }

      // Process intent records.
      (!is_proto_record) ? (record->set_time(msg->hybrid_time()))
                       : (row_message->set_commit_time(msg->hybrid_time()));
    }
    prev_key = primary_key;
    if (!is_proto_record)
      DCHECK(record);
    else
      DCHECK(proto_record);

    if ((!is_proto_record && ((record->operation() == CDCSDKRecordPB::INSERT) ||
                            (record->operation() == CDCSDKRecordPB::UPDATE))) ||
        (is_proto_record && (row_message != nullptr) &&
         ((row_message->op() == RowMessage_Op_INSERT) ||
          (row_message->op() == RowMessage_Op_UPDATE)))) {

      docdb::PrimitiveValue column_id;
      Slice key_column((const char*)(key.data() + key_size));
      RETURN_NOT_OK(docdb::SubDocument::DecodeKey(&key_column, &column_id));
      if (column_id.value_type() == docdb::ValueType::kColumnId) {
        const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_id.GetColumnId()));
        if (!is_proto_record) {
          AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), record->add_changes(), nullptr);
        } else {
          AddColumnToMap(
              tablet_peer, col, decoded_value.primitive_value(), nullptr,
              row_message->add_new_tuple());
          row_message->add_old_tuple();
        }
      } else if (column_id.value_type() != docdb::ValueType::kSystemColumnId) {
        LOG(DFATAL) << "Unexpected value type in key: " << column_id.value_type();
      }
    }
  }

  return Status::OK();
}

void SetTableProperties(
    CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb,
    const TablePropertiesPB* table_properties) {
  cdc_sdk_table_properties_pb->set_default_time_to_live(table_properties->default_time_to_live());
  cdc_sdk_table_properties_pb->set_num_tablets(table_properties->num_tablets());
  cdc_sdk_table_properties_pb->set_is_ysql_catalog_table(table_properties->is_ysql_catalog_table());
}

void SetColumnInfo (CDCSDKColumnInfoPB* column_info, const ColumnSchemaPB& column) {
  column_info->set_name(column.name());
  column_info->mutable_type()->CopyFrom(column.type());
  column_info->set_is_key(column.is_key());
  column_info->set_is_hash_key(column.is_hash_key());
  column_info->set_is_nullable(column.is_nullable());
  column_info->set_oid(column.pg_type_oid());
}

CHECKED_STATUS PopulateCDCSDKDDLRecord(
    const ReplicateMsgPtr& msg, CDCSDKRecordPB* record, bool is_proto_record,
    CDCSDKProtoRecordPB* proto_record, string table_name) {
  SCHECK(
      msg->has_change_metadata_request(), InvalidArgument,
      Format(
          "Change metadata (DDL) message requires metadata information: $0",
          msg->ShortDebugString()));

  RowMessage* row_message = nullptr;
  CDCSDKOpIdPB* cdcSdkOpIdPB;

  if (!is_proto_record) {
    record->set_operation(CDCSDKRecordPB_OperationType_DDL);
    cdcSdkOpIdPB = record->mutable_cdc_sdk_op_id();
  } else {
    row_message = proto_record->mutable_row_message();
    row_message->set_op(RowMessage_Op_DDL);
    row_message->set_table(table_name);
    cdcSdkOpIdPB = proto_record->mutable_cdc_sdk_op_id();
  }
  SetCDCSDKOpId(cdcSdkOpIdPB, msg->id().term(), msg->id().index(), 0, "");

  for (const auto& column : msg->change_metadata_request().schema().columns()) {
    CDCSDKColumnInfoPB* column_info = nullptr;
    if (!is_proto_record) {
      column_info = record->mutable_schema()->add_column_info();
    } else {
      column_info = row_message->mutable_schema()->add_column_info();
    }
    SetColumnInfo(column_info, column);
  }

  CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb;
  const TablePropertiesPB* table_properties =
      &(msg->change_metadata_request().schema().table_properties());
  if (!is_proto_record) {
    cdc_sdk_table_properties_pb = record->mutable_schema()->mutable_tab_info();
    record->set_schema_version(msg->change_metadata_request().schema_version());
    record->set_new_table_name(msg->change_metadata_request().new_table_name());
  } else {
    cdc_sdk_table_properties_pb = row_message->mutable_schema()->mutable_tab_info();
    row_message->set_schema_version(msg->change_metadata_request().schema_version());
    row_message->set_new_table_name(msg->change_metadata_request().new_table_name());
  }
  SetTableProperties(cdc_sdk_table_properties_pb, table_properties);

  return Status::OK();
}

CHECKED_STATUS PopulateCDCSDKTruncateRecord(
    const ReplicateMsgPtr& msg, CDCSDKRecordPB* record, bool is_proto_record,
    CDCSDKProtoRecordPB* proto_record) {
  SCHECK(
      msg->has_truncate(), InvalidArgument,
      Format(
          "Truncate message requires truncate request information: $0", msg->ShortDebugString()));

  RowMessage* row_message = nullptr;
  CDCSDKOpIdPB* cdcSdkOpIdPB;

  if (!is_proto_record) {
    record->set_operation(CDCSDKRecordPB::TRUNCATE);
    cdcSdkOpIdPB = record->mutable_cdc_sdk_op_id();
    record->mutable_truncate_request_info()->CopyFrom(msg->truncate());
  } else {
    row_message = proto_record->mutable_row_message();
    row_message->set_op(RowMessage_Op_TRUNCATE);
    cdcSdkOpIdPB = proto_record->mutable_cdc_sdk_op_id();
    row_message->mutable_truncate_request_info()->CopyFrom(msg->truncate());
  }
  SetCDCSDKOpId(cdcSdkOpIdPB, msg->id().term(), msg->id().index(), 0, "");

  return Status::OK();
}

CHECKED_STATUS ProcessIntents(
    const OpId& op_id,
    const TransactionId& transaction_id,
    const StreamMetadata& metadata,
    GetChangesResponsePB* resp,
    ScopedTrackedConsumption* consumption,
    CDCSDKCheckpointPB* checkpoint,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    std::vector<docdb::IntentKeyValueForCDC>* keyValueIntents,
    docdb::ApplyTransactionState* stream_state,
    Schema* schema,
    bool is_proto_record) {
  std::string reverse_index_key;
  IntraTxnWriteId write_id = 0;

  if (is_proto_record && stream_state->key.empty() && stream_state->write_id == 0) {
    CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
    RowMessage* row_message = proto_record->mutable_row_message();
    row_message->set_op(RowMessage_Op_BEGIN);
    row_message->set_transaction_id(transaction_id.ToString());
    row_message->set_table(tablet_peer->tablet()->metadata()->table_name());
  }

  auto tablet = tablet_peer->shared_tablet();
  RETURN_NOT_OK(tablet->GetIntents(transaction_id, keyValueIntents, stream_state));

  for (auto& keyValue : *keyValueIntents) {
    docdb::SubDocKey sub_doc_key;
    CHECK_OK(
        sub_doc_key.FullyDecodeFrom(Slice(keyValue.key_buf), docdb::HybridTimeRequired::kFalse));
    docdb::Value decoded_value;
    RETURN_NOT_OK(decoded_value.Decode(Slice(keyValue.value_buf)));
  }
  // Need to populate the CDCSDKRecords
  RETURN_NOT_OK(PopulateCDCSDKIntentRecord(
      op_id, transaction_id, keyValueIntents, metadata, tablet_peer, resp, consumption, &write_id,
      &reverse_index_key, schema, is_proto_record));

  checkpoint->set_term(op_id.term);
  checkpoint->set_index(op_id.index);

  if (stream_state->key.empty() && stream_state->write_id == 0) {
    CDCSDKOpIdPB* cdcSdkOpIdPB;
    if (!is_proto_record) {
      CDCSDKRecordPB* record = resp->add_cdc_sdk_records();
      record->set_operation(CDCSDKRecordPB::WRITE);
      cdcSdkOpIdPB = record->mutable_cdc_sdk_op_id();
      record->mutable_transaction_state()->set_transaction_id(transaction_id.ToString());
      record->mutable_transaction_state()->set_status(TransactionStatus::APPLYING);
    } else {
      CDCSDKProtoRecordPB* proto_record = resp->add_cdc_sdk_proto_records();
      RowMessage* row_message = proto_record->mutable_row_message();
      row_message->set_op(RowMessage_Op_COMMIT);
      row_message->set_transaction_id(transaction_id.ToString());
      row_message->set_table(tablet_peer->tablet()->metadata()->table_name());
      cdcSdkOpIdPB = proto_record->mutable_cdc_sdk_op_id();
    }
    SetCDCSDKOpId(cdcSdkOpIdPB, op_id.term, op_id.index, 0, "");
    checkpoint->set_key("");
    checkpoint->set_write_id(0);
  } else {
    checkpoint->set_key(reverse_index_key);
    checkpoint->set_write_id(write_id);
  }

  return Status::OK();
}

CHECKED_STATUS PopulateCDCSDKSnapshotRecord(GetChangesResponsePB* resp,
                                            const QLTableRow* row,
                                            const Schema& schema,
                                            const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                            ReadHybridTime time,
                                            bool is_proto_record) {
  CDCSDKRecordPB* record = nullptr;
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;
  cdc::KeyValuePairPB* kv_pair = nullptr;
  DatumMessagePB* cdc_datum_message = nullptr;
  string table_name = tablet_peer->tablet()->metadata()->table_name();

  if (!is_proto_record) {
    record = resp->add_cdc_sdk_records();
    record->set_operation(CDCSDKRecordPB_OperationType_READ);
  } else {
    proto_record = resp->add_cdc_sdk_proto_records();
    row_message = proto_record->mutable_row_message();
    row_message->set_table(table_name);
    row_message->set_op(RowMessage_Op_READ);
    row_message->set_commit_time(time.read.ToUint64());
  }

  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    ColumnId col_id = schema.column_id(col_idx);
    const auto* value = row->GetColumn(col_id);
    const ColumnSchema& col_schema = VERIFY_RESULT(schema.column_by_id(col_id));

    if (!is_proto_record) {
      kv_pair = record->add_key();
      kv_pair->set_key(col_schema.name());
    } else {
      cdc_datum_message = row_message->add_new_tuple();
      cdc_datum_message->set_column_name(col_schema.name());
    }

    if (value && value->value_case() != QLValuePB::VALUE_NOT_SET) {
      docdb::SetValueFromQLBinaryWrapper(
        *value, col_schema.pg_type_oid(), is_proto_record,
        (!is_proto_record) ? kv_pair->mutable_value() : nullptr, cdc_datum_message);
    } else {
      if (is_proto_record) {
        cdc_datum_message->set_column_type(col_schema.pg_type_oid());
      }
    }

    if (is_proto_record) {
      row_message->add_old_tuple();
    }
  }

  return Status::OK();
}

void FillDDLInfo (CDCSDKRecordPB* record,
  RowMessage* row_message,
  bool is_proto_record,
  const SchemaPB& schema,
  const uint32_t schema_version) {
  CDCSDKTablePropertiesPB* cdc_sdk_table_properties_pb;
  for (const auto& column : schema.columns()) {
    CDCSDKColumnInfoPB* column_info;
    if (!is_proto_record)
      column_info = record->mutable_schema()->add_column_info();
    else
      column_info = row_message->mutable_schema()->add_column_info();

    SetColumnInfo(column_info, column);
  }

  if (!is_proto_record) {
    record->set_schema_version(schema_version);
    cdc_sdk_table_properties_pb = record->mutable_schema()->mutable_tab_info();
  } else {
    row_message->set_schema_version(schema_version);
    cdc_sdk_table_properties_pb = row_message->mutable_schema()->mutable_tab_info();
  }
  const TablePropertiesPB* table_properties = &(schema.table_properties());
  SetTableProperties(cdc_sdk_table_properties_pb, table_properties);
}

// CDC get changes is different from 2DC as it doesn't need
// to read intents from WAL.

Status GetChangesForCDCSDK(
    const std::string& stream_id,
    const std::string& tablet_id,
    const CDCSDKCheckpointPB& from_op_id,
    const StreamMetadata& stream_metadata,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const MemTrackerPtr& mem_tracker,
    consensus::ReplicateMsgsHolder* msgs_holder,
    GetChangesResponsePB* resp,
    bool is_proto_record,
    std::string* commit_timestamp,
    Schema* cached_schema,
    OpId* last_streamed_op_id,
    int64_t* last_readable_opid_index,
    const CoarseTimePoint deadline) {
  RequestScope request_scope;
  OpId op_id{from_op_id.term(), from_op_id.index()};

  ScopedTrackedConsumption consumption;
  bool pending_intents = false;
  bool schema_streamed = false;
  CDCSDKRecordPB* record = nullptr;
  CDCSDKProtoRecordPB* proto_record = nullptr;
  RowMessage* row_message = nullptr;

  CDCSDKCheckpointPB checkpoint;
  bool checkpoint_updated = false;
  std::vector<docdb::IntentKeyValueForCDC> keyValueIntents;
  docdb::ApplyTransactionState stream_state;

  // It is snapshot call
  if (from_op_id.write_id() == -1) {
    auto txn_participant = tablet_peer->tablet()->transaction_participant();
    tablet::RemoveIntentsData data;
    ReadHybridTime time;
    std::string nextKey;
    SchemaPB schema_pb;
    // It is first call in snapshot then take snapshot
    if ((from_op_id.key().empty()) && (from_op_id.snapshot_time() == 0)) {
      if (txn_participant == nullptr || txn_participant->context() == nullptr)
        return STATUS_SUBSTITUTE(Corruption,
                                 "Cannot read data as the transaction participant context is null");
      txn_participant->context()->GetLastReplicatedData(&data);
      // Set the checkpoint and communicate to the follower.
      VLOG(1) << "The first snapshot term " << data.op_id.term << "index  " << data.op_id.index
              << "time " << data.log_ht.ToUint64();
      // update the CDCConsumerOpId
      {
        std::shared_ptr<consensus::Consensus> shared_consensus = tablet_peer->shared_consensus();
        shared_consensus->UpdateCDCConsumerOpId(data.op_id);
      }
      if (txn_participant == nullptr || txn_participant->context() == nullptr)
        return STATUS_SUBSTITUTE(Corruption,
                                 "Cannot read data as the transaction participant context is null");
      txn_participant->context()->GetLastReplicatedData(&data);
      time = ReadHybridTime::SingleTime(data.log_ht);

      // This should go to cdc_state table.
      // Below condition update the checkpoint in cdc_state table.
      SetCheckpoint(
          &checkpoint, nullptr, data.op_id.term, data.op_id.index, -1, "", time.read.ToUint64());
      checkpoint_updated = true;
    } else {
      // Snapshot is already taken.
      HybridTime ht;
      time = ReadHybridTime::FromUint64(from_op_id.snapshot_time());
      nextKey = from_op_id.key();
      VLOG(1) << "The after snapshot term " << from_op_id.term() << "index  " << from_op_id.index()
              << "key " << from_op_id.key() << "snapshot time " << from_op_id.snapshot_time();

      Schema schema = *tablet_peer->tablet()->schema().get();
      int limit = FLAGS_cdc_snapshot_batch_size;
      int fetched = 0;
      std::vector<QLTableRow> rows;
      auto iter = VERIFY_RESULT(tablet_peer->tablet()->CreateCDCSnapshotIterator(
          schema.CopyWithoutColumnIds(), time, &nextKey));

      QLTableRow row;
      SchemaToPB(*tablet_peer->tablet()->schema().get(), &schema_pb);
      if (!is_proto_record) {
        record = resp->add_cdc_sdk_records();
        record->set_operation(CDCSDKRecordPB_OperationType_DDL);
      } else {
        proto_record = resp->add_cdc_sdk_proto_records();
        row_message = proto_record->mutable_row_message();
        row_message->set_op(RowMessage_Op_DDL);
      }
      FillDDLInfo (record, row_message, is_proto_record, schema_pb,
                   tablet_peer->tablet()->metadata()->schema_version());

      while (VERIFY_RESULT(iter->HasNext()) && fetched < limit) {
        RETURN_NOT_OK(iter->NextRow(&row));
        RETURN_NOT_OK(PopulateCDCSDKSnapshotRecord(resp, &row, schema, tablet_peer,
                                                   time, is_proto_record));
        fetched++;
      }
      docdb::SubDocKey sub_doc_key;
      RETURN_NOT_OK(iter->GetNextReadSubDocKey(&sub_doc_key));

      // Snapshot ends when next key is empty.
      if (sub_doc_key.doc_key().empty()) {
        VLOG(1) << "Setting next sub doc key empty ";
        // Get the checkpoint or read the checkpoint from the table/cache.
        SetCheckpoint(&checkpoint, nullptr, from_op_id.term(), from_op_id.index(), 0, "", 0);
        checkpoint_updated = true;
      } else {
        VLOG(1) << "Setting next sub doc key is " << sub_doc_key.Encode().ToStringBuffer();

        checkpoint.set_write_id(-1);
        SetCheckpoint(
            &checkpoint, nullptr, from_op_id.term(), from_op_id.index(), -1,
            sub_doc_key.Encode().ToStringBuffer(), time.read.ToUint64());

        checkpoint_updated = true;
      }
    }
  } else if (!from_op_id.key().empty() && from_op_id.write_id() != 0) {
    stream_state.key = from_op_id.key();
    stream_state.write_id = from_op_id.write_id();
    std::string reverse_index_key = from_op_id.key();
    Slice reverse_index_key_slice(reverse_index_key);

    RETURN_NOT_OK(reverse_index_key_slice.consume_byte(docdb::ValueTypeAsChar::kTransactionId));
    auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&reverse_index_key_slice));

    RETURN_NOT_OK(ProcessIntents(
        op_id, transaction_id, stream_metadata, resp, &consumption, &checkpoint, tablet_peer,
        &keyValueIntents, &stream_state, nullptr, is_proto_record));
    if (checkpoint.write_id() == 0 && checkpoint.key().empty()) {
      last_streamed_op_id->term = checkpoint.term();
      last_streamed_op_id->index = checkpoint.index();
    }
    checkpoint_updated = true;
  } else {
    OpId checkpoint_op_id;
    auto read_ops = VERIFY_RESULT(tablet_peer->consensus()->ReadReplicatedMessagesForCDC(
        op_id, last_readable_opid_index, deadline));

    if (read_ops.read_from_disk_size && mem_tracker) {
      consumption = ScopedTrackedConsumption(mem_tracker, read_ops.read_from_disk_size);
    }

    auto txn_participant = tablet_peer->tablet()->transaction_participant();
    if (txn_participant) {
      request_scope = RequestScope(txn_participant);
    }

    Schema currentSchema;
    for (const auto& msg : read_ops.messages) {
      if (!schema_streamed && !(*cached_schema).initialized()) {
        string table_name = tablet_peer->tablet()->metadata()->table_name();
        schema_streamed = true;
        if (read_ops.header_schema.IsInitialized()) {
          RETURN_NOT_OK(SchemaFromPB(read_ops.header_schema, &currentSchema));

          if (!is_proto_record) {
            record = resp->add_cdc_sdk_records();
            record->set_operation(CDCSDKRecordPB_OperationType_DDL);
          } else {
            proto_record = resp->add_cdc_sdk_proto_records();
            row_message = proto_record->mutable_row_message();
            row_message->set_op(RowMessage_Op_DDL);
            row_message->set_table(table_name);
          }

          *cached_schema = currentSchema;
          FillDDLInfo (record, row_message, is_proto_record, read_ops.header_schema,
                       read_ops.header_schema_version);
        }
      } else {
        currentSchema = *cached_schema;
      }

      const auto& batch = msg->write().write_batch();
      switch (msg->op_type()) {
        case consensus::OperationType::UPDATE_TRANSACTION_OP:
          // Ignore intents.
          // Read from IntentDB after they have been applied.
          if (msg->transaction_state().status() == TransactionStatus::APPLYING) {
            auto txn_id =
                VERIFY_RESULT(FullyDecodeTransactionId(msg->transaction_state().transaction_id()));

            auto result = GetTransactionStatus(txn_id, tablet_peer->Now(), txn_participant);
            std::vector<docdb::IntentKeyValueForCDC> intents;
            docdb::ApplyTransactionState new_stream_state;

            *commit_timestamp = msg->transaction_state().commit_hybrid_time();
            op_id.term = msg->id().term();
            op_id.index = msg->id().index();
            RETURN_NOT_OK(ProcessIntents(
                op_id, txn_id, stream_metadata, resp, &consumption, &checkpoint, tablet_peer,
                &intents, &new_stream_state, &currentSchema, is_proto_record));

            if (new_stream_state.write_id != 0 && !new_stream_state.key.empty()) {
              pending_intents = true;
            } else {
              last_streamed_op_id->term = msg->id().term();
              last_streamed_op_id->index = msg->id().index();
            }
          }
          checkpoint_updated = true;
          break;

        case consensus::OperationType::WRITE_OP:
          if (!batch.has_transaction()) {
            RETURN_NOT_OK(PopulateCDCSDKWriteRecord(
                msg, stream_metadata, tablet_peer, resp, currentSchema, is_proto_record));

            SetCheckpoint(
                &checkpoint, last_streamed_op_id, msg->id().term(), msg->id().index(), 0, "", 0);
            checkpoint_updated = true;
          }
          break;

        case consensus::OperationType::CHANGE_METADATA_OP: {
          RETURN_NOT_OK(SchemaFromPB(msg->change_metadata_request().schema(), &currentSchema));
          string table_name = tablet_peer->tablet()->metadata()->table_name();
          *cached_schema = currentSchema;
          if ((!is_proto_record && resp->cdc_sdk_records_size() > 0 &&
               resp->cdc_sdk_records(resp->cdc_sdk_records_size() - 1).operation() ==
                   CDCSDKRecordPB_OperationType_DDL) ||
              (is_proto_record && resp->cdc_sdk_proto_records_size() > 0 &&
               resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                       .row_message()
                       .op() == RowMessage_Op_DDL)) {
            if ((!is_proto_record &&
                 resp->cdc_sdk_records(resp->cdc_sdk_records_size() - 1).schema_version() !=
                     msg->change_metadata_request().schema_version()) ||
                (is_proto_record &&
                 resp->cdc_sdk_proto_records(resp->cdc_sdk_proto_records_size() - 1)
                         .row_message()
                         .schema_version() != msg->change_metadata_request().schema_version())) {
              RETURN_NOT_OK(PopulateCDCSDKDDLRecord(
                  msg, (!is_proto_record) ? resp->add_cdc_sdk_records() : nullptr, is_proto_record,
                  (is_proto_record) ? resp->add_cdc_sdk_proto_records() : nullptr, table_name));
            }
          } else {
            RETURN_NOT_OK(PopulateCDCSDKDDLRecord(
                msg, (!is_proto_record) ? resp->add_cdc_sdk_records() : nullptr, is_proto_record,
                (is_proto_record) ? resp->add_cdc_sdk_proto_records() : nullptr, table_name));
          }
          SetCheckpoint(
              &checkpoint, last_streamed_op_id, msg->id().term(), msg->id().index(), 0, "");
          checkpoint_updated = true;
        } break;

        case consensus::OperationType::TRUNCATE_OP: {
          RETURN_NOT_OK(PopulateCDCSDKTruncateRecord(
              msg, resp->add_cdc_sdk_records(), is_proto_record,
              resp->add_cdc_sdk_proto_records()));
          SetCheckpoint(
              &checkpoint, last_streamed_op_id, msg->id().term(), msg->id().index(), 0, "");
          checkpoint_updated = true;
        } break;

        default:
          // Nothing to do for other operation types.
          break;
      }

      if (pending_intents) break;
    }
    if (read_ops.messages.size() > 0)
      *msgs_holder = consensus::ReplicateMsgsHolder(
          nullptr, std::move(read_ops.messages), std::move(consumption));
  }

  if (consumption) {
    consumption.Add(resp->SpaceUsedLong());
  }

  (checkpoint_updated) ? resp->mutable_cdc_sdk_checkpoint()->CopyFrom(checkpoint)
                       : resp->mutable_cdc_sdk_checkpoint()->CopyFrom(from_op_id);

  if (checkpoint_updated) {
    VLOG(1) << "The checkpoint is updated " << resp->checkpoint().DebugString();
  } else {
    VLOG(1) << "The checkpoint is not updated " << resp->checkpoint().DebugString();
  }

  if (last_streamed_op_id->index > 0) {
    resp->mutable_checkpoint()->mutable_op_id()->set_term(last_streamed_op_id->term);
    resp->mutable_checkpoint()->mutable_op_id()->set_index(last_streamed_op_id->index);
  }

  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
