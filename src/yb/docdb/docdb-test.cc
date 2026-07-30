// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/docdb-test.h"

#include "yb/common/transaction.h"

#include "yb/dockv/reader_projection.h"

#include "yb/util/minmax.h"

namespace yb::docdb {

namespace {

std::set<std::pair<TableLockType, TableLockType>> MakeTableLockConflicts() {
  // Populate the table lock conflict matrix by inserting lock type pairs that conflict.
  // For {a, b} in the below 'min_conflicts_with', 'a' conflicts with all lock types >= 'b', with
  // the exception of 'SHARE' (it doesn't conflict with self), which is explicitly removed later.
  static const std::unordered_map<TableLockType, TableLockType> min_conflicts_with = {
    {ACCESS_SHARE, ACCESS_EXCLUSIVE},
    {ROW_SHARE, EXCLUSIVE},
    {ROW_EXCLUSIVE, SHARE},
    {SHARE_UPDATE_EXCLUSIVE, SHARE_UPDATE_EXCLUSIVE},
    {SHARE, ROW_EXCLUSIVE},
    {SHARE_ROW_EXCLUSIVE, ROW_EXCLUSIVE},
    {EXCLUSIVE, ROW_SHARE},
    {ACCESS_EXCLUSIVE, ACCESS_SHARE}
  };

  static std::set<std::pair<TableLockType, TableLockType>> conflicts;
  for (auto l1 = TableLockType_MIN + 1; l1 <= TableLockType_MAX; l1++) {
    auto it = min_conflicts_with.find(TableLockType(l1));
    CHECK(it != min_conflicts_with.end());
    for (auto l2 = it->second; l2 <= TableLockType_MAX; l2 = static_cast<TableLockType>(l2 + 1)) {
      conflicts.insert({TableLockType(l1), TableLockType(l2)});
    }
  }
  conflicts.erase({SHARE, SHARE});
  return conflicts;
}

} // namespace

// This test confirms that we return the appropriate value for doc_found in the case that the last
// projection we look at is not present. Previously we had a bug where we would set doc_found to
// true if the last projection was present, and false otherwise, reguardless of other projections
// considered. This test ensures we have the correct behavior, returning true as long as any
// projection is present, even if the last one is absent.
TEST_F(DocDBTestQl, LastProjectionIsNull) {
  const auto doc_key = MakeDocKey("mydockey", kIntKey1);
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("p1")), QLValue::Primitive("value"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("p2")), ValueRef(ValueEntryType::kTombstone),
              2000_usec_ht));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["p1"; HT{ physical: 2000 }]) -> "value"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["p2"; HT{ physical: 2000 }]) -> DEL
      )#");

  auto subdoc_key = SubDocKey(doc_key);
  auto encoded_subdoc_key = subdoc_key.EncodeWithoutHt();
  SubDocument doc_from_rocksdb;
  bool subdoc_found_in_rocksdb = false;
  dockv::ReaderProjection projection;
  projection.columns = {
    { .id = ColumnId(1), .subkey = KeyEntryValue("p1"), .data_type = DataType::NULL_VALUE_TYPE },
    { .id = ColumnId(2), .subkey = KeyEntryValue("p2"), .data_type = DataType::NULL_VALUE_TYPE },
  };

  GetSubDocQl(
      doc_db(), encoded_subdoc_key, &doc_from_rocksdb, &subdoc_found_in_rocksdb,
      kNonTransactionalOperationContext, ReadHybridTime::SingleTime(4000_usec_ht),
      &projection);
  EXPECT_TRUE(subdoc_found_in_rocksdb);
  EXPECT_STR_EQ_VERBOSE_TRIMMED(R"#(
{
  "p1": "value"
}
  )#", doc_from_rocksdb.ToString());
}

