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

#include <deque>

#include "yb/client/client_fwd.h"

#include "yb/common/transaction.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/key_bytes.h"

#include "yb/tserver/twodc_write_interface.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/cdc/cdc_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/size_literals.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags.h"

#include "yb/common/hybrid_time.h"

DEFINE_int32(cdc_max_apply_batch_num_records, 1024, "Max CDC write request batch num records. If"
                                                    " set to 0, there is no max num records, which"
                                                    " means batches will be limited only by size.");
TAG_FLAG(cdc_max_apply_batch_num_records, runtime);

DEFINE_int32(cdc_max_apply_batch_size_bytes, 0, "Max CDC write request batch size in kb. If 0,"
                                                " default to consensus_max_batch_size_bytes.");
TAG_FLAG(cdc_max_apply_batch_size_bytes, runtime);

DEFINE_test_flag(bool, twodc_write_hybrid_time, false,
                 "Override external_hybrid_time with initialHybridTimeValue for testing.");

DECLARE_uint64(consensus_max_batch_size_bytes);

namespace yb {

using namespace yb::size_literals;

namespace tserver {
namespace enterprise {

Status CombineExternalIntents(
    const tablet::TransactionStatePB& transaction_state,
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& pairs,
    google::protobuf::RepeatedPtrField<docdb::KeyValuePairPB> *out) {

  class Provider : public docdb::ExternalIntentsProvider {
   public:
    Provider(
        const Uuid& involved_tablet,
        const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>* pairs,
        docdb::KeyValuePairPB* out)
        : involved_tablet_(involved_tablet), pairs_(*pairs), out_(out) {
    }

    void SetKey(const Slice& slice) override {
      out_->set_key(slice.cdata(), slice.size());
    }

    void SetValue(const Slice& slice) override {
      out_->set_value(slice.cdata(), slice.size());
    }

    const Uuid& InvolvedTablet() override {
      return involved_tablet_;
    }

    boost::optional<std::pair<Slice, Slice>> Next() override {
      if (next_idx_ >= pairs_.size()) {
        return boost::none;
      }

      const auto& input = pairs_[next_idx_];
      ++next_idx_;

      return std::pair<Slice, Slice>(input.key(), input.value().binary_value());
    }

   private:
    Uuid involved_tablet_;
    const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& pairs_;
    docdb::KeyValuePairPB* out_;
    int next_idx_ = 0;
  };

  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(transaction_state.transaction_id()));
  SCHECK_EQ(transaction_state.tablets().size(), 1, InvalidArgument, "Wrong tablets number");
  auto status_tablet = VERIFY_RESULT(Uuid::FromHexString(transaction_state.tablets()[0]));

  Provider provider(status_tablet, &pairs, out->Add());
  docdb::CombineExternalIntents(txn_id, &provider);
  return Status::OK();
}

Status AddRecord(const ProcessRecordInfo& process_record_info,
                 const cdc::CDCRecordPB& record,
                 docdb::KeyValueWriteBatchPB* write_batch) {
  if (record.operation() == cdc::CDCRecordPB::APPLY) {
    if (process_record_info.enable_replicate_transaction_status_table) {
      // If we are replicating the transaction status table, we don't need to process individual
      // APPLY records since the target txn status table will be responsible for fanning out Apply
      // RPCs to involved tablets.
      return Status::OK();
    }
    auto* apply_txn = write_batch->mutable_apply_external_transactions()->Add();
    apply_txn->set_transaction_id(record.transaction_state().transaction_id());
    apply_txn->set_commit_hybrid_time(record.transaction_state().commit_hybrid_time());
    return Status::OK();
  }

  if (!process_record_info.enable_replicate_transaction_status_table &&
      record.has_transaction_state()) {
    return CombineExternalIntents(
        record.transaction_state(), record.changes(), write_batch->mutable_write_pairs());
  }

  for (const auto& kv_pair : record.changes()) {
    auto* write_pair = write_batch->mutable_write_pairs()->Add();
    write_pair->set_key(kv_pair.key());
    write_pair->set_value(kv_pair.value().binary_value());
    if (PREDICT_FALSE(FLAGS_TEST_twodc_write_hybrid_time)) {
      // Used only for testing external hybrid time.
      write_pair->set_external_hybrid_time(yb::kInitialHybridTimeValue);
    } else {
      write_pair->set_external_hybrid_time(record.time());
    }
    if (record.has_transaction_state()) {
      // enable_replicate_transaction_status_table is true.
      TransactionMetadataPB metadata;
      metadata.set_transaction_id(record.transaction_state().transaction_id());
      metadata.set_status_tablet(process_record_info.status_tablet_id);
      metadata.set_isolation(IsolationLevel::SNAPSHOT_ISOLATION);
      *write_pair->mutable_transaction() = metadata;
      write_batch->set_enable_replicate_transaction_status_table(
        true /* enable_replicate_transaction_status_table */);
    }
  }

  return Status::OK();
}

// The BatchedWriteImplementation strategy batches together multiple records per WriteRequestPB.
// Max number of records in a request is cdc_max_apply_batch_num_records, and max size of a request
// is cdc_max_apply_batch_size_kb. Batches are not sent by opid order, since a GetChangesResponse
// can contain interleaved records to multiple tablets. Rather, we send batches to each tablet
// in order for that tablet, before moving on to the next tablet.
class BatchedWriteImplementation : public TwoDCWriteInterface {
  ~BatchedWriteImplementation() = default;

