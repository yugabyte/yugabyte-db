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

#include <limits>
#include <string>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/unique_index_verifier.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/db.h"

#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"

namespace yb::docdb {

namespace {

// Verification-window bounds shared by the tests. The backfill hybrid time sits strictly
// inside so tests can place records below, at, and above it.
constexpr HybridTime kWindowLower = 3000_usec_ht;   // backfill_read_ht.
constexpr HybridTime kBackfillHT = kWindowLower;    // Backfill writes land exactly here.
constexpr HybridTime kWindowUpper = 9000_usec_ht;   // verify_upper_ht.

// A marked-domain write ID: backfill writes carry kBackfillWriteIdFloor | raft_index, and
// the verifier uses the floor to attribute non-packed column records to backfill operations.
constexpr IntraTxnWriteId kBackfillWriteId = kBackfillWriteIdFloor + 1;

}  // namespace

class UniqueIndexVerifierTest : public DocDBTestBase {
 protected:
  void SetUp() override {
    DocDBTestBase::SetUp();

    // A unique-index-shaped schema: hashed indexed value, the null-suffix range column,
    // then ybidxbasectid and one INCLUDE column as regular value columns.
    SchemaBuilder builder;
    ASSERT_OK(builder.AddHashKeyColumn("v", DataType::INT32));
    ASSERT_OK(builder.AddKeyColumn("ybuniqueidxkeysuffix", DataType::BINARY));
    ASSERT_OK(builder.AddColumn("ybidxbasectid", DataType::BINARY));
    ASSERT_OK(builder.AddNullableColumn("inc", DataType::INT32));
    schema_ = builder.Build();
    schema_packing_ = std::make_shared<dockv::SchemaPacking>(
        TableType::PGSQL_TABLE_TYPE, schema_);

    const auto basectid_idx = ASSERT_RESULT(schema_.ColumnIndexByName("ybidxbasectid"));
    basectid_column_id_ = schema_.column_id(basectid_idx);
    const auto include_idx = ASSERT_RESULT(schema_.ColumnIndexByName("inc"));
    include_column_id_ = schema_.column_id(include_idx);
  }

  Schema CreateSchema() override { return Schema(); }

  // --- Physical shape builders ------------------------------------------------------------

  dockv::DocKey IndexDocKey(int32_t indexed_value) {
    return dockv::DocKey(
        kFixedHashCode, {dockv::KeyEntryValue::Int32(indexed_value)},
        {dockv::KeyEntryValue(dockv::KeyEntryType::kNullLow)});
  }

  std::string RowLevelKey(int32_t v, HybridTime ht, IntraTxnWriteId write_id) {
    dockv::KeyBytes key = IndexDocKey(v).Encode();
    key.AppendKeyEntryType(dockv::KeyEntryType::kHybridTime);
    key.AppendHybridTime(DocHybridTime(ht, write_id));
    return key.ToStringBuffer();
  }

  std::string ColumnKey(int32_t v, ColumnId column, HybridTime ht, IntraTxnWriteId write_id) {
    dockv::KeyBytes key = IndexDocKey(v).Encode();
    dockv::KeyEntryValue::MakeColumnId(column).AppendToKey(&key);
    key.AppendKeyEntryType(dockv::KeyEntryType::kHybridTime);
    key.AppendHybridTime(DocHybridTime(ht, write_id));
    return key.ToStringBuffer();
  }

  std::string LivenessKey(int32_t v, HybridTime ht, IntraTxnWriteId write_id) {
    dockv::KeyBytes key = IndexDocKey(v).Encode();
    dockv::KeyEntryValue::kLivenessColumn.AppendToKey(&key);
    key.AppendKeyEntryType(dockv::KeyEntryType::kHybridTime);
    key.AppendHybridTime(DocHybridTime(ht, write_id));
    return key.ToStringBuffer();
  }

  static QLValuePB BinaryValue(const std::string& data) {
    QLValuePB value;
    value.set_binary_value(data);
    return value;
  }

