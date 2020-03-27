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
//

#include "yb/docdb/docdb_compaction_filter_intents.h"

#include <memory>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/rocksdb/compaction_filter.h"
#include "yb/util/string_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/value.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/thread_pool.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/util/flag_tags.h"

DEFINE_uint64(aborted_intent_cleanup_ms, 60000, // 1 minute by default, 1 sec for testing
             "Duration in ms after which to check if a transaction is aborted.");

DEFINE_int32(aborted_intent_cleanup_max_batch_size, 256, // Cleanup 256 transactions at a time
             "Number of transactions to collect for possible cleanup.");

using std::shared_ptr;
using std::unique_ptr;
using std::unordered_set;
using rocksdb::CompactionFilter;
using rocksdb::VectorToString;

namespace yb {
namespace docdb {

// ------------------------------------------------------------------------------------------------

namespace {

class DocDBIntentsCompactionFilter : public rocksdb::CompactionFilter {
 public:
  explicit DocDBIntentsCompactionFilter(tablet::Tablet* tablet, const KeyBounds* key_bounds)
      : tablet_(tablet), compaction_start_time_(tablet->clock()->Now().GetPhysicalValueMicros()) {}

  ~DocDBIntentsCompactionFilter() override;

  rocksdb::FilterDecision Filter(
      int level, const Slice& key, const Slice& existing_value, std::string* new_value,
      bool* value_changed) override;

  const char* Name() const override;

  TransactionIdSet& transactions_to_cleanup() {
    return transactions_to_cleanup_;
  }

  void AddToSet(const TransactionId& transaction_id);

 private:
  tablet::Tablet* const tablet_;
  const MicrosTime compaction_start_time_;

  TransactionIdSet transactions_to_cleanup_;
  int rejected_transactions_ = 0;

  // We use this to only log a message that the filter is being used once on the first call to
  // the Filter function.
  bool filter_usage_logged_ = false;
};


DocDBIntentsCompactionFilter::~DocDBIntentsCompactionFilter() {
  VLOG(3) << "DocDB intents compaction filter is being deleted";
  if (transactions_to_cleanup_.empty()) {
    return;
  }
  TransactionStatusManager* manager = tablet_->transaction_participant();
  if (rejected_transactions_ > 0) {
    LOG(WARNING) << "Number of aborted transactions not cleaned up " <<
                 "on account of reaching size limits:" << rejected_transactions_;
  }
  manager->Cleanup(std::move(transactions_to_cleanup_));
}

rocksdb::FilterDecision DocDBIntentsCompactionFilter::Filter(
    int level, const Slice& key, const Slice& existing_value, std::string* new_value,
    bool* value_changed) {
  if (!filter_usage_logged_) {
    VLOG(3) << "DocDB intents compaction filter is being used for a compaction";
    filter_usage_logged_ = true;
  }

  // Find transaction metadata row.
  if (GetKeyType(key, StorageDbType::kIntents) == KeyType::kTransactionMetadata) {
    TransactionMetadataPB metadata_pb;
    if (!metadata_pb.ParseFromArray(existing_value.cdata(), existing_value.size())) {
      LOG(ERROR) << "Transaction metadata failed to parse.";
      return rocksdb::FilterDecision::kKeep;
    }
    uint64_t write_time = metadata_pb.metadata_write_time();
    if (!write_time) {
      write_time = HybridTime(metadata_pb.start_hybrid_time()).GetPhysicalValueMicros();
    }
    if (compaction_start_time_ < write_time + FLAGS_aborted_intent_cleanup_ms * 1000) {
      return rocksdb::FilterDecision::kKeep;
    }
    auto result = DecodeTransactionIdFromIntentValue(const_cast<Slice*>(&key));
    if (!result.ok()) {
      LOG(ERROR) << "Could not decode Transaction metadata: " << result.status();
      return rocksdb::FilterDecision::kKeep;
    }
    AddToSet(*result);
  }

  // TODO(dtxn): If/when we add processing of reverse index or intents here - we will need to
  // respect key_bounds passed to constructor in order to ignore/delete non-relevant keys. As of
  // 2019/06/19, intents and reverse indexes are being deleted by docdb::PrepareApplyIntentsBatch.

  return rocksdb::FilterDecision::kKeep;
}

void DocDBIntentsCompactionFilter::AddToSet(const TransactionId& transaction_id) {
  if (transactions_to_cleanup_.size() <= FLAGS_aborted_intent_cleanup_max_batch_size) {
    transactions_to_cleanup_.insert(transaction_id);
  } else {
    rejected_transactions_++;
  }
}

const char* DocDBIntentsCompactionFilter::Name() const {
  return "DocDBIntentsCompactionFilter";
}

} // namespace

// ------------------------------------------------------------------------------------------------

DocDBIntentsCompactionFilterFactory::DocDBIntentsCompactionFilterFactory(
    tablet::Tablet* tablet, const KeyBounds* key_bounds)
    : tablet_(tablet), key_bounds_(key_bounds) {}

DocDBIntentsCompactionFilterFactory::~DocDBIntentsCompactionFilterFactory() {}

std::unique_ptr<CompactionFilter> DocDBIntentsCompactionFilterFactory::CreateCompactionFilter(
    const CompactionFilter::Context& context) {
  return std::make_unique<DocDBIntentsCompactionFilter>(tablet_, key_bounds_);
}

const char* DocDBIntentsCompactionFilterFactory::Name() const {
  return "DocDBIntentsCompactionFilterFactory";
}

}  // namespace docdb
}  // namespace yb
