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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/client/session.h"
#include "yb/client/yb_op.h"
#include "yb/client/table_handle.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/master/master_defaults.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"

#include "yb/yql/cql/ql/util/statement_result.h"

DEFINE_int32(cdc_transaction_timeout_ms, 0,
  "Don't check for an aborted transaction unless its original write is lagging by this duration.");

DEFINE_bool(cdc_enable_replicate_intents, true,
            "Enable replication of intents before they've been committed.");

DEFINE_test_flag(bool, xcluster_simulate_have_more_records, false,
                 "Whether GetChanges should indicate that it has more records for safe time "
                 "calculation.");

DEFINE_test_flag(bool, xcluster_skip_meta_ops, false,
                 "Whether GetChanges should skip processing meta operations ");

namespace yb {
namespace cdc {

using consensus::ReplicateMsgPtr;
using consensus::ReplicateMsgs;
using docdb::PrimitiveValue;
using tablet::TransactionParticipant;

void AddColumnToMap(const ColumnSchema& col_schema,
                    const docdb::KeyEntryValue& col,
                    cdc::KeyValuePairPB* kv_pair) {
  kv_pair->set_key(col_schema.name());
  col.ToQLValuePB(col_schema.type(), kv_pair->mutable_value());
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

// Set committed record information including commit time for record.
// This will look at transaction status to determine commit time to be used for CDC record.
// Returns true if we need to stop processing WAL records beyond this, false otherwise.
Result<bool> SetCommittedRecordIndexForReplicateMsg(
    const ReplicateMsgPtr& msg, size_t index, const TxnStatusMap& txn_map,
    ReplicateIntents replicate_intents, std::vector<RecordTimeIndex>* records) {
  if (replicate_intents) {
    // If we're replicating intents, we have no stop condition, so add the record and continue.
    records->emplace_back(msg->hybrid_time(), index);
    return false;
  }
  switch (msg->op_type()) {
    case consensus::OperationType::UPDATE_TRANSACTION_OP: {
      if (msg->transaction_state().status() == TransactionStatus::APPLYING) {
        records->emplace_back(msg->transaction_state().commit_hybrid_time(), index);
      }
      // Ignore other transaction statuses since we only care about APPLYING
      // while sending CDC records.
      return false;
    }

    case consensus::OperationType::WRITE_OP: {
      if (msg->write().write_batch().has_transaction()) {
        auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
            msg->write().write_batch().transaction().transaction_id()));
        const auto txn_status = txn_map.find(txn_id);
        if (txn_status == txn_map.end()) {
          return STATUS(IllegalState, "Unexpected transaction ID", txn_id.ToString());
        }

        if (txn_status->second.status == PENDING || txn_status->second.status == CREATED) {
          // Ignore all records beyond this because we don't know whether those records
          // were committed before or after this record without the transaction commit time.
          return true;
        } else if (txn_status->second.status == COMMITTED) {
          // Add record to txn_msgs because there may be records appearing after this in WAL
          // but committed before this one. Example:
          // T0: WRITE K1 [TXN1]
          // T1: WRITE K2
          // T2: APPLYING TXN1
          // Here, WRITE K2 appears after WRITE K1 but is committed before K1.
          records->emplace_back(txn_status->second.status_time.ToUint64(), index);
        }
      } else {
        // Msg is not part of transaction. Use write hybrid time from msg itself.
        records->emplace_back(msg->hybrid_time(), index);
      }
      return false;
    }
    case consensus::OperationType::SPLIT_OP: {
      records->emplace_back(msg->hybrid_time(), index);
      return true;  // Don't need to process any records after a SPLIT_OP.
    }
    case consensus::OperationType::CHANGE_METADATA_OP: {
      if (FLAGS_TEST_xcluster_skip_meta_ops) {
        FALLTHROUGH_INTENDED;
      } else {
        records->emplace_back(msg->hybrid_time(), index);
        return true;  // Stop processing records after a CHANGE_METADATA_OP, wait for the Consumer.
      }
    }
    case consensus::OperationType::CHANGE_CONFIG_OP:
      FALLTHROUGH_INTENDED;
    case consensus::OperationType::HISTORY_CUTOFF_OP:
      FALLTHROUGH_INTENDED;
    case consensus::OperationType::NO_OP:
      FALLTHROUGH_INTENDED;
    case consensus::OperationType::SNAPSHOT_OP:
      FALLTHROUGH_INTENDED;
    case consensus::OperationType::TRUNCATE_OP:
      FALLTHROUGH_INTENDED;
    case consensus::OperationType::UNKNOWN_OP:
      return false;
  }
  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, msg->op_type());
}

