// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb_test_util.h"

#include <algorithm>
#include <memory>
#include <sstream>

#include "rocksdb/table.h"
#include "rocksdb/util/statistics.h"

#include "yb/common/hybrid_time.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/in_mem_docdb.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/tablet/tablet_options.h"

using std::endl;
using std::make_shared;
using std::string;
using std::unique_ptr;
using std::vector;
using std::stringstream;

using strings::Substitute;

using yb::util::ApplyEagerLineContinuation;
using yb::util::FormatBytesAsStr;

namespace yb {
namespace docdb {

PrimitiveValue GenRandomPrimitiveValue(RandomNumberGenerator* rng) {
  static vector<string> kFruit = {
      "Apple",
      "Apricot",
      "Avocado",
      "Banana",
      "Bilberry",
      "Blackberry",
      "Blackcurrant",
      "Blood orange",
      "Blueberry",
      "Boysenberry",
      "Cantaloupe",
      "Cherimoya",
      "Cherry",
      "Clementine",
      "Cloudberry",
      "Coconut",
      "Cranberry",
      "Cucumber",
      "Currant",
      "Custard apple",
      "Damson",
      "Date",
      "Decaisnea Fargesii",
      "Dragonfruit",
      "Durian",
      "Elderberry",
      "Feijoa",
      "Fig",
      "Goji berry",
      "Gooseberry",
      "Grape",
      "Grapefruit",
      "Guava",
      "Honeyberry",
      "Honeydew",
      "Huckleberry",
      "Jabuticaba",
      "Jackfruit",
      "Jambul",
      "Jujube",
      "Juniper berry",
      "Kiwifruit",
      "Kumquat",
      "Lemon",
      "Lime",
      "Longan",
      "Loquat",
      "Lychee",
      "Mandarine",
      "Mango",
      "Marionberry",
      "Melon",
      "Miracle fruit",
      "Mulberry",
      "Nance",
      "Nectarine",
      "Olive",
      "Orange",
      "Papaya",
      "Passionfruit",
      "Peach",
      "Pear",
      "Persimmon",
      "Physalis",
      "Pineapple",
      "Plantain",
      "Plum",
      "Plumcot (or Pluot)",
      "Pomegranate",
      "Pomelo",
      "Prune (dried plum)",
      "Purple mangosteen",
      "Quince",
      "Raisin",
      "Rambutan",
      "Raspberry",
      "Redcurrant",
      "Salak",
      "Salal berry",
      "Salmonberry",
      "Satsuma",
      "Star fruit",
      "Strawberry",
      "Tamarillo",
      "Tamarind",
      "Tangerine",
      "Tomato",
      "Ugli fruit",
      "Watermelon",
      "Yuzu"
  };
  switch ((*rng)() % 6) {
    case 0:
      return PrimitiveValue(static_cast<int64_t>((*rng)()));
    case 1: {
      string s;
      for (int j = 0; j < (*rng)() % 50; ++j) {
        s.push_back((*rng)() & 0xff);
      }
      return PrimitiveValue(s);
    }
    case 2: return PrimitiveValue(ValueType::kNull);
    case 3: return PrimitiveValue(ValueType::kTrue);
    case 4: return PrimitiveValue(ValueType::kFalse);
    case 5: return PrimitiveValue(kFruit[(*rng)() % kFruit.size()]);
  }
  LOG(FATAL) << "Should never get here";
  return PrimitiveValue();  // to make the compiler happy
}


// Generate a vector of random primitive values.
vector<PrimitiveValue> GenRandomPrimitiveValues(RandomNumberGenerator* rng, int max_num) {
  vector<PrimitiveValue> result;
  for (int i = 0; i < (*rng)() % (max_num + 1); ++i) {
    result.push_back(GenRandomPrimitiveValue(rng));
  }
  return result;
}

DocKey CreateMinimalDocKey(RandomNumberGenerator* rng, bool use_hash) {
  return use_hash ? DocKey(static_cast<DocKeyHash>((*rng)()), {}, {}) : DocKey();
}

DocKey GenRandomDocKey(RandomNumberGenerator* rng, bool use_hash) {
  if (use_hash) {
    return DocKey(
        static_cast<uint32_t>((*rng)()),  // this is just a random value, not a hash function result
        GenRandomPrimitiveValues(rng),
        GenRandomPrimitiveValues(rng));
  } else {
    return DocKey(GenRandomPrimitiveValues(rng));
  }
}

vector<DocKey> GenRandomDocKeys(RandomNumberGenerator* rng, bool use_hash, int num_keys) {
  vector<DocKey> result;
  result.push_back(CreateMinimalDocKey(rng, use_hash));
  for (int iteration = 0; iteration < num_keys; ++iteration) {
    result.push_back(GenRandomDocKey(rng, use_hash));
  }
  return result;
}

vector<SubDocKey> GenRandomSubDocKeys(RandomNumberGenerator* rng, bool use_hash, int num_keys) {
  vector<SubDocKey> result;
  result.push_back(SubDocKey(CreateMinimalDocKey(rng, use_hash), HybridTime((*rng)())));
  for (int iteration = 0; iteration < num_keys; ++iteration) {
    result.push_back(SubDocKey(GenRandomDocKey(rng, use_hash)));
    for (int i = 0; i < (*rng)() % (kMaxNumRandomSubKeys + 1); ++i) {
      result.back().AppendSubKeysAndMaybeHybridTime(GenRandomPrimitiveValue(rng));
    }
    const IntraTxnWriteId write_id = static_cast<IntraTxnWriteId>(
        (*rng)() % 2 == 0 ? 0 : (*rng)() % 1000000);
    result.back().set_hybrid_time(DocHybridTime(HybridTime((*rng)()), write_id));
  }
  return result;
}
// ------------------------------------------------------------------------------------------------

void LogicalRocksDBDebugSnapshot::Capture(rocksdb::DB* rocksdb) {
  kvs.clear();
  rocksdb::ReadOptions read_options;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_options));
  iter->SeekToFirst();
  while (iter->Valid()) {
    kvs.emplace_back(iter->key().ToString(/* hex = */ false),
                     iter->value().ToString(/* hex = */ false));
    iter->Next();
  }
  // Save the DocDB debug dump as a string so we can check that we've properly restored the snapshot
  // in RestoreTo.
  docdb_debug_dump_str = DocDBDebugDumpToStr(rocksdb);
}