namespace {

void SetId(DocKey* doc_key, ColocationId id) {
  doc_key->set_colocation_id(id);
}

void SetId(DocKey* doc_key, const Uuid& id) {
  doc_key->set_cotable_id(id);
}

std::string IdToString(ColocationId id) {
  return Format("ColocationId=$0", std::to_string(id));
}

std::string IdToString(const Uuid& id) {
  return Format("CoTableId=$0", id.ToHexString());
}

} // namespace

// Test that table-level tombstone properly hides records.
template<typename T>
void DocDBTestQl::TestTableTombstone(T id) {
  auto doc_key_1 = MakeDocKey("mydockey", kIntKey1);
  SetId(&doc_key_1, id);
  auto doc_key_2 = MakeDocKey("mydockey", kIntKey2);
  SetId(&doc_key_2, id);
  ASSERT_OK(SetPrimitive(
      doc_key_1.Encode(), QLValue::Primitive(1), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      doc_key_2.Encode(), QLValue::Primitive(2), 1000_usec_ht));

  DocKey doc_key_table;
  SetId(&doc_key_table, id);
  ASSERT_OK(DeleteSubDoc(
      doc_key_table.Encode(), 2000_usec_ht));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
      SubDocKey(DocKey($0, [], []), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey($0, [], ["mydockey", 789123]), [HT{ physical: 1000 }]) -> 2
      )#",
      IdToString(id)));
  VerifyDocument(doc_key_1, 4000_usec_ht, "");
  VerifyDocument(doc_key_1, 1500_usec_ht, "1");
}

TEST_F(DocDBTestQl, ColocatedTableTombstone) {
  TestTableTombstone<ColocationId>(0x4001);
}

TEST_F(DocDBTestQl, YsqlSystemTableTombstone) {
  TestTableTombstone<const Uuid&>(
      ASSERT_RESULT(Uuid::FromString("11111111-2222-3333-4444-555555555555")));
}

// Test that table-level tombstone is properly compacted.
template<typename T>
void DocDBTestQl::TestTableTombstoneCompaction(T id) {
  HybridTime t = 1000_usec_ht;

  // Simulate SQL:
  //   INSERT INTO t VALUES ("r1"), ("r2"), ("r3");
  for (int i = 1; i <= 3; ++i) {
    DocKey doc_key;
    std::string range_key_str = Format("r$0", i);

    SetId(&doc_key, id);
    doc_key.ResizeRangeComponents(1);
    doc_key.SetRangeComponent(KeyEntryValue(range_key_str), 0 /* idx */);
    ASSERT_OK(SetPrimitive(
        DocPath(doc_key.Encode(), KeyEntryValue::kLivenessColumn),
        ValueRef(ValueEntryType::kNullLow),
        t));
    t = t.AddDelta(1ms);
  }
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 3000 }]) -> null
      )#",
      IdToString(id)));

  // Simulate SQL (set table tombstone):
  //   TRUNCATE TABLE t;
  {
    DocKey doc_key(id);
    ASSERT_OK(SetPrimitive(
        DocPath(doc_key.Encode()),
        ValueRef(ValueEntryType::kTombstone),
        t));
    t = t.AddDelta(1ms);
  }
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
SubDocKey(DocKey($0, [], []), [HT{ physical: 4000 }]) -> DEL
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 3000 }]) -> null
      )#",
      IdToString(id)));

  // Simulate SQL:
  //  INSERT INTO t VALUES ("r1"), ("r2");
  for (int i = 1; i <= 2; ++i) {
    DocKey doc_key;
    std::string range_key_str = Format("r$0", i);

    SetId(&doc_key, id);
    doc_key.ResizeRangeComponents(1);
    doc_key.SetRangeComponent(KeyEntryValue(range_key_str), 0 /* idx */);
    ASSERT_OK(SetPrimitive(
        DocPath(doc_key.Encode(), KeyEntryValue::kLivenessColumn),
        ValueRef(ValueEntryType::kNullLow),
        t));
    t = t.AddDelta(1ms);
  }
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
SubDocKey(DocKey($0, [], []), [HT{ physical: 4000 }]) -> DEL
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 5000 }]) -> null
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 6000 }]) -> null
SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 3000 }]) -> null
      )#",
      IdToString(id)));

  // Simulate SQL:
  //  DELETE FROM t WHERE c = "r2";
  {
    DocKey doc_key;
    std::string range_key_str = Format("r$0", 2);

    SetId(&doc_key, id);
    doc_key.ResizeRangeComponents(1);
    doc_key.SetRangeComponent(KeyEntryValue(range_key_str), 0 /* idx */);
    ASSERT_OK(SetPrimitive(
        DocPath(doc_key.Encode()),
        ValueRef(ValueEntryType::kTombstone),
        t));
    t = t.AddDelta(1ms);
  }
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
SubDocKey(DocKey($0, [], []), [HT{ physical: 4000 }]) -> DEL
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 5000 }]) -> null
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
SubDocKey(DocKey($0, [], ["r2"]), [HT{ physical: 7000 }]) -> DEL
SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 6000 }]) -> null
SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 3000 }]) -> null
      )#",
      IdToString(id)));

  // Major compact.
  FullyCompactHistoryBefore(10000_usec_ht);
  auto fmt = YcqlPackedRowEnabled()
      ? R"#(
