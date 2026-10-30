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

#include <unordered_set>

#include "yb/client/client_fwd.h"

#include "yb/common/transaction.h"

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_types.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/intent_format.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/packed_row.h"

#include "yb/tserver/xcluster_write_interface.h"
#include "yb/tserver/tserver.messages.h"

#include "yb/util/atomic.h"
#include "yb/util/size_literals.h"
#include "yb/util/fast_varint.h"
#include "yb/util/flags.h"
#include "yb/util/status_format.h"

#include "yb/common/hybrid_time.h"

// Below batch related configs are deprecated because splitting a single producer side
// batch into multiple consumer side batches can cause overwrite of intents and lead to rocksdb
// corruption.
DEPRECATE_FLAG(int32, cdc_max_apply_batch_num_records, "4_2023");

DEPRECATE_FLAG(uint32, cdc_max_apply_batch_size_bytes, "4_2023");

DEFINE_test_flag(bool, xcluster_write_hybrid_time, false,
    "Override external_hybrid_time with initialHybridTimeValue for testing.");

DECLARE_uint64(consensus_max_batch_size_bytes);

DEFINE_RUNTIME_AUTO_bool(xcluster_use_encoded_key_filter, kLocalPersisted, false, true,
    "use the encoded key range for filtering xCluster intents at the time of APPLY");