void LogicalRocksDBDebugSnapshot::RestoreTo(rocksdb::DB *rocksdb) const {
  rocksdb::ReadOptions read_options;
  rocksdb::WriteOptions write_options;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_options));
  iter->SeekToFirst();
  while (iter->Valid()) {
    ASSERT_OK(rocksdb->Delete(write_options, iter->key()));
    iter->Next();
  }
  for (const auto& kv : kvs) {
    ASSERT_OK(rocksdb->Put(write_options, kv.first, kv.second));
  }
  ASSERT_OK(FullyCompactDB(rocksdb));
  ASSERT_EQ(docdb_debug_dump_str, DocDBDebugDumpToStr(rocksdb));
}

// ------------------------------------------------------------------------------------------------

DocDBLoadGenerator::DocDBLoadGenerator(DocDBRocksDBFixtureTest* fixture,
                                       const int num_doc_keys,
                                       const int num_unique_subkeys,
                                       const bool use_hash,
                                       const int deletion_chance,
                                       const int max_nesting_level,
                                       const uint64 random_seed,
                                       const int verification_frequency)
    : fixture_(fixture),
      doc_keys_(GenRandomDocKeys(&random_, use_hash, num_doc_keys)),
      possible_subkeys_(GenRandomPrimitiveValues(&random_, num_unique_subkeys)),
      iteration_(1),
      deletion_chance_(deletion_chance),
      max_nesting_level_(max_nesting_level),
      verification_frequency_(verification_frequency) {
  CHECK_GE(max_nesting_level_, 1);
  // Use a fixed seed so that tests are deterministic.
  random_.seed(random_seed);

  // This is done so we can use VerifySnapshot with in_mem_docdb_. That should preform a "latest"
  // read.
  in_mem_docdb_.SetCaptureHybridTime(HybridTime::kMax);
}

