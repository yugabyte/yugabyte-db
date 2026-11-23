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

#include "yb/client/schema.h"
#include "yb/client/table.h"

#include "yb/dockv/value_type.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_int32(timestamp_history_retention_interval_sec);

namespace yb::pgwrapper {

// Colocation + packing only (engine fixed). Distinct from PgVectorIndexReverseMappingTestParam,
// which is also tuple<bool, PackingMode> for ownership + packing.
struct PgVectorIndexColocationPackingTestParam {
  bool colocated = false;
  PackingMode packing_mode = PackingMode::kNone;

  friend bool operator==(
      const PgVectorIndexColocationPackingTestParam& lhs,
      const PgVectorIndexColocationPackingTestParam& rhs) {
    return lhs.colocated == rhs.colocated && lhs.packing_mode == rhs.packing_mode;
  }
};

template <>
struct TestParamTraits<PgVectorIndexColocationPackingTestParam> {
  using ParamType = PgVectorIndexColocationPackingTestParam;

  static bool IsColocated(const ParamType& param) {
    return param.colocated;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return param.packing_mode;
  }

  static auto TestParamGenerator() {
    std::vector<ParamType> params;
    for (const bool colocated : {false, true}) {
      for (const auto packing_mode : kPackingModeArray) {
        params.push_back(ParamType{ .colocated = colocated, .packing_mode = packing_mode });
      }
    }
    return testing::ValuesIn(params);
  }

  static auto TestParamNameGenerator() {
    // Engine is fixed for this suite; keep gtest names as Colocated/Distributed[+Packing].
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      const auto packing_mode = GetPackingMode(param_info.param);
      return Format(
          "$0$1",
          IsColocated(param_info.param) ? "Colocated" : "Distributed",
          packing_mode == PackingMode::kNone
              ? ""
              : "Packing" + ToString(packing_mode).substr(1));
    };
  }
};

template <typename TestClass>
using PgVectorIndexColocationPackingTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgVectorIndexColocationPackingTestParam>;

using PgVectorIndexColocatedPackingTestParam = PackingMode;

template <>
struct TestParamTraits<PgVectorIndexColocatedPackingTestParam> {
  using ParamType = PgVectorIndexColocatedPackingTestParam;

  static bool IsColocated(const ParamType&) {
    return true;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return param;
  }

  static auto TestParamGenerator() {
    return testing::ValuesIn(kPackingModeArray);
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      const auto packing_mode = GetPackingMode(param_info.param);
      // Colocation/engine are fixed; packing is the only name component.
      if (packing_mode == PackingMode::kNone) {
        return "None";
      }
      return "Packing" + ToString(packing_mode).substr(1);
    };
  }
};

template <typename TestClass>
using PgVectorIndexColocatedPackingTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgVectorIndexColocatedPackingTestParam>;

// Covers table-owned V1 reverse-mapping packing GC across packing modes.
class PgVectorIndexReverseMappingCompactionGcTestBase
    : public PgVectorIndexSingleServerDumpTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_timestamp_history_retention_interval_sec) = kRetentionIntervalSec;

    // Keep a single active tablet so dump/compact helpers stay deterministic.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

    PgVectorIndexSingleServerDumpTestBase::SetUp();

    // Enable table-owned V1 reverse mappings for compaction GC tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;
  }

  static constexpr auto kRetentionIntervalSec = 2;
};

class PgVectorIndexReverseMappingCompactionGcTest
    : public PgVectorIndexColocationPackingTestParamsDecorator<
          PgVectorIndexReverseMappingCompactionGcTestBase> {};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexReverseMappingCompactionGcTest);

TEST_P(PgVectorIndexReverseMappingCompactionGcTest, DropColumn) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN embedding"));

  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(CompactTabletForTable());
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));
}

// Table-owned reverse mappings survive DROP INDEX.
TEST_P(PgVectorIndexReverseMappingCompactionGcTest, DropIndexKeepsMapping) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  const auto before = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      before);

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(CompactTabletForTable());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(before, ASSERT_RESULT(DumpSingleTabletReverseMapping()));
}

// Covers colocation is being dropped.
class PgVectorIndexReverseMappingCompactionGcColocatedTest
    : public PgVectorIndexColocatedPackingTestParamsDecorator<
          PgVectorIndexReverseMappingCompactionGcTestBase> {};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexReverseMappingCompactionGcColocatedTest);

TEST_P(PgVectorIndexReverseMappingCompactionGcColocatedTest, DropTable) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(conn.Execute("CREATE TABLE dummy (id int PRIMARY KEY) WITH (COLOCATED = 1)"));
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  ASSERT_OK(conn.Execute("DROP TABLE test"));
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  // Keep the colocated tablet alive via dummy; compact and dump through that table.
  ASSERT_OK(CompactTabletForTable("dummy"));
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping("dummy")));
}

using PgVectorIndexReverseMappingTestParam = std::tuple<bool, PackingMode>;

template <>
struct TestParamTraits<PgVectorIndexReverseMappingTestParam> {
  using ParamType = PgVectorIndexReverseMappingTestParam;

  static bool TableOwnsReverseMapping(const ParamType& param) {
    return std::get<0>(param);
  }

  static bool IsColocated(const ParamType&) {
    return false;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return std::get<1>(param);
  }

  static auto TestParamGenerator() {
    return testing::Combine(testing::Bool(), testing::ValuesIn(kPackingModeArray));
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      const auto table_owned = TableOwnsReverseMapping(param_info.param);
      const auto packing_mode = GetPackingMode(param_info.param);
      return Format(
          "$0$1",
          table_owned ? "TableOwned" : "LegacyOwned",
          packing_mode == PackingMode::kNone ? "" : "Packing" + ToString(packing_mode).substr(1));
    };
  }
};

class PgVectorIndexReverseMappingTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerDumpTestBase, PgVectorIndexReverseMappingTestParam> {
 protected:
  void SetUp() override {
    PgVectorIndexSingleServerDumpTestBase::SetUp();

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) =
        TableOwnsVectorReverseMapping();
  }

  bool TableOwnsVectorReverseMapping() const {
    return ParamTraits::TableOwnsReverseMapping(GetParam());
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexReverseMappingTest);

TEST_P(PgVectorIndexReverseMappingTest, InsertReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED("", output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, InsertReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      output);
}

TEST_P(PgVectorIndexReverseMappingTest, InsertReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(InsertRows(conn, 1, 3));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      output);
}

TEST_P(PgVectorIndexReverseMappingTest, BackfillReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED("", output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, BackfillReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> ybctid_2
            ybctid_1_vector_1 -> ybctid_1
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, BackfillReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    // Each index backfill writes reverse mapping entries for all rows.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> ybctid_2
            ybctid_1_vector_1 -> ybctid_1
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> ybctid_2
            ybctid_1_vector_1 -> ybctid_1
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, UpdateReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    // Fast path writes the new reverse-mapping entry before tombstoning the old vector id.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_2 -> ybctid_2
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  } else {
    // Legacy update does tombstone the reverse mapping despite the index existence.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            vector_0 -> DEL
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, UpdateReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_2 -> ybctid_2
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  } else {
    // Legacy tables with an index still tombstone obsolete vector ids on update (#24064).
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_2 -> ybctid_2
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, UpdateReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_2 -> ybctid_2
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_2 -> ybctid_2
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, DeleteReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  } else {
    // Legacy delete tombstones the reverse mapping despite the index existence.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            vector_0 -> DEL
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, DeleteReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_2_vector_1 -> DEL
      )#",
      output);
}

TEST_P(PgVectorIndexReverseMappingTest, DeleteReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_2_vector_1 -> DEL
      )#",
      output);
}