SubDocKey(DocKey($0, [], ["r1"]), [HT{ physical: 5000 }]) -> { }
      )#"
      :  R"#(
SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 5000 }]) -> null
      )#";
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(fmt, IdToString(id)));
}

TEST_F(DocDBTestQl, ColocatedTableTombstoneCompaction) {
  TestTableTombstoneCompaction<ColocationId>(0x5678);
}

TEST_F(DocDBTestQl, YsqlSystemTableTombstoneCompaction) {
  TestTableTombstoneCompaction<const Uuid&>(
      ASSERT_RESULT(Uuid::FromString("66666666-7777-8888-9999-000000000000")));
}

TEST_F(DocDBTestQl, DestroyAfterFilesDeleted) {
  ASSERT_OK(Env::Default()->DeleteRecursively(rocksdb_dir_));
  ASSERT_OK(Env::Default()->DeleteRecursively(IntentsDBDir()));
  ASSERT_OK(DestroyRocksDB());
}

// YB associates a list of <KeyEntryType, IntentTypeSet> to each table lock type such that
// the table lock conflict matrix of postgres is achieved. The below test asserts the same.
//
// Pg conflict matrix - https://www.postgresql.org/docs/current/explicit-locking.html#LOCKING-TABLES
TEST_F(DocDBTableLocksConflictMatrixTest, TableConflictMatrix) {
  const std::set<std::pair<TableLockType, TableLockType>> conflicts = MakeTableLockConflicts();

  for (auto l1 = TableLockType_MIN + 1; l1 <= TableLockType_MAX; l1++) {
    auto lock1 = TableLockType(l1);
    auto entries1 = GetEntriesForLockType(lock1);
    for (auto l2 = l1; l2 <= TableLockType_MAX; l2++) {
      auto lock2 = TableLockType(l2);
      auto entries2 = GetEntriesForLockType(lock2);
      auto has_conflict = ASSERT_RESULT(ObjectLocksConflict(entries1, entries2));
      ASSERT_EQ(has_conflict, conflicts.find({lock1, lock2}) != conflicts.end())
          << Format("Expected $0 to $1have conflicted with $2", TableLockType_Name(lock1),
                    has_conflict ? "" : "not ", TableLockType_Name(lock2));
      ASSERT_EQ(has_conflict, ASSERT_RESULT(ObjectLocksConflict(entries2, entries1)));
    }
  }
}