void DocDBLoadGenerator::PerformOperation(bool compact_history) {
  // Increment the iteration right away so we can return from the function at any time.
  const int current_iteration = iteration_;
  ++iteration_;

  DOCDB_DEBUG_LOG("Starting iteration i=$0", current_iteration);
  DocWriteBatch dwb(fixture_->rocksdb(), &fixture_->monotonic_counter());
  const auto& doc_key = RandomElementOf(doc_keys_, &random_);
  const KeyBytes encoded_doc_key(doc_key.Encode());

  const SubDocument* current_doc = in_mem_docdb_.GetDocument(doc_key);

  bool is_deletion = false;
  if (current_doc != nullptr &&
      current_doc->value_type() != ValueType::kObject) {
    // The entire document is not an object, let's delete it.
    is_deletion = true;
  }

  vector<PrimitiveValue> subkeys;
  if (!is_deletion) {
    // Add up to (max_nesting_level_ - 1) subkeys. Combined with the document key itself, this
    // gives us the desired maximum nesting level.
    for (int j = 0; j < random_() % max_nesting_level_; ++j) {
      if (current_doc != nullptr && current_doc->value_type() != ValueType::kObject) {
        // We can't add any more subkeys because we've found a primitive subdocument.
        break;
      }
      subkeys.emplace_back(RandomElementOf(possible_subkeys_, &random_));
      if (current_doc != nullptr) {
        current_doc = current_doc->GetChild(subkeys.back());
      }
    }
  }

  const DocPath doc_path(encoded_doc_key, subkeys);
  const auto value = GenRandomPrimitiveValue(&random_);
  const HybridTime hybrid_time(current_iteration);
  last_operation_ht_ = hybrid_time;

  if (random_() % deletion_chance_ == 0) {
    is_deletion = true;
  }

  const bool doc_already_exists_in_mem =
      in_mem_docdb_.GetDocument(doc_key) != nullptr;

  if (is_deletion) {
    DOCDB_DEBUG_LOG("Iteration $0: deleting doc path $1", current_iteration, doc_path.ToString());
    ASSERT_OK(dwb.DeleteSubDoc(doc_path));
    ASSERT_OK(in_mem_docdb_.DeleteSubDoc(doc_path));
  } else {
    DOCDB_DEBUG_LOG("Iteration $0: setting value at doc path $1 to $2",
                    current_iteration, doc_path.ToString(), value.ToString());
    ASSERT_OK(in_mem_docdb_.SetPrimitive(doc_path, value));
    const auto set_primitive_status = dwb.SetPrimitive(doc_path, value);
    if (!set_primitive_status.ok()) {
      DocDBDebugDump(rocksdb(), std::cerr);
      LOG(INFO) << "doc_path=" << doc_path.ToString();
    }
    ASSERT_OK(set_primitive_status);
  }

  // We perform our randomly chosen operation first, both on the production version of DocDB
  // sitting on top of RocksDB, and on the in-memory single-threaded debug version used for
  // validation.
  ASSERT_OK(fixture_->WriteToRocksDB(dwb, hybrid_time));
  const SubDocument* const subdoc_from_mem = in_mem_docdb_.GetDocument(doc_key);

  // In case we are asked to compact history, we read the document from RocksDB before and after the
  // compaction, and expect to get the same result in both cases.
  for (int do_compaction_now = 0; do_compaction_now <= compact_history; ++do_compaction_now) {
    if (do_compaction_now) {
      // This will happen between the two iterations of the loop. If compact_history is false,
      // there is only one iteration and the compaction does not happen.
      fixture_->CompactHistoryBefore(hybrid_time);
    }
    SubDocument doc_from_rocksdb;
    bool doc_found_in_rocksdb = false;
    ASSERT_OK(
        GetSubDocument(rocksdb(), SubDocKey(doc_key), &doc_from_rocksdb, &doc_found_in_rocksdb,
                       rocksdb::kDefaultQueryId));
    if (is_deletion && (
            doc_path.num_subkeys() == 0 ||  // Deleted the entire sub-document,
            !doc_already_exists_in_mem)) {  // or the document did not exist in the first place.
      // In this case, after performing the deletion operation, we definitely should not see the
      // top-level document in RocksDB or in the in-memory database.
      ASSERT_FALSE(doc_found_in_rocksdb);
      ASSERT_EQ(nullptr, subdoc_from_mem);
    } else {
      // This is not a deletion, or we've deleted a sub-key from a document, but the top-level
      // document should still be there in RocksDB.
      ASSERT_TRUE(doc_found_in_rocksdb);
      ASSERT_NE(nullptr, subdoc_from_mem);

      ASSERT_EQ(*subdoc_from_mem, doc_from_rocksdb);
      DOCDB_DEBUG_LOG("Retrieved a document from RocksDB: $0", doc_from_rocksdb.ToString());
      ASSERT_STR_EQ_VERBOSE_TRIMMED(subdoc_from_mem->ToString(), doc_from_rocksdb.ToString());
    }
  }

  if (current_iteration % verification_frequency_ == 0) {
    // in_mem_docdb_ has its captured_at() hybrid_time set to HybridTime::kMax, so the following
    // will result in checking the latest state of DocDB stored in RocksDB against in_mem_docdb_.
    ASSERT_NO_FATALS(VerifySnapshot(in_mem_docdb_))
        << "Discrepancy between RocksDB-based and in-memory DocDB state found after iteration "
        << current_iteration;
  }
}

