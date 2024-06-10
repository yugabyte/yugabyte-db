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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/log_cache.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/master/master_defaults.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/flags.h"
#include "yb/gutil/stl_util.h"

DEPRECATE_FLAG(int32, cdc_transaction_timeout_ms, "05_2021");
DEPRECATE_FLAG(bool, cdc_enable_replicate_intents, "05_2021");

DEFINE_test_flag(bool, xcluster_simulate_have_more_records, false,
                 "Whether GetChanges should indicate that it has more records for safe time "
                 "calculation.");

DEFINE_test_flag(bool, xcluster_skip_meta_ops, false,
                 "Whether GetChanges should skip processing meta operations ");

DEFINE_RUNTIME_uint32(xcluster_consistent_wal_safe_time_frequency_ms, 250,
                      "Frequency in milliseconds at which apply safe time is computed.");

DEFINE_RUNTIME_AUTO_bool(xcluster_enable_subtxn_abort_propagation, kExternal, false, true,
    "Enable including information about which subtransactions aborted in CDC changes");

namespace yb {
namespace cdc {

using namespace std::chrono_literals;
using consensus::ReplicateMsgPtr;
using consensus::ReplicateMsgs;
using dockv::PrimitiveValue;
using tablet::TransactionParticipant;

namespace {

Status PopulateWriteRecord(
    const consensus::LWReplicateMsg& msg, const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    GetChangesResponsePB* resp) {
  const auto& batch = msg.write().write_batch();
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  // Write batch may contain records from different rows.
  // For xCluster, we need to split the batch into 1 CDC record per row of the table.
  // We'll use DocDB key hash to identify the records that belong to the same row.
  Slice prev_key;
  CDCRecordPB* record = nullptr;
  for (const auto& write_pair : batch.write_pairs()) {
    Slice key = write_pair.key();
    const auto key_size =
        VERIFY_RESULT(dockv::DocKey::EncodedSize(key, dockv::DocKeyPart::kWholeDocKey));

    Slice value_slice = write_pair.value();
    RETURN_NOT_OK(dockv::ValueControlFields::Decode(&value_slice));
    auto value_type = dockv::DecodeValueEntryType(value_slice);

    // Compare key hash with previously seen key hash to determine whether the write pair
    // is part of the same row or not.
    Slice primary_key(key.data(), key_size);
    if (prev_key != primary_key) {
      // Write pair contains record for different row. Create a new CDCRecord in this case.
      record = resp->add_records();
      Slice sub_doc_key = key;
      dockv::SubDocKey decoded_key;
      RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, dockv::HybridTimeRequired::kFalse));

      // For xCluster, populate serialized data from WAL, to avoid unnecessary deserializing on
      // producer and re-serializing on consumer.
      auto kv_pair = record->add_key();
      if (decoded_key.doc_key().has_hash()) {
        // TODO: is there another way of getting this? Perhaps using kUpToHashOrFirstRange?
        kv_pair->set_key(
            dockv::PartitionSchema::EncodeMultiColumnHashValue(decoded_key.doc_key().hash()));
      } else {
        kv_pair->set_key(decoded_key.doc_key().Encode().ToStringBuffer());
      }
      kv_pair->mutable_value()->set_binary_value(write_pair.key().ToBuffer());

      // Check whether operation is WRITE or DELETE.
      if (value_type == dockv::ValueEntryType::kTombstone && decoded_key.num_subkeys() == 0) {
        record->set_operation(CDCRecordPB::DELETE);
      } else {
        record->set_operation(CDCRecordPB::WRITE);
      }

      // Process intent records.
      record->set_time(msg.hybrid_time());
      if (batch.has_transaction()) {
        auto* transaction_state = record->mutable_transaction_state();
        transaction_state->set_transaction_id(batch.transaction().transaction_id().ToBuffer());
        transaction_state->add_tablets(tablet_peer->tablet_id());
        if (GetAtomicFlag(&FLAGS_xcluster_enable_subtxn_abort_propagation) &&
            batch.subtransaction().has_subtransaction_id()) {
          record->set_subtransaction_id(batch.subtransaction().subtransaction_id());
        }
      }
    }
    prev_key = primary_key;
    DCHECK(record);

