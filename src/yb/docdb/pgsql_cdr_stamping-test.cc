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

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb-test.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/pgsql_operation.h"

#include "yb/dockv/packed_row.h"

#include "yb/gutil/strings/split.h"

#include "yb/util/flags.h"

DECLARE_bool(ysql_enable_cdr_metadata);
DECLARE_bool(ysql_enable_pack_full_row_update);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_uint64(ysql_packed_row_size_limit);

namespace yb::docdb {

// The PostgreSQL write paths are where the stamp is actually applied - RowPackContext::Complete,
// the liveness writes in ApplyInsert, the new one in ApplyUpdate, and the tombstone in
// ApplyDelete. They are reachable without a running pggate: build the request, apply the operation
// onto a DocWriteBatch, and read the rows back out of RocksDB.
class PgsqlCdrStampingTest : public DocDBTestBase {
 public:
  Schema CreateSchema() override {
    SchemaBuilder builder;
    CHECK_OK(builder.AddHashKeyColumn("k", DataType::INT32));
    CHECK_OK(builder.AddColumn(ColumnSchema(
        "v1", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue, false, false, /* order= */ 2)));
    CHECK_OK(builder.AddColumn(ColumnSchema(
        "v2", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue, false, false, /* order= */ 3)));
    return builder.Build();
  }

 protected:
  // Column ids are assigned from kFirstColumnIdRep, which ASAN and TSAN builds start at 10, with
  // the hash key column first.
  static constexpr ColumnIdRep kV1Id = kFirstColumnIdRep + 1;
  static constexpr ColumnIdRep kV2Id = kFirstColumnIdRep + 2;
  static constexpr uint32_t kOrigin = 42;
  // The only version DocReadContext::TEST_Create registers a packing for.
  static constexpr SchemaVersion kSchemaVersion = 0;

  HybridTime SourceWriteHt() const { return HybridTime::FromMicros(2500); }

  // Applies one PostgreSQL write. `origin` unset means a local write - the batch carries no stamp,
  // exactly as it would when the apply is not a replication apply.
  Status Write(
      PgsqlWriteRequestPB::PgsqlStmtType stmt_type, int32_t key,
      const std::vector<std::pair<ColumnIdRep, int32_t>>& columns,
      std::optional<uint32_t> origin, HybridTime write_ht) {
    auto arena = SharedThreadSafeArena();
    auto& request = *arena->NewArenaObject<LWPgsqlWriteRequestPB>();
    auto& response = *arena->NewArenaObject<LWPgsqlResponsePB>();

    request.set_stmt_type(stmt_type);
    request.set_client(YQL_CLIENT_PGSQL);
    request.add_partition_column_values()->mutable_value()->set_int32_value(key);

    const auto is_update = stmt_type == PgsqlWriteRequestPB::PGSQL_UPDATE;
    for (const auto& [id, value] : columns) {
      auto* column_value =
          is_update ? request.add_column_new_values() : request.add_column_values();
      column_value->set_column_id(id);
      column_value->mutable_expr()->mutable_value()->set_int32_value(value);
    }

    PgsqlWriteOperation op(
        request, std::make_shared<DocReadContext>(DocReadContext::TEST_Create(doc_read_context()
            .schema())),
        kNonTransactionalOperationContext, nullptr /* sidecars */);
    RETURN_NOT_OK(op.Init(&response));

    auto doc_write_batch = MakeDocWriteBatch();
    if (origin) {
      doc_write_batch.SetCdrMetadata(
          {.source_origin = *origin, .source_write_ht = SourceWriteHt()});
    }
    ReadRestartData read_restart_data;
    RETURN_NOT_OK(op.Apply({
      .doc_write_batch = &doc_write_batch,
      .read_operation_data = ReadOperationData(),
      .read_restart_data = &read_restart_data,
      .schema_packing_provider = this,
    }));
    return WriteToRocksDB(doc_write_batch, write_ht);
  }

  // Packs a row the way pggate does for a batched INSERT, with no control fields of its own.
  Result<std::string> PackRow(const std::vector<std::pair<ColumnIdRep, int32_t>>& columns) {
    dockv::RowPackerV1 packer(
        kSchemaVersion,
        VERIFY_RESULT_REF(doc_read_context().schema_packing_storage.GetPacking(kSchemaVersion)),
        std::numeric_limits<ssize_t>::max(), Slice());
    for (const auto& [id, value] : columns) {
      QLValuePB pb;
      pb.set_int32_value(value);
      SCHECK(VERIFY_RESULT(packer.AddValue(ColumnId(id), pb)), InternalError, "Failed to pack");
    }
    // Copied out: the Slice points into the packer's buffer, which dies with it.
    return VERIFY_RESULT(packer.Complete()).ToBuffer();
  }

