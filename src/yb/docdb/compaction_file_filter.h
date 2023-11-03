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

#pragma once

#include <memory>
#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/doc_ttl_util.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/server/hybrid_clock.h"
#include "yb/util/compare_util.h"

namespace yb {
namespace docdb {

typedef enum {
  EXP_NORMAL,
  EXP_TABLE_ONLY,
  EXP_TRUST_VALUE
} ExpiryMode;

struct ExpirationTime {
  // Indicates value-level TTL expiration.
  HybridTime ttl_expiration_ht = dockv::kNoExpiration;
  // Indicates creation hybrid time, used to calculate table-level TTL expiration.
  HybridTime created_ht = HybridTime::kMax;

  std::string ToString() const;
};

bool operator==(const ExpirationTime& lhs, const ExpirationTime& rhs);

ExpirationTime ExtractExpirationTime(const rocksdb::FileMetaData* file);

bool TtlIsExpired(
    const ExpirationTime expiry,
    const MonoDelta table_ttl,
    const HybridTime now,
    const ExpiryMode mode = EXP_NORMAL);

bool IsLastKeyCreatedBeforeHistoryCutoff(ExpirationTime expiry, HybridTime history_cutoff);

// DocDBCompactionFileFilter will discard any files with a maximum HybridTime below
// its max_ht_to_expire_, and keep any files with a maximum HybridTime above that value.
// This parameter is determined by the filter factory at the time of the filter's creation.
// table_ttl_, history_cutoff_, and filter_ht_ are all recorded at the time of filter
// creation, and are used to sanity check the filter and ensure that we don't accidentally
// discard a file that hasn't expired.
class DocDBCompactionFileFilter : public rocksdb::CompactionFileFilter {
 public:
  DocDBCompactionFileFilter(
      const MonoDelta table_ttl,
      const HybridTime history_cutoff,
      const HybridTime max_ht_to_expire,
      const HybridTime filter_ht,
      const ExpiryMode mode)
      : table_ttl_(table_ttl),
        history_cutoff_(history_cutoff),
        max_ht_to_expire_(max_ht_to_expire),
        filter_ht_(filter_ht),
        mode_(mode) {}

  rocksdb::FilterDecision Filter(const rocksdb::FileMetaData* file) override;

  const char* Name() const override;

  std::string ToString() const;

 private:
  const MonoDelta table_ttl_;
  const HybridTime history_cutoff_;
  const HybridTime max_ht_to_expire_;
  const HybridTime filter_ht_;
  const ExpiryMode mode_;
};

// DocDBCompactionFileFilterFactory will create new DocDBCompactionFileFilters, using its
// history retention policy and the current HybridTime from its clock to create constant
// parameters for the new filter.
class DocDBCompactionFileFilterFactory : public rocksdb::CompactionFileFilterFactory {
 public:
  DocDBCompactionFileFilterFactory(
      std::shared_ptr<HistoryRetentionPolicy> retention_policy,
      scoped_refptr<server::Clock> clock)
      : retention_policy_(retention_policy), clock_(clock) {}

  std::unique_ptr<rocksdb::CompactionFileFilter> CreateCompactionFileFilter(
      const std::vector<rocksdb::FileMetaData*>& inputs) override;

  const char* Name() const override;

 private:
  std::shared_ptr<HistoryRetentionPolicy> retention_policy_;
  scoped_refptr<server::Clock> clock_;
};

}  // namespace docdb
}  // namespace yb