  static std::string EncodedBinary(const std::string& data) {
    std::string out;
    dockv::AppendEncodedValue(BinaryValue(data), &out);
    return out;
  }

  void Put(const std::string& key, const std::string& value) {
    rocksdb::WriteBatch batch;
    batch.Put(key, value);
    ASSERT_OK(regular_db_->Write(write_options(), &batch));
  }

  // A packed-row insert (V1 or V2) or update (V2 only) carrying the identity and a null
  // INCLUDE column.
  std::string PackedRow(
      dockv::PackedRowVersion version, const std::string& identity, bool is_update) {
    constexpr auto kNoLimit = std::numeric_limits<int64_t>::max();
    constexpr SchemaVersion kSchemaVersion = 0;
    QLValuePB include_value;  // null
    if (version == dockv::PackedRowVersion::kV1) {
      CHECK(!is_update);
      dockv::RowPackerV1 packer(kSchemaVersion, *schema_packing_, kNoLimit, Slice());
      CHECK_OK(packer.AddValue(basectid_column_id_, BinaryValue(identity)));
      CHECK_OK(packer.AddValue(include_column_id_, include_value));
      return CHECK_RESULT(packer.Complete()).ToBuffer();
    }
    dockv::RowPackerV2 packer(
        kSchemaVersion, *schema_packing_, kNoLimit, Slice(), is_update);
    CHECK_OK(packer.AddValue(basectid_column_id_, BinaryValue(identity)));
    CHECK_OK(packer.AddValue(include_column_id_, include_value));
    return CHECK_RESULT(packer.Complete()).ToBuffer();
  }

  // --- Logical event writers --------------------------------------------------------------

  // Backfill-shaped insert: packed V2 row at the backfill hybrid time with a marked-domain
  // write ID.
  void WriteBackfillInsert(
      int32_t v, const std::string& identity, IntraTxnWriteId write_id = kBackfillWriteId) {
    Put(RowLevelKey(v, kBackfillHT, write_id),
        PackedRow(dockv::PackedRowVersion::kV2, identity, /* is_update= */ false));
  }

  // Foreground-shaped non-packed insert: liveness + identity column + INCLUDE column at one
  // hybrid time with consecutive write IDs.
  void WriteNonPackedInsert(
      int32_t v, const std::string& identity, HybridTime ht, IntraTxnWriteId first_write_id) {
    Put(LivenessKey(v, ht, first_write_id), std::string(1, dockv::ValueEntryTypeAsChar::kNullLow));
    Put(ColumnKey(v, basectid_column_id_, ht, first_write_id + 1), EncodedBinary(identity));
    std::string include_encoded;
    QLValuePB include_value;
    include_value.set_int32_value(7);
    dockv::AppendEncodedValue(include_value, &include_encoded);
    Put(ColumnKey(v, include_column_id_, ht, first_write_id + 2), include_encoded);
  }

  // In-place identity update (yb_lsm.c doAssignForIdxUpdate shape): a lone ybidxbasectid
  // column write.
  void WriteInPlaceIdentityUpdate(
      int32_t v, const std::string& identity, HybridTime ht, IntraTxnWriteId write_id = 0) {
    Put(ColumnKey(v, basectid_column_id_, ht, write_id), EncodedBinary(identity));
  }

  // Backfill-shaped NON-packed insert (packing disabled): every record of one marked
  // operation shares the operation's Raft-index write ID.
  void WriteNonPackedBackfillInsert(
      int32_t v, const std::string& identity, IntraTxnWriteId write_id) {
    Put(LivenessKey(v, kBackfillHT, write_id),
        std::string(1, dockv::ValueEntryTypeAsChar::kNullLow));
    Put(ColumnKey(v, basectid_column_id_, kBackfillHT, write_id), EncodedBinary(identity));
  }

  void WriteRowTombstone(int32_t v, HybridTime ht, IntraTxnWriteId write_id = 0) {
    Put(RowLevelKey(v, ht, write_id), std::string(1, dockv::ValueEntryTypeAsChar::kTombstone));
  }