  // The pggate pass-through: the packed row arrives whole in packed_rows, keyed by its own encoded
  // doc key. Init uses the table root key as the doc key when packed_rows is set, so the request
  // carries the whole key here, which for this schema is the entire prefix.
  Status WritePackedRow(
      int32_t key, const std::string& packed_value, std::optional<uint32_t> origin,
      HybridTime write_ht) {
    auto arena = SharedThreadSafeArena();
    auto& request = *arena->NewArenaObject<LWPgsqlWriteRequestPB>();
    auto& response = *arena->NewArenaObject<LWPgsqlResponsePB>();

    request.set_stmt_type(PgsqlWriteRequestPB::PGSQL_UPSERT);
    request.set_client(YQL_CLIENT_PGSQL);

    const auto& schema = doc_read_context().schema();
    dockv::DocKey doc_key(schema, 0 /* hash */, {dockv::KeyEntryValue::Int32(key)});
    request.add_dup_packed_rows(doc_key.Encode().AsSlice());
    request.add_dup_packed_rows(Slice(packed_value));

    PgsqlWriteOperation op(
        request, std::make_shared<DocReadContext>(DocReadContext::TEST_Create(schema)),
        kNonTransactionalOperationContext, nullptr /* sidecars */);
    RETURN_NOT_OK(op.Init(&response));

    auto doc_write_batch = MakeDocWriteBatch();
    if (origin) {
      doc_write_batch.SetCdrMetadata(
          {.source_origin = *origin, .source_write_ht = SourceWriteHt()});
    }
    ReadRestartData read_restart_data;
    RETURN_NOT_OK(op.Apply({
      .doc_write_batch = &doc_write_batch,
      .read_operation_data = ReadOperationData(),
      .read_restart_data = &read_restart_data,
      .schema_packing_provider = this,
    }));
    return WriteToRocksDB(doc_write_batch, write_ht);
  }
};

// A packed INSERT applied from a remote origin: the stamp goes on the packed row itself, which is
// the row-level entry in this shape.
TEST_F(PgsqlCdrStampingTest, PackedInsertStampsThePackedRow) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, kOrigin,
      3000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  ASSERT_STR_CONTAINS(dump, "source origin: 42");
  ASSERT_STR_CONTAINS(dump, "source write ht: { physical: 2500 }");
  // A packed insert has no separate liveness entry.
  ASSERT_STR_NOT_CONTAINS(dump, "SystemColumnId(0)");
}

// pggate packs a row itself for a batched INSERT and sends it whole. That path writes the value
// through unchanged, so the stamp is spliced onto the front there - and the spliced bytes count
// against the packed size limit like any other. Deciding on the size pggate sent stores rows over
// the limit, which is the one thing the limit exists to prevent.
TEST_F(PgsqlCdrStampingTest, PassedThroughPackedRowCountsTheStampAgainstTheSizeLimit) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  auto packed_value = ASSERT_RESULT(PackRow({{kV1Id, 10}, {kV2Id, 20}}));
  // Wide enough for the row pggate sent, too narrow once the stamp is on the front.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = packed_value.size() + 1;

  ASSERT_OK(WritePackedRow(1, packed_value, kOrigin, 3000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  SCOPED_TRACE(dump);
  ASSERT_STR_CONTAINS(dump, "source origin: 42");
  // Over the limit, so it must have gone through the unpack-repack path instead, where the packer
  // spills whatever no longer fits into its own entry. The stamp costs eleven bytes, which is one
  // int32 column's worth here.
  ASSERT_STR_CONTAINS(dump, Format("ColumnId($0); HT", kV2Id));
}

// The same row without a stamp still fits, so it is passed straight through as one packed entry.
// Without this the test above would pass for the wrong reason - a limit too small for the row at
// all, rather than too small once stamped.
TEST_F(PgsqlCdrStampingTest, PassedThroughPackedRowFitsWhenUnstamped) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  auto packed_value = ASSERT_RESULT(PackRow({{kV1Id, 10}, {kV2Id, 20}}));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = packed_value.size() + 1;

  ASSERT_OK(WritePackedRow(1, packed_value, std::nullopt, 3000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  SCOPED_TRACE(dump);
  ASSERT_STR_NOT_CONTAINS(dump, "source origin");
  ASSERT_STR_NOT_CONTAINS(dump, Format("ColumnId($0); HT", kV2Id));
}

// The same insert with packing off: the stamp goes on the liveness column instead, and the column
// entries stay unstamped because the pair describes the row, not any one column.
TEST_F(PgsqlCdrStampingTest, NonPackedInsertStampsTheLivenessColumn) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, kOrigin,
      3000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  ASSERT_STR_CONTAINS(dump, "SystemColumnId(0)");
  // Exactly one entry carries the stamp: the liveness column.
  size_t stamps = 0;
  for (size_t pos = dump.find("source origin: 42"); pos != std::string::npos;
       pos = dump.find("source origin: 42", pos + 1)) {
    ++stamps;
  }
  ASSERT_EQ(stamps, 1) << "the stamp must be row-level, not per-column; dump:\n" << dump;
}

// A partial UPDATE writes only the changed column, so the liveness column is written to be the
// row-level entry that carries the stamp.
TEST_F(PgsqlCdrStampingTest, PartialUpdateStampsALivenessEntry) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, std::nullopt,
      3000_usec_ht));
  // Only one of the two non-key columns, so this takes the partial path rather than repacking.
  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_UPDATE, 1, {{kV1Id, 11}}, kOrigin, 4000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  ASSERT_STR_CONTAINS(dump, "SystemColumnId(0); HT{ physical: 4000 }");
  ASSERT_STR_CONTAINS(dump, "source origin: 42");
}