class DocDBTestBoundaryValues: public DocDBTestWrapper {
 protected:
  void TestBoundaryValues(size_t flush_rate) {
    struct Trackers {
      MinMaxTracker<int64_t> key_ints;
      MinMaxTracker<std::string> key_strs;
      MinMaxTracker<HybridTime> times;
    };

    auto dwb = MakeDocWriteBatch();
    constexpr int kTotalRows = 1000;
    constexpr std::mt19937_64::result_type kSeed = 2886476510;

    std::mt19937_64 rng(kSeed);
    std::uniform_int_distribution<int64_t> distribution(0, std::numeric_limits<int64_t>::max());

    std::vector<Trackers> trackers;
    for (int i = 0; i != kTotalRows; ++i) {
      if (i % flush_rate == 0) {
        trackers.emplace_back();
        ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
      }
      auto key_str = "key_" + std::to_string(distribution(rng));
      auto key_int = distribution(rng);
      auto value_str = "value_" + std::to_string(distribution(rng));
      auto time = HybridTime::FromMicros(distribution(rng));
      auto key = MakeDocKey(key_str, key_int).Encode();
      dockv::DocPath path(key);
      ASSERT_OK(SetPrimitive(path, QLValue::Primitive(value_str), time));
      trackers.back().key_ints(key_int);
      trackers.back().key_strs(key_str);
      trackers.back().times(time);
    }

    string dwb_str;
    ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
    SCOPED_TRACE("\nWrite batch:\n" + dwb_str);
    ASSERT_OK(WriteToRocksDB(dwb, 1000_usec_ht));
    ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

    for (auto i = 0; i != 2; ++i) {
      if (i) {
        ASSERT_OK(ReopenRocksDB());
      }
      std::vector<rocksdb::LiveFileMetaData> files;
      rocksdb()->GetLiveFilesMetaData(&files);
      ASSERT_EQ(trackers.size(), files.size());
      sort(files.begin(), files.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.name_id < rhs.name_id;
      });

      for (size_t j = 0; j != trackers.size(); ++j) {
        const auto &file = files[j];
        const auto &smallest = file.smallest.user_values;
        const auto &largest = file.largest.user_values;
        {
          auto &key_ints = trackers[j].key_ints;
          auto &key_strs = trackers[j].key_strs;
          KeyEntryValue temp = ASSERT_RESULT(TEST_GetKeyEntryValue(smallest, 0));
          ASSERT_EQ(KeyEntryValue(key_strs.min), temp);
          temp = ASSERT_RESULT(TEST_GetKeyEntryValue(largest, 0));
          ASSERT_EQ(KeyEntryValue(key_strs.max), temp);
          temp = ASSERT_RESULT(TEST_GetKeyEntryValue(smallest, 1));
          ASSERT_EQ(KeyEntryValue::Int64(key_ints.min), temp);
          temp = ASSERT_RESULT(TEST_GetKeyEntryValue(largest, 1));
          ASSERT_EQ(KeyEntryValue::Int64(key_ints.max), temp);
        }
      }
    }
  }
};


TEST_F_EX(DocDBTest, BoundaryValues, DocDBTestBoundaryValues) {
  TestBoundaryValues(std::numeric_limits<size_t>::max());
}

TEST_F_EX(DocDBTest, BoundaryValuesMultiFiles, DocDBTestBoundaryValues) {
  TestBoundaryValues(350);
}

void QueryBounds(const DocKey& doc_key, int lower, int upper, int base, const DocDB& doc_db,
                 SubDocument* doc_from_rocksdb, bool* subdoc_found,
                 const SubDocKey& subdoc_to_search) {
  HybridTime ht = 1000000_usec_ht;
  auto lower_key =
      SubDocKey(doc_key, KeyEntryValue("subkey" + std::to_string(base + lower))).EncodeWithoutHt();
  SliceKeyBound lower_bound(lower_key, BoundType::kInclusiveLower);
  auto upper_key =
      SubDocKey(doc_key, KeyEntryValue("subkey" + std::to_string(base + upper))).EncodeWithoutHt();
  SliceKeyBound upper_bound(upper_key, BoundType::kInclusiveUpper);
  auto encoded_subdoc_to_search = subdoc_to_search.EncodeWithoutHt();
  GetRedisSubDocumentData data = { encoded_subdoc_to_search, doc_from_rocksdb, subdoc_found };
  data.low_subkey = &lower_bound;
  data.high_subkey = &upper_bound;
  EXPECT_OK(GetRedisSubDocument(
      doc_db, data, rocksdb::kDefaultQueryId, kNonTransactionalOperationContext,
      ReadOperationData::FromSingleReadTime(ht)));
}

