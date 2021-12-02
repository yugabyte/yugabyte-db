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
#include "yb/common/partial_row.h"
#include "yb/common/partition.h"
#include "yb/common/schema.h"

#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"

#include "yb/yql/redis/redisserver/redis_constants.h"

using std::vector;
using std::string;

namespace yb {

string EncodeRedisKey(const Slice& key) {
  return PartitionSchema::EncodeMultiColumnHashValue(
      crc16(key.data(), key.size()) % kRedisClusterSlots);
}

TEST(PartitionTest, TestRedisEncoding) {
  Schema schema({ ColumnSchema("key", STRING, false, true) }, { ColumnId(0) }, 1);

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

} // namespace yb
