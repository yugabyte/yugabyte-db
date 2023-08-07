// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <stdint.h>

#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/crc16.h"
#include "yb/dockv/partial_row.h"
#include "yb/dockv/partition.h"
#include "yb/common/schema.h"

#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/redis/redisserver/redis_constants.h"

using std::vector;
using std::string;

namespace yb::dockv {

string EncodeRedisKey(const Slice& key) {
  return PartitionSchema::EncodeMultiColumnHashValue(
      crc16(key.data(), key.size()) % kRedisClusterSlots);
}

TEST(PartitionTest, TestRedisEncoding) {
  Schema schema({ ColumnSchema("key", DataType::STRING, ColumnKind::HASH) }, { ColumnId(0) });

  PartitionSchema partition_schema;
  ASSERT_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  YBPartialRow split1(&schema);
  ASSERT_OK(split1.SetStringCopy("key", "{user1000}.following"));
  string pk1;
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  YBPartialRow split2(&schema);
  ASSERT_OK(split2.SetStringCopy("key", "{user1000}.followers"));
  string pk2;
  ASSERT_OK(partition_schema.EncodeRedisKey(split2, &pk2));
  ASSERT_EQ(pk1, pk2);
  pk2 = EncodeRedisKey(Slice("user1000"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "foo{}{bar}"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("foo{}{bar}"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "foo{{bar}}zap"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("{bar"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "foo{bar}{zap}"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("bar"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "{}foobar"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("{}foobar"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "foobar{}"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("foobar{}"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "foobar{z}"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("z"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "foobar"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));
  pk2 = EncodeRedisKey(Slice("foobar"));
  ASSERT_EQ(pk1, pk2);

  ASSERT_OK(split1.SetStringCopy("key", "a"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk1));

  ASSERT_OK(split1.SetStringCopy("key", "{a}"));
  ASSERT_OK(partition_schema.EncodeRedisKey(split1, &pk2));
  ASSERT_EQ(pk1, pk2);
}

void CheckMiddleKey(
    const std::string& key_start, const std::string& key_end,
    const std::string& expected_middle_key) {
  ASSERT_EQ(
      ASSERT_RESULT(PartitionSchema::GetLexicographicMiddleKey(key_start, key_end)),
      expected_middle_key);
}

void CheckMiddleKey(
    const std::string& key_start, const std::string& key_end,
    const std::vector<uint8_t>& expected_middle_key_arr) {
  std::string expected_middle_key(expected_middle_key_arr.begin(), expected_middle_key_arr.end());
  CheckMiddleKey(key_start, key_end, expected_middle_key);
}

void CheckMiddleKey(
    const std::vector<uint8_t>& key_start_arr, const std::vector<uint8_t>& key_end_arr,
    const std::vector<uint8_t>& expected_middle_key_arr) {
  std::string key_start(key_start_arr.begin(), key_start_arr.end());
  std::string key_end(key_end_arr.begin(), key_end_arr.end());
  CheckMiddleKey(key_start, key_end, expected_middle_key_arr);
}

TEST(PartitionTest, TestLexicographicMiddleKey) {
// Macro to make a bit easier to read.
#define vint8 vector<uint8_t>

  // Special case.
  CheckMiddleKey("", "", vint8{128});

  // Test with some default hash partitions for 6 tablets.
  // 0x0000, 0x2AAA -> 0x1555
  CheckMiddleKey(vint8{0, 0}, vint8{42, 170}, vint8{21, 85});
  // 0x2AAA, 0x5554 -> 0x3FFF
  CheckMiddleKey(vint8{42, 170}, vint8{85, 84}, vint8{63, 255});
  // 0x5554, 0x7FFD -> 0x6AA8
  CheckMiddleKey(vint8{85, 84}, vint8{127, 253}, vint8{106, 168});
  // 0x7FFE, 0xAAA7 -> 0x9552
  CheckMiddleKey(vint8{127, 253}, vint8{170, 167}, vint8{149, 82});
  // 0xAAA8, 0xD551 -> 0xBFFC
  CheckMiddleKey(vint8{170, 167}, vint8{213, 81}, vint8{191, 252});
  // 0xD552, 0xFFFF -> 0xEAA8
  CheckMiddleKey(vint8{213, 81}, vint8{255, 255}, vint8{234, 168});

  // 0xFFFD, 0xFFFF -> 0xFFFE
  CheckMiddleKey(vint8{255, 253}, vint8{255, 255}, vint8{255, 254});

  // Test with some ranged partitions, these are arbitrary length strings.
  // A = 65, so expect 65/2 = 32.5, then we have (65+256)/2 = 160.5
  CheckMiddleKey("", "AAAAAA", vint8{32, 160, 160, 160, 160, 160});
  // A = 65 and consider "" as 0xFFFFFF, so expect (65+255)/2 = 160
  CheckMiddleKey("AAAAAA", "", vint8{160, 160, 160, 160, 160, 160});
  // Simple cases.
  CheckMiddleKey("AAAAAA", "CCCCCC", "BBBBBB");
  CheckMiddleKey("AAAAAA", "AAAAAC", "AAAAAB");
  // Uneven lengths.
  CheckMiddleKey("A", "AAAAAA", vint8{65, 32, 160, 160, 160, 160});
  CheckMiddleKey("BBBBBB", "D", vint8{67, 161, 33, 33, 33, 32});

  // Example from partition.cc:
  CheckMiddleKey(vint8{1, 255, 20}, vint8{2, 5, 101}, vint8{2, 2, 60});

#undef vint8
}

TEST(PartitionTest, Distribution) {
  constexpr auto kMaxNumTablets = RegularBuildVsDebugVsSanitizers(10000, 1000, 1000);

  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("key", DataType::STRING));
  ASSERT_OK(builder.AddColumn("val", DataType::STRING));
  Schema schema = builder.Build();
  PartitionSchemaPB partition_schema_pb;
  partition_schema_pb.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);

  PartitionSchema partition_schema;
  ASSERT_OK(PartitionSchema::FromPB(partition_schema_pb, schema, &partition_schema));

  std::vector<Partition> partitions;

  for (auto num_tablets = 1; num_tablets <= kMaxNumTablets; ++num_tablets) {
    ASSERT_OK(partition_schema.CreatePartitions(num_tablets, &partitions));

    int32_t total_hash_codes = 0;
    uint16_t next_hash_code = 0;
    int32_t min_num_hash_codes = std::numeric_limits<int32_t>::max();
    int32_t max_num_hash_codes = std::numeric_limits<int32_t>::min();
    bool first_partition = true;
    for (const auto& partition : partitions) {
      const auto bounds = PartitionSchema::GetHashPartitionBounds(partition);
      ASSERT_EQ(bounds.first, next_hash_code);
      ASSERT_LT(bounds.first, bounds.second);
      const auto num_hash_codes = bounds.second - bounds.first + 1;

      if (num_tablets <= 16) {
        LOG(INFO) << AsString(partition) << " " << AsString(bounds) << " " << num_hash_codes;
      }

      if (first_partition) {
        first_partition = false;
        max_num_hash_codes = min_num_hash_codes = num_hash_codes;
      } else {
        min_num_hash_codes = std::min(min_num_hash_codes, num_hash_codes);
        max_num_hash_codes = std::max(max_num_hash_codes, num_hash_codes);
        ASSERT_LE(max_num_hash_codes - min_num_hash_codes, 1);
      }

      total_hash_codes += num_hash_codes;
      next_hash_code = bounds.second + 1;
    }
    ASSERT_EQ(total_hash_codes, PartitionSchema::kMaxPartitionKey + 1);
  }
}

}  // namespace yb::dockv
