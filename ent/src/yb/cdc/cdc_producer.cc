// Copyright (c) YugaByte, Inc.

#include "yb/cdc/cdc_producer.h"

#include "yb/cdc/cdc_service.pb.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

namespace yb {
namespace cdc {

using consensus::ReplicateMsgPtr;
using consensus::ReplicateMsgs;
using docdb::PrimitiveValue;
using tablet::TransactionParticipant;

namespace {
void AddColumnToMap(const ColumnSchema& col_schema,
                    const docdb::PrimitiveValue& col,
                    cdc::KeyValuePairPB* kv_pair) {
  kv_pair->set_key(col_schema.name());
  PrimitiveValue::ToQLValuePB(col, col_schema.type(), kv_pair->mutable_value());
}

void AddPrimaryKey(const docdb::SubDocKey& decoded_key,
                   const Schema& tablet_schema,
                   CDCRecordPB* record) {
  size_t i = 0;
  for (const auto& col : decoded_key.doc_key().hashed_group()) {
    AddColumnToMap(tablet_schema.column(i), col, record->add_key());
    i++;
  }
  for (const auto& col : decoded_key.doc_key().range_group()) {
    AddColumnToMap(tablet_schema.column(i), col, record->add_key());
    i++;
  }
}
} // namespace

Status CDCProducer::GetChanges(const GetChangesRequestPB& req,
                               GetChangesResponsePB* resp) {
  const auto& record = VERIFY_RESULT(GetRecordMetadataForSubscriber(req.subscriber_uuid()));

  HybridTime now = tablet_peer_->Now();
  OpIdPB from_op_id;
  if (req.has_from_checkpoint()) {
    from_op_id = req.from_checkpoint().op_id();
  } else {
    from_op_id = GetLastCheckpoint(req.subscriber_uuid());
  }

  // Request scope on transaction participant so that transactions are not removed from participant
  // while RequestScope is active.
  RequestScope request_scope;
  if (tablet_peer_->tablet()->transaction_participant()) {
    request_scope = RequestScope(tablet_peer_->tablet()->transaction_participant());
  }

  ReplicateMsgs messages;
  RETURN_NOT_OK(tablet_peer_->consensus()->ReadReplicatedMessages(from_op_id, &messages));

  TxnStatusMap txn_map = VERIFY_RESULT(BuildTxnStatusMap(messages, now));
  const auto& ordered_msgs = VERIFY_RESULT(SortWrites(messages, txn_map));

  for (const auto& msg : ordered_msgs) {
    switch (msg->op_type()) {
      case consensus::OperationType::UPDATE_TRANSACTION_OP:
        // If record format is WAL, then add CDCRecord.
        if (record.record_format == CDCRecordFormat::WAL) {
          RETURN_NOT_OK(PopulateTransactionRecord(msg, resp->add_records()));
        }
        break;

      case consensus::OperationType::WRITE_OP:
        RETURN_NOT_OK(PopulateWriteRecord(msg, txn_map, record, resp));
        break;

      default:
        // Nothing to do for other operation types.
        break;
    }
  }

  resp->mutable_checkpoint()->mutable_op_id()->CopyFrom(
      ordered_msgs.empty() ? from_op_id : ordered_msgs.back()->id());
  return Status::OK();
}

Result<ReplicateMsgs> CDCProducer::SortWrites(const ReplicateMsgs& msgs,
                                              const TxnStatusMap& txn_map) {

  std::vector<RecordTimeIndex> records = VERIFY_RESULT(GetCommittedRecordIndexes(msgs, txn_map));
  std::sort(records.begin(), records.end());

  ReplicateMsgs ordered_msgs;
  ordered_msgs.reserve(records.size());
  for (const auto& record : records) {
    ordered_msgs.emplace_back(msgs[record.second]);
  }
  return ordered_msgs;
}

Result<std::vector<RecordTimeIndex>> CDCProducer::GetCommittedRecordIndexes(
    const ReplicateMsgs& msgs,
    const TxnStatusMap& txn_map) {
  size_t index = 0;
  std::vector<RecordTimeIndex> records;

  // Order ReplicateMsgs based on commit time.
  for (const auto &msg : msgs) {
    switch (msg->op_type()) {
      case consensus::OperationType::UPDATE_TRANSACTION_OP:
        if (msg->transaction_state().status() == TransactionStatus::APPLYING) {
          records.emplace_back(msg->transaction_state().commit_hybrid_time(), index);
        }
        break;

      case consensus::OperationType::WRITE_OP: {
        if (msg->write_request().write_batch().has_transaction()) {
          const auto &txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
              msg->write_request().write_batch().transaction().transaction_id()));
          const auto txn_status = txn_map.find(txn_id);
          if (txn_status == txn_map.end()) {
            return STATUS(IllegalState, "Unexpected transaction ID",
                          boost::uuids::to_string(txn_id));
          }
          if (txn_status->second.status == PENDING || txn_status->second.status == CREATED) {
            // Ignore all records beyond this because we don't know whether those records
            // were committed before or after this record without the transaction commit time.
            return records;
          } else if (txn_status->second.status == COMMITTED) {
            // Add record to txn_msgs because there may be records appearing after this in WAL
            // but committed before this one. Example:
            // T0: WRITE K1 [TXN1]
            // T1: WRITE K2
            // T2: APPLYING TXN1
            // Here, WRITE K2 appears after WRITE K1 but is committed before K1.
            records.emplace_back(txn_status->second.status_time.ToUint64(), index);
          }
        } else {
          records.emplace_back(msg->hybrid_time(), index);
        }
        break;
      }

      default:
        break;
    }
    index++;
  }
  return records;
}