Result<std::vector<RecordTimeIndex>> GetCommittedRecordIndexes(
    const ReplicateMsgs& msgs, const TxnStatusMap& txn_map, ReplicateIntents replicate_intents,
    OpId* checkpoint) {
  size_t index = 0;
  std::vector<RecordTimeIndex> records;

  // Order ReplicateMsgs based on commit time.
  for (const auto &msg : msgs) {
    if (!msg->write().has_external_hybrid_time()) {
      // If the message came from an external source, ignore it when producing change list.
      // Note that checkpoint, however, will be updated and will account for external message too.
      bool stop = VERIFY_RESULT(SetCommittedRecordIndexForReplicateMsg(
          msg, index, txn_map, replicate_intents, &records));
      if (stop) {
        return records;
      }
    }
    *checkpoint = OpId::FromPB(msg->id());
    index++;
  }
  return records;
}

// Filter out WAL records that are external and order records based on transaction commit time.
// Records in WAL don't represent the exact order in which records are written in DB due to delay
// in writing txn APPLYING record.
// Consider the following WAL entries:
// TO: WRITE K0
// T1: WRITE K1 (TXN1)
// T2: WRITE K2 (TXN2)
// T3: WRITE K3
// T4: APPLYING TXN2
// T5: APPLYING TXN1
// T6: WRITE K4
// The order in which keys are written to DB in this example is K0, K3, K2, K1, K4.
// This method will also set checkpoint to the op id of last processed record.
Result<ReplicateMsgs> FilterAndSortWrites(const ReplicateMsgs& msgs,
                                          const TxnStatusMap& txn_map,
                                          ReplicateIntents replicate_intents,
                                          OpId* checkpoint) {
  std::vector<RecordTimeIndex> records = VERIFY_RESULT(GetCommittedRecordIndexes(
      msgs, txn_map, replicate_intents, checkpoint));

  if (!replicate_intents) {
    std::sort(records.begin(), records.end());
  }

  ReplicateMsgs ordered_msgs;
  ordered_msgs.reserve(records.size());
  for (const auto& record : records) {
    ordered_msgs.emplace_back(msgs[record.second]);
  }
  return ordered_msgs;
}

Result<TransactionStatusResult> GetTransactionStatus(
    const TransactionId& txn_id,
    const HybridTime& hybrid_time,
    TransactionParticipant* txn_participant) {
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

// Build transaction status as of hybrid_time.
Result<TxnStatusMap> BuildTxnStatusMap(const ReplicateMsgs& messages,
                                       bool more_replicate_msgs,
                                       const HybridTime& cdc_read_hybrid_time,
                                       TransactionParticipant* txn_participant) {
  TxnStatusMap txn_map;
  // First go through all APPLYING records and mark transaction as committed.
  for (const auto& msg : messages) {
    if (msg->op_type() == consensus::OperationType::UPDATE_TRANSACTION_OP
        && msg->transaction_state().status() == TransactionStatus::APPLYING) {
      auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
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
        && msg->write().write_batch().has_transaction()) {
      auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
          msg->write().write_batch().transaction().transaction_id()));

      if (!txn_map.count(txn_id)) {
        TransactionStatusResult txn_status(TransactionStatus::PENDING, HybridTime::kMin);

        auto result = GetTransactionStatus(txn_id, cdc_read_hybrid_time, txn_participant);
        if (!result.ok()) {
          if (result.status().IsNotFound()) {
            // Naive heuristic for handling whether a transaction is aborted or still pending:
            // 1. If the normal transaction timeout is not reached, assume good operation.
            // 2. If more_replicate_messages, assume a race between reading
            //    TransactionParticipant & LogCache.
            // TODO (#2405) : Handle long running or very large transactions correctly.
            if (!more_replicate_msgs) {
              auto timeout = HybridTime::FromPB(msg->hybrid_time())
                  .AddMilliseconds(FLAGS_cdc_transaction_timeout_ms);
              if (timeout < cdc_read_hybrid_time) {
                LOG(INFO) << "Transaction not found, considering it aborted: " << txn_id;
                txn_status = TransactionStatusResult::Aborted();
              }
            }
          } else {
            return result.status();
          }
        } else {
          txn_status = *result;
        }
        txn_map.emplace(txn_id, txn_status);
      }
    }
  }
  return txn_map;
}

