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

#include <boost/multi_index/member.hpp>

#include "yb/common/common.pb.h"

#include "yb/docdb/docdb.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/dockv/intent.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/compaction_filter.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/string_util.h"
#include "yb/util/flags.h"

DEFINE_RUNTIME_uint64(aborted_intent_cleanup_ms, 60000,  // 1 minute by default, 1 sec for testing
    "Duration in ms after which to check if a transaction is aborted.");

DEFINE_UNKNOWN_uint64(aborted_intent_cleanup_max_batch_size, 256,
    // Cleanup 256 transactions at a time
    "Number of transactions to collect for possible cleanup.");

DEFINE_UNKNOWN_uint32(external_intent_cleanup_secs, 60 * 60 * 24,  // 24 hours by default
    "Duration in secs after which to cleanup external intents.");

DEFINE_UNKNOWN_uint64(intents_compaction_filter_max_errors_to_log, 100,
              "Maximum number of errors to log for life cycle of the intents compcation filter.");

using std::unique_ptr;
using rocksdb::CompactionFilter;

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

  void CompactionFinished() override;

 private:
  Status CleanupTransactions();

  std::string LogPrefix() const;

  Result<boost::optional<TransactionId>> FilterTransactionMetadata(
      const Slice& key, const Slice& existing_value);

  Result<boost::optional<TransactionId>> FilterExternalIntent(const Slice& key);

  Result<std::pair<TransactionId, OpId>> ParsePostApplyTransactionMetadata(
      const Slice& key, const Slice& existing_value);

  void AddTransactionToCleanup(const TransactionId& transaction_id);

  void SetTransactionApplyOpId(const TransactionId& transaction_id, const OpId& apply_op_id);

  tablet::Tablet* const tablet_;
  const MicrosTime compaction_start_time_;

  TransactionIdApplyOpIdMap transactions_to_cleanup_;
  int rejected_transactions_ = 0;
  uint64_t num_errors_ = 0;

  // We use this to only log a message that the filter is being used once on the first call to
  // the Filter function.
  bool filter_usage_logged_ = false;
};

#define MAYBE_LOG_ERROR_AND_RETURN_KEEP(result) { \
  if (!result.ok()) { \
    if (num_errors_ < GetAtomicFlag(&FLAGS_intents_compaction_filter_max_errors_to_log)) { \
      LOG_WITH_PREFIX(ERROR) << StatusToString(result.status()); \
    } \
    num_errors_++; \
    return rocksdb::FilterDecision::kKeep; \
  } \
}

Status DocDBIntentsCompactionFilter::CleanupTransactions() {
  VLOG_WITH_PREFIX(3) << "DocDB intents compaction filter is being deleted";
  if (!transactions_to_cleanup_.empty()) {
    TransactionStatusManager* manager = tablet_->transaction_participant();
    if (rejected_transactions_ > 0) {
      LOG_WITH_PREFIX(WARNING) << "Number of aborted transactions not cleaned up "
                               << "on account of reaching size limits: " << rejected_transactions_;
    }
    RETURN_NOT_OK(manager->Cleanup(std::move(transactions_to_cleanup_)));
  }

  return Status::OK();
}

DocDBIntentsCompactionFilter::~DocDBIntentsCompactionFilter() {
}

rocksdb::FilterDecision DocDBIntentsCompactionFilter::Filter(
    int level, const Slice& key, const Slice& existing_value, std::string* new_value,
    bool* value_changed) {
  if (!filter_usage_logged_) {
    VLOG_WITH_PREFIX(3) << "DocDB intents compaction filter is being used for a compaction";
    filter_usage_logged_ = true;
  }

  const auto key_type = GetKeyType(key, StorageDbType::kIntents);
  if (key_type == KeyType::kExternalIntents) {
    auto transaction_id_result = FilterExternalIntent(key);
    MAYBE_LOG_ERROR_AND_RETURN_KEEP(transaction_id_result);
    auto transaction_id_optional = *transaction_id_result;
    if (!transaction_id_optional.has_value()) {
      return rocksdb::FilterDecision::kKeep;
    }
    return rocksdb::FilterDecision::kDiscard;
  }

  if (key_type == KeyType::kTransactionMetadata) {
    auto transaction_id_result = FilterTransactionMetadata(key, existing_value);
    MAYBE_LOG_ERROR_AND_RETURN_KEEP(transaction_id_result);
    auto transaction_id_optional = *transaction_id_result;
    if (!transaction_id_optional.has_value()) {
      return rocksdb::FilterDecision::kKeep;
    }
    AddTransactionToCleanup(*transaction_id_optional);
  }

  if (key_type == KeyType::kPostApplyTransactionMetadata) {
    auto result = ParsePostApplyTransactionMetadata(key, existing_value);
    MAYBE_LOG_ERROR_AND_RETURN_KEEP(result);
    const auto& [transaction_id, apply_op_id] = *result;
    SetTransactionApplyOpId(transaction_id, apply_op_id);
  }

  // TODO(dtxn): If/when we add processing of reverse index or intents here - we will need to
  // respect key_bounds passed to constructor in order to ignore/delete non-relevant keys. As of
  // 06/19/2019, intents and reverse indexes are being deleted by docdb::PrepareApplyIntentsBatch.
  return rocksdb::FilterDecision::kKeep;
}