class PgVectorValueFormatTest :
    public PgMiniTestBase, public ::testing::WithParamInterface<PackingMode> {
 protected:
  static constexpr char kTypedTable[] = "typed_table";
  static constexpr char kLegacyTable[] = "legacy_table";
  static constexpr char kVectorColumn[] = "embedding";
  static constexpr char kTypedPrefix = dockv::ValueEntryTypeAsChar::kVector;
  static constexpr char kLegacyPrefix = dockv::ValueEntryTypeAsChar::kString;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

    // CollectTypePrefixes dumps leader peers only, so a load balancer leader stepdown racing the
    // dump would leave it with nothing to read.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

    const auto packing_mode = GetParam();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = packing_mode != PackingMode::kNone;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = packing_mode == PackingMode::kV2;

    PgMiniTestBase::SetUp();
  }

  // Dumps table SST files and collects type prefixes for the vector column.
  // Table must have only one vector column.
  Result<std::vector<char>> CollectTypePrefixes(
      const std::string& table_name, const std::string& vector_column_name,
      PackingMode packing_mode) {
    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    const auto yb_table = VERIFY_RESULT(client_->OpenTable(table_id));
    ColumnId vector_column_id = kInvalidColumnId;
    const auto& columns = yb_table->schema().columns();
    for (size_t i = 0; i < columns.size(); ++i) {
      if (columns[i].is_vector()) {
        vector_column_id = ColumnId(yb_table->schema().ColumnId(i));
        break;
      }
    }
    SCHECK(vector_column_id != kInvalidColumnId, NotFound,
           "Vector column $0 not found in table $1", vector_column_name, table_name);

    const auto dump = VERIFY_RESULT(DumpTableLeadersDocDBToVector(cluster_.get(), table_name));
    const auto column_subkey = Format("[ColumnId($0)]", vector_column_id);

    std::vector<char> prefixes;
    for (const auto& line : dump) {
      // V1 reverse-mapping values also contain [ColumnId(N)] in their dump text, so the
      // column_subkey search below would count them as vector-column cells. Skip those lines.
      if (line.find("MetaKey(VectorId(") != std::string::npos) {
        continue;
      }
      std::optional<std::string> value;
      if (line.find(column_subkey) != std::string::npos) {
        value = line.substr(line.find(" -> ") + 4);
      } else {
        value = ExtractPackedColumnValueDump(line, vector_column_id);
      }
      if (!value) {
        continue;
      }
      prefixes.push_back(ParseTypePrefixFromValueDump(*value, packing_mode));
    }
    return prefixes;
  }

  Status ValidateVectorColumnPrefixes(const std::string& table_name, size_t expected_count) {
    return ValidateVectorColumnPrefixes(table_name, expected_count, GetParam());
  }

  // packing_mode - packed row version the vector values are expected to be stored in.
  Status ValidateVectorColumnPrefixes(
      const std::string& table_name, size_t expected_count, PackingMode packing_mode) {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster_.get()));
    RETURN_NOT_OK(cluster_->FlushTablets());

    const auto prefixes = VERIFY_RESULT(CollectTypePrefixes(
        table_name, kVectorColumn, packing_mode));

    const auto expected_prefix = packing_mode == PackingMode::kV2
        ? kNoTypePrefix : table_name == kLegacyTable ? kLegacyPrefix : kTypedPrefix;
    return CheckTypePrefixes(prefixes, expected_prefix, expected_count);
  }

  Result<bool> TableOwnsVectorReverseMapping(const std::string& table_name) {
    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    const auto yb_table = VERIFY_RESULT(client_->OpenTable(table_id));
    return yb_table->schema().table_properties().owns_vector_reverse_mapping();
  }

  Status RestartClusterWithTableOwnedFlag() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;
    RETURN_NOT_OK(cluster_->RestartSync());
    return cluster_->WaitForAllTabletServers();
  }
};

TEST_P(PgVectorValueFormatTest, TableOwnedEncodingSurvivesClusterRestart) {
  constexpr char kCreateQuery[] =
      "CREATE TABLE $0 (id INT PRIMARY KEY, $1 vector(3)) SPLIT INTO 1 TABLETS";

  // Force legacy ownership for kLegacyTable before it is created.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(kCreateQuery, kLegacyTable, kVectorColumn));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, '[1, 2, 3]')", kLegacyTable));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_FALSE(ASSERT_RESULT(TableOwnsVectorReverseMapping(kLegacyTable)));

  ASSERT_OK(ValidateVectorColumnPrefixes(kLegacyTable, 1));

  ASSERT_OK(RestartClusterWithTableOwnedFlag());
  conn = ASSERT_RESULT(Connect());

  ASSERT_FALSE(ASSERT_RESULT(TableOwnsVectorReverseMapping(kLegacyTable)));

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, '[4, 5, 6]')", kLegacyTable));
  ASSERT_OK(conn.ExecuteFormat(kCreateQuery, kTypedTable, kVectorColumn));
  ASSERT_TRUE(ASSERT_RESULT(TableOwnsVectorReverseMapping(kTypedTable)));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, '[7, 8, 9]')", kTypedTable));

  ASSERT_OK(ValidateVectorColumnPrefixes(kLegacyTable, 2));
  ASSERT_OK(ValidateVectorColumnPrefixes(kTypedTable, 1));

  const auto get_count = [&conn](const std::string& table_name) {
    return conn.FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", table_name));
  };
  ASSERT_EQ(2, ASSERT_RESULT(get_count(kLegacyTable)));
  ASSERT_EQ(1, ASSERT_RESULT(get_count(kTypedTable)));
}

