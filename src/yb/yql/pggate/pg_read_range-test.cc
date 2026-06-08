//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/util/test_util.h"

#include "yb/yql/pggate/pg_read_range.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"

namespace yb::pggate {

class PgReadRangeTest : public YBTest {
 protected:
  static Schema MakeHashSchema() {
    return Schema({
        ColumnSchema("h1", DataType::INT32, ColumnKind::HASH, Nullable::kFalse, false, false, 1),
        ColumnSchema("h2", DataType::INT32, ColumnKind::HASH, Nullable::kFalse, false, false, 2),
        ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST,
                     Nullable::kFalse, false, false, 3),
        ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST,
                     Nullable::kFalse, false, false, 4),
        ColumnSchema("v", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue, false, false, 5),
    }, {ColumnId(1), ColumnId(2), ColumnId(3), ColumnId(4), ColumnId(5)});
  }

  static Schema MakeRangeSchema() {
    return Schema({
        ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST,
                    Nullable::kFalse, false, false, 1),
        ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST,
                     Nullable::kFalse, false, false, 2),
        ColumnSchema("r3", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST,
                     Nullable::kFalse, false, false, 3),
        ColumnSchema("v", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue, false, false, 4),
    }, {ColumnId(1), ColumnId(2), ColumnId(3), ColumnId(4)});
  }

  static std::string MakeRangeDocKeyString(
      const Schema& schema, int32_t r1, int32_t r2, int32_t r3) {
    dockv::DocKey doc_key(
        schema,
        dockv::KeyEntryValues{
            dockv::KeyEntryValue::Int32(r1),
            dockv::KeyEntryValue::Int32(r2),
            dockv::KeyEntryValue::Int32(r3)});
    return doc_key.Encode().AsSlice().ToBuffer();
  }

  static PgTable MakeTable(bool is_hash_partitioned) {
    const auto schema = is_hash_partitioned ? MakeHashSchema() : MakeRangeSchema();
    master::GetTableSchemaResponsePB resp;
    SchemaToPB(schema, resp.mutable_schema());
    if (is_hash_partitioned) {
      resp.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::PGSQL_HASH_SCHEMA);
    }
    resp.set_version(1);

    client::VersionedTablePartitionList partition_list;
    partition_list.version = 1;
    partition_list.keys.emplace_back("");
    if (is_hash_partitioned) {
      partition_list.keys.emplace_back(dockv::PartitionSchema::EncodeMultiColumnHashValue(100));
      partition_list.keys.emplace_back(dockv::PartitionSchema::EncodeMultiColumnHashValue(200));
    } else {
      partition_list.keys.emplace_back(MakeRangeDocKeyString(schema, 100, 100, 100));
      partition_list.keys.emplace_back(MakeRangeDocKeyString(schema, 200, 200, 200));
    }

    PgObjectId relfilenode_id(1, 1);
    auto desc = make_scoped_refptr<PgTableDesc>(
        relfilenode_id, resp, std::move(partition_list));
    CHECK_OK(desc->Init());
    return PgTable(desc);
  }

  PgTable hash_table_ = MakeTable(true);
  PgTable range_table_ = MakeTable(false);

  dockv::DocKey MakeRangeDocKey(int32_t r1, int32_t r2, int32_t r3) const {
    return dockv::DocKey(
        range_table_->schema(),
        dockv::KeyEntryValues{
            dockv::KeyEntryValue::Int32(r1),
            dockv::KeyEntryValue::Int32(r2),
            dockv::KeyEntryValue::Int32(r3)});
  }

  dockv::DocKey MakeHashDocKey(
      uint16_t hash, int32_t h1_val, int32_t h2_val, int32_t r1_val, int32_t r2_val) const {
    return dockv::DocKey(
        hash_table_->schema(),
        hash,
        dockv::KeyEntryValues{
            dockv::KeyEntryValue::Int32(h1_val), dockv::KeyEntryValue::Int32(h2_val)},
        dockv::KeyEntryValues{
            dockv::KeyEntryValue::Int32(r1_val), dockv::KeyEntryValue::Int32(r2_val)});
  }
};

TEST_F(PgReadRangeTest, InitialState) {
  {
    PgReadRange range(hash_table_);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(range_table_);
    EXPECT_FALSE(range.IsEmpty());
  }
}

