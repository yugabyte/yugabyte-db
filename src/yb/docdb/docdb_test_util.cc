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

#include "yb/docdb/docdb_test_util.h"

#include <algorithm>
#include <memory>
#include <sstream>

#include "yb/common/hybrid_time.h"
#include "yb/common/ql_value.h"

#include "yb/common/transaction.h"
#include "yb/dockv/doc_key.h"
#include "yb/docdb/doc_reader.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/in_mem_docdb.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/rocksdb/db/filename.h"

#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/tostring.h"

using std::string;
using std::unique_ptr;
using std::vector;
using std::stringstream;

using yb::util::ApplyEagerLineContinuation;
using yb::util::TrimStr;
using yb::util::LeftShiftTextBlock;
using yb::util::TrimCppComments;

DECLARE_bool(ycql_enable_packed_row);

namespace yb {
namespace docdb {

using dockv::DocKey;
using dockv::KeyEntryValue;
using dockv::SubDocKey;
using dockv::ValueEntryType;

namespace {

class NonTransactionalStatusProvider: public TransactionStatusManager {
 public:
  HybridTime LocalCommitTime(const TransactionId& id) override {
    Fail();
    return HybridTime::kInvalid;
  }

  boost::optional<TransactionLocalState> LocalTxnData(const TransactionId& id) override {
    Fail();
    return boost::none;
  }

  void RequestStatusAt(const StatusRequest& request) override {
    Fail();
  }

  Result<TransactionMetadata> PrepareMetadata(const LWTransactionMetadataPB& pb) override {
    Fail();
    return STATUS(Expired, "");
  }

  Result<int64_t> RegisterRequest() override {
    Fail();
    return 0;
  }

  void UnregisterRequest(int64_t) override {
    Fail();
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override {
    Fail();
  }

  Status Cleanup(TransactionIdApplyOpIdMap&& set) override {
    Fail();
    return STATUS(NotSupported, "Cleanup not implemented");
  }

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) override {
    Fail();
    return STATUS(NotSupported, "FillPriorities not implemented");
  }

  Result<boost::optional<TabletId>> FindStatusTablet(const TransactionId& id) override {
    return boost::none;
  }

  HybridTime MinRunningHybridTime() const override {
    return HybridTime::kMax;
  }

  Result<HybridTime> WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) override {
    return STATUS(NotSupported, "WaitForSafeTime not implemented");
  }

  const TabletId& tablet_id() const override {
    static TabletId result;
    return result;
  }

  void RecordConflictResolutionKeysScanned(int64_t num_keys) override {}

  void RecordConflictResolutionScanLatency(MonoDelta latency) override {}

 private:
  static void Fail() {
    LOG(FATAL) << "Internal error: trying to get transaction status for non transactional table";
  }
};

NonTransactionalStatusProvider kNonTransactionalStatusProvider;

} // namespace

const TransactionOperationContext kNonTransactionalOperationContext = {
    TransactionId::Nil(), &kNonTransactionalStatusProvider
};

ValueRef GenRandomPrimitiveValue(dockv::RandomNumberGenerator* rng, QLValuePB* holder) {
  auto custom_value_type = dockv::GenRandomPrimitiveValue(rng, holder);
  if (custom_value_type != dockv::ValueEntryType::kInvalid) {
    return ValueRef(custom_value_type);
  }
  return ValueRef(*holder);
}

// ------------------------------------------------------------------------------------------------

Status LogicalRocksDBDebugSnapshot::Capture(rocksdb::DB* rocksdb) {
  kvs.clear();
  rocksdb::ReadOptions read_options;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_options));
  iter->SeekToFirst();
  while (VERIFY_RESULT(iter->CheckedValid())) {
    kvs.emplace_back(iter->key().ToBuffer(), iter->value().ToBuffer());
    iter->Next();
  }
  // Save the DocDB debug dump as a string so we can check that we've properly restored the snapshot
  // in RestoreTo.  It's okay that we have no packing information here because even without packing
  // information, the debugging dump has all the information: when we are missing packing
  // information, we still dump out the value in raw form.
  docdb_debug_dump_str = DocDBDebugDumpToStr(rocksdb, nullptr /*schema_packing_provider*/);
  return Status::OK();
}