namespace yb {

using namespace yb::size_literals;

namespace tserver {

// Updates the packed row encoded in the value with the local schema version
// without decoding the value. In case of a non-packed row, return as is.
Status UpdatePackedRow(
    const Slice& key, const Slice& value, const cdc::XClusterSchemaVersionMap& schema_versions_map,
    std::optional<SchemaVersion>& new_schema_version, ValueBuffer* out) {
  CHECK(out != nullptr);
  VLOG(3) << Format("Original kv with producer schema version $0=$1",
      key.ToDebugHexString(), value.ToDebugHexString());

  Slice value_slice = value;
  auto control_fields = VERIFY_RESULT(dockv::ValueControlFields::Decode(&value_slice));

  // Don't perform any changes to the value for the following cases:
  // 1. Non-packed rows
  // 2. We don't have a schema version map of producer to consumer schema versions
  if (!IsPackedRow(dockv::DecodeValueEntryType(value_slice)) || schema_versions_map.empty()) {
    // Return the whole value without changes
    out->Truncate(0);
    out->Reserve(value.size());
    out->Append(value);
    return Status::OK();
  }

  auto mapper = [&schema_versions_map,
                 &new_schema_version](SchemaVersion version) -> Result<SchemaVersion> {
    auto it = schema_versions_map.find(version);
    if (it == schema_versions_map.end()) {
      return STATUS_FORMAT(NotFound, "Schema version mapping for $0 not found", version);
    }
    new_schema_version = it->second;
    return it->second;
  };
  auto status = ReplaceSchemaVersionInPackedValue(value_slice, control_fields, mapper, out);

  if (status.ok()) {
    VLOG(3) << Format("Updated kv with producer schema version $0=$1",
        key.ToDebugHexString(), out->AsSlice().ToDebugHexString());
  }

  return status;
}

Status CombineExternalIntents(
    const TransactionId& txn_id, const Uuid& involved_tablet, SubTransactionId subtransaction_id,
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& pairs,
    Slice delete_vector_ids,
    docdb::KeyValuePairMsg* out, std::unordered_set<SchemaVersion>& used_packed_schema_versions,
    const cdc::XClusterSchemaVersionMap& schema_versions_map) {
  class Provider : public docdb::ExternalIntentsProvider {
   public:
    Provider(
        const Uuid& involved_tablet,
        const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>* pairs,
        const cdc::XClusterSchemaVersionMap& schema_versions_map, docdb::KeyValuePairMsg* out,
        std::unordered_set<SchemaVersion>& used_packed_schema_versions,
        Slice delete_vector_ids)
        : involved_tablet_(involved_tablet),
          pairs_(*pairs),
          schema_versions_map_(schema_versions_map),
          used_packed_schema_versions_(used_packed_schema_versions),
          delete_vector_ids_(delete_vector_ids),
          out_(out) {}

    void SetKey(const Slice& slice) override {
      out_->dup_key(slice);
    }

    void SetValue(const Slice& slice) override {
      out_->ref_value(out_->arena().CatSlices(slice, delete_vector_ids_));
    }

    const Uuid& InvolvedTablet() override {
      return involved_tablet_;
    }

    const Status& GetOutcome() { return status; }

    std::optional<std::pair<Slice, Slice>> Next() override {
      if (next_idx_ >= pairs_.size()) {
        return std::nullopt;
      }

      const auto& input = pairs_[next_idx_];
      ++next_idx_;

      Slice key(input.key());
      Slice value(input.value().binary_value());
      std::optional<SchemaVersion> new_schema_version;
      status =
          UpdatePackedRow(key, value, schema_versions_map_, new_schema_version, &updated_value);
      if (!status.ok()) {
        LOG(WARNING) << "Could not update packed row with consumer schema version";
        return std::nullopt;
      }

      if (new_schema_version) {
        used_packed_schema_versions_.insert(*new_schema_version);
      }

      return std::pair(key, updated_value.AsSlice());
    }

   private:
    Uuid involved_tablet_;
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& pairs_;
    const cdc::XClusterSchemaVersionMap& schema_versions_map_;
    std::unordered_set<SchemaVersion>& used_packed_schema_versions_;
    Slice delete_vector_ids_;
    docdb::KeyValuePairMsg* out_;
    int next_idx_ = 0;
    ValueBuffer updated_value;
    Status status = Status::OK();
  };

  Provider provider(
      involved_tablet, &pairs, schema_versions_map, out, used_packed_schema_versions,
      delete_vector_ids);
  docdb::CombineExternalIntents(txn_id, subtransaction_id, &provider);
  return provider.GetOutcome();
}

Status CombineExternalIntents(
    const tablet::TransactionStatePB& transaction_state, SubTransactionId subtransaction_id,
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& pairs,
    Slice delete_vector_ids,
    docdb::KeyValuePairMsg* out, std::unordered_set<SchemaVersion>& used_packed_schema_versions,
    const cdc::XClusterSchemaVersionMap& schema_versions_map) {
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(transaction_state.transaction_id()));
  SCHECK_EQ(transaction_state.tablets().size(), 1, InvalidArgument, "Wrong tablets number");
  auto source_tablet = VERIFY_RESULT(Uuid::FromHexString(transaction_state.tablets()[0]));
  return CombineExternalIntents(
      txn_id, source_tablet, subtransaction_id, pairs, delete_vector_ids, out,
      used_packed_schema_versions, schema_versions_map);
}

// Tombstones the ids replaced by a non-transactional source op at the op's hybrid time: the request
// applies an external transaction without key-value pairs, committed at that time. Batch level ids
// would be tombstoned at the request's hybrid time, the time of its first record.
Status AddVectorIdTombstones(
    const TransactionId& txn_id, const cdc::CDCRecordPB& record,
    std::unordered_set<SchemaVersion>& used_packed_schema_versions,
    const cdc::XClusterSchemaVersionMap& schema_versions_map,
    docdb::KeyValueWriteBatchMsg& write_batch) {
  RETURN_NOT_OK(CombineExternalIntents(
      txn_id, Uuid::Nil(), kMinSubTransactionId,
      google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>(), Slice(record.delete_vector_ids()),
      write_batch.mutable_write_pairs()->Add(), used_packed_schema_versions, schema_versions_map));
  auto* apply_txn = write_batch.mutable_apply_external_transactions()->Add();
  apply_txn->dup_transaction_id(txn_id.AsSlice());
  apply_txn->set_commit_hybrid_time(record.time());
  apply_txn->set_filter_range_encoded(true);
  return Status::OK();
}

Status AddRecord(
    const ProcessRecordInfo& process_record_info, const cdc::CDCRecordPB& record,
    std::unordered_set<SchemaVersion>& used_packed_schema_versions,
    docdb::KeyValueWriteBatchMsg* write_batch) {
  if (record.operation() == cdc::CDCRecordPB::APPLY) {
    auto* apply_txn = write_batch->mutable_apply_external_transactions()->Add();
    apply_txn->dup_transaction_id(record.transaction_state().transaction_id());
    auto aborted_subtransactions =
        VERIFY_RESULT(SubtxnSet::FromPB(record.transaction_state().aborted().set()));
    aborted_subtransactions.ToPB(apply_txn->mutable_aborted_subtransactions()->mutable_set());
    apply_txn->set_commit_hybrid_time(record.transaction_state().commit_hybrid_time());

    // Only apply records within the same range that the producer applied.
    // In Bi-directional xCluster, both clusters can upgrade at the same time which can lead to
    // producer, and poller node running on the new version, while the consumer tablet leader (in
    // uneven distributions) is still on the old version.
    // AutoFlag ensures the destination tserver that performs the APPLY is on a version that can
    // handle encoded keys. The has_encoded* check is used to handle the case when the producer is
    // still on an older version.
    if (FLAGS_xcluster_use_encoded_key_filter &&
        (record.has_encoded_start_key() || record.has_encoded_end_key())) {
      apply_txn->dup_filter_start_key(record.encoded_start_key());
      apply_txn->dup_filter_end_key(record.encoded_end_key());
      apply_txn->set_filter_range_encoded(true);
    } else {
      // (DEPRECATE_EOL 2.27) If the producer has yet to upgrade, we will not have the encoded keys.
      // Fallback to the old behavior which only works for hash partitioned tables.
      apply_txn->dup_filter_start_key(record.partition().partition_key_start());
      apply_txn->dup_filter_end_key(record.partition().partition_key_end());
      apply_txn->set_filter_range_encoded(false);
    }

    return Status::OK();
  }

  if (record.has_transaction_state()) {
    auto* write_pair = write_batch->mutable_write_pairs()->Add();
    Slice delete_vector_ids;
    if (record.has_delete_vector_ids()) {
      delete_vector_ids = Slice(record.delete_vector_ids());
    }
    return CombineExternalIntents(
        record.transaction_state(),
        record.has_subtransaction_id() ? record.subtransaction_id() : kMinSubTransactionId,
        record.changes(), delete_vector_ids, write_pair, used_packed_schema_versions,
        process_record_info.schema_versions_map);
  }

  for (const auto& kv_pair : record.changes()) {
    auto* write_pair = write_batch->mutable_write_pairs()->Add();
    write_pair->dup_key(kv_pair.key());

    // Update value with local schema version before writing it out.
    Slice key(kv_pair.key());
    Slice value(kv_pair.value().binary_value());
    ValueBuffer updated_value;
    std::optional<SchemaVersion> new_schema_version;
    RETURN_NOT_OK(UpdatePackedRow(
        key, value, process_record_info.schema_versions_map, new_schema_version, &updated_value));

    write_pair->dup_value(updated_value.AsSlice());

    if (PREDICT_FALSE(FLAGS_TEST_xcluster_write_hybrid_time)) {
      // Used only for testing external hybrid time.
      write_pair->set_external_hybrid_time(yb::kInitialHybridTimeValue);
    } else {
      write_pair->set_external_hybrid_time(record.time());
    }

    if (new_schema_version) {
      used_packed_schema_versions.insert(*new_schema_version);
    }
  }

  return Status::OK();
}

// XClusterWriteImplementation creates a WriteRequest for each tablet records present within a
// single producer batch.
class XClusterWriteImplementation : public XClusterWriteInterface {
  ~XClusterWriteImplementation() = default;

