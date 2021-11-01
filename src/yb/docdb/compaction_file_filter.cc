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

#include "yb/docdb/compaction_file_filter.h"
#include <algorithm>

#include "yb/common/hybrid_time.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/util/flag_tags.h"

DEFINE_bool(file_expiration_ignore_value_ttl, false,
             "When deciding whether a file has expired, assume that it is safe to ignore "
             "value-level TTL and expire based on table TTL only. Useful for expiration of "
             "older SST files without value-level TTL metadata, or for expiring files with "
             "unexpectedly long value-level expiration. Can result in the deletion of "
             "live data - make sure that the data you are expiring is no longer wanted!");
TAG_FLAG(file_expiration_ignore_value_ttl, unsafe);
TAG_FLAG(file_expiration_ignore_value_ttl, runtime);

namespace yb {
namespace docdb {

using rocksdb::CompactionFileFilter;
using rocksdb::FileMetaData;
using rocksdb::FilterDecision;
using std::unique_ptr;
using std::vector;

ExpirationTime ExtractExpirationTime(const FileMetaData* file) {
  // If no frontier detected, return an expiration time that will not expire.
  if (!file || !file->largest.user_frontier) {
      return ExpirationTime{};
  }
  auto& consensus_frontier = down_cast<ConsensusFrontier&>(*file->largest.user_frontier);
  // If the TTL expiration time is uninitialized, return a max expiration time with the
  // frontier's hybrid time.
  const auto ttl_expiry_ht =
      consensus_frontier.max_value_level_ttl_expiration_time().GetValueOr(kNoExpiration);

  return ExpirationTime{
    .ttl_expiration_ht = ttl_expiry_ht,
    .created_ht = consensus_frontier.hybrid_time()
  };
}

bool TtlIsExpired(ExpirationTime expiry, MonoDelta table_ttl, HybridTime now) {
  // If FLAGS_file_expiration_ignore_value_ttl is set, ignore the value level TTL
  // entirely and use only the default table TTL.
  const auto ttl_expiry_ht =
      FLAGS_file_expiration_ignore_value_ttl ? kUseDefaultTTL : expiry.ttl_expiration_ht;

  auto file_expiry_ht = MaxExpirationFromValueAndTableTTL(
      expiry.created_ht, table_ttl, ttl_expiry_ht);
  return HasExpiredTTL(file_expiry_ht, now);
}

bool IsLastKeyCreatedBeforeHistoryCutoff(ExpirationTime expiry, HybridTime history_cutoff) {
  return expiry.created_ht < history_cutoff;
}

FilterDecision DocDBCompactionFileFilter::Filter(const FileMetaData* file) {
  // Filtering a file based on TTL expiration needs to be done from the oldest files to
  // the newest in order to prevent conflicts with tombstoned values that have expired,
  // but are referenced in later files or later versions. If any file is "kept" by
  // the file_filter, then we need to stop filtering files at that point.
  //
  // max_ht_to_expire_ indicates the expiration cutoff as determined when the filter was created.
  // history_cutoff_ indicates the timestamp after which it is unsafe to delete data.
  // table_ttl_ indicates the current default_time_to_live for the table.
  // filter_ht_ indicates the timestamp at which the filter was created.

  auto expiry = ExtractExpirationTime(file);
  // If the created HT is less than the max to expire, then we're clear to expire the file.
  if (expiry.created_ht < max_ht_to_expire_) {
    // Sanity checks to ensure that we don't accidentally expire a file that should be kept.
    // These paths should never be taken.
    if (!IsLastKeyCreatedBeforeHistoryCutoff(expiry, history_cutoff_)) {
      LOG(DFATAL) << "Attempted to discard a file that has not exceeded its "
          << "history cutoff: "
          << " filter: " << ToString()
          << " file: " << file->ToString();
      return FilterDecision::kKeep;
    } else if (!TtlIsExpired(expiry, table_ttl_, filter_ht_)) {
      LOG(DFATAL) << "Attempted to discard a file that has not expired: "
          << " filter: " << ToString()
          << " file: " << file->ToString();
      return FilterDecision::kKeep;
    }
    LOG(INFO) << "Filtering file, TTL expired: "
        << " filter: " << ToString()
        << " file: " << file->ToString();
    return FilterDecision::kDiscard;
  } else {
    VLOG(3) << "Keeping file, has a key HybridTime greater than the max to expire ("
        << max_ht_to_expire_ << "): "
        << " filter: " << ToString()
        << " file: " << file->ToString();
    return FilterDecision::kKeep;
  }
}

const char* DocDBCompactionFileFilter::Name() const {
  return "DocDBCompactionFileFilter";
}

unique_ptr<CompactionFileFilter> DocDBCompactionFileFilterFactory::CreateCompactionFileFilter(
    const vector<FileMetaData*>& input_files) {
  const HybridTime filter_ht = clock_->Now();
  auto history_retention = retention_policy_->GetRetentionDirective();
  MonoDelta table_ttl = history_retention.table_ttl;
  HybridTime history_cutoff = history_retention.history_cutoff;
  HybridTime min_kept_ht = HybridTime::kMax;

  // Need to iterate through all files and determine the minimum HybridTime of a file that
  // will *not* be expired. This will prevent us from expiring a file prematurely and accidentally
  // exposing old data.
  for (auto file : input_files) {
    auto expiry = ExtractExpirationTime(file);
    auto format_expiration_details = [expiry, table_ttl, history_cutoff, file]() {
      return Format("file expiration info: $0, table ttl: $1, history_cutoff: $2, file: $3",
          expiry, table_ttl, history_cutoff, file);
    };

    // A file is *not* expired if either A) its latest table TTL/value TTL time has not expired,
    // or B) its latest key is still within the history retention window.
    if (!TtlIsExpired(expiry, table_ttl, filter_ht) ||
        !IsLastKeyCreatedBeforeHistoryCutoff(expiry, history_cutoff)) {
      VLOG(4) << "File is not expired or contains data created after history cutoff time, "
          << "updating minimum HybridTime for filter: " << format_expiration_details();
      min_kept_ht = min_kept_ht < expiry.created_ht ? min_kept_ht : expiry.created_ht;
    } else {
      VLOG(4) << "File is expired (may or may not be filtered during compaction): "
          << format_expiration_details();
    }
  }
  return std::make_unique<DocDBCompactionFileFilter>(
      table_ttl, history_cutoff, min_kept_ht, filter_ht);
}

const char* DocDBCompactionFileFilterFactory::Name() const {
  return "DocDBCompactionFileFilterFactory";
}

}  // namespace docdb
}  // namespace yb
