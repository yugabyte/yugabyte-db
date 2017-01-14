// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb_compaction_filter.h"

#include <memory>

#include <glog/logging.h>

#include "rocksdb/compaction_filter.h"
#include "rocksdb/util/string_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/value.h"

using std::shared_ptr;
using std::unique_ptr;
using rocksdb::CompactionFilter;
using rocksdb::VectorToString;

namespace yb {
namespace docdb {

// ------------------------------------------------------------------------------------------------

DocDBCompactionFilter::DocDBCompactionFilter(Timestamp history_cutoff, bool is_full_compaction)
    : history_cutoff_(history_cutoff),
      is_full_compaction_(is_full_compaction),
      is_first_key_value_(true),
      filter_usage_logged_(false) {
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
  CHECK_OK(subdoc_key.FullyDecodeFrom(key));

  if (is_first_key_value_) {
    // This assumption won't be true if when we enable optional object init markers at the top
    // level, e.g. for CQL/SQL tables.
    CHECK_EQ(0, subdoc_key.num_subkeys())
        << "First key in compaction is expected to only have the DocKey component: "
        << subdoc_key.ToString()
        << ", is_full_compaction: " << this->is_full_compaction_;
    CHECK_EQ(0, overwrite_ts_.size());
    is_first_key_value_ = false;
  }

  const size_t num_shared_components = prev_subdoc_key_.NumSharedPrefixComponents(subdoc_key);

  // Remove overwrite timestamps for components that are no longer relevant for the current
  // SubDocKey.
  overwrite_ts_.resize(min(overwrite_ts_.size(), num_shared_components));

  const Timestamp ts = subdoc_key.timestamp();

  // We're comparing the timestamp in this key with the _previous_ stack top of overwrite_ts_,
  // after truncating the previous timestamp to the number of components in the common prefix
  // of previous and current key.
  //
  // Example (history_cutoff_ = 12):
  // --------------------------------------------------------------------------------------------
  // Key          overwrite_ts_ stack and relevant notes
  // --------------------------------------------------------------------------------------------
  // k1 T10       [MinTS]
  // k1 T5        [T10]
  // k1 col1 T11  [T10, T11]
  // k1 col1 T7   The stack does not get truncated (shared prefix length is 2), so
  //              prev_overwrite_ts = 11. Removing this entry because 7 < 11.
  // k1 col2 T9   Truncating the stack to [T10], setting prev_overwrite_ts to 10, and therefore
  //              deciding to remove this entry because 9 < 10.
  //
  const Timestamp prev_overwrite_ts =
      overwrite_ts_.empty() ? Timestamp::kMin : overwrite_ts_.back();

  // We only keep entries with timestamp equal to or later than the latest time the subdocument
  // was fully overwritten or deleted prior to or at the history cutoff timestamp. The intuition
  // is that key/value pairs that were overwritten at or before history cutoff time will not be
  // visible at history cutoff time or any later time anyway.
  //
  // Furthermore, we only need to update the overwrite timestamp stack in case we have decided to
  // keep the new entry. Otherwise, the current entry's timestamp ts is less than the previous
  // overwrite timestamp prev_overwrite_ts, and therefore it does not provide any new information
  // about key/value pairs that follow being overwritten at a particular timestamp. Another way to
  // explain this is to look at the logic that follows. If we don't early-exit here while ts is less
  // than prev_overwrite_ts, we'll end up adding more prev_overwrite_ts values to the overwrite
  // timestamp stack, and we might as well do that while handling the next key/value pair that does
  // not get cleaned up the same way as this one.
  if (ts < prev_overwrite_ts) {
    return true;  // Remove this key/value pair.
  }

  const int new_stack_size = subdoc_key.num_subkeys() + 1;

  // Every subdocument was fully overwritten at least at the time any of its parents was fully
  // overwritten.
  while (overwrite_ts_.size() < new_stack_size - 1) {
    overwrite_ts_.push_back(prev_overwrite_ts);
  }

  // This will happen in case previous key has the same document key and subkeys as the current
  // key, and the only difference is in the timestamp. We want to replace the timestamp at the top
  // of the overwrite_ts stack in this case.
  if (overwrite_ts_.size() == new_stack_size) {
    overwrite_ts_.pop_back();
  }

  // See if we found a higher timestamp not exceeding the history cutoff timestamp at which the
  // subdocument (including a primitive value) rooted at the current key was fully overwritten.
  // In case ts > history_cutoff_, we just keep the parent document's highest known overwrite
  // timestamp that does not exceed the cutoff timestamp. In that case this entry is obviously
  // too new to be garbage-collected.
  overwrite_ts_.push_back(ts <= history_cutoff_ ? max(prev_overwrite_ts, ts) : prev_overwrite_ts);

  CHECK_EQ(new_stack_size, overwrite_ts_.size());
  prev_subdoc_key_ = std::move(subdoc_key);

  const ValueType value_type = DecodeValueType(existing_value);
  MonoDelta ttl;

  // If the value expires by the time of history cutoff, it is treated as deleted and filtered out.
  CHECK_OK(Value::DecodeTTL(existing_value, &ttl));

  bool has_expired = false;

  CHECK_OK(HasExpiredTTL(key, ttl, history_cutoff_, &has_expired));

  // We don't support TTLs in object markers yet.
  if (value_type != ValueType::kObject && has_expired) {
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

  // Deletes at or below the history cutoff timestamp can always be cleaned up on full (major)
  // compactions. However, we do need to update the overwrite timestamp stack in this case (as we
  // just did), because this deletion (tombstone) entry might be the only reason for cleaning up
  // more entries appearing at earlier timestamps.
  return value_type == ValueType::kTombstone && ts <= history_cutoff_ && is_full_compaction_;
}

const char* DocDBCompactionFilter::Name() const {
  return "DocDBCompactionFilter";
}

// ------------------------------------------------------------------------------------------------

DocDBCompactionFilterFactory::DocDBCompactionFilterFactory(
    shared_ptr<HistoryRetentionPolicy> retention_policy)
    : retention_policy_(retention_policy) {
}

DocDBCompactionFilterFactory::~DocDBCompactionFilterFactory() {
}

unique_ptr<CompactionFilter> DocDBCompactionFilterFactory::CreateCompactionFilter(
    const CompactionFilter::Context& context) {
  return unique_ptr<DocDBCompactionFilter>(
      new DocDBCompactionFilter(retention_policy_->GetHistoryCutoff(),
                                context.is_full_compaction));
}

const char* DocDBCompactionFilterFactory::Name() const {
  return "DocDBCompactionFilterFactory";
}

}  // namespace docdb
}  // namespace yb