TEST_F(PgReadRangeTest, HashCodeEquals) {
  PgReadRange r1(hash_table_);
  PgReadRange r2(hash_table_);
  EXPECT_EQ(r1, r2);

  r1.SetHashCodeBound(100, true, true);
  EXPECT_NE(r1, r2);

  r2.SetHashCodeBound(100, true, true);
  EXPECT_EQ(r1, r2);

  r2.SetHashCodeBound(200, false, false);
  EXPECT_NE(r1, r2);

  r1.SetHashCodeBound(200, false, false);
  EXPECT_EQ(r1, r2);
}

TEST_F(PgReadRangeTest, RangeKeyEquals) {
  PgReadRange r1(range_table_);
  PgReadRange r2(range_table_);
  EXPECT_EQ(r1, r2);

  auto k1 = MakeRangeDocKey(10, 0, 10);
  auto k2 = MakeRangeDocKey(20, 0, 20);

  r1.SetDocKeyBound(k1, true, true);
  EXPECT_NE(r1, r2);

  r2.SetDocKeyBound(k1, true, true);
  EXPECT_EQ(r1, r2);

  r2.SetDocKeyBound(k2, false, false);
  EXPECT_NE(r1, r2);

  r1.SetDocKeyBound(k2, false, false);
  EXPECT_EQ(r1, r2);
}

TEST_F(PgReadRangeTest, HashKeyEquals) {
  PgReadRange r1(hash_table_);
  PgReadRange r2(hash_table_);

  auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
  auto k2 = MakeHashDocKey(200, 20, 0, 20, 0);

  r1.SetDocKeyBound(k1, true, true);
  EXPECT_NE(r1, r2);

  r2.SetDocKeyBound(k1, true, true);
  EXPECT_EQ(r1, r2);

  r2.SetDocKeyBound(k2, false, false);
  EXPECT_NE(r1, r2);

  r1.SetDocKeyBound(k2, false, false);
  EXPECT_EQ(r1, r2);
}

TEST_F(PgReadRangeTest, HashCodeIntersectsBasic) {
  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetHashCodeBound(100, true, true);
    r1.SetHashCodeBound(300, true, false);
    r2.SetHashCodeBound(200, true, true);
    r2.SetHashCodeBound(400, true, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetHashCodeBound(100, true, true);
    r1.SetHashCodeBound(400, true, false);
    r2.SetHashCodeBound(200, true, true);
    r2.SetHashCodeBound(300, true, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetHashCodeBound(100, true, true);
    r1.SetHashCodeBound(200, true, false);
    r2.SetHashCodeBound(300, true, true);
    r2.SetHashCodeBound(400, true, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, RangeKeyIntersectsBasic) {
  auto k1 = MakeRangeDocKey(10, 0, 10);
  auto k2 = MakeRangeDocKey(20, 0, 20);
  auto k3 = MakeRangeDocKey(30, 0, 30);
  auto k4 = MakeRangeDocKey(40, 0, 40);

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k3, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k4, false, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k4, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    r2.SetDocKeyBound(k3, true, true);
    r2.SetDocKeyBound(k4, false, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, HashKeyIntersectsBasic) {
  auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
  auto k2 = MakeHashDocKey(200, 20, 0, 20, 0);
  auto k3 = MakeHashDocKey(300, 30, 0, 30, 0);
  auto k4 = MakeHashDocKey(400, 40, 0, 40, 0);

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k3, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k4, false, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k4, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    r2.SetDocKeyBound(k3, true, true);
    r2.SetDocKeyBound(k4, false, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, RangeIntersectsTouching) {
  auto k1 = MakeRangeDocKey(10, 0, 10);
  auto k2 = MakeRangeDocKey(20, 0, 20);
  auto k3 = MakeRangeDocKey(30, 0, 30);

  {
    // Both inclusive
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, true, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, true, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    // Both exclusive
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    r2.SetDocKeyBound(k2, false, true);
    r2.SetDocKeyBound(k3, false, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }

  {
    // One inclusive, one exclusive
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, HashIntersectsTouching) {
  auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
  auto k2 = MakeHashDocKey(200, 20, 0, 20, 0);
  auto k3 = MakeHashDocKey(300, 30, 0, 30, 0);

  {
    // Both inclusive
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, true, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, true, false);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    // Both exclusive
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    r2.SetDocKeyBound(k2, false, true);
    r2.SetDocKeyBound(k3, true, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }

  {
    // One inclusive, one exclusive
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, true, false);
    r2.SetDocKeyBound(k2, false, true);
    r2.SetDocKeyBound(k3, false, false);
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, IntersectsWithEmptyRange) {
  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);

    // Make r1 empty.
    auto lower = MakeRangeDocKey(30, 0, 30);
    auto upper = MakeRangeDocKey(10, 0, 10);
    r1.SetDocKeyBound(lower, true, true);
    r1.SetDocKeyBound(upper, true, false);
    EXPECT_TRUE(r1.IsEmpty());

    // r2 has an open range.
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);

    // Make r1 empty.
    r1.SetHashCodeBound(200, true, true);
    r1.SetDocKeyBound(MakeHashDocKey(100, 10, 0, 10, 0), true, false);
    EXPECT_TRUE(r1.IsEmpty());

    // r2 has an open range.
    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, RangeIntersectsOpenRanges) {
  auto k1 = MakeRangeDocKey(10, 0, 10);
  auto k2 = MakeRangeDocKey(20, 0, 20);
  auto k3 = MakeRangeDocKey(30, 0, 30);

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));

    // Open upper bound.
    r1.SetDocKeyBound(k1, true, true);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);

    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);

    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    // Open lower bound.
    r2.SetDocKeyBound(k3, false, false);

    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);

    // Open lower bound.
    r1.SetDocKeyBound(k1, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);

    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(range_table_);
    PgReadRange r2(range_table_);

    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    // Open upper bound.
    r2.SetDocKeyBound(k3, true, true);

    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, HashIntersectsOpenRanges) {
  auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
  auto k2 = MakeHashDocKey(200, 20, 0, 20, 0);
  auto k3 = MakeHashDocKey(300, 30, 0, 30, 0);

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);
    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));

    // Open upper bound.
    r1.SetDocKeyBound(k1, true, true);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);

    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);

    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    // Open lower bound.
    r2.SetDocKeyBound(k3, false, false);

    EXPECT_TRUE(r1.Intersects(r2));
    EXPECT_TRUE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);

    // Open lower bound.
    r1.SetDocKeyBound(k1, false, false);
    r2.SetDocKeyBound(k2, true, true);
    r2.SetDocKeyBound(k3, false, false);

    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }

  {
    PgReadRange r1(hash_table_);
    PgReadRange r2(hash_table_);

    r1.SetDocKeyBound(k1, true, true);
    r1.SetDocKeyBound(k2, false, false);
    // Open upper bound.
    r2.SetDocKeyBound(k3, true, true);

    EXPECT_FALSE(r1.Intersects(r2));
    EXPECT_FALSE(r2.Intersects(r1));
  }
}