    auto kv_pair = record->add_changes();
    kv_pair->set_key(write_pair.key().ToBuffer());
    kv_pair->mutable_value()->set_binary_value(write_pair.value().ToBuffer());
  }
  return Status::OK();
}

Status PopulateTransactionRecord(
    const consensus::LWReplicateMsg& msg,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    GetChangesResponsePB* resp) {
  SCHECK(
      msg.has_transaction_state(), InvalidArgument,
      Format("Update transaction message requires transaction_state: $0", msg.ShortDebugString()));

  const auto& transaction_state = msg.transaction_state();
  if (transaction_state.status() != TransactionStatus::APPLYING) {
    // This is an unsupported transaction status.
    return Status::OK();
  }

  auto* record = resp->add_records();
  record->set_time(msg.hybrid_time());
  auto* txn_state = record->mutable_transaction_state();
  txn_state->set_transaction_id(transaction_state.transaction_id().ToBuffer());

  record->set_operation(CDCRecordPB::APPLY);
  txn_state->set_commit_hybrid_time(transaction_state.commit_hybrid_time());
  if (GetAtomicFlag(&FLAGS_xcluster_enable_subtxn_abort_propagation)) {
    auto aborted_subtransactions =
        VERIFY_RESULT(SubtxnSet::FromPB(transaction_state.aborted().set()));
    aborted_subtransactions.ToPB(txn_state->mutable_aborted()->mutable_set());
  }
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  tablet->metadata()->partition()->ToPB(record->mutable_partition());
  return Status::OK();
}

// Populate a CDCRecordPB for a tablet split operation.
Status PopulateSplitOpRecord(
    const xrepl::StreamId& stream_id, const TabletId& tablet_id,
    const consensus::LWReplicateMsg& msg, UpdateOnSplitOpFunc update_on_split_op_func,
    GetChangesResponsePB* resp) {
  SCHECK(
      msg.has_split_request(), InvalidArgument,
      Format("Split op message requires split_request: $0", msg.ShortDebugString()));
  if (msg.split_request().tablet_id() != tablet_id) {
    return Status::OK();
  }

  // Only send split if it is our split, and if we can update the children tablet entries
  // in cdc_state table correctly (the reason for this check is that it is possible to
  // read our parent tablet splits).
  auto s = update_on_split_op_func(msg.ToGoogleProtobuf());
  if (!s.ok()) {
    LOG(INFO) << "Not replicating SPLIT_OP yet for tablet: " << tablet_id
              << ", stream: " << stream_id << " : " << s;
    return s;
  }

  CDCRecordPB* record = resp->add_records();
  record->set_operation(CDCRecordPB::SPLIT_OP);
  record->set_time(msg.hybrid_time());
  msg.split_request().ToGoogleProtobuf(record->mutable_split_tablet_request());

  return Status::OK();
}

// Populate a CDCRecordPB for a Change Metadata operation. Returns true if the record was added and
// false if it can be skipped. If a record was added then further batch processing should not be
// done.
Result<bool> PopulateChangeMetadataRecord(
    const std::string& tablet_id, const consensus::LWReplicateMsg& msg,
    GetChangesResponsePB* resp) {
  if (FLAGS_TEST_xcluster_skip_meta_ops) {
    return false;
  }

  SCHECK(
      msg.has_change_metadata_request(), InvalidArgument,
      Format("Change Meta op message requires payload $0", msg.ShortDebugString()));

  if (msg.change_metadata_request().tablet_id() != tablet_id) {
    return false;
  }

  auto* record = resp->add_records();
  record->set_operation(CDCRecordPB::CHANGE_METADATA);
  record->set_time(msg.hybrid_time());
  msg.change_metadata_request().ToGoogleProtobuf(record->mutable_change_metadata_request());
  return true;
}

HybridTime GetSafeTimeForTarget(
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
}  // namespace

Status GetChangesForXCluster(
    const xrepl::StreamId& stream_id,
    const TabletId& tablet_id,
    const OpId& from_op_id,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    UpdateOnSplitOpFunc update_on_split_op_func,
    const MemTrackerPtr& mem_tracker,
    const CoarseTimePoint& deadline,
    StreamMetadata* stream_metadata,
    consensus::ReplicateMsgsHolder* msgs_holder,
    GetChangesResponsePB* resp,
    int64_t* last_readable_opid_index) {
  SCHECK(tablet_peer, NotFound, Format("Tablet id $0 not found", tablet_id));
  RSTATUS_DCHECK_EQ(
      stream_metadata->GetRecordFormat(), CDCRecordFormat::WAL, IllegalState,
      "xCluster only supports WAL record format");

  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());