Status SetRecordTime(const TransactionId& txn_id,
                             const TxnStatusMap& txn_map,
                             CDCRecordPB* record) {
  auto txn_status = txn_map.find(txn_id);
  if (txn_status == txn_map.end()) {
    return STATUS(IllegalState, "Unexpected transaction ID", txn_id.ToString());
  }
  record->set_time(txn_status->second.status_time.ToUint64());
  return Status::OK();
}

// Populate CDC record corresponding to WAL batch in ReplicateMsg.
Status PopulateWriteRecord(const ReplicateMsgPtr& msg,
                                   const TxnStatusMap& txn_map,
                                   const StreamMetadata& metadata,
                                   const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                   ReplicateIntents replicate_intents,
                                   GetChangesResponsePB* resp) {
  const auto& batch = msg->write().write_batch();
  const auto& schema = *tablet_peer->tablet()->schema();
  // Write batch may contain records from different rows.
  // For CDC, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;
  CDCRecordPB* record = nullptr;
  for (const auto& write_pair : batch.write_pairs()) {
    Slice key = write_pair.key();
    const auto key_size = VERIFY_RESULT(
        docdb::DocKey::EncodedSize(key, docdb::DocKeyPart::kWholeDocKey));

    Slice value_slice = write_pair.value();
    RETURN_NOT_OK(docdb::ValueControlFields::Decode(&value_slice));
    auto value_type = docdb::DecodeValueEntryType(value_slice);

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice primary_key(key.data(), key_size);
    if (prev_key != primary_key) {
      // Write pair contains record for different row. Create a new CDCRecord in this case.
      record = resp->add_records();
      Slice sub_doc_key = key;
      docdb::SubDocKey decoded_key;
      RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, docdb::HybridTimeRequired::kFalse));

      if (metadata.record_format == CDCRecordFormat::WAL) {
        // For 2DC, populate serialized data from WAL, to avoid unnecessary deserializing on
        // producer and re-serializing on consumer.
        auto kv_pair = record->add_key();
        kv_pair->set_key(std::to_string(decoded_key.doc_key().hash()));
        kv_pair->mutable_value()->set_binary_value(write_pair.key());
      } else {
        AddPrimaryKey(decoded_key, schema, record);
      }

      // Check whether operation is WRITE or DELETE.
      if (value_type == docdb::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        record->set_operation(CDCRecordPB::DELETE);
      } else {
        record->set_operation(CDCRecordPB::WRITE);
      }

      // Process intent records.
      record->set_time(msg->hybrid_time());
      if (batch.has_transaction()) {
        if (!replicate_intents) {
          auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(
              batch.transaction().transaction_id()));
          // If we're not replicating intents, set record time using the transaction map.
          RETURN_NOT_OK(SetRecordTime(txn_id, txn_map, record));
        } else {
          record->mutable_transaction_state()->set_transaction_id(
              batch.transaction().transaction_id());
          record->mutable_transaction_state()->add_tablets(tablet_peer->tablet_id());
        }
      }
    }
    prev_key = primary_key;
    DCHECK(record);

    if (metadata.record_format == CDCRecordFormat::WAL) {
      auto kv_pair = record->add_changes();
      kv_pair->set_key(write_pair.key());
      kv_pair->mutable_value()->set_binary_value(write_pair.value());
    } else if (record->operation() == CDCRecordPB_OperationType_WRITE) {
      docdb::KeyEntryValue column_id;
      Slice key_column = write_pair.key().data() + key_size;
      RETURN_NOT_OK(column_id.DecodeFromKey(&key_column));
      if (column_id.type() == docdb::KeyEntryType::kColumnId) {
        docdb::Value decoded_value;
        RETURN_NOT_OK(decoded_value.Decode(write_pair.value()));

        const ColumnSchema& col = VERIFY_RESULT(schema.column_by_id(column_id.GetColumnId()));
        AddColumnToMap(col, decoded_value.primitive_value(), record->add_changes());
      } else if (column_id.type() != docdb::KeyEntryType::kSystemColumnId) {
        LOG(DFATAL) << "Unexpected value type in key: " << column_id.type();
      }
    }
  }
  return Status::OK();
}