Status LogicalRocksDBDebugSnapshot::RestoreTo(rocksdb::DB* rocksdb) const {
  rocksdb::ReadOptions read_options;
  rocksdb::WriteOptions write_options;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_options));
  iter->SeekToFirst();
  while (VERIFY_RESULT(iter->CheckedValid())) {
    RETURN_NOT_OK(rocksdb->Delete(write_options, iter->key()));
    iter->Next();
  }
  for (const auto& kv : kvs) {
    RETURN_NOT_OK(rocksdb->Put(write_options, kv.first, kv.second));
  }
  RETURN_NOT_OK(FullyCompactDB(rocksdb));
  SCHECK_EQ(
      docdb_debug_dump_str, DocDBDebugDumpToStr(rocksdb, nullptr /*schema_packing_provider*/),
      InternalError, "DocDB dump mismatch");
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------

DocDBLoadGenerator::DocDBLoadGenerator(DocDBRocksDBFixture* fixture,
                                       const int num_doc_keys,
                                       const int num_unique_subkeys,
                                       const dockv::UseHash use_hash,
                                       const ResolveIntentsDuringRead resolve_intents,
                                       const int deletion_chance,
                                       const int max_nesting_level,
                                       const uint64 random_seed,
                                       const int verification_frequency)
    : fixture_(fixture),
      doc_keys_(GenRandomDocKeys(&random_, use_hash, num_doc_keys)),
      resolve_intents_(resolve_intents),
      possible_subkeys_(dockv::GenRandomKeyEntryValues(&random_, num_unique_subkeys)),
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

DocDBLoadGenerator::~DocDBLoadGenerator() = default;

template<typename T>
const T& RandomElementOf(const std::vector<T>& v, dockv::RandomNumberGenerator* rng) {
  return v[(*rng)() % v.size()];
}

