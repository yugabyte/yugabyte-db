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

#include <algorithm>
#include <cstddef>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/column_id.h"
#include "yb/common/ql_value.h"

#include "yb/tablet/tablet_dump_helper.h"

#include "yb/util/test_util.h"

namespace yb::tablet {

namespace {

QLValuePB Int32Value(int32_t value) {
  QLValuePB pb;
  pb.set_int32_value(value);
  return pb;
}

QLValuePB TextValue(const std::string& value) {
  QLValuePB pb;
  pb.set_string_value(value);
  return pb;
}

QLValuePB MapValue(const std::vector<std::pair<QLValuePB, QLValuePB>>& entries) {
  QLValuePB pb;
  auto* map_value = pb.mutable_map_value();
  for (const auto& [key, value] : entries) {
    *map_value->add_keys() = key;
    *map_value->add_values() = value;
  }
  return pb;
}

// One row as the scan paths see it: the (column id, value) pairs of its non-NULL columns. A NULL
// column is simply absent, which is how both DumpTabletData paths feed the accumulator.
using Row = std::vector<std::pair<int32_t, QLValuePB>>;

uint64_t RowHash(const Row& row) {
  RowHashAccumulator accumulator;
  for (const auto& [column_id, value] : row) {
    accumulator.AddValue(ColumnId(column_id), value);
  }
  return accumulator.RowHash();
}

// A table's xor_hash: rows fold together with xor, exactly as DumpTabletData does.
uint64_t TableHash(const std::vector<Row>& rows) {
  uint64_t hash = 0;
  for (const auto& row : rows) {
    hash ^= RowHash(row);
  }
  return hash;
}

// Fixed so a failure below reproduces.
constexpr uint64_t kSeed = 0x5346;

// Identifies a table up to the differences the hash must ignore: the order of the rows, and the
// order of the columns within a row. Two tables with the same canonical form must hash alike; two
// with different forms must not.
std::string CanonicalForm(const std::vector<Row>& rows) {
  std::vector<std::string> encoded_rows;
  for (const auto& row : rows) {
    auto sorted = row;
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.first < rhs.first;
    });
    std::string encoded;
    for (const auto& [column_id, value] : sorted) {
      encoded += std::to_string(column_id) + "=" + value.SerializeAsString() + ";";
    }
    encoded_rows.push_back(std::move(encoded));
  }
  std::sort(encoded_rows.begin(), encoded_rows.end());
  std::string canonical;
  for (const auto& encoded : encoded_rows) {
    canonical += encoded + "|";
  }
  return canonical;
}

class RandomTableGenerator {
 public:
  explicit RandomTableGenerator(uint64_t seed) : rng_(seed) {}

  std::vector<Row> NextTable() {
    const size_t num_rows = 1 + Index(kMaxRows);
    std::vector<Row> rows;
    for (size_t i = 0; i < num_rows; ++i) {
      // Every row carries a distinct key, as a table with a primary key does. Rows combine with
      // xor, so two identical rows would cancel and hash as an empty table. A generator emitting
      // duplicates would fail this test over a state the primary key rules out.
      Row row{{kKeyColumnId, Int32Value(static_cast<int32_t>(i))}};
      for (int32_t column_id = 1; column_id <= kValueColumns; ++column_id) {
        // Columns go missing sometimes, which is how a NULL reaches the accumulator.
        if (Index(4) == 0) {
          continue;
        }
        row.emplace_back(column_id, RandomValue());
      }
      rows.push_back(std::move(row));
    }
    return rows;
  }

  size_t Index(size_t bound) { return rng_() % bound; }

 private:
  QLValuePB RandomValue() {
    // Deliberately a tiny alphabet. Repeated values, across columns and across rows, are precisely
    // what an xor can cancel, so they need to be common here rather than vanishingly rare.
    if (Index(2) == 0) {
      return Int32Value(static_cast<int32_t>(Index(kValueAlphabet)));
    }
    return TextValue(
        std::string(1 + Index(3), static_cast<char>('a' + Index(kValueAlphabet))));
  }

  static constexpr int32_t kKeyColumnId = 0;
  static constexpr int32_t kValueColumns = 3;
  static constexpr size_t kMaxRows = 6;
  static constexpr size_t kValueAlphabet = 4;

  std::mt19937_64 rng_;
};

}  // namespace

class TabletDumpHelperTest : public YBTest {};

