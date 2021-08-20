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

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/rocksdb/compaction_filter.h"

namespace yb {
namespace docdb {
using rocksdb::FilterDecision;

ExpirationTime DocDBCompactionFileFilter::Extract(const rocksdb::FileMetaData* file) {
  // If no frontier detected, return an expiration time that will not expire.
  if (!file || !file->largest.user_frontier) {
      return ExpirationTime{};
  }
  auto& consensus_frontier = down_cast<ConsensusFrontier&>(*file->largest.user_frontier);
  // If the TTL expiration time is uninitialized, return a max expiration time with the
  // frontier's hybrid time.
  if (!consensus_frontier.max_value_level_ttl_expiration_time().is_valid()) {
    return ExpirationTime{
      .ttl_expiration_ht = kNoExpiration,
      .created_ht = consensus_frontier.hybrid_time()
    };
  }
  return ExpirationTime{
    .ttl_expiration_ht = consensus_frontier.max_value_level_ttl_expiration_time(),
    .created_ht = consensus_frontier.hybrid_time()
  };
}

FilterDecision DocDBCompactionFileFilter::Filter(const rocksdb::FileMetaData* file) {
  // Filtering a file based on TTL expiration needs to be done from the oldest files to
  // the newest in order to prevent conflicts with tombstoned values that have expired,
  // but are referenced in later files or later versions. If any file is "kept" by
  // the file_filter, then we need to stop filtering files at that point.
  //
  // This logic assumes files are evaluated in order of creation time during compaction.
  if (has_kept_file_) {
    VLOG(4) << "Not filtering file, filtering disabled (already kept a file): "
        << file->ToString();
    return FilterDecision::kKeep;
  }

  auto expiration = Extract(file);
  auto table_ttl = TableTTL(*schema_);
  auto file_expiry = MaxExpirationFromValueAndTableTTL(
      expiration.created_ht, table_ttl, expiration.ttl_expiration_ht);
  if (HasExpiredTTL(file_expiry, filter_ht_)) {
    VLOG(3) << "Filtering file, TTL expired: " << file->ToString();
    return FilterDecision::kDiscard;
  } else {
    VLOG(4) << "Not filtering file, TTL not expired: " << file->ToString();
    has_kept_file_ = true;
    return FilterDecision::kKeep;
  }
}

const char* DocDBCompactionFileFilter::Name() const {
  return "DocDBCompactionFileFilter";
}

std::unique_ptr<rocksdb::CompactionFileFilter>
    DocDBCompactionFileFilterFactory::CreateCompactionFileFilter() {
  const HybridTime filter_ht = clock_->Now();
  return std::make_unique<DocDBCompactionFileFilter>(schema_, filter_ht);
}

const char* DocDBCompactionFileFilterFactory::Name() const {
  return "DocDBCompactionFileFilterFactory";
}

}  // namespace docdb
}  // namespace yb