  // --- Verification -----------------------------------------------------------------------

  Result<UniqueIndexVerificationResult> Verify(
      size_t max_dockey_groups = 0, const std::string& start_dockey = std::string(),
      size_t max_buffered = 1024) {
    // Flush so verification reads SSTs as production does (also exercises key bounds).
    RETURN_NOT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
    TestSchemaPackingProvider provider(schema_packing_);
    UniqueIndexVerifierOptions options;
    options.window_lower = kWindowLower;
    options.window_upper = kWindowUpper;
    options.ybidxbasectid_column_id = basectid_column_id_;
    options.start_dockey = start_dockey;
    options.max_dockey_groups = max_dockey_groups;
    options.max_buffered_versions_per_group = max_buffered;
    return VerifyUniqueIndexTablet(
        regular_db_.get(), KeyBounds(), &provider, options);
  }

  void ExpectOutcome(
      UniqueIndexVerificationOutcome expected,
      const UniqueIndexVerificationResult& result) {
    ASSERT_EQ(expected, result.outcome) << result.ToString();
  }

  class TestSchemaPackingProvider : public SchemaPackingProvider {
   public:
    explicit TestSchemaPackingProvider(std::shared_ptr<const dockv::SchemaPacking> packing)
        : packing_(std::move(packing)) {}

    Result<CompactionSchemaInfo> CotablePacking(
        const Uuid& cotable_id, uint32_t schema_version, HybridTime history_cutoff) override {
      CompactionSchemaInfo info;
      info.table_type = TableType::PGSQL_TABLE_TYPE;
      info.schema_version = schema_version;
      info.schema_packing = packing_;
      return info;
    }

    Result<CompactionSchemaInfo> ColocationPacking(
        ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) override {
      return CotablePacking(Uuid::Nil(), schema_version, history_cutoff);
    }

   private:
    std::shared_ptr<const dockv::SchemaPacking> packing_;
  };

  static constexpr uint16_t kFixedHashCode = 0;

  Schema schema_;
  std::shared_ptr<dockv::SchemaPacking> schema_packing_;
  ColumnId basectid_column_id_{0};
  ColumnId include_column_id_{0};
};

TEST_F(UniqueIndexVerifierTest, EmptyTabletIsClean) {
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
  ASSERT_EQ(0, result.dockey_groups_scanned);
  ASSERT_TRUE(result.resume_from_dockey.empty());
}

TEST_F(UniqueIndexVerifierTest, SingleBackfillInsertIsClean) {
  WriteBackfillInsert(1, "ctid-A");
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
  ASSERT_EQ(1, result.dockey_groups_scanned);
}

TEST_F(UniqueIndexVerifierTest, TwoDistinctIdentitiesAtBackfillHTIsViolation) {
  // The funnel-duplicate shape SKIP_ALL exists to catch: two base rows with the same indexed
  // value, both re-materialized at backfill_read_ht under distinct marked write IDs.
  WriteBackfillInsert(1, "ctid-A", kBackfillWriteId);
  WriteBackfillInsert(1, "ctid-B", kBackfillWriteId + 1);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, TwoNonPackedBackfillInsertsDistinctIdentitiesIsViolation) {
  // The funnel-duplicate shape on a packing-disabled cluster: each marked operation's column
  // records share one write ID, so the two candidates assemble into two insert events and
  // the duplicate is a Violation, not an ambiguity.
  WriteNonPackedBackfillInsert(1, "ctid-A", kBackfillWriteId);
  WriteNonPackedBackfillInsert(1, "ctid-B", kBackfillWriteId + 1);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, TwoNonPackedBackfillInsertsSameIdentityIsClean) {
  WriteNonPackedBackfillInsert(1, "ctid-A", kBackfillWriteId);
  WriteNonPackedBackfillInsert(1, "ctid-A", kBackfillWriteId + 5);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
}

TEST_F(UniqueIndexVerifierTest, RetriedBackfillChunkIsIdempotent) {
  // 3a review L3: marked writes make chunk retries physically visible -- same identity at
  // the same hybrid time under distinct write IDs. Idempotent, never a violation.
  WriteBackfillInsert(1, "ctid-A", kBackfillWriteId);
  WriteBackfillInsert(1, "ctid-A", kBackfillWriteId + 7);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
}

TEST_F(UniqueIndexVerifierTest, ForegroundBeforeBackfillAtEqualHT) {
  // A foreground insert landing at exactly backfill_read_ht carries a write ID below the
  // marked-domain floor, so chronological replay orders it first; the backfill insert of the
  // same identity is then an idempotent repeat.
  WriteNonPackedInsert(1, "ctid-A", kBackfillHT, /* first_write_id= */ 3);
  WriteBackfillInsert(1, "ctid-A", kBackfillWriteId);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);