Result<TxnStatusMap> CDCProducer::BuildTxnStatusMap(const ReplicateMsgs& messages,
                                                    const HybridTime& hybrid_time) {
  TxnStatusMap txn_map;
  // First go through all APPLYING records and mark transaction as committed.
  for (const auto& msg : messages) {
    if (msg->op_type() == consensus::OperationType::UPDATE_TRANSACTION_OP
        && msg->transaction_state().status() == TransactionStatus::APPLYING) {
      const auto& txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
          msg->transaction_state().transaction_id()));
      txn_map.emplace(txn_id,
                      TransactionStatusResult(
                          TransactionStatus::COMMITTED,
                          HybridTime(msg->transaction_state().commit_hybrid_time())));
    }
  }

  // Now go through all WRITE_OP records and get transaction status of records for which
  // corresponding APPLYING record does not exist in WAL as yet.
  for (const auto& msg : messages) {
    if (msg->op_type() == consensus::OperationType::WRITE_OP
        && msg->write_request().write_batch().has_transaction()) {
      const auto& txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
          msg->write_request().write_batch().transaction().transaction_id()));
      if (!txn_map.count(txn_id)) {
        const auto& result = GetTransactionStatus(txn_id, hybrid_time);
        if (!result.ok()) {
          if (result.status().IsNotFound()) {
            LOG(INFO) << "Transaction not found, considering it aborted: " << txn_id;
          } else {
            return result.status();
          }
        }
        const auto& txn_status = result.ok() ? *result : TransactionStatusResult::Aborted();
        txn_map.emplace(txn_id, txn_status);
      }
    }
  }
  return txn_map;
}

Result<TransactionStatusResult> CDCProducer::GetTransactionStatus(
    const TransactionId& txn_id, const HybridTime& hybrid_time) {
  TransactionParticipant *txn_participant = tablet_peer_->tablet()->transaction_participant();
  DCHECK(txn_participant);

  static const std::string reason = "cdc";

  std::promise<Result<TransactionStatusResult>> txn_status_promise;
  auto future = txn_status_promise.get_future();
  auto callback = [&txn_status_promise](Result<TransactionStatusResult> result) {
    txn_status_promise.set_value(std::move(result));
  };

  txn_participant->RequestStatusAt(
      {&txn_id, hybrid_time, hybrid_time, 0, &reason, MustExist::kFalse, callback});
  future.wait();
  return future.get();
}

Result<CDCRecordMetadata> CDCProducer::GetRecordMetadataForSubscriber(
    const std::string& subscriber_uuid) {
  // TODO: This should read details from cdc_state table.
  return CDCRecordMetadata(CDCRecordType::CHANGE, CDCRecordFormat::WAL);
}