void VerifyBounds(SubDocument* doc_from_rocksdb, int lower, int upper, int base) {
  EXPECT_EQ(upper - lower + 1, doc_from_rocksdb->object_num_keys());

  for (int i = lower; i <= upper; i++) {
    SubDocument* subdoc = doc_from_rocksdb->GetChild(
        KeyEntryValue("subkey" + std::to_string(base + i)));
    ASSERT_TRUE(subdoc != nullptr);
    EXPECT_EQ("value" + std::to_string(i), subdoc->GetString());
  }
}

void QueryBoundsAndVerify(const DocKey& doc_key, int lower, int upper, int base,
                          const DocDB& doc_db, const SubDocKey& subdoc_to_search) {
  SubDocument doc_from_rocksdb;
  bool subdoc_found = false;
  QueryBounds(doc_key, lower, upper, base, doc_db, &doc_from_rocksdb, &subdoc_found,
              subdoc_to_search);
  EXPECT_TRUE(subdoc_found);
  VerifyBounds(&doc_from_rocksdb, lower, upper, base);
}

TEST_F(DocDBTestRedis, TestBuildSubDocumentBounds) {
  const auto doc_key = MakeDocKey("key");
  KeyBytes encoded_doc_key(doc_key.Encode());
  const int nsubkeys = 100;
  const int base = 11000; // To ensure ints can be compared lexicographically.
  string expected_docdb_str;
  AddSubKeys(encoded_doc_key, nsubkeys, base, &expected_docdb_str);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(expected_docdb_str);

  const SubDocKey subdoc_to_search(doc_key);

  QueryBoundsAndVerify(doc_key, 25, 75, base, doc_db(), subdoc_to_search);
  QueryBoundsAndVerify(doc_key, 50, 60, base, doc_db(), subdoc_to_search);
  QueryBoundsAndVerify(doc_key, 0, nsubkeys - 1, base, doc_db(), subdoc_to_search);

  SubDocument doc_from_rocksdb;
  bool subdoc_found = false;
  QueryBounds(doc_key, -100, 200, base, doc_db(), &doc_from_rocksdb, &subdoc_found,
              subdoc_to_search);
  EXPECT_TRUE(subdoc_found);
  VerifyBounds(&doc_from_rocksdb, 0, nsubkeys - 1, base);

  QueryBounds(doc_key, -100, 50, base, doc_db(), &doc_from_rocksdb, &subdoc_found,
              subdoc_to_search);
  EXPECT_TRUE(subdoc_found);
  VerifyBounds(&doc_from_rocksdb, 0, 50, base);

  QueryBounds(doc_key, 50, 150, base, doc_db(), &doc_from_rocksdb, &subdoc_found,
              subdoc_to_search);
  EXPECT_TRUE(subdoc_found);
  VerifyBounds(&doc_from_rocksdb, 50, nsubkeys - 1, base);

  QueryBounds(doc_key, -100, -50, base, doc_db(), &doc_from_rocksdb, &subdoc_found,
              subdoc_to_search);
  EXPECT_FALSE(subdoc_found);

  QueryBounds(doc_key, 101, 150, base, doc_db(), &doc_from_rocksdb, &subdoc_found,
              subdoc_to_search);
  EXPECT_FALSE(subdoc_found);

  // Try bounds without appropriate doc key.
  QueryBounds(dockv::MakeDocKey("abc"), 0, nsubkeys - 1, base, doc_db(), &doc_from_rocksdb,
              &subdoc_found, subdoc_to_search);
  EXPECT_FALSE(subdoc_found);

  // Try bounds different from doc key.
  QueryBounds(doc_key, 0, 99, base, doc_db(), &doc_from_rocksdb, &subdoc_found,
              SubDocKey(dockv::MakeDocKey("abc")));
  EXPECT_FALSE(subdoc_found);

  // Try with bounds pointing to wrong doc key.
  auto doc_key_xyz = MakeDocKey("xyz");
  AddSubKeys(doc_key_xyz.Encode(), nsubkeys, base, &expected_docdb_str);
  QueryBounds(doc_key_xyz, 0, nsubkeys - 1, base, doc_db(), &doc_from_rocksdb,
              &subdoc_found, subdoc_to_search);
  EXPECT_FALSE(subdoc_found);
}