// The same partial UPDATE applied locally still writes the row-level entry, unstamped. That is
// what lets the reader tell a local write from a stale remote one underneath it.
TEST_F(PgsqlCdrStampingTest, LocalPartialUpdateWritesAnUnstampedLivenessEntry) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, std::nullopt,
      3000_usec_ht));
  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_UPDATE, 1, {{kV1Id, 11}}, std::nullopt, 4000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  ASSERT_STR_CONTAINS(dump, "SystemColumnId(0); HT{ physical: 4000 }");
  ASSERT_STR_NOT_CONTAINS(dump, "source origin");
}

// An UPDATE can arrive with no column values at all: ybOptimizeModifyTable strips unmodified
// primary key columns, so `SET pk = <the value it already has>` reaches DocDB empty and reports
// skipped. It must write no row-level entry either - the entry means "this statement produced
// this row version", and here nothing was produced. Writing one would move the row's last-writer
// time and, as below, silently discard a remote origin's claim on the current version.
TEST_F(PgsqlCdrStampingTest, UpdateWithNoColumnsWritesNoRowLevelEntry) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, kOrigin, 3000_usec_ht));
  ASSERT_OK(Write(PgsqlWriteRequestPB::PGSQL_UPDATE, 1, {}, std::nullopt, 4000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  ASSERT_STR_NOT_CONTAINS(dump, "SystemColumnId(0); HT{ physical: 4000 }");
  // The remote insert is still the newest row-level entry, so the row still reads as remote.
  ASSERT_STR_CONTAINS(dump, "source origin: 42");
}

// With the feature off a local partial UPDATE writes exactly what it always wrote - no row-level
// entry at all. This is the cost boundary: clusters without CDR pay nothing.
TEST_F(PgsqlCdrStampingTest, LocalPartialUpdateWritesNothingExtraWithFeatureOff) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, std::nullopt,
      3000_usec_ht));
  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_UPDATE, 1, {{kV1Id, 11}}, std::nullopt, 4000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  // No row-level entry should be written when the feature is off.
  ASSERT_STR_NOT_CONTAINS(dump, "SystemColumnId(0); HT{ physical: 4000 }");
}

// A remote DELETE stamps the tombstone. This is the case a column-based scheme could not express,
// and what delete_origin_differs reads.
TEST_F(PgsqlCdrStampingTest, DeleteStampsTheTombstone) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, std::nullopt,
      3000_usec_ht));
  ASSERT_OK(Write(PgsqlWriteRequestPB::PGSQL_DELETE, 1, {}, kOrigin, 4000_usec_ht));

  auto dump = DocDBDebugDumpToStr();
  ASSERT_STR_CONTAINS(dump, "DEL; cdr source origin: 42");
}


// Every ordered pair of second-writer/first-writer operations on one row. The rule under test is
// uniform across all of them: the newest row-level entry describes the row, so it carries a stamp
// exactly when the last write that produced one came from a replication apply. Which columns each
// write touched, and whether they were the same column, must not change that.
YB_DEFINE_ENUM(CdrOp, (kLocalUpdateV1)(kLocalUpdateV2)(kLocalDelete)
                      (kRemoteUpdateV1)(kRemoteUpdateV2)(kRemoteDelete));

