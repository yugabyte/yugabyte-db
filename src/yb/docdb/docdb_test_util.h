// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_TEST_UTIL_H_
#define YB_DOCDB_DOCDB_TEST_UTIL_H_

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "rocksdb/db.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/in_mem_docdb.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/subdocument.h"

namespace yb {
namespace docdb {

using RandomNumberGenerator = std::mt19937_64;

// Maximum number of components in a randomly-generated DocKey.
static constexpr int kMaxNumRandomDocKeyParts = 10;

// Maximum number of subkeys in a randomly-geneerated SubDocKey.
static constexpr int kMaxNumRandomSubKeys = 10;

// Note: test data generator methods below are using a non-const reference for the random number
// generator for simplicity, even though it is against Google C++ Style Guide. If we used a pointer,
// we would have to invoke the RNG as (*rng)().

// Generate a random primitive value.
PrimitiveValue GenRandomPrimitiveValue(RandomNumberGenerator* rng);

// Generate a random sequence of primitive values.
std::vector<PrimitiveValue> GenRandomPrimitiveValues(RandomNumberGenerator* rng,
                                                     int max_num = kMaxNumRandomDocKeyParts);

// Generate a "minimal" DocKey.
DocKey CreateMinimalDocKey(RandomNumberGenerator* rng, bool use_hash);

// Generate a random DocKey with up to the default number of components.
DocKey GenRandomDocKey(RandomNumberGenerator* rng, bool use_hash);

std::vector<DocKey> GenRandomDocKeys(RandomNumberGenerator* rng, bool use_hash, int num_keys);

std::vector<SubDocKey> GenRandomSubDocKeys(RandomNumberGenerator* rng,
                                           bool use_hash,
                                           int num_keys);

template<typename T>
const T& RandomElementOf(const std::vector<T>& v, RandomNumberGenerator* rng) {
  return v[(*rng)() % v.size()];
}

// Perform a major compaction on the given database.
void FullyCompactDB(rocksdb::DB* rocksdb);

// An implementation of the document node visitor interface that dumps all events (document
// start/end, object keys and values, etc.) to a string as separate lines.
class DebugDocVisitor : public DocVisitor {
 public:
  DebugDocVisitor();
  virtual ~DebugDocVisitor();

  CHECKED_STATUS StartSubDocument(const SubDocKey &key) override;

  CHECKED_STATUS VisitKey(const PrimitiveValue& key) override;
  CHECKED_STATUS VisitValue(const PrimitiveValue& value) override;

  CHECKED_STATUS EndSubDocument() override;
  CHECKED_STATUS StartObject() override;
  CHECKED_STATUS EndObject() override;
  CHECKED_STATUS StartArray() override;
  CHECKED_STATUS EndArray() override;

  std::string ToString();

 private:
  std::stringstream out_;
};

class LogicalRocksDBDebugSnapshot {
 public:
  LogicalRocksDBDebugSnapshot() {}
  void Capture(rocksdb::DB* rocksdb);
  void RestoreTo(rocksdb::DB *rocksdb) const;
 private:
  std::vector<std::pair<std::string, std::string>> kvs;
  string docdb_debug_dump_str;
};

// A wrapper around a RocksDB instance and provides utility functions on top of it, such as
// compacting the history until a certain point. This is also a convenient base class for GTest test
// classes, because it exposes member functions such as rocksdb() and write_oiptions().
class DocDBRocksDBFixture {
 public:
  DocDBRocksDBFixture();
  ~DocDBRocksDBFixture();

  rocksdb::DB* rocksdb();

  const rocksdb::WriteOptions& write_options() const { return write_options_; }

  const rocksdb::Options& options() const { return rocksdb_options_; }

  void OpenRocksDB();
  void DestroyRocksDB();

  // Writes the given DocWriteBatch to RocksDB. We substitue the hybrid time, if provided.
  rocksdb::Status WriteToRocksDB(const DocWriteBatch& write_batch,
                                 const HybridTime& hybrid_time = HybridTime::kMax);

  void SetHistoryCutoffHybridTime(HybridTime history_cutoff);
  void CompactHistoryBefore(HybridTime history_cutoff);

  // Produces a string listing the contents of the entire RocksDB database, with every key and value
  // decoded as a DocDB key/value and converted to a human-readable string representation.
  std::string DocDBDebugDumpToStr();

  // Checks that contents of the entire RocksDB database is exactly as expected.
  void AssertDocDbDebugDumpStrEqVerboseTrimmed(const string &expected);

  // "Walks" the latest state of the given document using using DebugDocVisitor and returns a string
  // that lists all "events" encountered (document start/end, object start/end/keys/values, etc.)
  std::string DebugWalkDocument(const KeyBytes& encoded_doc_key);

  void SetPrimitive(const DocPath& doc_path,
                    const Value& value,
                    HybridTime hybrid_time,
                    DocWriteBatch* doc_write_batch = nullptr,
                    InitMarkerBehavior use_init_marker = InitMarkerBehavior::kRequired);

  void SetPrimitive(const DocPath& doc_path,
                    const PrimitiveValue& value,
                    HybridTime hybrid_time,
                    DocWriteBatch* doc_write_batch = nullptr,
                    InitMarkerBehavior use_init_marker = InitMarkerBehavior::kRequired) {
    SetPrimitive(doc_path, Value(value), hybrid_time, doc_write_batch, use_init_marker);
  }

  void InsertSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::kOptional);

  void ExtendSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      HybridTime hybrid_time,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::kOptional);

  void DocDBDebugDumpToConsole();

  void FlushRocksDB();

  void SetTableTTL(uint64_t ttl_msec);

 private:
  std::unique_ptr<rocksdb::DB> rocksdb_;
  std::shared_ptr<HistoryRetentionPolicy> retention_policy_;
  Schema schema_;
  rocksdb::Options rocksdb_options_;
  rocksdb::WriteOptions write_options_;
  string rocksdb_dir_;
};

class DocDBLoadGenerator {
 public:
  static constexpr uint64_t kDefaultRandomSeed = 23874297385L;

  DocDBLoadGenerator(DocDBRocksDBFixture* fixture,
                     int num_doc_keys,
                     int num_unique_subkeys,
                     int deletion_chance = 100,
                     int max_nesting_level = 10,
                     uint64 random_seed = kDefaultRandomSeed,
                     int verification_frequency = 100);

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

  int num_divergent_old_snapshot() { return divergent_snapshot_ht_and_cleanup_ht_.size(); }

  std::vector<std::pair<int, int>> divergent_snapshot_ht_and_cleanup_ht() {
    return divergent_snapshot_ht_and_cleanup_ht_;
  }

 private:
  rocksdb::DB* rocksdb() { return fixture_->rocksdb(); }

  DocDBRocksDBFixture* fixture_;
  RandomNumberGenerator random_;  // Using default seed.
  std::vector<DocKey> doc_keys_;
  std::vector<PrimitiveValue> possible_subkeys_;
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
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_TEST_UTIL_H_