class DocDBMissingSchemaTest : public DocDBTestQl {
 public:
  void MarkMissing(const Uuid& id) {
    missing_cotable_ids_.insert(id);
  }

  void MarkMissing(ColocationId id) {
    missing_colocation_ids_.insert(id);
  }

  template <typename T>
  void TestRowLevelTombstoneRetainedDuringPartialCompaction(T id);

  template <typename T>
  void TestTableLevelTombstoneRetainedDuringPartialCompaction(T id);

  template <typename T>
  void TestNonTombstoneDroppedDuringPartialCompaction(T missing_id, T known_id);

  template <typename T>
  Status InsertLivenessColumnRecord(const T& id, int row_idx, HybridTime ht) {
    DocKey doc_key;
    SetId(&doc_key, id);
    doc_key.ResizeRangeComponents(/* new_size = */ 1);
    doc_key.SetRangeComponent(KeyEntryValue(Format("r$0", row_idx)), /* idx = */ 0);
    return SetPrimitive(
        DocPath(doc_key.Encode(), KeyEntryValue::kLivenessColumn),
        ValueRef(ValueEntryType::kNullLow), ht);
  }

  template <typename T>
  Status InsertLivenessColumnRecords(const T& id, int start_idx, int end_idx, HybridTime ht) {
    for (auto i = start_idx; i <= end_idx; ++i) {
      RETURN_NOT_OK(InsertLivenessColumnRecord(id, i, ht));
    }
    return Status::OK();
  }

  template <typename T>
  Status InsertTombstoneRecords(const T& id, int start_idx, int end_idx, HybridTime ht) {
    for (auto row_idx = start_idx; row_idx <= end_idx; ++row_idx) {
      DocKey doc_key;
      SetId(&doc_key, id);
      doc_key.ResizeRangeComponents(/* new_size = */ 1);
      doc_key.SetRangeComponent(KeyEntryValue(Format("r$0", row_idx)), /* idx = */ 0);
      RETURN_NOT_OK(SetPrimitive(
         DocPath(doc_key.Encode()), ValueRef(ValueEntryType::kTombstone), ht));
    }
    return Status::OK();
  }

  template <typename T>
  Status InsertTableTombstoneRecord(const T& id, HybridTime ht) {
    DocKey table_key;
    SetId(&table_key, id);
    return SetPrimitive(DocPath(table_key.Encode()), ValueRef(ValueEntryType::kTombstone), ht);
  }

 protected:
  Result<CompactionSchemaInfo> CotablePacking(
      const Uuid& table_id, uint32_t schema_version, HybridTime history_cutoff) override {
    if (missing_cotable_ids_.contains(table_id)) {
      return STATUS(NotFound, "Table has been dropped");
    }
    return DocDBRocksDBUtil::CotablePacking(table_id, schema_version, history_cutoff);
  }