TEST_F(PgReadRangeTest, SetHashCodeBoundLower) {
  PgReadRange expected(hash_table_);
  expected.SetHashCodeBound(100, true, true);
  EXPECT_FALSE(expected.IsEmpty());

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, true, true);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(99, false, true);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(50, false, true);
    range.SetHashCodeBound(100, true, true);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, true, true);
    range.SetHashCodeBound(50, false, true);
    EXPECT_EQ(range, expected);
  }
}

TEST_F(PgReadRangeTest, SetHashCodeBoundUpper) {
  PgReadRange expected(hash_table_);
  expected.SetHashCodeBound(200, false, false);
  EXPECT_FALSE(expected.IsEmpty());

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(200, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(199, true, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(300, true, false);
    range.SetHashCodeBound(200, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(200, false, false);
    range.SetHashCodeBound(300, true, false);
    EXPECT_EQ(range, expected);
  }
}

TEST_F(PgReadRangeTest, SetHashCodeBoundBoth) {
  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, true, true);
    range.SetHashCodeBound(200, true, false);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(200, true, true);
    range.SetHashCodeBound(100, true, false);
    EXPECT_TRUE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, true, true);
    range.SetHashCodeBound(100, true, false);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, false, true);
    range.SetHashCodeBound(100, true, false);
    EXPECT_TRUE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, true, true);
    range.SetHashCodeBound(100, false, false);
    EXPECT_TRUE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetHashCodeBound(100, false, true);
    range.SetHashCodeBound(100, false, false);
    EXPECT_TRUE(range.IsEmpty());
  }
}

TEST_F(PgReadRangeTest, RangeSetDocKeyBounds) {
  auto lower = MakeRangeDocKey(10, 20, 30);
  auto upper = MakeRangeDocKey(30, 40, 50);

  {
    PgReadRange range(range_table_);
    range.SetDocKeyBound(lower, true, true);
    range.SetDocKeyBound(upper, true, false);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(range_table_);
    range.SetDocKeyBound(upper, true, true);
    range.SetDocKeyBound(lower, true, false);
    EXPECT_TRUE(range.IsEmpty());
  }

  {
    PgReadRange range(range_table_);
    range.SetDocKeyBound(lower, true, true);
    range.SetDocKeyBound(lower, true, false);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(range_table_);
    range.SetDocKeyBound(upper, true, true);
    range.SetDocKeyBound(upper, false, false);
    EXPECT_TRUE(range.IsEmpty());
  }
}