// Populate CDC record corresponding to WAL UPDATE_TRANSACTION_OP entry.
Status PopulateTransactionRecord(const ReplicateMsgPtr& msg,
                                         const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                         ReplicateIntents replicate_intents,
                                         CDCRecordPB* record) {
  SCHECK(msg->has_transaction_state(), InvalidArgument,
         Format("Update transaction message requires transaction_state: $0",
                msg->ShortDebugString()));
  record->set_operation(CDCRecordPB_OperationType_WRITE);
  record->set_time(replicate_intents ?
      msg->hybrid_time() : msg->transaction_state().commit_hybrid_time());
  record->mutable_transaction_state()->CopyFrom(msg->transaction_state());
  if (replicate_intents && msg->transaction_state().status() == TransactionStatus::APPLYING) {
    // Add the partition metadata so the consumer knows which tablets to apply the transaction
    // to.
    tablet_peer->tablet()->metadata()->partition()->ToPB(record->mutable_partition());
  }
  return Status::OK();
}

Status PopulateSplitOpRecord(const ReplicateMsgPtr& msg, CDCRecordPB* record) {
  record->set_operation(CDCRecordPB::SPLIT_OP);
  record->set_time(msg->hybrid_time());
  record->mutable_split_tablet_request()->CopyFrom(msg->split_request());
  return Status::OK();
}

Status PopulateChangeMetadataRecord(const ReplicateMsgPtr& msg, CDCRecordPB* record) {
  SCHECK(msg->has_change_metadata_request(), InvalidArgument,
      Format("METADATA message requires change_metadata_request: $0", msg->ShortDebugString()));
  record->set_operation(CDCRecordPB::CHANGE_METADATA);
  record->set_time(msg->hybrid_time());
  record->mutable_change_metadata_request()->CopyFrom(msg->change_metadata_request());
  return Status::OK();
}

Result<HybridTime> GetSafeTimeForTarget(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                        HybridTime ht_of_last_returned_message,
                                        HaveMoreMessages have_more_messages) {
  if (ht_of_last_returned_message != HybridTime::kInvalid && have_more_messages) {
    return ht_of_last_returned_message;
  }
  return tablet_peer->LeaderSafeTime();
}