  Status ProcessRecord(
      const ProcessRecordInfo& process_record_info, const cdc::CDCRecordPB& record) override {
    const auto& tablet_id = process_record_info.tablet_id;
    auto& used_packed_schema_versions =
        used_packed_schema_versions_[process_record_info.colocation_id];
    auto& tablet_request = GetTabletRequest(tablet_id, record);
    RETURN_NOT_OK(AddRecord(
        process_record_info, record, used_packed_schema_versions,
        tablet_request.request->mutable_write_batch()));

    if (record.operation() != cdc::CDCRecordPB::WRITE &&
        record.operation() != cdc::CDCRecordPB::DELETE) {
      return Status::OK();
    }
    // All records of a source WAL op arrive consecutively and carry the op's hybrid time, so the
    // tablets stamped with it are the ones that received rows of the op.
    tablet_request.last_write_op_time = record.time();
    if (!record.has_delete_vector_ids()) {
      return Status::OK();
    }

    // The source does not know which row each replaced vector id belongs to, so it attaches the
    // ids of its whole write batch to the batch's last record. When the target is partitioned
    // differently, the batch's rows can land on several target tablets, and each of them must
    // tombstone the ids or its vector index keeps returning the replaced vectors (#34419).
    // Tombstoning an id on a tablet that never had it is correct, since vector ids are unique.
    Slice delete_vector_ids(record.delete_vector_ids());
    std::optional<TransactionId> tombstones_txn_id;
    for (auto& [op_tablet_id, op_tablet_request] : records_) {
      if (op_tablet_request.last_write_op_time != record.time()) {
        continue;
      }
      auto& write_batch = *op_tablet_request.request->mutable_write_batch();
      // Empty batch level ids tombstone nothing but mark the tablet as having vector deletions, so
      // its searches check their candidates. Tables without the reverse mapping send only empty
      // ids, see DocWriteBatch::MarkVectorDeletion.
      write_batch.dup_delete_vector_ids(Slice());
      if (delete_vector_ids.empty()) {
        continue;
      }
      if (!record.has_transaction_state()) {
        if (!tombstones_txn_id) {
          tombstones_txn_id = TransactionId::GenerateRandom();
        }
        RETURN_NOT_OK(AddVectorIdTombstones(
            *tombstones_txn_id, record, used_packed_schema_versions,
            process_record_info.schema_versions_map, write_batch));
        continue;
      }
      // AddRecord already appended the ids to this record's external intents.
      if (op_tablet_id == tablet_id) {
        continue;
      }
      // External intents without key-value pairs: applying them only tombstones the ids.
      RETURN_NOT_OK(CombineExternalIntents(
          record.transaction_state(),
          record.has_subtransaction_id() ? record.subtransaction_id() : kMinSubTransactionId,
          google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>(), delete_vector_ids,
          write_batch.mutable_write_pairs()->Add(), used_packed_schema_versions,
          process_record_info.schema_versions_map));
    }
    return Status::OK();
  }