  Result<CompactionSchemaInfo> ColocationPacking(
      ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) override {
    if (missing_colocation_ids_.contains(colocation_id)) {
      return STATUS(NotFound, "Table has been dropped");
    }
    return DocDBRocksDBUtil::ColocationPacking(colocation_id, schema_version, history_cutoff);
  }

 private:
  std::unordered_set<Uuid> missing_cotable_ids_;
  std::unordered_set<ColocationId> missing_colocation_ids_;
};

// Verifies that tombstones for entries with a missing schema (dropped table) are retained
// during partial compaction. Dropping them would expose stale data in SST files not included
// in the compaction. This is the scenario described in
// https://github.com/yugabyte/yugabyte-db/issues/28314.
template <typename T>
void DocDBMissingSchemaTest::TestRowLevelTombstoneRetainedDuringPartialCompaction(T id) {
  ASSERT_OK(DisableCompactions());

  // 1. SST file #1: data rows for the colocated table.
  ASSERT_OK(InsertLivenessColumnRecords(id, 1, 5, HybridTime::FromMicros(1000)));
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

  // 2. SST file #2: tombstones deleting some of those rows and new rows.
  ASSERT_OK(InsertTombstoneRecords(id, 1, 3, HybridTime::FromMicros(2000)));
  ASSERT_OK(InsertLivenessColumnRecords(id, 6, 8, HybridTime::FromMicros(2000)));
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

  ASSERT_EQ(2, NumSSTableFiles());

  const auto id_str = IdToString(id);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
      SubDocKey(DocKey($0, [], ["r1"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r2"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r3"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r4"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r5"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r6"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      SubDocKey(DocKey($0, [], ["r7"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      SubDocKey(DocKey($0, [], ["r8"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      )#",
      id_str));

  // Simulate the table being dropped: schema is no longer resolvable.
  MarkMissing(id);

  // Minor compaction of only the tombstone file (file #2). The SST file #1 is NOT included.
  // Before the fix, tombstones would be dropped here because the schema is missing,
  // exposing stale data from file 1.
  MinorCompaction(
      HybridTime::FromMicros(5000), /* num_files_to_compact = */ 1, /* start_index = */ 1);

  // Tombstones must survive the partial compaction, otherwise the data in
  // SST file #1 would become visible again, leading to stale data exposure.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
      SubDocKey(DocKey($0, [], ["r1"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r2"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r3"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r4"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r5"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      )#",
      id_str));
}

// Verifies that when a table-level tombstone is present for a missing-schema coprefix, row-level
// tombstones are redundant and get dropped during partial compaction. The table-level tombstone
// already masks all data, so individual row tombstones are not needed.
template <typename T>
void DocDBMissingSchemaTest::TestTableLevelTombstoneRetainedDuringPartialCompaction(T id) {
  ASSERT_OK(DisableCompactions());

  // 1. SST file #1: data rows for the colocated table.
  ASSERT_OK(InsertLivenessColumnRecords(id, 1, 5, HybridTime::FromMicros(1000)));
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

  // 2. SST file #2: table-level tombstone (DROP TABLE) followed by row-level tombstones.
  ASSERT_OK(InsertTableTombstoneRecord(id, HybridTime::FromMicros(3000)));
  ASSERT_OK(InsertTombstoneRecords(id, 1, 3, HybridTime::FromMicros(2000)));
  ASSERT_OK(InsertLivenessColumnRecords(id, 6, 8, HybridTime::FromMicros(2000)));
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

  ASSERT_EQ(2, NumSSTableFiles());

  const auto id_str = IdToString(id);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
      SubDocKey(DocKey($0, [], []), [HT{ physical: 3000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r1"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r2"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r3"]), [HT{ physical: 2000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r4"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r5"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r6"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      SubDocKey(DocKey($0, [], ["r7"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      SubDocKey(DocKey($0, [], ["r8"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      )#",
      id_str));

  MarkMissing(id);

  // Minor compaction of only the tombstone file (file #2).
  MinorCompaction(
      HybridTime::FromMicros(5000), /* num_files_to_compact = */ 1, /* start_index = */ 1);

  // Only the table-level tombstone should survive. Row-level tombstones are redundant
  // because the table tombstone masks all data beneath it.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
      SubDocKey(DocKey($0, [], []), [HT{ physical: 3000 }]) -> DEL
      SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r2"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r4"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r5"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      )#",
      id_str));
}

// Verifies that non-tombstone entries with a missing schema are still dropped during
// partial compaction (the fix only preserves tombstones, not regular values).
template <typename T>
void DocDBMissingSchemaTest::TestNonTombstoneDroppedDuringPartialCompaction(
    T id, T missing_id) {
  ASSERT_OK(DisableCompactions());

  // 1. SST file #1: data rows for non-missing colocated table.
  ASSERT_OK(InsertLivenessColumnRecord(id, 1, HybridTime::FromMicros(1000)));
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

  // 2. SST file #2: data for both missing and non-missing colocated tables.
  // Entry from the missing colocated table (should be dropped).
  ASSERT_OK(InsertLivenessColumnRecord(missing_id, 2, HybridTime::FromMicros(2000)));
  // Entry from the non-missing colocated table (should survive).
  ASSERT_OK(InsertLivenessColumnRecord(id, 3, HybridTime::FromMicros(2000)));
  ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));

  ASSERT_EQ(2, NumSSTableFiles());

  MarkMissing(missing_id);

  // Minor compaction of only the newer SST file #2.
  MinorCompaction(
      HybridTime::FromMicros(5000), /* num_files_to_compact = */ 1, /* start_index = */ 1);

  // The non-tombstone entry from the missing colocated table should be removed.
  // Both non-missing colocated table entries survive (one in each SST file).
  const auto id_str = IdToString(id);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
      SubDocKey(DocKey($0, [], ["r1"]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey($0, [], ["r3"]), [SystemColumnId(0); HT{ physical: 2000 }]) -> null
      )#",
      id_str));
}

TEST_F(DocDBMissingSchemaTest, CotableRowLevelTombstoneRetainedDuringPartialCompaction) {
  TestRowLevelTombstoneRetainedDuringPartialCompaction(Uuid::Generate());
}

TEST_F(DocDBMissingSchemaTest, ColocationRowLevelTombstoneRetainedDuringPartialCompaction) {
  const auto id = RandomUniformInt<ColocationId>(
      kFirstNormalColocationId, std::numeric_limits<ColocationId>::max());
  TestRowLevelTombstoneRetainedDuringPartialCompaction(id);
}

TEST_F(DocDBMissingSchemaTest, CotableTableLevelTombstoneRetainedDuringPartialCompaction) {
  TestTableLevelTombstoneRetainedDuringPartialCompaction(Uuid::Generate());
}

TEST_F(DocDBMissingSchemaTest, ColocationTableLevelTombstoneRetainedDuringPartialCompaction) {
  const auto id = RandomUniformInt<ColocationId>(
      kFirstNormalColocationId, std::numeric_limits<ColocationId>::max());
  TestTableLevelTombstoneRetainedDuringPartialCompaction(id);
}

TEST_F(DocDBMissingSchemaTest, CotableNonTombstoneDroppedDuringPartialCompaction) {
  const auto id = Uuid::Generate();
  auto missing_id = Uuid::Generate();
  while (id == missing_id) {
    missing_id = Uuid::Generate();
  }
  TestNonTombstoneDroppedDuringPartialCompaction(id, missing_id);
}

TEST_F(DocDBMissingSchemaTest, ColocationNonTombstoneDroppedDuringPartialCompaction) {
  const auto id = RandomUniformInt<ColocationId>(
      kFirstNormalColocationId, std::numeric_limits<ColocationId>::max() - 1);
  TestNonTombstoneDroppedDuringPartialCompaction(id, id + 1);
}

}  // namespace yb::docdb