  Status ProcessRecord(
      const ProcessRecordInfo& process_record_info, const cdc::CDCRecordPB& record) override {
    const auto& tablet_id = process_record_info.tablet_id;
    // Finally, handle records to be applied to both regular and intents db.
    auto it = records_.find(tablet_id);
    if (it == records_.end()) {
      it = records_.emplace(tablet_id, std::deque<std::unique_ptr<WriteRequestPB>>()).first;
    }

    auto& queue = it->second;

    auto max_batch_records = FLAGS_cdc_max_apply_batch_num_records != 0 ?
        FLAGS_cdc_max_apply_batch_num_records : std::numeric_limits<uint32_t>::max();
    auto max_batch_size = FLAGS_cdc_max_apply_batch_size_bytes != 0 ?
        FLAGS_cdc_max_apply_batch_size_bytes : FLAGS_consensus_max_batch_size_bytes;

    if (queue.empty() ||
        implicit_cast<size_t>(queue.back()->write_batch().write_pairs_size())
            >= max_batch_records ||
        queue.back()->ByteSizeLong() >= max_batch_size) {
      // Create a new batch.
      auto req = std::make_unique<WriteRequestPB>();
      req->set_tablet_id(tablet_id);
      req->set_external_hybrid_time(record.time());
      queue.push_back(std::move(req));
    }
    auto* write_request = queue.back().get();

    return AddRecord(process_record_info, record, write_request->mutable_write_batch());
  }

  Status ProcessCommitRecord(
      const std::string& status_tablet,
      const std::vector<std::string>& involved_target_tablet_ids,
      const cdc::CDCRecordPB& record) override {
    DCHECK(record.operation() == cdc::CDCRecordPB::COMMITTED);
    transaction_metadatas_.push_back(client::ExternalTransactionMetadata {
      .transaction_id = VERIFY_RESULT(
          FullyDecodeTransactionId(record.transaction_state().transaction_id())),
      .status_tablet = status_tablet,
      .commit_ht = record.time(),
      .involved_tablet_ids = involved_target_tablet_ids
    });
    return Status::OK();
  }

  std::unique_ptr<WriteRequestPB> GetNextWriteRequest() override {
    if (records_.empty()) {
      return nullptr;
    }
    auto& queue = records_.begin()->second;
    auto next_req = std::move(queue.front());
    queue.pop_front();
    if (queue.empty()) {
      records_.erase(next_req->tablet_id());
    }
    return next_req;
  }

  std::vector<client::ExternalTransactionMetadata>& GetTransactionMetadatas() override {
    return transaction_metadatas_;
  }

 private:
  // Contains key value pairs to apply to regular and intents db. The key of this map is the
  // tablet to send to.
  std::map<TabletId, std::deque<std::unique_ptr<WriteRequestPB>>> records_;
  std::vector<client::ExternalTransactionMetadata> transaction_metadatas_;
};

void ResetWriteInterface(std::unique_ptr<TwoDCWriteInterface>* write_strategy) {
  write_strategy->reset(new BatchedWriteImplementation());
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