// Pins the scheme to the version number that advertises it.
//
// Every other test here compares two hashes from one build, so all of them stay green under any
// scheme with the right structural properties. Only a pinned value catches a change to the hashing
// that did not bump kTabletDataHashSchemeVersion, which is what makes identical data report as
// diverged when source and target upgrade independently. A failure here means either the scheme
// changed unintentionally, or it changed on purpose and wants a version bump and new values
// recorded below.
//
// These values also pin two assumptions nothing else states: that a QLValuePB serializes
// deterministically, and that MurmurHash2_64's 8-byte block reads make the result little-endian.
TEST_F(TabletDumpHelperTest, HashSchemeIsPinnedToItsVersion) {
  // EXPECT so that one run reports every value that needs re-recording, not one per rebuild.
  EXPECT_EQ(kTabletDataHashSchemeVersion, 1u);

  EXPECT_EQ(RowHash({{0, Int32Value(1)}}), UINT64_C(0xd7527da2dae5229a));
  EXPECT_EQ(
      RowHash({{0, Int32Value(1)}, {1, TextValue("alpha")}}), UINT64_C(0xe022bfb6f0e35146));
  EXPECT_EQ(
      TableHash(
          {{{0, Int32Value(1)}, {1, TextValue("alpha")}},
           {{0, Int32Value(2)}, {1, TextValue("beta")}}}),
      UINT64_C(0xac674f9ba07c5e6b));
}

// The minimal statement of column identity, which the two tests below exercise through richer rows:
// the same value read out of a different column is a different contribution.
TEST_F(TabletDumpHelperTest, SameValueInDifferentColumnsHashesDifferently) {
  const auto value = Int32Value(5);
  ASSERT_NE(RowHash({{1, value}}), RowHash({{2, value}}));
}

// A value has to carry the identity of the column it came from. A serialized QLValuePB is tagged by
// type and not by column, so without the column-id salt these two rows hash identically: the same
// three values in a different arrangement, one of which has visibly lost data.
TEST_F(TabletDumpHelperTest, SwappingValuesBetweenColumnsChangesRowHash) {
  ASSERT_NE(
      RowHash({{0, Int32Value(1)}, {1, Int32Value(5)}, {2, Int32Value(7)}}),
      RowHash({{0, Int32Value(1)}, {1, Int32Value(7)}, {2, Int32Value(5)}}));
}

// Two same-typed columns holding the same value must not annihilate each other, which is what an
// unsalted xor of the two identical serializations did.
TEST_F(TabletDumpHelperTest, EqualValuesInDifferentColumnsDoNotCancel) {
  const auto fives = RowHash({{0, Int32Value(1)}, {1, Int32Value(5)}, {2, Int32Value(5)}});
  const auto sevens = RowHash({{0, Int32Value(1)}, {1, Int32Value(7)}, {2, Int32Value(7)}});
  ASSERT_NE(fives, sevens);
  // Neither may collapse to the hash of a row carrying the key alone, which is what cancellation
  // used to leave behind.
  const auto key_only = RowHash({{0, Int32Value(1)}});
  ASSERT_NE(fives, key_only);
  ASSERT_NE(sevens, key_only);
}

// The same value under the same column in two different rows must not cancel either. Xoring
// per-column contributions straight into the table total would annihilate them, making a swap of
// two rows' values -- the shape a misapplied update takes -- invisible.
TEST_F(TabletDumpHelperTest, MovingValuesBetweenRowsChangesTableHash) {
  ASSERT_NE(
      TableHash(
          {{{0, Int32Value(1)}, {1, Int32Value(5)}}, {{0, Int32Value(2)}, {1, Int32Value(7)}}}),
      TableHash(
          {{{0, Int32Value(1)}, {1, Int32Value(7)}}, {{0, Int32Value(2)}, {1, Int32Value(5)}}}));

  // Same reason, worse consequence: a NULL contributes nothing, so a pair of rows sharing a value
  // and the same pair with that column nulled out would both reduce to just the key contributions.
  ASSERT_NE(
      TableHash(
          {{{0, Int32Value(1)}, {1, Int32Value(5)}}, {{0, Int32Value(2)}, {1, Int32Value(5)}}}),
      TableHash({{{0, Int32Value(1)}}, {{0, Int32Value(2)}}}));
}

// Commutativity is the foundation of the whole comparison: the total may not depend on the order
// rows are scanned in, or the hash of a table would depend on how it happens to be split into
// tablets and the two clusters could never be compared.
TEST_F(TabletDumpHelperTest, TableHashIsIndependentOfRowOrder) {
  const Row first{{0, Int32Value(1)}, {1, TextValue("alpha")}};
  const Row second{{0, Int32Value(2)}, {1, TextValue("beta")}};
  const Row third{{0, Int32Value(3)}, {1, TextValue("gamma")}};
  ASSERT_EQ(TableHash({first, second, third}), TableHash({third, first, second}));

  // Columns within a row commute too. They have to: the YSQL path visits them in column-id order
  // and the YCQL path in schema order.
  ASSERT_EQ(
      RowHash({{0, Int32Value(1)}, {1, Int32Value(5)}, {2, Int32Value(7)}}),
      RowHash({{2, Int32Value(7)}, {0, Int32Value(1)}, {1, Int32Value(5)}}));
}