void DocDBLoadGenerator::PerformOperation(bool compact_history) {
  // Increment the iteration right away so we can return from the function at any time.
  const int current_iteration = iteration_;
  ++iteration_;

  DOCDB_DEBUG_LOG("Starting iteration i=$0", current_iteration);
  auto dwb = fixture_->MakeDocWriteBatch();
  const auto& doc_key = RandomElementOf(doc_keys_, &random_);
  const auto encoded_doc_key = doc_key.Encode();

  const auto* current_doc = in_mem_docdb_.GetDocument(doc_key);

  bool is_deletion = false;
  if (current_doc != nullptr &&
      current_doc->value_type() != ValueEntryType::kObject) {
    // The entire document is not an object, let's delete it.
    is_deletion = true;
  }

  vector<KeyEntryValue> subkeys;
  if (!is_deletion) {
    // Add up to (max_nesting_level_ - 1) subkeys. Combined with the document key itself, this
    // gives us the desired maximum nesting level.
    for (size_t j = 0; j < random_() % max_nesting_level_; ++j) {
      if (current_doc != nullptr && current_doc->value_type() != ValueEntryType::kObject) {
        // We can't add any more subkeys because we've found a primitive subdocument.
        break;
      }
      subkeys.emplace_back(RandomElementOf(possible_subkeys_, &random_));
      if (current_doc != nullptr) {
        current_doc = current_doc->GetChild(subkeys.back());
      }
    }
  }

  const dockv::DocPath doc_path(encoded_doc_key, subkeys);
  QLValuePB value_holder;
  const auto value = GenRandomPrimitiveValue(&random_, &value_holder);
  const HybridTime hybrid_time(current_iteration);
  last_operation_ht_ = hybrid_time;

  if (random_() % deletion_chance_ == 0) {
    is_deletion = true;
  }

  const bool doc_already_exists_in_mem =
      in_mem_docdb_.GetDocument(doc_key) != nullptr;

  if (is_deletion) {
    DOCDB_DEBUG_LOG("Iteration $0: deleting doc path $1", current_iteration, doc_path.ToString());
    ASSERT_OK(dwb.DeleteSubDoc(doc_path, ReadOperationData()));
    ASSERT_OK(in_mem_docdb_.DeleteSubDoc(doc_path));
  } else {
    DOCDB_DEBUG_LOG(
        "Iteration $0: setting value at doc path $1 to $2", current_iteration, doc_path.ToString(),
        value.ToString());
    auto pv = value.custom_value_type() != ValueEntryType::kInvalid
                  ? dockv::PrimitiveValue(value.custom_value_type())
                  : dockv::PrimitiveValue::FromQLValuePB(value_holder);
    ASSERT_OK(in_mem_docdb_.SetPrimitive(doc_path, pv));
    const auto set_primitive_status = dwb.SetPrimitive(doc_path, value);
    if (!set_primitive_status.ok()) {
      DocDBDebugDump(
          rocksdb(), std::cerr, fixture_ /*schema_packing_provider*/, StorageDbType::kRegular);
      LOG(INFO) << "doc_path=" << doc_path.ToString();
    }
    ASSERT_OK(set_primitive_status);
  }

  // We perform our randomly chosen operation first, both on the production version of DocDB
  // sitting on top of RocksDB, and on the in-memory single-threaded debug version used for
  // validation.
  ASSERT_OK(fixture_->WriteToRocksDB(dwb, hybrid_time));
  const auto* const subdoc_from_mem = in_mem_docdb_.GetDocument(doc_key);

  TransactionOperationContext txn_op_context = GetReadOperationTransactionContext();

  // In case we are asked to compact history, we read the document from RocksDB before and after the
  // compaction, and expect to get the same result in both cases.
  for (int do_compaction_now = 0; do_compaction_now <= compact_history; ++do_compaction_now) {
    if (do_compaction_now) {
      // This will happen between the two iterations of the loop. If compact_history is false,
      // there is only one iteration and the compaction does not happen.
      fixture_->FullyCompactHistoryBefore(hybrid_time);
    }
    SubDocKey sub_doc_key(doc_key);
    auto encoded_sub_doc_key = sub_doc_key.EncodeWithoutHt();
    auto doc_from_rocksdb_opt = ASSERT_RESULT(TEST_GetSubDocument(
      encoded_sub_doc_key, doc_db(), rocksdb::kDefaultQueryId, txn_op_context,
      ReadOperationData()));
    if (is_deletion && (
            doc_path.num_subkeys() == 0 ||  // Deleted the entire sub-document,
            !doc_already_exists_in_mem)) {  // or the document did not exist in the first place.
      // In this case, after performing the deletion operation, we definitely should not see the
      // top-level document in RocksDB or in the in-memory database.
      ASSERT_FALSE(doc_from_rocksdb_opt);
      ASSERT_EQ(nullptr, subdoc_from_mem);
    } else {
      // This is not a deletion, or we've deleted a sub-key from a document, but the top-level
      // document should still be there in RocksDB.
      ASSERT_TRUE(doc_from_rocksdb_opt);
      ASSERT_NE(nullptr, subdoc_from_mem);

      ASSERT_EQ(*subdoc_from_mem, *doc_from_rocksdb_opt);
      DOCDB_DEBUG_LOG("Retrieved a document from RocksDB: $0", doc_from_rocksdb_opt->ToString());
      ASSERT_STR_EQ_VERBOSE_TRIMMED(subdoc_from_mem->ToString(), doc_from_rocksdb_opt->ToString());
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
  CHECK(last_operation_ht_.is_valid());
  return last_operation_ht_;
}

void DocDBLoadGenerator::FlushRocksDB() {
  LOG(INFO) << "Forcing a RocksDB flush after hybrid_time " << last_operation_ht().value();
  ASSERT_OK(fixture_->FlushRocksDbAndWait());
}

void DocDBLoadGenerator::CaptureDocDbSnapshot() {
  // Capture snapshots from time to time.
  docdb_snapshots_.emplace_back();
  docdb_snapshots_.back().CaptureAt(doc_db(), HybridTime::kMax);
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
    const int snapshot_idx = NextRandomInt(narrow_cast<int>(docdb_snapshots_.size()));
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

  flashback_state.CaptureAt(doc_db(), snap_ht);
  const bool is_match = flashback_state.EqualsAndLogDiff(snapshot);
  if (!is_match) {
    LOG(ERROR) << details_msg << "\nDOCDB SNAPSHOT VERIFICATION FAILED, DOCDB STATE:";
    fixture_->DocDBDebugDumpToConsole();
  }
  ASSERT_TRUE(is_match) << details_msg;
}

void DocDBLoadGenerator::RecordSnapshotDivergence(const InMemDocDbState &snapshot,
                                                  const HybridTime cleanup_ht) {
  InMemDocDbState flashback_state;
  const auto snap_ht = snapshot.captured_at();
  flashback_state.CaptureAt(doc_db(), snap_ht);
  if (!flashback_state.EqualsAndLogDiff(snapshot, /* log_diff = */ false)) {
    // Implicitly converting hybrid_times to ints. That's OK, because we're using small enough
    // integer values for hybrid_times.
    divergent_snapshot_ht_and_cleanup_ht_.emplace_back(snapshot.captured_at().value(),
                                                       cleanup_ht.value());
  }
}

TransactionOperationContext DocDBLoadGenerator::GetReadOperationTransactionContext() {
  if (resolve_intents_) {
    return kNonTransactionalOperationContext;
  }
  return TransactionOperationContext();
}

// ------------------------------------------------------------------------------------------------

void DocDBRocksDBFixture::AssertDocDbDebugDumpStrEq(
    const std::string &pre_expected, const std::string& packed_row_expected) {
  const auto& expected = !packed_row_expected.empty() && YcqlPackedRowEnabled()
      ? packed_row_expected : pre_expected;
  const string debug_dump_str = TrimDocDbDebugDumpStr(DocDBDebugDumpToStr());
  const string expected_str = TrimDocDbDebugDumpStr(expected);
  if (expected_str != debug_dump_str) {
    auto expected_lines = StringSplit(expected_str, '\n');
    auto actual_lines = StringSplit(debug_dump_str, '\n');
    vector<size_t> mismatch_line_numbers;
    for (size_t i = 0; i < std::min(expected_lines.size(), actual_lines.size()); ++i) {
      if (expected_lines[i] != actual_lines[i]) {
        mismatch_line_numbers.push_back(i + 1);
      }
    }
    LOG(ERROR) << "Assertion failure"
               << "\nExpected DocDB contents:\n\n" << expected_str << "\n"
               << "\nActual DocDB contents:\n\n" << debug_dump_str << "\n"
               << "\nExpected # of lines: " << expected_lines.size()
               << ", actual # of lines: " << actual_lines.size()
               << "\nLines not matching: " << AsString(mismatch_line_numbers)
               << "\nPlease check if source files have trailing whitespace and remove it.";

    FAIL();
  }
}

void DocDBRocksDBFixture::FullyCompactHistoryBefore(HybridTime history_cutoff) {
  LOG(INFO) << "Major-compacting history before hybrid_time " << history_cutoff;
  SetHistoryCutoffHybridTime(history_cutoff);
  auto se = ScopeExit([this] {
    SetHistoryCutoffHybridTime(HybridTime::kMin);
  });

  ASSERT_OK(FlushRocksDbAndWait());
  ASSERT_OK(FullyCompactDB(regular_db_.get()));
}

void DocDBRocksDBFixture::FullyCompactHistoryBefore(
    HistoryCutoff history_cutoff) {
  LOG(INFO) << "Major-compacting history before hybrid_time " << history_cutoff;
  retention_policy_->SetHistoryCutoff(history_cutoff);
  auto se = ScopeExit([this] {
    SetHistoryCutoffHybridTime(HybridTime::kMin);
  });

  ASSERT_OK(FlushRocksDbAndWait());
  ASSERT_OK(FullyCompactDB(regular_db_.get()));
}

void DocDBRocksDBFixture::MinorCompaction(
    HybridTime history_cutoff,
    size_t num_files_to_compact,
    ssize_t start_index) {

  ASSERT_OK(FlushRocksDbAndWait());
  SetHistoryCutoffHybridTime(history_cutoff);
  auto se = ScopeExit([this] {
    SetHistoryCutoffHybridTime(HybridTime::kMin);
  });

  rocksdb::ColumnFamilyMetaData cf_meta;
  regular_db_->GetColumnFamilyMetaData(&cf_meta);

  vector<string> compaction_input_file_names;
  vector<string> remaining_file_names;

  size_t initial_num_files = 0;
  {
    const auto& files = cf_meta.levels[0].files;
    initial_num_files = files.size();
    ASSERT_LE(num_files_to_compact, files.size());
    vector<string> file_names;
    for (const auto& sst_meta : files) {
      file_names.push_back(sst_meta.Name());
    }
    SortByKey(file_names.begin(), file_names.end(), rocksdb::TableFileNameToNumber);

    if (start_index < 0) {
      start_index = file_names.size() - num_files_to_compact;
    }

    for (size_t i = 0; i < file_names.size(); ++i) {
      if (implicit_cast<size_t>(start_index) <= i &&
          compaction_input_file_names.size() < num_files_to_compact) {
        compaction_input_file_names.push_back(file_names[i]);
      } else {
        remaining_file_names.push_back(file_names[i]);
      }
    }
    ASSERT_EQ(num_files_to_compact, compaction_input_file_names.size())
        << "Tried to add " << num_files_to_compact << " files starting with index " << start_index
        << ", ended up adding " << yb::ToString(compaction_input_file_names)
        << " and leaving " << yb::ToString(remaining_file_names) << " out. All files: "
        << yb::ToString(file_names);

    LOG(INFO) << "Minor-compacting history before hybrid_time " << history_cutoff << ":\n"
              << "  files being compacted: " << yb::ToString(compaction_input_file_names) << "\n"
              << "  other files: " << yb::ToString(remaining_file_names);

    auto minor_compaction = file_names.size() != compaction_input_file_names.size();
    if (minor_compaction) {
      delete_marker_retention_time_ = HybridTime::kMin;
    }
    ASSERT_OK(regular_db_->CompactFiles(
        rocksdb::CompactionOptions(),
        compaction_input_file_names,
        /* output_level */ 0));
    if (minor_compaction) {
      delete_marker_retention_time_ = HybridTime::kMax;
    }
    const auto sstables_after_compaction = SSTableFileNames();
    LOG(INFO) << "SSTable files after compaction: " << sstables_after_compaction.size()
              << " (" << yb::ToString(sstables_after_compaction) << ")";
    for (const auto& remaining_file : remaining_file_names) {
      ASSERT_TRUE(
          std::find(sstables_after_compaction.begin(), sstables_after_compaction.end(),
                    remaining_file) != sstables_after_compaction.end()
      ) << "File " << remaining_file << " not found in file list after compaction: "
        << yb::ToString(sstables_after_compaction) << ", even though none of these files were "
        << "supposed to be compacted: " << yb::ToString(remaining_file_names);
    }
  }

  regular_db_->GetColumnFamilyMetaData(&cf_meta);
  vector<string> files_after_compaction;
  for (const auto& sst_meta : cf_meta.levels[0].files) {
    files_after_compaction.push_back(sst_meta.Name());
  }
  const int64_t expected_resulting_num_files = initial_num_files - num_files_to_compact + 1;
  ASSERT_EQ(expected_resulting_num_files,
            static_cast<int64_t>(cf_meta.levels[0].files.size()))
      << "Files after compaction: " << yb::ToString(files_after_compaction);
}

size_t DocDBRocksDBFixture::NumSSTableFiles() {
  rocksdb::ColumnFamilyMetaData cf_meta;
  regular_db_->GetColumnFamilyMetaData(&cf_meta);
  return cf_meta.levels[0].files.size();
}

StringVector DocDBRocksDBFixture::SSTableFileNames() {
  rocksdb::ColumnFamilyMetaData cf_meta;
  regular_db_->GetColumnFamilyMetaData(&cf_meta);
  StringVector files;
  for (const auto& sstable_meta : cf_meta.levels[0].files) {
    files.push_back(sstable_meta.Name());
  }
  SortByKey(files.begin(), files.end(), rocksdb::TableFileNameToNumber);
  return files;
}

Status DocDBRocksDBFixture::FormatDocWriteBatch(const DocWriteBatch &dwb, string* dwb_str) {
  WriteBatchFormatter formatter;
  rocksdb::WriteBatch rocksdb_write_batch;
  RETURN_NOT_OK(PopulateRocksDBWriteBatch(dwb, &rocksdb_write_batch));
  RETURN_NOT_OK(rocksdb_write_batch.Iterate(&formatter));
  *dwb_str = formatter.str();
  return Status::OK();
}

Status FullyCompactDB(rocksdb::DB* rocksdb) {
  rocksdb::CompactRangeOptions compact_range_options;
  return rocksdb->CompactRange(compact_range_options, nullptr, nullptr);
}

Status DocDBRocksDBFixture::InitRocksDBDir() {
  string test_dir;
  RETURN_NOT_OK(Env::Default()->GetTestDirectory(&test_dir));
  rocksdb_dir_ = JoinPathSegments(test_dir, StringPrintf("mytestdb-%d", rand()));
  CHECK(!rocksdb_dir_.empty());  // Check twice before we recursively delete anything.
  CHECK_NE(rocksdb_dir_, "/");
  RETURN_NOT_OK(Env::Default()->DeleteRecursively(rocksdb_dir_));
  RETURN_NOT_OK(Env::Default()->DeleteRecursively(IntentsDBDir()));
  return Status::OK();
}

string DocDBRocksDBFixture::tablet_id() {
  return "mytablet";
}

Status DocDBRocksDBFixture::InitRocksDBOptions() {
  RETURN_NOT_OK(InitCommonRocksDBOptionsForTests(tablet_id()));
  return Status::OK();
}

string TrimDocDbDebugDumpStr(const string& debug_dump_str) {
  return TrimStr(ApplyEagerLineContinuation(LeftShiftTextBlock(TrimCppComments(debug_dump_str))));
}

void DisableYcqlPackedRow() {
  ASSERT_OK(SET_FLAG(ycql_enable_packed_row, false));
}

bool YcqlPackedRowEnabled() {
  return FLAGS_ycql_enable_packed_row;
}

}  // namespace docdb
}  // namespace yb
