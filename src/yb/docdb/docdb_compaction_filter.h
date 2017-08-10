// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_COMPACTION_FILTER_H
#define YB_DOCDB_DOCDB_COMPACTION_FILTER_H

#include <atomic>
#include <memory>
#include <vector>

#include "rocksdb/compaction_filter.h"

#include "yb/common/schema.h"
#include "yb/common/hybrid_time.h"
#include "yb/docdb/doc_key.h"

namespace yb {
namespace docdb {

class DocDBCompactionFilter : public rocksdb::CompactionFilter {
 public:
  DocDBCompactionFilter(HybridTime history_cutoff,
                        ColumnIdsPtr deleted_cols,
                        bool is_full_compaction,
                        MonoDelta table_ttl);

  ~DocDBCompactionFilter() override;
  bool Filter(int level,
              const rocksdb::Slice& key,
              const rocksdb::Slice& existing_value,
              std::string* new_value,
              bool* value_changed) const override;
  const char* Name() const override;

 private:
  // We will not keep history below this hybrid_time. The view of the database at this hybrid_time
  // is preserved, but after the compaction completes, we should not expect to be able to do
  // consistent scans at DocDB hybrid_times lower than this. Those scans will result in missing
  // data. Therefore, it is really important to always set this to a value lower than or equal to
  // the lowest "read point" of any pending read operations.
  const HybridTime history_cutoff_;
  const bool is_full_compaction_;

  mutable bool is_first_key_value_;
  mutable SubDocKey prev_subdoc_key_;

  // A stack of highest hybrid_times lower than or equal to history_cutoff_ at which parent
  // subdocuments of the key that has just been processed, or the subdocument / primitive value
  // itself stored at that key itself, were fully overwritten or deleted. A full overwrite of a
  // parent document is considered a full overwrite of all its subdocuments at every level for the
  // purpose of this definition. Therefore, the following inequalities hold:
  //
  // overwrite_ht_[0] <= ... <= overwrite_ht_[N - 1] <= history_cutoff_
  //
  // The following example shows contents of RocksDB being compacted, as well as the state of the
  // overwrite_ht_ stack and how it is being updated at each step. history_cutoff_ is 25 in this
  // example.
  //
  // RocksDB key/value                    | overwrite_ht_ | Filter logic
  // ----------------------------------------------------------------------------------------------
  // doc_key1 HT(30) -> {}                | [MinHT]       | Always keeping the first entry
  // doc_key1 HT(20) -> DEL               | [20]          | 20 >= MinHT, keeping the entry
  //                                      |               | ^^    ^^^^^
  //                                      | Note: we're comparing the hybrid_time in this key with
  //                                      | the _previous_ stack top of overwrite_ht_.
  //                                      |               |
  // doc_key1 HT(10) -> {}                | [20]          | 10 < 20, deleting the entry
  // doc_key1 subkey1 HT(35) -> "value4"  | [20, 20]      | 35 >= 20, keeping the entry
  //                     ^^               |      ^^       |
  //                      \----------------------/ HT(35) is higher than history_cutoff_, so we're
  //                                      |        just duplicating the top value on the stack
  //                                      |        HT(20) this step.
  //                                      |               |
  // doc_key1 subkey1 HT(23) -> "value3"  | [20, 23]      | 23 >= 20, keeping the entry
  //                                      |      ^^       |
  //                                      |      Now we have actually found a hybrid_time that is
  //                                      |      <= history_cutoff_, so we're replacing the stack
  //                                      |      top with that hybrid_time.
  //                                      |               |
  // doc_key1 subkey1 HT(21) -> "value2"  | [20, 23]      | 21 < 23, deleting the entry
  // doc_key1 subkey1 HT(15) -> "value1"  | [20, 23]      | 15 < 23, deleting the entry

  mutable std::vector<DocHybridTime> overwrite_ht_;

  // We use this to only log a message that the filter is being used once on the first call to
  // the Filter function.
  mutable bool filter_usage_logged_;

  // Default TTL of table.
  MonoDelta table_ttl_;

  ColumnIdsPtr deleted_cols_;
};

// A strategy for deciding the history cutoff. We may implement this differently in production and
// in tests.
class HistoryRetentionPolicy {
 public:
  virtual ~HistoryRetentionPolicy() {}
  virtual HybridTime GetHistoryCutoff() = 0;
  virtual ColumnIdsPtr GetDeletedColumns() = 0;
  virtual MonoDelta GetTableTTL() = 0;
};

// A history retention policy that always returns the same hybrid_time. Useful in tests. This class
// is thread-safe.
class FixedHybridTimeRetentionPolicy : public HistoryRetentionPolicy {
 public:
  explicit FixedHybridTimeRetentionPolicy(HybridTime history_cutoff, MonoDelta table_ttl)
      : history_cutoff_(history_cutoff), table_ttl_(table_ttl) {
  }

  HybridTime GetHistoryCutoff() override { return history_cutoff_.load(); }
  void SetHistoryCutoff(HybridTime history_cutoff) { history_cutoff_.store(history_cutoff); }

  ColumnIdsPtr GetDeletedColumns() override {
    return std::make_shared<ColumnIds>(deleted_cols_);
  }
  void AddDeletedColumn(ColumnId col) { deleted_cols_.insert(col); }

  MonoDelta GetTableTTL() override { return table_ttl_; }
  void SetTableTTL(MonoDelta ttl) {  table_ttl_ = ttl; }

 private:
  std::atomic<HybridTime> history_cutoff_;
  ColumnIds deleted_cols_;
  MonoDelta table_ttl_;
};

class DocDBCompactionFilterFactory : public rocksdb::CompactionFilterFactory {
 public:
  explicit DocDBCompactionFilterFactory(std::shared_ptr<HistoryRetentionPolicy> retention_policy);
  ~DocDBCompactionFilterFactory() override;
  std::unique_ptr<rocksdb::CompactionFilter> CreateCompactionFilter(
      const rocksdb::CompactionFilter::Context& context) override;
  const char* Name() const override;

 private:
  std::shared_ptr<HistoryRetentionPolicy> retention_policy_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_COMPACTION_FILTER_H
