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

#ifndef YB_TABLET_RESTORE_UTIL_H
#define YB_TABLET_RESTORE_UTIL_H

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/rocksdb_writer.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/tablet/tablet.h"

namespace yb {

class FetchState {
 public:
  explicit FetchState(const docdb::DocDB& doc_db, const ReadHybridTime& read_time)
      : iterator_(CreateIntentAwareIterator(
          doc_db,
          docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
          boost::none,
          rocksdb::kDefaultQueryId,
          TransactionOperationContext(),
          CoarseTimePoint::max(),
          read_time)) {
  }

  Status SetPrefix(const Slice& prefix);

  bool finished() const {
    return finished_;
  }

  Slice key() const {
    return key_.key;
  }

  Slice value() const {
    return iterator_->value();
  }

  docdb::FetchKeyResult FullKey() const {
    return key_;
  }

  Status NextEntry() {
    iterator_->SeekPastSubKey(key_.key);
    return Update();
  }

  Status Next() {
    RETURN_NOT_OK(NextEntry());
    return NextNonDeletedEntry();
  }

  // Returns true if the entry corresponds to a deleted row
  // in rocksdb.
  Result<bool> IsDeletedRowEntry();

  // Returns true if it has been deleted since the time it was inserted.
  bool IsDeletedSinceInsertion();

 private:
  Status Update();
  Status NextNonDeletedEntry();
  Result<bool> IsDeletedEntry();

  std::unique_ptr<docdb::IntentAwareIterator> iterator_;
  Slice prefix_;
  docdb::FetchKeyResult key_;
  KeyBuffer last_deleted_key_bytes_;
  DocHybridTime last_deleted_key_write_time_;
  bool finished_ = false;
};

YB_DEFINE_ENUM(RestoreTicker, (kUpdates)(kInserts)(kDeletes));

class RestorePatch {
 public:
  RestorePatch(FetchState* existing_state, FetchState* restoring_state,
               docdb::DocWriteBatch* doc_batch) :
      existing_state_(existing_state), restoring_state_(restoring_state),
      doc_batch_(doc_batch) {}

  Status PatchCurrentStateFromRestoringState();

  size_t GetTicker(RestoreTicker ticker) { return tickers_[static_cast<size_t>(ticker)]; }

  docdb::DocWriteBatch* DocBatch() { return doc_batch_; }

  int64_t TotalTickerCount() {
    int64_t res = 0;
    for (const auto& ticker : tickers_) {
      res += ticker;
    }
    return res;
  }

  std::string TickersToString() {
    return Format("total: $0, updates: $1, inserts: $2, deletes: $3",
                  TotalTickerCount(),
                  GetTicker(RestoreTicker::kUpdates),
                  GetTicker(RestoreTicker::kInserts),
                  GetTicker(RestoreTicker::kDeletes));
  }

  virtual ~RestorePatch() = default;

 protected:
  void IncrementTicker(RestoreTicker ticker) { tickers_[static_cast<size_t>(ticker)]++; }

 private:
  FetchState* existing_state_;
  FetchState* restoring_state_;
  docdb::DocWriteBatch* doc_batch_;
  std::array<size_t, kRestoreTickerMapSize> tickers_ = { 0, 0, 0 };

  virtual Status ProcessEqualEntries(
      const Slice& existing_key, const Slice& existing_value,
      const Slice& restoring_key, const Slice& restoring_value);

  Status ProcessRestoringLessThanExisting(
      const Slice& existing_key, const Slice& existing_value,
      const Slice& restoring_key, const Slice& restoring_value);

  Status ProcessRestoringGreaterThanExisting(
      const Slice& existing_key, const Slice& existing_value,
      const Slice& restoring_key, const Slice& restoring_value);

  virtual Result<bool> ShouldSkipEntry(const Slice& key, const Slice& value) = 0;
};

void AddKeyValue(const Slice& key, const Slice& value, docdb::DocWriteBatch* write_batch);

void WriteToRocksDB(
    docdb::DocWriteBatch* write_batch, const HybridTime& write_time, const OpId& op_id,
    tablet::Tablet* tablet, const std::optional<docdb::KeyValuePairPB>& restore_kv);

} // namespace yb

#endif // YB_TABLET_RESTORE_UTIL_H
