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

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/in_mem_docdb.h"

#include "yb/dockv/subdocument.h"
#include "yb/dockv/dockv_test_util.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {

YB_STRONGLY_TYPED_BOOL(ResolveIntentsDuringRead);

// Intended only for testing, when we want to enable transaction aware code path for cases when we
// really have no transactions. This way we will test that transaction aware code path works
// correctly in absence of transactions and also doesn't use transaction status provider (it
// shouldn't because there are no transactions).
// TODO(dtxn) everywhere(?) in tests code where we use kNonTransactionalOperationContext we need
// to check both code paths - for transactional tables (passing kNonTransactionalOperationContext)
// and non-transactional tables (passing std::nullopt as transaction context).
// https://yugabyte.atlassian.net/browse/ENG-2177.
extern const TransactionOperationContext kNonTransactionalOperationContext;

// Note: test data generator methods below are using a non-const reference for the random number
// generator for simplicity, even though it is against Google C++ Style Guide. If we used a pointer,
// we would have to invoke the RNG as (*rng)().

// Generate a random primitive value.
ValueRef GenRandomPrimitiveValue(dockv::RandomNumberGenerator* rng, QLValuePB* holder);

// Represents a full logical snapshot of a RocksDB instance. An instance of this class will record
// the state of a RocksDB instance via Capture, which can then be written to a new RocksDB instance
// via RestoreTo.
class LogicalRocksDBDebugSnapshot {
 public:
  LogicalRocksDBDebugSnapshot() {}
  Status Capture(rocksdb::DB* rocksdb);
  Status RestoreTo(rocksdb::DB *rocksdb) const;
 private:
  std::vector<std::pair<std::string, std::string>> kvs;
  std::string docdb_debug_dump_str;
};

class DocDBRocksDBFixture : public DocDBRocksDBUtil {
 public:
  void AssertDocDbDebugDumpStrEq(
      const std::string &expected, const std::string& packed_row_expected = "");
  void FullyCompactHistoryBefore(HybridTime history_cutoff);
  void FullyCompactHistoryBefore(HistoryCutoff history_cutoff);

  // num_files_to_compact - number of files that should participate in the minor compaction
  // start_index - the index of the file to start with (0 = the oldest file, -1 = compact
  //               num_files_to_compact newest files).
  void MinorCompaction(
      HybridTime history_cutoff, size_t num_files_to_compact, ssize_t start_index = -1);

  size_t NumSSTableFiles();
  StringVector SSTableFileNames();

  Status InitRocksDBDir() override;
  Status InitRocksDBOptions() override;
  TabletId tablet_id() override;
  Status FormatDocWriteBatch(const DocWriteBatch& dwb, std::string* dwb_str);
};

// Perform a major compaction on the given database.
Status FullyCompactDB(rocksdb::DB* rocksdb);

class DocDBLoadGenerator {
 public:
  static constexpr uint64_t kDefaultRandomSeed = 23874297385L;

  DocDBLoadGenerator(DocDBRocksDBFixture* fixture,
                     int num_doc_keys,
                     int num_unique_subkeys,
                     dockv::UseHash use_hash,
                     ResolveIntentsDuringRead resolve_intents = ResolveIntentsDuringRead::kTrue,
                     int deletion_chance = 100,
                     int max_nesting_level = 10,
                     uint64 random_seed = kDefaultRandomSeed,
                     int verification_frequency = 100);
  ~DocDBLoadGenerator();

  // Performs a random DocDB operation according to the configured options. This also verifies
  // the consistency of RocksDB-backed DocDB (which is close to the production codepath) with an
  // in-memory non-thread-safe data structure maintained just for sanity checking. Such verification
  // is only performed from time to time, not on every call to PerformOperation.
  //
  // The caller should wrap calls to this function in NO_FATALS.
  //
  // @param compact_history If this is set, we perform the RocksDB-backed DocDB read before and
  //                        after history cleanup, and verify that the state of the document is
  //                        the same in both cases.
  void PerformOperation(bool compact_history = false);

  // @return The next "iteration number" to be performed when PerformOperation is called.
  int next_iteration() const { return iteration_; }

  // The hybrid_time of the last operation performed is always based on the last iteration number.
  // Most times it will be one less than what next_iteration() would return, if we convert the
  // hybrid_time to an integer. This can only be called after PerformOperation() has been called at
  // least once.
  //
  // @return The hybrid_time of the last operation performed.
  HybridTime last_operation_ht() const;