// A NULL contributes nothing, so it has to stay distinguishable from the values that are easiest to
// confuse it with: a zero and an empty string both serialize to a non-empty QLValuePB.
TEST_F(TabletDumpHelperTest, NullIsDistinguishableFromZeroAndEmptyString) {
  const auto null_value = RowHash({{0, Int32Value(1)}});
  ASSERT_NE(null_value, RowHash({{0, Int32Value(1)}, {1, Int32Value(0)}}));
  ASSERT_NE(null_value, RowHash({{0, Int32Value(1)}, {1, TextValue("")}}));
}

// A value's bytes must be hashed in order. The old scheme xored the serialization together 8 bytes
// at a time, so two long text values differing only by a swap of aligned blocks hashed the same.
TEST_F(TabletDumpHelperTest, ReorderedBytesWithinValueChangeRowHash) {
  ASSERT_NE(
      RowHash({{0, TextValue("aaaaaaaabbbbbbbb")}}),
      RowHash({{0, TextValue("bbbbbbbbaaaaaaaa")}}));
}

// A collection's element order is part of what gets hashed, since the hash runs over a serialized
// QLValuePB and nothing in it understands collection equality. Two clusters holding the same map
// would report divergence if either materialized its elements in a different order.
//
// They do not, and that comes from the read path rather than from anything here: a map or set is a
// DocDB subdocument whose children live in a std::map<KeyEntryValue, SubDocument>, and
// SubDocument::ToQLValuePB emits them in sorted order. A list is the same mechanism keyed by
// ArrayIndex. Frozen collections are canonicalized on write instead. This test pins the sensitivity
// that assumption protects against.
TEST_F(TabletDumpHelperTest, CollectionElementOrderIsPartOfTheHash) {
  const auto ascending =
      MapValue({{Int32Value(1), TextValue("a")}, {Int32Value(2), TextValue("b")}});
  const auto descending =
      MapValue({{Int32Value(2), TextValue("b")}, {Int32Value(1), TextValue("a")}});
  ASSERT_NE(RowHash({{0, ascending}}), RowHash({{0, descending}}));

  // Keys and values are separate repeated fields, so which value belongs to which key has to reach
  // the hash as well.
  const auto repaired =
      MapValue({{Int32Value(1), TextValue("b")}, {Int32Value(2), TextValue("a")}});
  ASSERT_NE(RowHash({{0, ascending}}), RowHash({{0, repaired}}));
}

// The cases above are the cancellations someone thought of. This covers the ones nobody did: over
// many random tables, two that differ may not share a hash, and two that match must. At 64 bits a
// real collision across this many samples is far less likely than a structural flaw, so a failure
// here means the scheme cancels somewhere.
TEST_F(TabletDumpHelperTest, DistinctTablesHashDistinctly) {
  constexpr size_t kIterations = 4000;
  RandomTableGenerator generator(kSeed);
  std::unordered_map<std::string, uint64_t> hash_by_table;
  std::unordered_map<uint64_t, std::string> table_by_hash;

  for (size_t i = 0; i < kIterations; ++i) {
    const auto rows = generator.NextTable();
    const auto canonical = CanonicalForm(rows);
    const auto hash = TableHash(rows);

    auto [existing, is_new_table] = hash_by_table.emplace(canonical, hash);
    if (!is_new_table) {
      // The generator repeats itself, deliberately: its value alphabet is small. A table hashing
      // differently on a second encounter would mean the hash depends on the order it was built in.
      ASSERT_EQ(existing->second, hash) << "same table hashed two ways: " << canonical;
      continue;
    }
    auto [colliding, is_new_hash] = table_by_hash.emplace(hash, canonical);
    ASSERT_TRUE(is_new_hash) << "distinct tables share a hash:\n  " << colliding->second << "\n  "
                             << canonical;
  }
}

// Every cell has to reach the total. Overwriting one value, or dropping it to NULL, is the shape a
// lost or misapplied replication change takes, and either has to move the table's hash.
TEST_F(TabletDumpHelperTest, ChangingOneCellChangesTableHash) {
  constexpr size_t kIterations = 2000;
  RandomTableGenerator generator(kSeed + 1);

  for (size_t i = 0; i < kIterations; ++i) {
    auto rows = generator.NextTable();
    const auto before = TableHash(rows);
    const auto canonical_before = CanonicalForm(rows);

    auto& row = rows[generator.Index(rows.size())];
    if (row.size() == 1) {
      // No value column survived the generator's NULLs, so move the row to an unused key instead.
      row[0].second = Int32Value(static_cast<int32_t>(rows.size()));
    } else {
      const auto cell = static_cast<std::ptrdiff_t>(1 + generator.Index(row.size() - 1));
      if (generator.Index(2) == 0) {
        row.erase(row.begin() + cell);
      } else {
        // A string the generator cannot produce, so this always is a change.
        row[cell].second = TextValue("mutated");
      }
    }

    ASSERT_NE(before, TableHash(rows))
        << "a changed cell left the table hash alone:\n  before: " << canonical_before
        << "\n  after:  " << CanonicalForm(rows);
  }
}

}  // namespace yb::tablet
