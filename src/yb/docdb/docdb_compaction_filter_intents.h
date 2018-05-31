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

#ifndef YB_DOCDB_DOCDB_COMPACTION_FILTER_INTENTS_H
#define YB_DOCDB_DOCDB_COMPACTION_FILTER_INTENTS_H

#include <atomic>
#include <memory>
#include <vector>

#include "yb/rocksdb/compaction_filter.h"

#include "yb/client/transaction_manager.h"
#include "yb/common/schema.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/docdb/doc_key.h"
#include "yb/tablet/tablet.h"

namespace yb {
namespace docdb {

class DocDBIntentsCompactionFilter : public rocksdb::CompactionFilter {
 public:
  explicit DocDBIntentsCompactionFilter(tablet::Tablet* tablet);

  ~DocDBIntentsCompactionFilter() override;
  bool Filter(int level,
              const rocksdb::Slice& key,
              const rocksdb::Slice& existing_value,
              std::string* new_value,
              bool* value_changed) const override {
    return const_cast<DocDBIntentsCompactionFilter*>(this)->DoFilter(level, key,
              existing_value, new_value, value_changed);
  }
  const char* Name() const override;

  TransactionIdSet& transactions_to_cleanup() {
    return transactions_to_cleanup_;
  }

  void AddToSet(TransactionId transactionId);

  void Cleanup(TransactionId transactionId);

 private:
  bool DoFilter(int level,
                const rocksdb::Slice& key,
                const rocksdb::Slice& existing_value,
                std::string* new_value,
                bool* value_changed);
  tablet::Tablet* tablet_;
  TransactionIdSet transactions_to_cleanup_;
  int rejected_transactions_ = 0;

  // We use this to only log a message that the filter is being used once on the first call to
  // the Filter function.
  bool filter_usage_logged_ = false;

  // Default TTL of table.
  MonoDelta table_ttl_;
};

class DocDBIntentsCompactionFilterFactory : public rocksdb::CompactionFilterFactory {
 public:
  explicit DocDBIntentsCompactionFilterFactory(tablet::Tablet* tablet);
  ~DocDBIntentsCompactionFilterFactory() override;
  std::unique_ptr<rocksdb::CompactionFilter> CreateCompactionFilter(
      const rocksdb::CompactionFilter::Context& context) override;
  const char* Name() const override;
 private:
  tablet::Tablet* tablet_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_COMPACTION_FILTER_INTENTS_H
