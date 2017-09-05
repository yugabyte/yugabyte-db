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

#include "yb/docdb/docdb_compaction_filter.h"

#include <memory>

#include <glog/logging.h>

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/util/string_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/value.h"
#include "yb/rocksutil/yb_rocksdb.h"

using std::shared_ptr;
using std::unique_ptr;
using std::unordered_set;
using rocksdb::CompactionFilter;
using rocksdb::VectorToString;

namespace yb {
namespace docdb {

// ------------------------------------------------------------------------------------------------

DocDBCompactionFilter::DocDBCompactionFilter(HybridTime history_cutoff,
                                             ColumnIdsPtr deleted_cols,
                                             bool is_full_compaction,
                                             MonoDelta table_ttl)
    : history_cutoff_(history_cutoff),
      is_full_compaction_(is_full_compaction),
      is_first_key_value_(true),
      filter_usage_logged_(false),
      table_ttl_(table_ttl),
      deleted_cols_(deleted_cols) {
}

DocDBCompactionFilter::~DocDBCompactionFilter() {
}

bool DocDBCompactionFilter::Filter(int level,
                                   const rocksdb::Slice& key,
                                   const rocksdb::Slice& existing_value,
                                   std::string* new_value,
                                   bool* value_changed) const {
  if (!is_full_compaction_) {
    // By default, we only perform history garbage collection on full compactions
    // (or major compactions, in the HBase terminology).
    //
    // TODO: Enable history garbage collection on minor (non-full) compactions as well.
    //       This should be similar to the existing workflow, but should be extensively tested.
    //
    // Here, false means "keep the key/value pair" (don't filter it out).
    return false;
  }

  if (!filter_usage_logged_) {
    // TODO: switch this to VLOG if it becomes too chatty.
    LOG(INFO) << "DocDB compaction filter is being used";
    filter_usage_logged_ = true;
  }

  SubDocKey subdoc_key;

  // TODO: Find a better way for handling of data corruption encountered during compactions.
  const Status key_decode_status = subdoc_key.FullyDecodeFrom(key);
  CHECK(key_decode_status.ok())
    << "Error decoding a key during compaction: " << key_decode_status.ToString() << "\n"
    << "    Key (raw): " << FormatRocksDBSliceAsStr(key) << "\n"
    << "    Key (best-effort decoded): " << BestEffortDocDBKeyToStr(key);

  if (is_first_key_value_) {
    CHECK_EQ(0, overwrite_ht_.size());
    is_first_key_value_ = false;
  }

  const size_t num_shared_components = prev_subdoc_key_.NumSharedPrefixComponents(subdoc_key);

  // Remove overwrite hybrid_times for components that are no longer relevant for the current
  // SubDocKey.
  overwrite_ht_.resize(min(overwrite_ht_.size(), num_shared_components));

  const DocHybridTime& ht = subdoc_key.doc_hybrid_time();

  // We're comparing the hybrid_time in this key with the _previous_ stack top of overwrite_ht_,
  // after truncating the previous hybrid_time to the number of components in the common prefix
  // of previous and current key.
  //
  // Example (history_cutoff_ = 12):
  // --------------------------------------------------------------------------------------------
  // Key          overwrite_ht_ stack and relevant notes
  // --------------------------------------------------------------------------------------------
  // k1 T10       [MinHT]
  // k1 T5        [T10]
  // k1 col1 T11  [T10, T11]
  // k1 col1 T7   The stack does not get truncated (shared prefix length is 2), so
  //              prev_overwrite_ht = 11. Removing this entry because 7 < 11.
  // k1 col2 T9   Truncating the stack to [T10], setting prev_overwrite_ht to 10, and therefore
  //              deciding to remove this entry because 9 < 10.
  //
  const DocHybridTime prev_overwrite_ht =
    overwrite_ht_.empty() ? DocHybridTime::kMin : overwrite_ht_.back();

  // We only keep entries with hybrid_time equal to or later than the latest time the subdocument
  // was fully overwritten or deleted prior to or at the history cutoff hybrid_time. The intuition
  // is that key/value pairs that were overwritten at or before history cutoff time will not be
  // visible at history cutoff time or any later time anyway.
  //
  // Furthermore, we only need to update the overwrite hybrid_time stack in case we have decided to
  // keep the new entry. Otherwise, the current entry's hybrid_time ts is less than the previous
  // overwrite hybrid_time prev_overwrite_ht, and therefore it does not provide any new information
  // about key/value pairs that follow being overwritten at a particular hybrid_time. Another way to
  // explain this is to look at the logic that follows. If we don't early-exit here while ts is less
  // than prev_overwrite_ht, we'll end up adding more prev_overwrite_ht values to the overwrite
  // hybrid_time stack, and we might as well do that while handling the next key/value pair that
  // does not get cleaned up the same way as this one.
  if (ht < prev_overwrite_ht) {
    return true;  // Remove this key/value pair.
  }

  const int new_stack_size = subdoc_key.num_subkeys() + 1;

  // Every subdocument was fully overwritten at least at the time any of its parents was fully
  // overwritten.
  while (overwrite_ht_.size() < new_stack_size - 1) {
    overwrite_ht_.push_back(prev_overwrite_ht);
  }

  // This will happen in case previous key has the same document key and subkeys as the current
  // key, and the only difference is in the hybrid_time. We want to replace the hybrid_time at the
  // top of the overwrite_ht stack in this case.
  if (overwrite_ht_.size() == new_stack_size) {
    overwrite_ht_.pop_back();
  }

  const bool ht_at_or_below_cutoff = ht.hybrid_time() <= history_cutoff_;

  // See if we found a higher hybrid_time not exceeding the history cutoff hybrid_time at which the
  // subdocument (including a primitive value) rooted at the current key was fully overwritten.
  // In case ts > history_cutoff_, we just keep the parent document's highest known overwrite
  // hybrid_time that does not exceed the cutoff hybrid_time. In that case this entry is obviously
  // too new to be garbage-collected.
  overwrite_ht_.push_back(ht_at_or_below_cutoff ? max(prev_overwrite_ht, ht) : prev_overwrite_ht);

  CHECK_EQ(new_stack_size, overwrite_ht_.size());
  prev_subdoc_key_ = std::move(subdoc_key);

  if (prev_subdoc_key_.num_subkeys() > 0 &&
      prev_subdoc_key_.subkeys()[0].value_type() == ValueType::kColumnId) {
    // Column ID is first subkey in YQL tables.
    ColumnId col_id = prev_subdoc_key_.subkeys()[0].GetColumnId();

    if (deleted_cols_->find(col_id) != deleted_cols_->end()) {
      return true;
    }
  }

  const ValueType value_type = DecodeValueType(existing_value);
  MonoDelta ttl;

  // If the value expires by the time of history cutoff, it is treated as deleted and filtered out.
  CHECK_OK(Value::DecodeTTL(existing_value, &ttl));

  bool has_expired = false;

  CHECK_OK(HasExpiredTTL(subdoc_key.hybrid_time(), ComputeTTL(ttl, table_ttl_), history_cutoff_,
                         &has_expired));

  // As of 02/2017, we don't have init markers for top level documents in YQL. As a result, we can
  // compact away each column if it has expired, including the liveness system column. The init
  // markers in Redis wouldn't be affected since they don't have any TTL associated with them and
  // the ttl would default to kMaxTtl which would make has_expired false.
  if (has_expired) {
    // This is consistent with the condition we're testing for deletes at the bottom of the function
    // because ts <= history_cutoff_ is implied by has_expired.
    if (is_full_compaction_) {
      return true;
    }
    // During minor compactions, expired values are written back as tombstones because removing the
    // record might expose earlier values which would be incorrect.
    *value_changed = true;
    *new_value = Value(PrimitiveValue(ValueType::kTombstone)).Encode();
  }

  // Deletes at or below the history cutoff hybrid_time can always be cleaned up on full (major)
  // compactions. However, we do need to update the overwrite hybrid_time stack in this case (as we
  // just did), because this deletion (tombstone) entry might be the only reason for cleaning up
  // more entries appearing at earlier hybrid_times.
  return value_type == ValueType::kTombstone && ht_at_or_below_cutoff && is_full_compaction_;
}

const char* DocDBCompactionFilter::Name() const {
  return "DocDBCompactionFilter";
}

// ------------------------------------------------------------------------------------------------

DocDBCompactionFilterFactory::DocDBCompactionFilterFactory(
    shared_ptr<HistoryRetentionPolicy> retention_policy)
    :
    retention_policy_(retention_policy) {
}

DocDBCompactionFilterFactory::~DocDBCompactionFilterFactory() {
}

unique_ptr<CompactionFilter> DocDBCompactionFilterFactory::CreateCompactionFilter(
    const CompactionFilter::Context& context) {
  return unique_ptr<DocDBCompactionFilter>(
      new DocDBCompactionFilter(retention_policy_->GetHistoryCutoff(),
                                retention_policy_->GetDeletedColumns(),
                                context.is_full_compaction, retention_policy_->GetTableTTL()));
}

const char* DocDBCompactionFilterFactory::Name() const {
  return "DocDBCompactionFilterFactory";
}

}  // namespace docdb
}  // namespace yb