  // Distinct identities at the equal hybrid time are a violation regardless of order.
  WriteBackfillInsert(2, "ctid-C", kBackfillWriteId);
  WriteNonPackedInsert(2, "ctid-D", kBackfillHT, /* first_write_id= */ 3);
  result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, InsertDeleteInsertIsClean) {
  WriteBackfillInsert(1, "ctid-A");
  WriteRowTombstone(1, 5000_usec_ht);
  WriteNonPackedInsert(1, "ctid-B", 6000_usec_ht, /* first_write_id= */ 0);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
}

TEST_F(UniqueIndexVerifierTest, TransientOverlapIsViolation) {
  // The overlap existed between 4000 and 5000 even though it was later resolved; verification
  // reports what the index allowed, not just its final state.
  WriteBackfillInsert(1, "ctid-A");
  WriteNonPackedInsert(1, "ctid-B", 4000_usec_ht, /* first_write_id= */ 0);
  WriteRowTombstone(1, 5000_usec_ht);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, InPlaceIdentityUpdateIsClean) {
  // Legal PK update: the base row's ctid changes in place (standalone ybidxbasectid column
  // shape); the identity is replaced, never a duplicate.
  WriteBackfillInsert(1, "ctid-A");
  WriteInPlaceIdentityUpdate(1, "ctid-B", 5000_usec_ht);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
}

TEST_F(UniqueIndexVerifierTest, PackedUpdateReplacesIdentity) {
  // The same legal PK update in the packed-V2-with-kIsUpdateFlag shape
  // (pack_full_row_update + mark).
  WriteBackfillInsert(1, "ctid-A");
  Put(RowLevelKey(1, 5000_usec_ht, 0),
      PackedRow(dockv::PackedRowVersion::kV2, "ctid-B", /* is_update= */ true));
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);

  // An unflagged packed row of a distinct identity is an insert and must still violate.
  Put(RowLevelKey(1, 6000_usec_ht, 0),
      PackedRow(dockv::PackedRowVersion::kV2, "ctid-C", /* is_update= */ false));
  result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, PackedV1InsertShapes) {
  WriteBackfillInsert(1, "ctid-A");
  Put(RowLevelKey(1, 5000_usec_ht, 0),
      PackedRow(dockv::PackedRowVersion::kV1, "ctid-A", /* is_update= */ false));
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);

  Put(RowLevelKey(1, 6000_usec_ht, 0),
      PackedRow(dockv::PackedRowVersion::kV1, "ctid-B", /* is_update= */ false));
  result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, UpdateEstablishesOnEmptyLiveSet) {
  // Defensive semantics (scan-algorithm-0812 Finding 4): an update as the first in-window
  // event establishes the identity. It cannot mask a duplicate: had two identities been live
  // at the window start, SKIP_ALL would have re-materialized both as inserts.
  WriteInPlaceIdentityUpdate(1, "ctid-A", 5000_usec_ht);
  WriteBackfillInsert(1, "ctid-A");  // Same identity later re-asserted: idempotent.
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
}