HybridTime DocDBLoadGenerator::last_operation_ht() const {
  CHECK_NE(last_operation_ht_, HybridTime::kInvalidHybridTime);
  return last_operation_ht_;
}

void DocDBLoadGenerator::FlushRocksDB() {
  LOG(INFO) << "Forcing a RocksDB flush after hybrid_time " << last_operation_ht().value();
  ASSERT_OK(fixture_->FlushRocksDB());
}

void DocDBLoadGenerator::CaptureDocDbSnapshot() {
  // Capture snapshots from time to time.
  docdb_snapshots_.emplace_back();
  docdb_snapshots_.back().CaptureAt(rocksdb(), HybridTime::kMax);
  docdb_snapshots_.back().SetCaptureHybridTime(last_operation_ht_);
}

void DocDBLoadGenerator::VerifyOldestSnapshot() {
  if (!docdb_snapshots_.empty()) {
    ASSERT_NO_FATALS(VerifySnapshot(GetOldestSnapshot()));
  }
}

void DocDBLoadGenerator::CheckIfOldestSnapshotIsStillValid(const HybridTime cleanup_ht) {
  if (docdb_snapshots_.empty()) {
    return;
  }

  const InMemDocDbState* latest_snapshot_before_ht = nullptr;
  for (const auto& snapshot : docdb_snapshots_) {
    const HybridTime snap_ht = snapshot.captured_at();
    if (snap_ht.CompareTo(cleanup_ht) < 0 &&
        (latest_snapshot_before_ht == nullptr ||
         latest_snapshot_before_ht->captured_at().CompareTo(snap_ht) < 0)) {
      latest_snapshot_before_ht = &snapshot;
    }
  }

  if (latest_snapshot_before_ht == nullptr) {
    return;
  }

  const auto& snapshot = *latest_snapshot_before_ht;
  LOG(INFO) << "Checking whether snapshot at hybrid_time "
            << snapshot.captured_at().ToDebugString()
            << " is no longer valid after history cleanup for hybrid_times before "
            << cleanup_ht.ToDebugString()
            << ", last operation hybrid_time: " << last_operation_ht() << ".";
  RecordSnapshotDivergence(snapshot, cleanup_ht);
}

void DocDBLoadGenerator::VerifyRandomDocDbSnapshot() {
  if (!docdb_snapshots_.empty()) {
    const int snapshot_idx = NextRandomInt(docdb_snapshots_.size());
    ASSERT_NO_FATALS(VerifySnapshot(docdb_snapshots_[snapshot_idx]));
  }
}

void DocDBLoadGenerator::RemoveSnapshotsBefore(HybridTime ht) {
  docdb_snapshots_.erase(
      std::remove_if(docdb_snapshots_.begin(),
                     docdb_snapshots_.end(),
                     [=](const InMemDocDbState& entry) { return entry.captured_at() < ht; }),
      docdb_snapshots_.end());
  // Double-check that there is no state corruption in any of the snapshots. Such corruption
  // happened when I (Mikhail) initially forgot to add the "erase" call above (as per the
  // "erase/remove" C++ idiom), and ended up with a bunch of moved-from objects still in the
  // snapshots array.
  for (const auto& snapshot : docdb_snapshots_) {
    snapshot.SanityCheck();
  }
}

const InMemDocDbState& DocDBLoadGenerator::GetOldestSnapshot() {
  CHECK(!docdb_snapshots_.empty());
  return *std::min_element(
      docdb_snapshots_.begin(),
      docdb_snapshots_.end(),
      [](const InMemDocDbState& a, const InMemDocDbState& b) {
        return a.captured_at() < b.captured_at();
      });
}

