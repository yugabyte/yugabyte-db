// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_COMPACTION_FILTER_H
#define YB_DOCDB_DOCDB_COMPACTION_FILTER_H

#include <atomic>
#include <memory>
#include <vector>

#include "rocksdb/compaction_filter.h"

#include "yb/common/timestamp.h"
#include "yb/docdb/doc_key.h"

namespace yb {
namespace docdb {

class DocDBCompactionFilter : public rocksdb::CompactionFilter {
 public:
  DocDBCompactionFilter(Timestamp history_cutoff, bool is_full_compaction);

  ~DocDBCompactionFilter() override;
  bool Filter(int level,
              const rocksdb::Slice& key,
              const rocksdb::Slice& existing_value,
              std::string* new_value,
              bool* value_changed) const override;
  const char* Name() const override;

 private:
  // We will not keep history below this timestamp. The view of the database at this timestamp is
  // preserved, but after the compaction completes, we should not expect to be able to do consistent
  // scans at DocDB timestamps lower than this. Those scans will result in missing data. Therefore,
  // it is really important to always set this to a value lower than or equal to the lowest "read
  // point" of any pending read operations.
  const Timestamp history_cutoff_;
  const bool is_full_compaction_;

  mutable bool is_first_key_value_;
  mutable SubDocKey prev_subdoc_key_;

  // A stack of highest timestamps lower than or equal to history_cutoff_ at which parent
  // subdocuments of the key that has just been processed, or the subdocument / primitive value
  // itself stored at that key itself, were fully overwritten or deleted. A full overwrite of a
  // parent document is considered a full overwrite of all its subdocuments at every level for the
  // purpose of this definition. Therefore, the following inequalities hold:
  //
  // overwrite_ts_[0] <= ... <= overwrite_ts_[N - 1] <= history_cutoff_
  //
  // The following example shows contents of RocksDB being compacted, as well as the state of the
  // overwrite_ts_ stack and how it is being updated at each step. history_cutoff_ is 25 in this
  // example.
  //
  // RocksDB key/value                    | overwrite_ts_ | Filter logic
  // ----------------------------------------------------------------------------------------------
  // doc_key1 TS(30) -> {}                | [MinTS]       | Always keeping the first entry
  // doc_key1 TS(20) -> DEL               | [20]          | 20 >= MinTS, keeping the entry
  //                                      |               | ^^    ^^^^^
  //                                      | Note: we're comparing the timestamp in this key with
  //                                      | the _previous_ stack top of overwrite_ts_.
  //                                      |               |
  // doc_key1 TS(10) -> {}                | [20]          | 10 < 20, deleting the entry
  // doc_key1 subkey1 TS(35) -> "value4"  | [20, 20]      | 35 >= 20, keeping the entry
  //                     ^^               |      ^^       |
  //                      \----------------------/ TS(35) is higher than history_cutoff_, so we're
  //                                      |        just duplicating the top value on the stack
  //                                      |        TS(20) this step.
  //                                      |               |
  // doc_key1 subkey1 TS(23) -> "value3"  | [20, 23]      | 23 >= 20, keeping the entry
  //                                      |      ^^       |
  //                                      |      Now we have actually found a timestamp that is
  //                                      |      <= history_cutoff_, so we're replacing the stack
  //                                      |      top with that timestamp.
  //                                      |               |
  // doc_key1 subkey1 TS(21) -> "value2"  | [20, 23]      | 21 < 23, deleting the entry
  // doc_key1 subkey1 TS(15) -> "value1"  | [20, 23]      | 15 < 23, deleting the entry

  mutable std::vector<Timestamp> overwrite_ts_;

  // We use this to only log a message that the filter is being used once on the first call to
  // the Filter function.
  mutable bool filter_usage_logged_;
};

// A strategy for deciding the history cutoff. We may implement this differently in production and
// in tests.
class HistoryRetentionPolicy {
 public:
  virtual ~HistoryRetentionPolicy() {}
  virtual Timestamp GetHistoryCutoff() = 0;
};

// A history retention policy that always returns the same timestamp. Useful in tests. This class
// is thread-safe.
class FixedTimestampRetentionPolicy : public HistoryRetentionPolicy {
 public:
  explicit FixedTimestampRetentionPolicy(Timestamp history_cutoff)
      : history_cutoff_(history_cutoff) {
  }

  Timestamp GetHistoryCutoff() override { return history_cutoff_.load(); }
  void SetHistoryCutoff(Timestamp history_cutoff) { history_cutoff_.store(history_cutoff); }

 private:
  std::atomic<Timestamp> history_cutoff_;
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