TEST_F(UniqueIndexVerifierTest, IncludeColumnUpdateKeepsLiveIdentity) {
  WriteBackfillInsert(1, "ctid-A");
  // INCLUDE-column-only update: no identity change.
  std::string include_encoded;
  QLValuePB include_value;
  include_value.set_int32_value(9);
  dockv::AppendEncodedValue(include_value, &include_encoded);
  Put(ColumnKey(1, include_column_id_, 5000_usec_ht, 0), include_encoded);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);

  // The live identity survived the no-op event: a later distinct insert still violates.
  WriteNonPackedInsert(1, "ctid-B", 6000_usec_ht, /* first_write_id= */ 0);
  result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
}

TEST_F(UniqueIndexVerifierTest, RecordsOutsideWindowAreIgnored) {
  // Below the window: pre-backfill foreground maintenance, superseded by SKIP_ALL
  // re-materialization. Above the window: outside this verification's responsibility.
  WriteNonPackedInsert(1, "ctid-old", 1000_usec_ht, /* first_write_id= */ 0);
  WriteBackfillInsert(1, "ctid-A");
  WriteNonPackedInsert(1, "ctid-new", 9500_usec_ht, /* first_write_id= */ 0);
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, result);
}

TEST_F(UniqueIndexVerifierTest, UnknownEncodingIsInconclusive) {
  WriteBackfillInsert(1, "ctid-A");
  // A row-level value type the verifier does not understand (an object container marker).
  Put(RowLevelKey(1, 5000_usec_ht, 0),
      std::string(1, dockv::ValueEntryTypeAsChar::kObject));
  auto result = ASSERT_RESULT(Verify());
  ExpectOutcome(UniqueIndexVerificationOutcome::kInconclusive, result);
  ASSERT_FALSE(result.reason.empty());
}

TEST_F(UniqueIndexVerifierTest, PaginationIsDocKeyAligned) {
  WriteBackfillInsert(1, "ctid-A");
  WriteBackfillInsert(2, "ctid-B");
  WriteBackfillInsert(2, "ctid-C", kBackfillWriteId + 1);  // Violation in the second group.

  auto first_page = ASSERT_RESULT(Verify(/* max_dockey_groups= */ 1));
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, first_page);
  ASSERT_EQ(1, first_page.dockey_groups_scanned);
  ASSERT_FALSE(first_page.resume_from_dockey.empty());

  auto second_page = ASSERT_RESULT(
      Verify(/* max_dockey_groups= */ 0, first_page.resume_from_dockey));
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, second_page);
}

TEST_F(UniqueIndexVerifierTest, OversizedGroupFallsBackToReverseWalk) {
  // A group larger than the buffer bound is replayed by the reverse walk. The event order
  // matters: insert A, delete, insert B is clean only when replayed chronologically.
  WriteBackfillInsert(1, "ctid-A");
  WriteRowTombstone(1, 4000_usec_ht);
  WriteNonPackedInsert(1, "ctid-B", 5000_usec_ht, /* first_write_id= */ 0);
  for (int i = 0; i != 8; ++i) {
    WriteInPlaceIdentityUpdate(1, "ctid-B", HybridTime::FromMicros(6000 + i), /* write_id= */ 0);
  }
  // A second, buffered group after the oversized one must still be verified.
  WriteBackfillInsert(2, "ctid-C", kBackfillWriteId);
  WriteBackfillInsert(2, "ctid-D", kBackfillWriteId + 1);

  auto result = ASSERT_RESULT(Verify(
      /* max_dockey_groups= */ 0, std::string(), /* max_buffered= */ 4));
  ExpectOutcome(UniqueIndexVerificationOutcome::kViolation, result);
  ASSERT_EQ(1, result.dockey_groups_scanned);  // Group 1 clean; violation stops in group 2.

  // The same oversized group is clean on its own (fallback replay order is chronological).
  auto clean_result = ASSERT_RESULT(Verify(
      /* max_dockey_groups= */ 1, std::string(), /* max_buffered= */ 4));
  ExpectOutcome(UniqueIndexVerificationOutcome::kClean, clean_result);
}

}  // namespace yb::docdb
