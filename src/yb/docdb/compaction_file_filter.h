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

#ifndef YB_DOCDB_COMPACTION_FILE_FILTER_H_
#define YB_DOCDB_COMPACTION_FILE_FILTER_H_

#include <memory>
#include "yb/common/schema.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/server/hybrid_clock.h"
#include "yb/util/compare_util.h"

namespace yb {
namespace docdb {

struct ExpirationTime {
  // Indicates value-level TTL expiration.
  HybridTime ttl_expiration_ht = kNoExpiration;
  // Indicates creation hybrid time, used to calculate table-level TTL expiration.
  HybridTime created_ht = HybridTime::kMax;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(ttl_expiration_ht, created_ht);
  }
};

inline bool operator==(const ExpirationTime& lhs, const ExpirationTime& rhs) {
  return YB_STRUCT_EQUALS(ttl_expiration_ht, created_ht);
}

ExpirationTime ExtractExpirationTime(const rocksdb::FileMetaData* file);

bool IsExpired(ExpirationTime expiry, MonoDelta table_ttl, HybridTime now);

// DocDBCompactionFileFilter will check the file's value expiration time
// and its table schema for table TTL, and during compaction will filter files
// which have expired.
class DocDBCompactionFileFilter : public rocksdb::CompactionFileFilter {
 public:
  DocDBCompactionFileFilter(
      const MonoDelta table_ttl, const HybridTime max_ht_to_expire, const HybridTime filter_ht)
      : table_ttl_(table_ttl), max_ht_to_expire_(max_ht_to_expire), filter_ht_(filter_ht) {}

  rocksdb::FilterDecision Filter(const rocksdb::FileMetaData* file) override;

  const char* Name() const override;

 private:
  const MonoDelta table_ttl_;
  const HybridTime max_ht_to_expire_;
  const HybridTime filter_ht_;
};

// DocDBCompactionFileFilterFactory will create new DocDBCompactionFileFilters, passing
// it the table schema and the current HybridTime from its clock.
class DocDBCompactionFileFilterFactory : public rocksdb::CompactionFileFilterFactory {
 public:
  DocDBCompactionFileFilterFactory(std::shared_ptr<Schema> schema,
      scoped_refptr<server::Clock> clock)
      : schema_(schema), clock_(clock) {}

  std::unique_ptr<rocksdb::CompactionFileFilter> CreateCompactionFileFilter(
      const std::vector<rocksdb::FileMetaData*>& inputs) override;

  const char* Name() const override;

 private:
  std::shared_ptr<Schema> schema_;
  scoped_refptr<server::Clock> clock_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_COMPACTION_FILE_FILTER_H_