  void FlushRocksDB();

  // Generate a random unsiged 64-bit integer using the random number generator maintained by this
  // object.
  uint64_t NextRandom() { return random_(); }

  // Generate a random integer from 0 to n - 1 using the random number generator maintained by this
  // object.
  int NextRandomInt(int n) { return NextRandom() % n; }

  // Capture and remember a "logical DocDB snapshot" (not to be confused with what we call
  // a "logical RocksDB snapshot"). This keeps track of all document keys and corresponding
  // documents existing at the latest hybrid_time.
  void CaptureDocDbSnapshot();

  void VerifyOldestSnapshot();
  void VerifyRandomDocDbSnapshot();

  // Perform a flashback query at the time of the latest snapshot before the given cleanup
  // hybrid_time and compare it to the state recorded with the snapshot. Expect the two to diverge
  // using ASSERT_TRUE. This is used for testing that old history is actually being cleaned up
  // during compactions.
  void CheckIfOldestSnapshotIsStillValid(const HybridTime cleanup_ht);

  // Removes all snapshots taken before the given hybrid_time. This is done to test history cleanup.
  void RemoveSnapshotsBefore(HybridTime ht);

  size_t num_divergent_old_snapshot() { return divergent_snapshot_ht_and_cleanup_ht_.size(); }

  std::vector<std::pair<int, int>> divergent_snapshot_ht_and_cleanup_ht() {
    return divergent_snapshot_ht_and_cleanup_ht_;
  }

 private:
  rocksdb::DB* rocksdb() { return fixture_->rocksdb(); }
  DocDB doc_db() { return fixture_->doc_db(); }

  DocDBRocksDBFixture* fixture_;
  dockv::RandomNumberGenerator random_;  // Using default seed.
  std::vector<dockv::DocKey> doc_keys_;

  // Whether we should pass transaction context during reads, so DocDB tries to resolve write
  // intents.
  const ResolveIntentsDuringRead resolve_intents_;

  dockv::KeyEntryValues possible_subkeys_;
  int iteration_;
  InMemDocDbState in_mem_docdb_;

  // Deletions happen once every this number of iterations.
  const int deletion_chance_;

  // If this is 1, we'll only use primitive-type documents. If this is 2, we'll make some documents
  // objects (maps). If this is 3, we'll use maps of maps, etc.
  const int max_nesting_level_;

  HybridTime last_operation_ht_;

  std::vector<InMemDocDbState> docdb_snapshots_;

  // HybridTimes and cleanup hybrid_times of examples when
  std::vector<std::pair<int, int>> divergent_snapshot_ht_and_cleanup_ht_;

  // PerformOperation() will verify DocDB state consistency once in this number of iterations.
  const int verification_frequency_;

  const InMemDocDbState& GetOldestSnapshot();

  // Perform a "flashback query" in the RocksDB-based DocDB at the hybrid_time
  // snapshot.captured_at() and verify that the state matches what's in the provided snapshot.
  // This is only invoked on snapshots whose capture hybrid_time has not been garbage-collected,
  // and therefore we always expect this verification to succeed.
  //
  // Calls to this function should be wrapped in NO_FATALS.
  void VerifySnapshot(const InMemDocDbState& snapshot);

  // Look at whether the given snapshot is still valid, and if not, track it in
  // divergent_snapshot_ht_and_cleanup_ht_, so we can later verify that some snapshots have become
  // invalid after history cleanup.
  void RecordSnapshotDivergence(const InMemDocDbState &snapshot, HybridTime cleanup_ht);

  TransactionOperationContext GetReadOperationTransactionContext();
};

// Used for pre-processing multi-line DocDB debug dump strings in tests.  Removes common indentation
// and C++-style comments and applies backslash line continuation.
std::string TrimDocDbDebugDumpStr(const std::string& debug_dump);

#define ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(expected) \
  do { \
    ASSERT_STR_EQ_VERBOSE_TRIMMED( \
        ::yb::util::ApplyEagerLineContinuation(expected), DocDBDebugDumpToStr()); \
  } while(false)

void DisableYcqlPackedRow();
bool YcqlPackedRowEnabled();

}  // namespace docdb
}  // namespace yb