Status CDCProducer::SetRecordTxnAndTime(const TransactionId& txn_id,
                                        const TxnStatusMap& txn_map,
                                        CDCRecordPB* record) {
  auto txn_status = txn_map.find(txn_id);
  if (txn_status == txn_map.end()) {
    return STATUS(IllegalState, "Unexpected transaction ID", boost::uuids::to_string(txn_id));
  }
  record->mutable_transaction_state()->set_transaction_id(txn_id.data, txn_id.size());
  record->set_time(txn_status->second.status_time.ToUint64());
  return Status::OK();
}

Status CDCProducer::PopulateWriteRecord(const ReplicateMsgPtr& msg,
                                        const TxnStatusMap& txn_map,
                                        const CDCRecordMetadata& metadata,
                                        GetChangesResponsePB* resp) {
  const auto& batch = msg->write_request().write_batch();

  // Write batch may contain records from different rows.
  // For CDC, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;
  CDCRecordPB* record = nullptr;
  for (const auto& write_pair : batch.write_pairs()) {
    Slice key = write_pair.key();
    const auto& key_sizes = VERIFY_RESULT(docdb::DocKey::EncodedSizes(key));

    Slice value = write_pair.value();
    docdb::Value decoded_value;
    RETURN_NOT_OK(decoded_value.Decode(value));

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice key_hash(write_pair.key().data(), key_sizes.first);
    if (prev_key != key_hash) {
      // Write pair contains record for different row. Create a new CDCRecord in this case.
      record = resp->add_records();
      Slice sub_doc_key = write_pair.key();
      docdb::SubDocKey decoded_key;
      RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, docdb::HybridTimeRequired::kFalse));
      AddPrimaryKey(decoded_key, *tablet_peer_->tablet()->schema(), record);

      // Check whether operation is WRITE or DELETE.
      if (decoded_value.value_type() == docdb::ValueType::kTombstone &&
          decoded_key.num_subkeys() == 0) {
        record->set_operation(CDCRecordPB_OperationType_DELETE);
      } else {
        record->set_operation(CDCRecordPB_OperationType_WRITE);
      }

      if (batch.has_transaction()) {
        const auto& txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
            batch.transaction().transaction_id()));
        RETURN_NOT_OK(SetRecordTxnAndTime(txn_id, txn_map, record));
      } else {
        record->set_time(msg->hybrid_time());
      }
    }
    prev_key = key_hash;
    DCHECK(record);

    if (record->operation() == CDCRecordPB_OperationType_WRITE) {
      PrimitiveValue column_id;
      Slice key_column = write_pair.key().data() + key_sizes.second;
      RETURN_NOT_OK(PrimitiveValue::DecodeKey(&key_column, &column_id));
      if (column_id.value_type() == docdb::ValueType::kColumnId) {
        ColumnSchema col = VERIFY_RESULT(tablet_peer_->tablet()->schema()->column_by_id(
            column_id.GetColumnId()));
        AddColumnToMap(col, decoded_value.primitive_value(), record->add_changes());
      } else if (column_id.value_type() != docdb::ValueType::kSystemColumnId) {
        LOG(DFATAL) << "Unexpected value type in key: "<< column_id.value_type();
      }
    }
  }
  return Status::OK();
}

Status CDCProducer::PopulateTransactionRecord(const ReplicateMsgPtr& msg,
                                              CDCRecordPB* record) {
  record->set_operation(CDCRecordPB_OperationType_WRITE);
  record->set_time(msg->transaction_state().commit_hybrid_time());
  record->mutable_transaction_state()->CopyFrom(msg->transaction_state());
  // TODO: Deserialize record.
  return Status::OK();
}

OpIdPB CDCProducer::GetLastCheckpoint(const std::string& subscriber_uuid) {
  // TODO: Read value from cdc_subscribers table.
  OpIdPB op_id;
  op_id.set_term(0);
  op_id.set_index(0);
  return op_id;
}

}  // namespace cdc
}  // namespace yb