bool IsRemote(CdrOp op) {
  return op == CdrOp::kRemoteUpdateV1 || op == CdrOp::kRemoteUpdateV2 ||
         op == CdrOp::kRemoteDelete;
}

bool IsDelete(CdrOp op) {
  return op == CdrOp::kLocalDelete || op == CdrOp::kRemoteDelete;
}

class PgsqlCdrInterleavingTest
    : public PgsqlCdrStampingTest,
      public ::testing::WithParamInterface<std::tuple<CdrOp, CdrOp>> {
 protected:
  void SetUp() override {
    PgsqlCdrStampingTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  }

  Status Apply(CdrOp op, HybridTime write_ht) {
    const auto origin = IsRemote(op) ? std::optional<uint32_t>(kOrigin) : std::nullopt;
    switch (op) {
      case CdrOp::kLocalDelete: [[fallthrough]];
      case CdrOp::kRemoteDelete:
        return Write(PgsqlWriteRequestPB::PGSQL_DELETE, 1, {}, origin, write_ht);
      case CdrOp::kLocalUpdateV1: [[fallthrough]];
      case CdrOp::kRemoteUpdateV1:
        return Write(PgsqlWriteRequestPB::PGSQL_UPDATE, 1, {{kV1Id, 11}}, origin, write_ht);
      case CdrOp::kLocalUpdateV2: [[fallthrough]];
      case CdrOp::kRemoteUpdateV2:
        return Write(PgsqlWriteRequestPB::PGSQL_UPDATE, 1, {{kV2Id, 21}}, origin, write_ht);
    }
    return STATUS(InvalidArgument, "unknown op");
  }

  // The row-level entry of our single row with the highest write time. A liveness column, a
  // tombstone and a packed row all describe the row as a whole, so all three qualify.
  //
  // Selected by write time rather than by position: the dump is ordered by key, so the bare
  // DocKey entries sort before the SystemColumnId(0) ones whatever their hybrid times are.
  std::string NewestRowLevelEntry(const std::string& dump) {
    std::string newest;
    int64_t newest_ht = -1;
    for (const auto& line : StringSplit(dump, '\n')) {
      const auto is_liveness = line.find("[SystemColumnId(0); HT{") != std::string::npos;
      const auto is_root = line.find("], []), [HT{") != std::string::npos;
      if (!is_liveness && !is_root) {
        continue;
      }
      static const std::string kMarker = "physical: ";
      const auto pos = line.find(kMarker);
      if (pos == std::string::npos) {
        continue;
      }
      const auto ht = atoll(line.c_str() + pos + kMarker.size());
      if (ht > newest_ht) {
        newest_ht = ht;
        newest = line;
      }
    }
    return newest;
  }
};

INSTANTIATE_TEST_CASE_P(
    AllPairs, PgsqlCdrInterleavingTest,
    ::testing::Combine(::testing::ValuesIn(kCdrOpArray), ::testing::ValuesIn(kCdrOpArray)));

TEST_P(PgsqlCdrInterleavingTest, NewestRowLevelEntryDescribesTheRow) {
  const auto [first, second] = GetParam();
  SCOPED_TRACE(Format("first: $0, second: $1", first, second));

  // Seed locally, so every case starts from an unstamped packed row.
  ASSERT_OK(Write(
      PgsqlWriteRequestPB::PGSQL_INSERT, 1, {{kV1Id, 10}, {kV2Id, 20}}, std::nullopt,
      3000_usec_ht));
  ASSERT_OK(Apply(first, 4000_usec_ht));
  ASSERT_OK(Apply(second, 5000_usec_ht));

  const auto dump = DocDBDebugDumpToStr();
  const auto newest = NewestRowLevelEntry(dump);
  ASSERT_FALSE(newest.empty()) << "no row-level entry at all; dump:\n" << dump;

  // Once the row is gone, nothing the second write does reaches disk: an UPDATE finds no row to
  // update, and a second DELETE finds no row to delete. The newest row-level entry is then still
  // the tombstone from the DELETE that actually removed the row, which is the right answer -
  // delete_origin_differs should name whoever deleted it, not whoever tried to delete it again.
  const auto second_wrote = !IsDelete(first);
  const auto expected_writer = second_wrote ? second : first;
  const auto expected_ht = second_wrote ? "physical: 5000" : "physical: 4000";

  ASSERT_STR_CONTAINS(newest, expected_ht);
  ASSERT_EQ(newest.find("source origin: 42") != std::string::npos, IsRemote(expected_writer))
      << "newest row-level entry: " << newest << "\nfull dump:\n" << dump;
}

}  // namespace yb::docdb