  auto leader_safe_time = VERIFY_RESULT(tablet_peer->LeaderSafeTime());
  SCHECK(
      !leader_safe_time.is_special(), IllegalState, "Leader safe time for tablet $0 is not valid",
      tablet_id);

  auto stream_tablet_metadata = stream_metadata->GetTabletMetadata(tablet_id);
  // There should only be one thread at a time calling GetChanges per tablet. But we cannot trust
  // calls we get over the network so we lock here.
  std::lock_guard l(stream_tablet_metadata->mutex_);

  bool update_apply_safe_time = false;
  auto now = MonoTime::Now();
  auto* txn_participant = tablet->transaction_participant();
  // Check if both the table and stream are transactional.
  bool transactional = (txn_participant != nullptr) && stream_metadata->IsTransactional();

  // In order to provide a transactionally consistent WAL, we need to perform the below steps:
  // If last_apply_safe_time is kInvalid
  //  1. last_apply_safe_time = Tablet Safe time
  //  2. Resolve all intents and apply all the committed intents
  //  3. Set apply_safe_time_checkpoint_op_id = max raft-replicated opId
  //  4. If we send all records including apply_safe_time_checkpoint_op_id in the
  //     GetChangesResponsePB
  //    a. Set response.safe_time to last_apply_safe_time
  //    b. Reset last_apply_safe_time and apply_safe_time_checkpoint_op_id
  //  5. Else don't set any response.apply_safe_time. The next GetChanges RPC will reuse the
  //     computed last_apply_safe_time and apply_safe_time_checkpoint_op_id
  if (transactional && !stream_tablet_metadata->last_apply_safe_time_.is_valid()) {
    // See if its time to update the apply safe time.
    if (txn_participant->GetNumRunningTransactions() == 0 ||
        !stream_tablet_metadata->last_apply_safe_time_update_time_ ||
        stream_tablet_metadata->last_apply_safe_time_update_time_ +
                (FLAGS_xcluster_consistent_wal_safe_time_frequency_ms * 1ms) <
            now) {
      update_apply_safe_time = true;
      // Resolve and apply intents to make the leader_safe_time a valid apply_safe_time candidate.
      RETURN_NOT_OK(txn_participant->ResolveIntents(leader_safe_time, deadline));
    }
  }

  consensus::ReadOpsResult read_ops;
  {
    auto consensus = VERIFY_RESULT(tablet_peer->GetConsensus());
    read_ops = VERIFY_RESULT(
        consensus->ReadReplicatedMessagesForCDC(from_op_id, last_readable_opid_index, deadline));
  }

  if (update_apply_safe_time) {
    DCHECK(!stream_tablet_metadata->last_apply_safe_time_.is_valid());
    stream_tablet_metadata->last_apply_safe_time_ = leader_safe_time;

    DCHECK_EQ(stream_tablet_metadata->apply_safe_time_checkpoint_op_id_, 0);
    stream_tablet_metadata->apply_safe_time_checkpoint_op_id_ = *last_readable_opid_index;

    stream_tablet_metadata->last_apply_safe_time_update_time_ = now;
  }

