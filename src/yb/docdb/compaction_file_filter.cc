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
#include "yb/dockv/doc_ttl_util.h"
#include "yb/docdb/docdb_compaction_context.h"

#include "yb/gutil/casts.h"

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/db/version_edit.h"

#include "yb/util/flags.h"

DEFINE_RUNTIME_bool(file_expiration_ignore_value_ttl, false,
    "When deciding whether a file has expired, assume that it is safe to ignore "
    "value-level TTL and expire based on table TTL only. CAUTION - Shoule only be "
    "used for expiration of older SST files without value-level TTL metadata, or "
    "for expiring files with incorrect value-level expiration. Misuse can result "
    "in the deletion of live data!");
TAG_FLAG(file_expiration_ignore_value_ttl, unsafe);

DEFINE_RUNTIME_bool(file_expiration_value_ttl_overrides_table_ttl, false,
    "When deciding whether a file has expired, assume that any file with "
    "value-level TTL metadata can be expired solely on that metadata. Useful for "
    "the expiration of files earlier than the table-level TTL that is set. "
    "CAUTION - Should only be used in workloads where the user is certain all data is "
    "written with a value-level TTL. Misuse can result in the deletion of live data!");
TAG_FLAG(file_expiration_value_ttl_overrides_table_ttl, unsafe);

namespace yb {
namespace docdb {

using rocksdb::CompactionFileFilter;
using rocksdb::FileMetaData;
using rocksdb::FilterDecision;
using std::unique_ptr;
using std::vector;

namespace {
  const ExpiryMode CurrentExpiryMode() {
    if (FLAGS_file_expiration_ignore_value_ttl) {
      return EXP_TABLE_ONLY;
    } else if (FLAGS_file_expiration_value_ttl_overrides_table_ttl) {
      return EXP_TRUST_VALUE;
    }
    return EXP_NORMAL;
  }
}

ExpirationTime ExtractExpirationTime(const FileMetaData* file) {
  // If no frontier detected, return an expiration time that will not expire.
  if (!file || !file->largest.user_frontier) {
      return ExpirationTime{};
  }
  auto& consensus_frontier = down_cast<ConsensusFrontier&>(*file->largest.user_frontier);
  // If the TTL expiration time is uninitialized, return a max expiration time with the
  // frontier's hybrid time.
  const auto ttl_expiry_ht =
      consensus_frontier.max_value_level_ttl_expiration_time().GetValueOr(dockv::kNoExpiration);

  return ExpirationTime{
    .ttl_expiration_ht = ttl_expiry_ht,
    .created_ht = consensus_frontier.hybrid_time()
  };
}

bool TtlIsExpired(const ExpirationTime expiry,
    const MonoDelta table_ttl,
    const HybridTime now,
    const ExpiryMode mode) {
  // If FLAGS_file_expiration_ignore_value_ttl is set, ignore the value level TTL
  // entirely and use only the default table TTL.
  const auto ttl_expiry_ht =
      mode == EXP_TABLE_ONLY ? dockv::kUseDefaultTTL : expiry.ttl_expiration_ht;

  if (mode == EXP_TRUST_VALUE && ttl_expiry_ht.is_valid() &&
      ttl_expiry_ht != dockv::kUseDefaultTTL) {
    return dockv::HasExpiredTTL(ttl_expiry_ht, now);
  }

  auto file_expiry_ht = dockv::MaxExpirationFromValueAndTableTTL(
      expiry.created_ht, table_ttl, ttl_expiry_ht);
  return dockv::HasExpiredTTL(file_expiry_ht, now);
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
    } else if (!TtlIsExpired(expiry, table_ttl_, filter_ht_, mode_)) {
      LOG(DFATAL) << "Attempted to discard a file that has not expired: "
          << " filter: " << ToString()
          << " file: " << file->ToString();
      return FilterDecision::kKeep;
    }
    VLOG(2) << "Filtering file, TTL expired: "
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

std::string DocDBCompactionFileFilter::ToString() const {
  return YB_CLASS_TO_STRING(table_ttl, history_cutoff, max_ht_to_expire, filter_ht);
}

const char* DocDBCompactionFileFilter::Name() const {
  return "DocDBCompactionFileFilter";
}

unique_ptr<CompactionFileFilter> DocDBCompactionFileFilterFactory::CreateCompactionFileFilter(
    const vector<FileMetaData*>& input_files) {
  const HybridTime filter_ht = clock_->Now();
  auto history_retention = retention_policy_->GetRetentionDirective();
  MonoDelta table_ttl = history_retention.table_ttl;
  // For the sys catalog tablet, the expiration time is never set
  // since it does not have TTL so the chosen
  // history cutoff does not matter since the files will never expire. For tablets
  // on tserver, only the primary_cutoff_ht will be valid and that can
  // be used to detect expiration. Still, to be safe here we simply take the minimum
  // of both.
  HybridTime history_cutoff = HybridTime::kMax;
  if (history_retention.history_cutoff.cotables_cutoff_ht) {
    history_cutoff.MakeAtMost(
        history_retention.history_cutoff.cotables_cutoff_ht);
  }
  if (history_retention.history_cutoff.primary_cutoff_ht) {
    history_cutoff.MakeAtMost(
        history_retention.history_cutoff.primary_cutoff_ht);
  }
  HybridTime min_kept_ht = HybridTime::kMax;
  const ExpiryMode mode = CurrentExpiryMode();

  // Need to iterate through all files and determine the minimum HybridTime of a file that
  // will *not* be expired. This will prevent us from expiring a file prematurely and accidentally
  // exposing old data.
  for (auto file : input_files) {
    auto expiry = ExtractExpirationTime(file);
    auto format_expiration_details = [expiry, table_ttl, mode, history_cutoff, file]() {
      return Format("file expiration info: $0, table ttl: $1,"
          " mode: $2, history_cutoff: $3, file: $4",
          expiry, table_ttl, mode, history_cutoff, file);
    };

    // A file is *not* expired if either A) its latest table TTL/value TTL time has not expired,
    // or B) its latest key is still within the history retention window.
    if (!TtlIsExpired(expiry, table_ttl, filter_ht, mode) ||
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
      table_ttl, history_cutoff, min_kept_ht, filter_ht, mode);
}

const char* DocDBCompactionFileFilterFactory::Name() const {
  return "DocDBCompactionFileFilterFactory";
}

std::string ExpirationTime::ToString() const {
  return YB_STRUCT_TO_STRING(ttl_expiration_ht, created_ht);
}

bool operator==(const ExpirationTime& lhs, const ExpirationTime& rhs) {
  return YB_STRUCT_EQUALS(ttl_expiration_ht, created_ht);
}

}  // namespace docdb
}  // namespace yb