Result<boost::optional<TransactionId>> DocDBIntentsCompactionFilter::FilterTransactionMetadata(
    const Slice& key, const Slice& existing_value) {
  TransactionMetadataPB metadata_pb;
  if (!metadata_pb.ParseFromArray(
          existing_value.cdata(), narrow_cast<int>(existing_value.size()))) {
    return STATUS(IllegalState, "Failed to parse transaction metadata");
  }
  uint64_t write_time = metadata_pb.metadata_write_time();
  if (!write_time) {
    write_time = HybridTime(metadata_pb.start_hybrid_time()).GetPhysicalValueMicros();
  }

  if (write_time > compaction_start_time_) {
    return boost::none;
  }

  const uint64_t delta_micros = compaction_start_time_ - write_time;
  if (delta_micros <
      GetAtomicFlag(&FLAGS_aborted_intent_cleanup_ms) * MonoTime::kMillisecondsPerSecond) {
    return boost::none;
  }

  Slice key_slice = key;
  return VERIFY_RESULT_PREPEND(
      dockv::DecodeTransactionIdFromIntentValue(&key_slice),
      "Could not decode Transaction metadata");
}

Result<std::pair<TransactionId, OpId>>
DocDBIntentsCompactionFilter::ParsePostApplyTransactionMetadata(
    const Slice& key, const Slice& existing_value) {
  PostApplyTransactionMetadataPB metadata_pb;
  if (!metadata_pb.ParseFromArray(
          existing_value.cdata(), narrow_cast<int>(existing_value.size()))) {
    return STATUS(IllegalState, "Failed to parse post-apply transaction metadata");
  }

  OpId apply_op_id = metadata_pb.has_apply_op_id() ? OpId::FromPB(metadata_pb.apply_op_id())
                                                   : OpId::Invalid();

  Slice key_slice = key;
  TransactionId transaction_id = VERIFY_RESULT_PREPEND(
      dockv::DecodeTransactionIdFromIntentValue(&key_slice),
      "Could not decode post-apply transaction metadata");

  return std::make_pair(transaction_id, apply_op_id);
}

Result<boost::optional<TransactionId>> DocDBIntentsCompactionFilter::FilterExternalIntent(
    const Slice& key) {
  Slice key_slice = key;
  // We know the first byte of the slice is kExternalTransactionId or kTransactionId, so we can
  // safely strip if off.
  key_slice.consume_byte();
  auto txn_id = VERIFY_RESULT_PREPEND(
      DecodeTransactionId(&key_slice), "Could not decode external transaction id");
  auto doc_hybrid_time = VERIFY_RESULT_PREPEND(
      dockv::DecodeInvertedDocHt(key_slice), "Could not decode hybrid time");
  auto write_time_micros = doc_hybrid_time.hybrid_time().GetPhysicalValueMicros();
  int64_t delta_micros = compaction_start_time_ - write_time_micros;
  if (delta_micros >
      GetAtomicFlag(&FLAGS_external_intent_cleanup_secs) * MonoTime::kMicrosecondsPerSecond) {
    return txn_id;
  }
  return boost::none;
}

void DocDBIntentsCompactionFilter::CompactionFinished() {
  if (num_errors_ > 0) {
    LOG_WITH_PREFIX(WARNING) << Format(
        "Found $0 total errors during intents compaction filter.", num_errors_);
  }
  const auto s = CleanupTransactions();
  if (!s.ok()) {
    YB_LOG_EVERY_N_SECS(DFATAL, 60) << "Error cleaning up transactions: " << AsString(s);
  }
}

void DocDBIntentsCompactionFilter::AddTransactionToCleanup(const TransactionId& transaction_id) {
  if (transactions_to_cleanup_.size() <= FLAGS_aborted_intent_cleanup_max_batch_size) {
    transactions_to_cleanup_.emplace(transaction_id, OpId::Invalid());
  } else {
    rejected_transactions_++;
  }
}

void DocDBIntentsCompactionFilter::SetTransactionApplyOpId(
    const TransactionId& transaction_id, const OpId& apply_op_id) {
  auto itr = transactions_to_cleanup_.find(transaction_id);
  if (itr != transactions_to_cleanup_.end()) {
    itr->second = apply_op_id;
  }
}

const char* DocDBIntentsCompactionFilter::Name() const {
  return "DocDBIntentsCompactionFilter";
}

std::string DocDBIntentsCompactionFilter::LogPrefix() const {
  return Format("T $0: ", tablet_->tablet_id());
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