  ScopedTrackedConsumption consumption;
  if (read_ops.read_from_disk_size && mem_tracker) {
    consumption = ScopedTrackedConsumption(mem_tracker, read_ops.read_from_disk_size);
  }

  ReplicateMsgs& messages = read_ops.messages;
  // Get the last checkpoint of records read, even if it is external.
  OpId last_checkpoint = messages.empty() ? from_op_id : OpId::FromPB(messages.back()->id());
  // Filter out WAL records that are external.
  EraseIf([](const auto& msg) { return msg->write().has_external_hybrid_time(); }, &messages);

  HaveMoreMessages have_more_messages =
      PREDICT_FALSE(FLAGS_TEST_xcluster_simulate_have_more_records) ? HaveMoreMessages::kTrue
                                                                    : read_ops.have_more_messages;
  auto ht_of_last_returned_message = HybridTime::kInvalid;
  OpId checkpoint = from_op_id;
  OpId previous_checkpoint = from_op_id;  // value of checkpoint from previous loop iteration.

  bool exit_early = false;
  for (const auto& msg_ptr : messages) {
    const auto& msg = *msg_ptr;
    checkpoint = OpId::FromPB(msg.id());
    switch (msg.op_type()) {
      case consensus::OperationType::UPDATE_TRANSACTION_OP:
        RETURN_NOT_OK(PopulateTransactionRecord(msg, tablet_peer, resp));
        break;
      case consensus::OperationType::WRITE_OP:
        RETURN_NOT_OK(PopulateWriteRecord(msg, tablet_peer, resp));
        break;
      case consensus::OperationType::SPLIT_OP:
        RETURN_NOT_OK(
            PopulateSplitOpRecord(stream_id, tablet_id, msg, update_on_split_op_func, resp));
        break;
      case consensus::OperationType::CHANGE_METADATA_OP:
        if (VERIFY_RESULT(PopulateChangeMetadataRecord(tablet_id, msg, resp))) {
          // Change metadata should be the last record in the batch.
          exit_early = true;
        }
        break;
      default:
        // Nothing to do for other operation types.
        break;
    }
    ht_of_last_returned_message = HybridTime(msg.hybrid_time());

    if (exit_early) {
      have_more_messages = HaveMoreMessages::kTrue;
      break;
    }

    previous_checkpoint = checkpoint;
  }

  if (!exit_early) {
    // Processed the entire batch of messages we read, so set checkpoint to last opid of those
    // records. This also ensures we don't try to reread external records again on future calls.
    checkpoint = last_checkpoint;
  }

  if (consumption) {
    consumption.Add(resp->SpaceUsedLong());
  }

  if (transactional) {
    // We can set the apply_safe_time if no messages were read from the WAL and there is nothing
    // to send. Or, the apply_safe_time_checkpoint_op_id_ was included in the response.
    if (stream_tablet_metadata->last_apply_safe_time_.is_valid()) {
      if ((checkpoint.index == 0 && !read_ops.have_more_messages) ||
          checkpoint.index >= stream_tablet_metadata->apply_safe_time_checkpoint_op_id_) {
        resp->set_safe_hybrid_time(stream_tablet_metadata->last_apply_safe_time_.ToUint64());
        // Clear out the checkpoint and recompute it on next call.
        stream_tablet_metadata->apply_safe_time_checkpoint_op_id_ = 0;
        stream_tablet_metadata->last_apply_safe_time_ = HybridTime::kInvalid;
      }
    }
  } else {
    auto safe_time =
        GetSafeTimeForTarget(leader_safe_time, ht_of_last_returned_message, have_more_messages);
    resp->set_safe_hybrid_time(safe_time.ToUint64());
  }

  *msgs_holder = consensus::ReplicateMsgsHolder(
      nullptr, std::move(messages), std::move(consumption));
  (checkpoint.index > 0 ? checkpoint : from_op_id).ToPB(
      resp->mutable_checkpoint()->mutable_op_id());
  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