Status GetChangesForXCluster(const std::string& stream_id,
                             const std::string& tablet_id,
                             const OpId& from_op_id,
                             const StreamMetadata& stream_metadata,
                             const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                             const client::YBSessionPtr& session,
                             UpdateOnSplitOpFunc update_on_split_op_func,
                             const MemTrackerPtr& mem_tracker,
                             consensus::ReplicateMsgsHolder* msgs_holder,
                             GetChangesResponsePB* resp,
                             int64_t* last_readable_opid_index,
                             const CoarseTimePoint deadline) {
  auto replicate_intents = ReplicateIntents(GetAtomicFlag(&FLAGS_cdc_enable_replicate_intents));
  // Request scope on transaction participant so that transactions are not removed from participant
  // while RequestScope is active.
  RequestScope request_scope;

  auto read_ops = VERIFY_RESULT(tablet_peer->consensus()->
    ReadReplicatedMessagesForCDC(from_op_id, last_readable_opid_index, deadline));
  ScopedTrackedConsumption consumption;
  if (read_ops.read_from_disk_size && mem_tracker) {
    consumption = ScopedTrackedConsumption(mem_tracker, read_ops.read_from_disk_size);
  }

  OpId checkpoint;
  TxnStatusMap txn_map;
  if (!replicate_intents) {
    auto txn_participant = tablet_peer->tablet()->transaction_participant();
    if (txn_participant) {
      request_scope = RequestScope(txn_participant);
    }
    txn_map = TxnStatusMap(VERIFY_RESULT(BuildTxnStatusMap(
      read_ops.messages, read_ops.have_more_messages, tablet_peer->Now(), txn_participant)));
  }
  ReplicateMsgs messages = VERIFY_RESULT(FilterAndSortWrites(
      read_ops.messages, txn_map, replicate_intents, &checkpoint));

  auto first_unreplicated_index = messages.size();
  bool exit_early = false;
  for (size_t i = 0; i < messages.size(); ++i) {
    const auto msg = messages[i];
    switch (msg->op_type()) {
      case consensus::OperationType::UPDATE_TRANSACTION_OP:
        if (!replicate_intents) {
          RETURN_NOT_OK(PopulateTransactionRecord(
              msg, tablet_peer, replicate_intents, resp->add_records()));
        } else if (msg->transaction_state().status() == TransactionStatus::APPLYING) {
          auto record = resp->add_records();
          record->set_operation(CDCRecordPB::APPLY);
          record->set_time(msg->hybrid_time());
          auto* txn_state = record->mutable_transaction_state();
          txn_state->set_transaction_id(msg->transaction_state().transaction_id());
          txn_state->set_commit_hybrid_time(msg->transaction_state().commit_hybrid_time());
          tablet_peer->tablet()->metadata()->partition()->ToPB(record->mutable_partition());
        }
        break;
      case consensus::OperationType::WRITE_OP:
        RETURN_NOT_OK(PopulateWriteRecord(msg, txn_map, stream_metadata, tablet_peer,
                                          replicate_intents, resp));
        break;
      case consensus::OperationType::SPLIT_OP:
        SCHECK(msg->has_split_request(), InvalidArgument,
            Format("Split op message requires split_request: $0", msg->ShortDebugString()));
        if (msg->split_request().tablet_id() == tablet_id) {
          // Only send split if it is our split, and if we can update the children tablet entries
          // in cdc_state table correctly (the reason for this check is that it is possible to
          // read our parent tablet splits).
          auto s = update_on_split_op_func(msg);
          if (s.ok()) {
            RETURN_NOT_OK(PopulateSplitOpRecord(msg, resp->add_records()));
          } else {
            LOG(INFO) << "Not replicating SPLIT_OP yet for tablet: " << tablet_id << ", stream: "
                      << stream_id << " : " << s;
            // Can still process all previous records, but stop processing anything from here on.
            if (i > 0) {
              checkpoint = OpId::FromPB(messages[i-1]->id());
            } else {
              checkpoint = from_op_id;
            }
            first_unreplicated_index = i;
            exit_early = true;
          }
        }
        break;
      case consensus::OperationType::CHANGE_METADATA_OP:
        if (FLAGS_TEST_xcluster_skip_meta_ops) {
          break;
        }
        SCHECK(msg->has_change_metadata_request(), InvalidArgument,
               Format("Change Meta op message requires payload $0", msg->ShortDebugString()));
        if (msg->change_metadata_request().tablet_id() == tablet_id) {
          RETURN_NOT_OK(PopulateChangeMetadataRecord(msg, resp->add_records()));
          // This should be the last record we send to the Consumer.
          checkpoint = OpId::FromPB(msg->id());
          first_unreplicated_index = i+1;
          exit_early = true;
        }
        break;
      default:
        // Nothing to do for other operation types.
        break;
    }
    if (exit_early) {
      break;
    }
  }

  if (consumption) {
    consumption.Add(resp->SpaceUsedLong());
  }
  auto ht_of_last_returned_message = first_unreplicated_index == 0 ?
      HybridTime::kInvalid : HybridTime(messages[first_unreplicated_index - 1]->hybrid_time());
  auto have_more_messages =
      exit_early || PREDICT_FALSE(FLAGS_TEST_xcluster_simulate_have_more_records) ?
          HaveMoreMessages::kTrue : read_ops.have_more_messages;
  auto safe_time_result = GetSafeTimeForTarget(
      tablet_peer, ht_of_last_returned_message, have_more_messages);
  if (safe_time_result.ok()) {
    resp->set_safe_hybrid_time((*safe_time_result).ToUint64());
  } else {
    YB_LOG_EVERY_N_SECS(WARNING, 10) <<
        "Could not compute safe time: " << safe_time_result.status();
  }
  *msgs_holder = consensus::ReplicateMsgsHolder(
      nullptr, std::move(messages), std::move(consumption));
  (checkpoint.index > 0 ? checkpoint : from_op_id).ToPB(
      resp->mutable_checkpoint()->mutable_op_id());
  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