// Compaction merges column updates older than the history cutoff into the packed row and packs
// the result with the version ysql_use_packed_row_v2 selects at that moment. Columns the update
// did not touch are carried over from the old packed row, so flipping the flag between the insert
// and the compaction converts the stored vector value from one version to the other (DB-23864).
TEST_P(PgVectorValueFormatTest, RepackOnVersionSwitch) {
  constexpr int kNumRows = 10;
  constexpr int kRetentionIntervalSec = 2;

  if (GetParam() == PackingMode::kNone) {
    GTEST_SKIP() << "Requires packed rows";
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      kRetentionIntervalSec;

  const std::vector<std::string> tables = {kLegacyTable, kTypedTable};

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  for (const auto& table : tables) {
    // The vector value format is chosen at table creation.
    const auto typed = table == kTypedTable;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = typed;
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, $1 vector(3), value INT) SPLIT INTO 1 TABLETS",
        table, kVectorColumn));
    ASSERT_EQ(ASSERT_RESULT(TableOwnsVectorReverseMapping(table)), typed);
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, ARRAY[i, i * 2, i * 3]::vector, 0 FROM generate_series(1, $1) i",
        table, kNumRows));
  }
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto fetch = [&conn](const std::string& table) {
    return conn.FetchRows<int32_t, std::string>(
        Format("SELECT id, $0::text FROM $1 ORDER BY id", kVectorColumn, table));
  };
  std::vector<std::decay_t<decltype(*fetch(""))>> expected;
  for (const auto& table : tables) {
    expected.push_back(ASSERT_RESULT(fetch(table)));
    ASSERT_EQ(expected.back().size(), kNumRows);
  }

  const auto new_packing_mode =
      GetParam() == PackingMode::kV2 ? PackingMode::kV1 : PackingMode::kV2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = new_packing_mode == PackingMode::kV2;
  for (const auto& table : tables) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET value = 1", table));
  }
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Separate column entries are gone only when the update was merged into the packed rows, which
  // happens once the history cutoff passes the update.
  ASSERT_OK(WaitFor([this, &tables]() -> Result<bool> {
    RETURN_NOT_OK(cluster_->CompactTablets());
    for (const auto& table : tables) {
      auto dump = VERIFY_RESULT(DumpTableLeadersDocDBToVector(cluster_.get(), table));
      for (const auto& line : dump) {
        // Reverse mapping values also mention ColumnId.
        if (line.find("ColumnId(") != std::string::npos &&
            line.find("MetaKey(VectorId(") == std::string::npos) {
          return false;
        }
      }
    }
    return true;
  }, 30s * kTimeMultiplier, "Merge updates into packed rows"));

  for (size_t i = 0; i != tables.size(); ++i) {
    SCOPED_TRACE(tables[i]);
    ASSERT_EQ(ASSERT_RESULT(fetch(tables[i])), expected[i]);
    // The V1 reader accepts either type byte, so check the stored one.
    ASSERT_OK(ValidateVectorColumnPrefixes(tables[i], kNumRows, new_packing_mode));
  }
}

INSTANTIATE_TEST_SUITE_P(
    , PgVectorValueFormatTest,
    ::testing::Values(PackingMode::kNone, PackingMode::kV1, PackingMode::kV2),
    [](const testing::TestParamInfo<PackingMode>& param_info) {
      switch (param_info.param) {
        case PackingMode::kNone: return "None";
        case PackingMode::kV1: return "PackingV1";
        case PackingMode::kV2: return "PackingV2";
      }
      FATAL_INVALID_ENUM_VALUE(PackingMode, param_info.param);
    });

}  // namespace yb::pgwrapper