TEST_F(PgReadRangeTest, HashSetDocKeyBounds) {
  auto lower = MakeHashDocKey(100, 10, 0, 10, 0);
  auto upper = MakeHashDocKey(200, 20, 0, 20, 0);

  {
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(lower, true, true);
    range.SetDocKeyBound(upper, true, false);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(upper, true, true);
    range.SetDocKeyBound(lower, true, false);
    EXPECT_TRUE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(lower, true, true);
    range.SetDocKeyBound(lower, true, false);
    EXPECT_FALSE(range.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(upper, true, true);
    range.SetDocKeyBound(upper, false, false);
    EXPECT_TRUE(range.IsEmpty());
  }
}

TEST_F(PgReadRangeTest, BoundsOnlyTighten) {
  {
    PgReadRange range(range_table_);
    auto k0 = MakeRangeDocKey(0, 10, 20);
    auto k1 = MakeRangeDocKey(10, 20, 30);
    auto k2 = MakeRangeDocKey(20, 30, 40);
    auto k3 = MakeRangeDocKey(30, 40, 50);
    auto k4 = MakeRangeDocKey(40, 50, 60);
    auto k5 = MakeRangeDocKey(50, 60, 70);
    auto k6 = MakeRangeDocKey(60, 70, 80);

    range.SetDocKeyBound(k1, true, true);
    range.SetDocKeyBound(k5, true, false);
    EXPECT_FALSE(range.IsEmpty());

    // Setting a bound outside the current one should be a no-op.
    PgReadRange snapshot1 = range;
    range.SetDocKeyBound(k0, true, true);
    EXPECT_EQ(range, snapshot1);
    range.SetDocKeyBound(k6, true, false);
    EXPECT_EQ(range, snapshot1);

    // Setting a bound inside the current one should tighten.
    range.SetDocKeyBound(k2, false, true);
    EXPECT_NE(range, snapshot1);
    PgReadRange snapshot2 = range;
    range.SetDocKeyBound(k4, false, false);
    EXPECT_NE(range, snapshot2);
    PgReadRange snapshot3 = range;

    // Changing exclusive to inclusive is no-op.
    range.SetDocKeyBound(k2, true, true);
    range.SetDocKeyBound(k4, true, false);
    EXPECT_EQ(range, snapshot3);

    // Changing inclusive to exclusive should tighten.
    range.SetDocKeyBound(k3, true, true);
    range.SetDocKeyBound(k3, true, false);
    EXPECT_FALSE(range.IsEmpty());
    PgReadRange snapshot4 = range;
    range.SetDocKeyBound(k3, false, true);
    EXPECT_TRUE(range.IsEmpty());
    snapshot4.SetDocKeyBound(k3, false, false);
    EXPECT_TRUE(snapshot4.IsEmpty());
  }

  {
    PgReadRange range(hash_table_);
    auto k0 = MakeHashDocKey(0, 0, 0, 0, 0);
    auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
    auto k2 = MakeHashDocKey(200, 20, 0, 20, 0);
    auto k3 = MakeHashDocKey(300, 30, 0, 30, 0);
    auto k4 = MakeHashDocKey(400, 40, 0, 40, 0);
    auto k5 = MakeHashDocKey(500, 50, 0, 50, 0);
    auto k6 = MakeHashDocKey(600, 60, 0, 60, 0);

    range.SetDocKeyBound(k1, true, true);
    range.SetDocKeyBound(k5, true, false);
    EXPECT_FALSE(range.IsEmpty());

    // Setting a bound outside the current one should be a no-op.
    PgReadRange snapshot1 = range;
    range.SetDocKeyBound(k0, true, true);
    EXPECT_EQ(range, snapshot1);
    range.SetDocKeyBound(k6, true, false);
    EXPECT_EQ(range, snapshot1);

    // Setting a bound inside the current one should tighten.
    range.SetDocKeyBound(k2, false, true);
    EXPECT_NE(range, snapshot1);
    PgReadRange snapshot2 = range;
    range.SetDocKeyBound(k4, false, false);
    EXPECT_NE(range, snapshot2);
    PgReadRange snapshot3 = range;

    // Changing exclusive to inclusive is no-op.
    range.SetDocKeyBound(k2, true, true);
    range.SetDocKeyBound(k4, true, false);
    EXPECT_EQ(range, snapshot3);

    // Changing inclusive to exclusive should tighten.
    range.SetDocKeyBound(k3, true, true);
    range.SetDocKeyBound(k3, true, false);
    EXPECT_FALSE(range.IsEmpty());
    PgReadRange snapshot4 = range;
    range.SetDocKeyBound(k3, false, true);
    EXPECT_TRUE(range.IsEmpty());
    snapshot4.SetDocKeyBound(k3, false, false);
    EXPECT_TRUE(snapshot4.IsEmpty());
  }
}

TEST_F(PgReadRangeTest, ApplyBoundsToRequest) {
  {
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB req(&arena);

    auto k0 = MakeRangeDocKey(0, 10, 20);
    auto k1 = MakeRangeDocKey(10, 20, 30);
    auto k2 = MakeRangeDocKey(20, 30, 40);
    auto k3 = MakeRangeDocKey(30, 40, 50);
    auto k4 = MakeRangeDocKey(40, 50, 60);

    PgReadRange range1(range_table_);
    range1.SetDocKeyBound(k1, true, true);
    range1.SetDocKeyBound(k3, false, false);
    EXPECT_TRUE(range1.ApplyBounds(req));
    PgReadRange result1(range_table_);
    result1.SetRequestBounds(req);
    EXPECT_EQ(result1, range1);

    PgReadRange range2(range_table_);
    range2.SetDocKeyBound(k0, false, true);
    range2.SetDocKeyBound(k4, true, false);
    EXPECT_TRUE(range2.ApplyBounds(req));
    PgReadRange result2(range_table_);
    result2.SetRequestBounds(req);
    EXPECT_EQ(result2, range1);

    PgReadRange range3(range_table_);
    range3.SetDocKeyBound(k2, true, true);
    range3.SetDocKeyBound(k2, false, false);
    EXPECT_TRUE(range3.IsEmpty());
    EXPECT_FALSE(range3.ApplyBounds(req));
  }

  {
    ThreadSafeArena arena;
    LWPgsqlReadRequestPB req(&arena);

    auto k0 = MakeHashDocKey(0, 0, 0, 0, 0);
    auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
    auto k2 = MakeHashDocKey(200, 20, 0, 20, 0);
    auto k3 = MakeHashDocKey(300, 30, 0, 30, 0);
    auto k4 = MakeHashDocKey(400, 40, 0, 40, 0);

    PgReadRange range1(hash_table_);
    range1.SetDocKeyBound(k1, true, true);
    range1.SetDocKeyBound(k3, false, false);
    EXPECT_TRUE(range1.ApplyBounds(req));
    PgReadRange result1(hash_table_);
    result1.SetRequestBounds(req);
    EXPECT_EQ(result1, range1);

    PgReadRange range2(hash_table_);
    range2.SetDocKeyBound(k0, false, true);
    range2.SetDocKeyBound(k4, true, false);
    EXPECT_TRUE(range2.ApplyBounds(req));
    PgReadRange result2(hash_table_);
    result2.SetRequestBounds(req);
    EXPECT_EQ(result2, range1);

    PgReadRange range3(hash_table_);
    range3.SetDocKeyBound(k2, true, true);
    range3.SetDocKeyBound(k2, false, false);
    EXPECT_TRUE(range3.IsEmpty());
    EXPECT_FALSE(range3.ApplyBounds(req));
  }
}

TEST_F(PgReadRangeTest, SetPartitionBoundsHash) {
  {
    PgReadRange range(hash_table_);
    range.SetPartitionBounds(0);
    PgReadRange expected(hash_table_);
    expected.SetHashCodeBound(100, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetPartitionBounds(1);
    PgReadRange expected(hash_table_);
    expected.SetHashCodeBound(100, true, true);
    expected.SetHashCodeBound(200, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(hash_table_);
    range.SetPartitionBounds(2);
    PgReadRange expected(hash_table_);
    expected.SetHashCodeBound(200, true, true);
    EXPECT_EQ(range, expected);
  }
}

TEST_F(PgReadRangeTest, SetPartitionBoundsRange) {
  auto k1 = MakeRangeDocKey(100, 100, 100);
  auto k2 = MakeRangeDocKey(200, 200, 200);
  {
    PgReadRange range(range_table_);
    range.SetPartitionBounds(0);
    PgReadRange expected(range_table_);
    expected.SetDocKeyBound(k1, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(range_table_);
    range.SetPartitionBounds(1);
    PgReadRange expected(range_table_);
    expected.SetDocKeyBound(k1, true, true);
    expected.SetDocKeyBound(k2, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    PgReadRange range(range_table_);
    range.SetPartitionBounds(2);
    PgReadRange expected(range_table_);
    expected.SetDocKeyBound(k2, true, true);
    EXPECT_EQ(range, expected);
  }
}

TEST_F(PgReadRangeTest, SetQLValueBoundsHash) {
  ThreadSafeArena arena;
  LWQLValuePB value_10(&arena);
  value_10.set_int32_value(10);
  LWQLValuePB value_0(&arena);
  value_0.set_int32_value(0);
  LWQLValuePB value_null(&arena);

  {
    // Full key
    std::vector<const LWQLValuePB*> values = {&value_10, &value_0, &value_10, &value_0};
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(100, values, true, true);
    range.SetDocKeyBound(100, values, true, false);
    PgReadRange expected(hash_table_);
    auto k1 = MakeHashDocKey(100, 10, 0, 10, 0);
    expected.SetDocKeyBound(k1, true, true);
    expected.SetDocKeyBound(k1, true, false);
    EXPECT_EQ(range, expected);
  }

  {
    // Short key
    std::vector<const LWQLValuePB*> values = {&value_10, &value_0, &value_10};
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(100, values, true, true);
    range.SetDocKeyBound(100, values, true, false);
    PgReadRange expected(hash_table_);
    dockv::DocKey k1(hash_table_->schema(), 100,
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue::Int32(0)},
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue(dockv::KeyEntryType::kLowest)});
    dockv::DocKey k2(hash_table_->schema(), 100,
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue::Int32(0)},
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue(dockv::KeyEntryType::kHighest)});
    expected.SetDocKeyBound(k1, false, true);
    expected.SetDocKeyBound(k2, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    // Null key
    std::vector<const LWQLValuePB*> values = {&value_10, &value_0, &value_10, &value_null};
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(100, values, true, true);
    range.SetDocKeyBound(100, values, true, false);
    PgReadRange expected(hash_table_);
    dockv::DocKey k1(hash_table_->schema(), 100,
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue::Int32(0)},
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue(dockv::KeyEntryType::kLowest)});
    dockv::DocKey k2(hash_table_->schema(), 100,
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue::Int32(0)},
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue(dockv::KeyEntryType::kHighest)});
    expected.SetDocKeyBound(k1, false, true);
    expected.SetDocKeyBound(k2, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    // nullptr hashkey
    std::vector<const LWQLValuePB*> values = {&value_10, &value_0, nullptr, &value_0};
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(100, values, true, true);
    range.SetDocKeyBound(100, values, true, false);
    PgReadRange expected(hash_table_);
    dockv::DocKey k1(hash_table_->schema(), 100,
      dockv::KeyEntryValues{dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue::Int32(0)},
      dockv::KeyEntryValues{dockv::KeyEntryValue(dockv::KeyEntryType::kLowest)});
    dockv::DocKey k2(hash_table_->schema(), 100,
      dockv::KeyEntryValues{dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue::Int32(0)},
      dockv::KeyEntryValues{dockv::KeyEntryValue(dockv::KeyEntryType::kHighest)});
    expected.SetDocKeyBound(k1, false, true);
    expected.SetDocKeyBound(k2, false, false);
    EXPECT_EQ(range, expected);
  }

  {
    // missing hashkey
    std::vector<const LWQLValuePB*> values = {&value_10, nullptr, &value_10, &value_0};
    PgReadRange range(hash_table_);
    range.SetDocKeyBound(100, values, true, true);
    range.SetDocKeyBound(100, values, true, false);
    PgReadRange expected(hash_table_);
    dockv::DocKey k1(hash_table_->schema(), 100,
      dockv::KeyEntryValues{
          dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue(dockv::KeyEntryType::kLowest)});
    dockv::DocKey k2(hash_table_->schema(), 100,
      dockv::KeyEntryValues{
        dockv::KeyEntryValue::Int32(10), dockv::KeyEntryValue(dockv::KeyEntryType::kHighest)});
    expected.SetDocKeyBound(k1, false, true);
    expected.SetDocKeyBound(k2, false, false);
    EXPECT_EQ(range, expected);
  }
}

} // namespace yb::pggate
