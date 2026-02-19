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

#include "yb/common/hybrid_time.h"

// Below batch related configs are deprecated because splitting a single producer side
// batch into multiple consumer side batches can cause overwrite of intents and lead to rocksdb
// corruption.
DEPRECATE_FLAG(int32, cdc_max_apply_batch_num_records, "4_2023");

DEPRECATE_FLAG(uint32, cdc_max_apply_batch_size_bytes, "4_2023");

DEFINE_test_flag(
    bool, xcluster_write_hybrid_time, false,
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
    const tablet::TransactionStatePB& transaction_state, SubTransactionId subtransaction_id,
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& pairs,
    docdb::KeyValuePairMsg* out, std::unordered_set<SchemaVersion>& used_packed_schema_versions,
    const cdc::XClusterSchemaVersionMap& schema_versions_map) {
  class Provider : public docdb::ExternalIntentsProvider {
   public:
    Provider(
        const Uuid& involved_tablet,
        const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>* pairs,
        const cdc::XClusterSchemaVersionMap& schema_versions_map, docdb::KeyValuePairMsg* out,
        std::unordered_set<SchemaVersion>& used_packed_schema_versions)
        : involved_tablet_(involved_tablet),
          pairs_(*pairs),
          schema_versions_map_(schema_versions_map),
          used_packed_schema_versions_(used_packed_schema_versions),
          out_(out) {}

    void SetKey(const Slice& slice) override {
      out_->set_key(slice.cdata(), slice.size());
    }

    void SetValue(const Slice& slice) override {
      out_->set_value(slice.cdata(), slice.size());
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
    docdb::KeyValuePairMsg* out_;
    int next_idx_ = 0;
    ValueBuffer updated_value;
    Status status = Status::OK();
  };

  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(transaction_state.transaction_id()));
  SCHECK_EQ(transaction_state.tablets().size(), 1, InvalidArgument, "Wrong tablets number");
  auto source_tablet = VERIFY_RESULT(Uuid::FromHexString(transaction_state.tablets()[0]));

  Provider provider(source_tablet, &pairs, schema_versions_map, out, used_packed_schema_versions);
  docdb::CombineExternalIntents(txn_id, subtransaction_id, &provider);
  return provider.GetOutcome();
}

Status AddRecord(
    const ProcessRecordInfo& process_record_info, const cdc::CDCRecordPB& record,
    std::unordered_set<SchemaVersion>& used_packed_schema_versions,
    docdb::KeyValueWriteBatchMsg* write_batch) {
  if (record.operation() == cdc::CDCRecordPB::APPLY) {
    auto* apply_txn = write_batch->mutable_apply_external_transactions()->Add();
    apply_txn->set_transaction_id(record.transaction_state().transaction_id());
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
      apply_txn->set_filter_start_key(record.encoded_start_key());
      apply_txn->set_filter_end_key(record.encoded_end_key());
      apply_txn->set_filter_range_encoded(true);
    } else {
      // (DEPRECATE_EOL 2.27) If the producer has yet to upgrade, we will not have the encoded keys.
      // Fallback to the old behavior which only works for hash partitioned tables.
      apply_txn->set_filter_start_key(record.partition().partition_key_start());
      apply_txn->set_filter_end_key(record.partition().partition_key_end());
      apply_txn->set_filter_range_encoded(false);
    }

    return Status::OK();
  }

  if (record.has_transaction_state()) {
    auto* write_pair = write_batch->mutable_write_pairs()->Add();
    return CombineExternalIntents(
        record.transaction_state(),
        record.has_subtransaction_id() ? record.subtransaction_id() : kMinSubTransactionId,
        record.changes(), write_pair, used_packed_schema_versions,
        process_record_info.schema_versions_map);
  }

  for (const auto& kv_pair : record.changes()) {
    auto* write_pair = write_batch->mutable_write_pairs()->Add();
    write_pair->set_key(kv_pair.key());

    // Update value with local schema version before writing it out.
    Slice key(kv_pair.key());
    Slice value(kv_pair.value().binary_value());
    ValueBuffer updated_value;
    std::optional<SchemaVersion> new_schema_version;
    RETURN_NOT_OK(UpdatePackedRow(
        key, value, process_record_info.schema_versions_map, new_schema_version, &updated_value));

    const Slice& updated_value_slice = updated_value.AsSlice();
    write_pair->set_value(updated_value_slice.cdata(), updated_value_slice.size());

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
    docdb::KeyValueWriteBatchMsg* write_batch = nullptr;
    // Finally, handle records to be applied to both regular and intents db.
    auto it = records_.find(tablet_id);
    if (it == records_.end()) {
      // Create a write request for tablet.
      auto write_request = std::make_unique<WriteRequestPB>();
      write_request->set_tablet_id(tablet_id);
      write_request->set_external_hybrid_time(record.time());
      write_batch = write_request->mutable_write_batch();

      records_.emplace(tablet_id, std::move(write_request));
    } else {
      write_batch = it->second->mutable_write_batch();
    }

    return AddRecord(
        process_record_info, record,
        used_packed_schema_versions_[process_record_info.colocation_id], write_batch);
  }

  std::shared_ptr<WriteRequestMsg> FetchNextRequest() override {
    if (records_.empty()) {
      return nullptr;
    }
    auto it = records_.begin();
    auto next_req = std::move(it->second);
    records_.erase(it);

    for (const auto& [colocation_id, used_packed_schema_versions] : used_packed_schema_versions_) {
      if (used_packed_schema_versions.empty()) {
        continue;
      }
      auto used_schema_versions =
          next_req->mutable_write_batch()->add_xcluster_used_schema_versions();
      used_schema_versions->set_colocation_id(colocation_id);
      used_schema_versions->mutable_schema_versions()->Assign(
          used_packed_schema_versions.begin(), used_packed_schema_versions.end());
    }
    return next_req;
  }

 private:
  // Contains key value pairs to apply to regular and intents db. The key of this map is the
  // tablet to send to.
  UnorderedStringMap<TabletId, std::shared_ptr<WriteRequestMsg>> records_;

  // Keep track of the packed schema versions used.
  // kColocationIdNotSet is used for non-colocated tables.
  std::unordered_map<ColocationId, std::unordered_set<SchemaVersion>> used_packed_schema_versions_;
};

void ResetWriteInterface(std::unique_ptr<XClusterWriteInterface>* write_strategy) {
  write_strategy->reset(new XClusterWriteImplementation());
}

} // namespace tserver
} // namespace yb