void DocDBLoadGenerator::VerifySnapshot(const InMemDocDbState& snapshot) {
  const HybridTime snap_ht = snapshot.captured_at();
  InMemDocDbState flashback_state;

  string details_msg;
  {
    stringstream details_ss;
    details_ss << "After operation at hybrid_time " << last_operation_ht().value() << ": "
        << "performing a flashback query at hybrid_time " << snap_ht.ToDebugString() << " "
        << "(last operation's hybrid_time: " << last_operation_ht() << ") "
        << "and verifying it against the snapshot captured at that hybrid_time.";
    details_msg = details_ss.str();
  }
  LOG(INFO) << details_msg;

  flashback_state.CaptureAt(rocksdb(), snap_ht);
  const bool is_match = flashback_state.EqualsAndLogDiff(snapshot);
  if (!is_match) {
    LOG(ERROR) << details_msg << "\nDOCDB SNAPSHOT VERIFICATION FAILED, DOCDB STATE:";
    ASSERT_OK(fixture_->DocDBDebugDumpToConsole());
  }
  ASSERT_TRUE(is_match) << details_msg;
}

void DocDBLoadGenerator::RecordSnapshotDivergence(const InMemDocDbState &snapshot,
                                                  const HybridTime cleanup_ht) {
  InMemDocDbState flashback_state;
  const auto snap_ht = snapshot.captured_at();
  flashback_state.CaptureAt(rocksdb(), snap_ht);
  if (!flashback_state.EqualsAndLogDiff(snapshot, /* log_diff = */ false)) {
    // Implicitly converting hybrid_times to ints. That's OK, because we're using small enough
    // integer values for hybrid_times.
    divergent_snapshot_ht_and_cleanup_ht_.emplace_back(snapshot.captured_at().value(),
                                                       cleanup_ht.value());
  }
}

// ------------------------------------------------------------------------------------------------

void DocDBRocksDBFixtureTest::AssertDocDbDebugDumpStrEq(const string &expected) {
  ASSERT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected), DocDBDebugDumpToStr());
}

void DocDBRocksDBFixtureTest::CompactHistoryBefore(HybridTime history_cutoff) {
  LOG(INFO) << "Compacting history before hybrid_time " << history_cutoff.ToDebugString();
  SetHistoryCutoffHybridTime(history_cutoff);
  ASSERT_OK(FlushRocksDB());
  ASSERT_OK(FullyCompactDB(rocksdb_.get()));
  SetHistoryCutoffHybridTime(HybridTime::kMin);
}

Status DocDBRocksDBFixtureTest::FormatDocWriteBatch(const DocWriteBatch &dwb, string* dwb_str) {
  WriteBatchFormatter formatter;
  rocksdb::WriteBatch rocksdb_write_batch;
  RETURN_NOT_OK(PopulateRocksDBWriteBatch(dwb, &rocksdb_write_batch));
  RETURN_NOT_OK(rocksdb_write_batch.Iterate(&formatter));
  *dwb_str = formatter.str();
  return Status::OK();
}

Status DocDBRocksDBFixtureTest::DebugWalkDocument(const KeyBytes& encoded_doc_key,
                                                  string* doc_str) {
  DebugDocVisitor doc_visitor;
  RETURN_NOT_OK(ScanSubDocument(rocksdb(), encoded_doc_key, &doc_visitor,
                                rocksdb::kDefaultQueryId));
  *doc_str = doc_visitor.ToString();
  return Status::OK();
}

Status FullyCompactDB(rocksdb::DB* rocksdb) {
  rocksdb::CompactRangeOptions compact_range_options;
  return rocksdb->CompactRange(compact_range_options, nullptr, nullptr);
}

Status DocDBRocksDBFixtureTest::InitRocksDBDir() {
  string test_dir;
  RETURN_NOT_OK(Env::Default()->GetTestDirectory(&test_dir));
  rocksdb_dir_ = JoinPathSegments(test_dir, StringPrintf("mytestdb-%d", rand()));
  CHECK(!rocksdb_dir_.empty());  // Check twice before we recursively delete anything.
  CHECK_NE(rocksdb_dir_, "/");
  RETURN_NOT_OK(Env::Default()->DeleteRecursively(rocksdb_dir_));
  return Status::OK();
}

string DocDBRocksDBFixtureTest::tablet_id() {
  return "mytablet";
}

Status DocDBRocksDBFixtureTest::InitRocksDBOptions() {
  return InitCommonRocksDBOptions();
}

}  // namespace docdb
}  // namespace yb