  std::shared_ptr<WriteRequestMsg> FetchNextRequest() override {
    if (records_.empty()) {
      return nullptr;
    }
    auto it = records_.begin();
    auto next_req = std::move(it->second.request);
    records_.erase(it);

    for (const auto& [colocation_id, used_packed_schema_versions] : used_packed_schema_versions_) {
      if (used_packed_schema_versions.empty()) {
        continue;
      }
      auto used_schema_versions =
          next_req->mutable_write_batch()->add_xcluster_used_schema_versions();
      used_schema_versions->set_colocation_id(colocation_id);
      used_schema_versions->mutable_schema_versions()->assign(
          used_packed_schema_versions.begin(), used_packed_schema_versions.end());
    }
    return next_req;
  }

 private:
  struct TabletRequest {
    std::shared_ptr<WriteRequestMsg> request;
    // Hybrid time of the last source WAL op whose WRITE/DELETE records reached this tablet.
    uint64_t last_write_op_time = 0;
  };

  TabletRequest& GetTabletRequest(const TabletId& tablet_id, const cdc::CDCRecordPB& record) {
    auto it = records_.find(tablet_id);
    if (it != records_.end()) {
      return it->second;
    }
    auto write_request = rpc::SharedMessage<LWWriteRequestPB>();
    write_request->dup_tablet_id(tablet_id);
    write_request->set_external_hybrid_time(record.time());
    // Set on all consumer writes, but only used for producer filters on automatic mode.
    write_request->set_xcluster_target_applied(true);
    return records_.emplace(tablet_id, TabletRequest{.request = std::move(write_request)})
        .first->second;
  }

  // Contains key value pairs to apply to regular and intents db. The key of this map is the
  // tablet to send to.
  UnorderedStringMap<TabletId, TabletRequest> records_;

  // Keep track of the packed schema versions used.
  // kColocationIdNotSet is used for non-colocated tables.
  std::unordered_map<ColocationId, std::unordered_set<SchemaVersion>> used_packed_schema_versions_;
};

void ResetWriteInterface(std::unique_ptr<XClusterWriteInterface>* write_strategy) {
  write_strategy->reset(new XClusterWriteImplementation());
}

} // namespace tserver
} // namespace yb
