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

#include <iterator>
#include <stdint.h>
#include <vector>

#include "kudu/common/common.pb.h"
#include "kudu/common/partial_row.h"
#include "kudu/common/partition.h"
#include "kudu/common/row.h"
#include "kudu/common/scan_predicate.h"
#include "kudu/common/schema.h"
#include "kudu/util/hash_util.h"
#include "kudu/util/test_util.h"

using std::vector;
using std::string;

namespace kudu {

namespace {
void AddHashBucketComponent(PartitionSchemaPB* partition_schema_pb,
                            const vector<string>& columns,
                            uint32_t num_buckets, int32_t seed) {
  PartitionSchemaPB::HashBucketSchemaPB* hash_bucket_schema =
      partition_schema_pb->add_hash_bucket_schemas();
  for (const string& column : columns) {
    hash_bucket_schema->add_columns()->set_name(column);
  }
  hash_bucket_schema->set_num_buckets(num_buckets);
  hash_bucket_schema->set_seed(seed);
}

void SetRangePartitionComponent(PartitionSchemaPB* partition_schema_pb,
                                const vector<string>& columns) {
  PartitionSchemaPB::RangeSchemaPB* range_schema = partition_schema_pb->mutable_range_schema();
  range_schema->Clear();
  for (const string& column : columns) {
    range_schema->add_columns()->set_name(column);
  }
}
} // namespace

TEST(PartitionTest, TestPartitionKeyEncoding) {
  // CREATE TABLE t (a INT32, b VARCHAR, c VARCHAR, PRIMARY KEY (a, b, c))
  // PARITITION BY [HASH BUCKET (a, b), HASH BUCKET (c), RANGE (a, b, c)];
  Schema schema({ ColumnSchema("a", INT32),
                  ColumnSchema("b", STRING),
                  ColumnSchema("c", STRING) },
                { ColumnId(0), ColumnId(1), ColumnId(2) }, 3);

  PartitionSchemaPB schema_builder;
  AddHashBucketComponent(&schema_builder, { "a", "b" }, 32, 0);
  AddHashBucketComponent(&schema_builder, { "c" }, 32, 42);
  PartitionSchema partition_schema;
  ASSERT_OK(PartitionSchema::FromPB(schema_builder, schema, &partition_schema));

  ASSERT_EQ("hash bucket components: [(bucket count: 32, columns: [a, b]), "
            "(bucket count: 32, seed: 42, columns: [c])], range columns: [a, b, c]",
            partition_schema.DebugString(schema));

  {
    string key;
    KuduPartialRow row(&schema);
    ASSERT_OK(row.SetInt32("a", 0));
    ASSERT_OK(partition_schema.EncodeKey(row, &key));

    EXPECT_EQ(string("\0\0\0\0"   // hash(0, "")
                     "\0\0\0\x14" // hash("")
                     "\x80\0\0\0" // a = 0
                     "\0\0",      // b = ""; c is elided
                     14), key);
    string debug = "bucket=0, bucket=20, int32 a=0, string b=, string c=";
    EXPECT_EQ(debug, partition_schema.RowDebugString(row));
    EXPECT_EQ(debug, partition_schema.PartitionKeyDebugString(key, schema));
  }

  {
    string key;
    KuduPartialRow row(&schema);
    ASSERT_OK(row.SetInt32("a", 1));
    ASSERT_OK(partition_schema.EncodeKey(row, &key));

    EXPECT_EQ(string("\0\0\0\x5"    // hash(1, "")
                     "\0\0\0\x14"   // hash("")
                     "\x80\0\0\x01" // a = 1
                     "\0\0",        // b = ""; c is elided
                     14), key);

    string debug_b = "bucket=5, bucket=20, int32 a=1, string b=, string c=";
    EXPECT_EQ(debug_b, partition_schema.RowDebugString(row));
    EXPECT_EQ(debug_b, partition_schema.PartitionKeyDebugString(key, schema));
  }

  {
    string key;
    KuduPartialRow row(&schema);
    ASSERT_OK(row.SetInt32("a", 0));
    ASSERT_OK(row.SetStringCopy("b", "b"));
    ASSERT_OK(row.SetStringCopy("c", "c"));
    ASSERT_OK(partition_schema.EncodeKey(row, &key));

    EXPECT_EQ(string("\0\0\0\x1A" // hash(0, "b")
                     "\0\0\0\x1D" // hash("c")
                     "\x80\0\0\0" // a = 0
                     "b\0\0"      // b = "b"
                     "c",         // c = "c"
                     16), key);

    string debug = "bucket=26, bucket=29, int32 a=0, string b=b, string c=c";
    EXPECT_EQ(debug, partition_schema.RowDebugString(row));
    EXPECT_EQ(debug, partition_schema.PartitionKeyDebugString(key, schema));
  }

  {
    string key;
    KuduPartialRow row(&schema);
    ASSERT_OK(row.SetInt32("a", 1));
    ASSERT_OK(row.SetStringCopy("b", "b"));
    ASSERT_OK(row.SetStringCopy("c", "c"));
    ASSERT_OK(partition_schema.EncodeKey(row, &key));

    EXPECT_EQ(string("\0\0\0\x0"   // hash(1, "b")
                     "\0\0\0\x1D"  // hash("c")
                     "\x80\0\0\x1" // a = 1
                     "b\0\0"       // b = "b"
                     "c",          // c = "c"
                     16), key);

    string debug = "bucket=0, bucket=29, int32 a=1, string b=b, string c=c";
    EXPECT_EQ(debug, partition_schema.RowDebugString(row));
    EXPECT_EQ(debug, partition_schema.PartitionKeyDebugString(key, schema));
  }
}

TEST(PartitionTest, TestCreateRangePartitions) {
  // CREATE TABLE t (a VARCHAR PRIMARY KEY),
  // PARITITION BY [RANGE (a)];
  Schema schema({ ColumnSchema("a", STRING) }, { ColumnId(0) }, 1);

  PartitionSchema partition_schema;
  ASSERT_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  ASSERT_EQ("range columns: [a]", partition_schema.DebugString(schema));

  // Split Rows:
  //
  // { a: "1" }
  // { a: "2" }
  //
  // Encoded Partition Keys:
  //
  // [ ( ""), ("1") )
  // [ ("1"), ("2") )
  // [ ("2"), ( "") )

  KuduPartialRow split1(&schema);
  ASSERT_OK(split1.SetStringCopy("a", "1"));
  string pk1;
  ASSERT_OK(partition_schema.EncodeKey(split1, &pk1));

  KuduPartialRow split2(&schema);
  ASSERT_OK(split2.SetStringCopy("a", "2"));
  string pk2;
  ASSERT_OK(partition_schema.EncodeKey(split2, &pk2));

  // Split keys need not be passed in sorted order.
  vector<KuduPartialRow> split_rows = { split2, split1 };
  vector<Partition> partitions;
  ASSERT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));
  ASSERT_EQ(3, partitions.size());

  EXPECT_TRUE(partitions[0].hash_buckets().empty());
  EXPECT_EQ("", partitions[0].range_key_start());
  EXPECT_EQ("1", partitions[0].range_key_end());
  EXPECT_EQ("", partitions[0].partition_key_start());
  EXPECT_EQ("1", partitions[0].partition_key_end());
  EXPECT_EQ("range: [(<start>), (string a=1))",
            partition_schema.PartitionDebugString(partitions[0], schema));

  EXPECT_TRUE(partitions[1].hash_buckets().empty());
  EXPECT_EQ("1", partitions[1].range_key_start());
  EXPECT_EQ("2", partitions[1].range_key_end());
  EXPECT_EQ("1", partitions[1].partition_key_start());
  EXPECT_EQ("2", partitions[1].partition_key_end());
  EXPECT_EQ("range: [(string a=1), (string a=2))",
            partition_schema.PartitionDebugString(partitions[1], schema));

  EXPECT_TRUE(partitions[2].hash_buckets().empty());
  EXPECT_EQ("2", partitions[2].range_key_start());
  EXPECT_EQ("", partitions[2].range_key_end());
  EXPECT_EQ("2", partitions[2].partition_key_start());
  EXPECT_EQ("", partitions[2].partition_key_end());
  EXPECT_EQ("range: [(string a=2), (<end>))",
            partition_schema.PartitionDebugString(partitions[2], schema));
}

TEST(PartitionTest, TestCreateHashBucketPartitions) {
  // CREATE TABLE t (a VARCHAR PRIMARY KEY),
  // PARITITION BY [HASH BUCKET (a)];
  Schema schema({ ColumnSchema("a", STRING) }, { ColumnId(0) }, 1);

  PartitionSchemaPB schema_builder;
  SetRangePartitionComponent(&schema_builder, vector<string>());
  AddHashBucketComponent(&schema_builder, { "a" }, 3, 42);
  PartitionSchema partition_schema;
  ASSERT_OK(PartitionSchema::FromPB(schema_builder, schema, &partition_schema));

  ASSERT_EQ("hash bucket components: [(bucket count: 3, seed: 42, columns: [a])]",
            partition_schema.DebugString(schema));

  // Encoded Partition Keys:
  //
  // [ (_), (1) )
  // [ (1), (2) )
  // [ (3), (_) )

  vector<Partition> partitions;
  ASSERT_OK(partition_schema.CreatePartitions(vector<KuduPartialRow>(), schema, &partitions));
  ASSERT_EQ(3, partitions.size());

  EXPECT_EQ(0, partitions[0].hash_buckets()[0]);
  EXPECT_EQ("", partitions[0].range_key_start());
  EXPECT_EQ("", partitions[0].range_key_end());
  EXPECT_EQ(string("", 0), partitions[0].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1", 4), partitions[0].partition_key_end());
  EXPECT_EQ("hash buckets: (0)",
            partition_schema.PartitionDebugString(partitions[0], schema));

  EXPECT_EQ(1, partitions[1].hash_buckets()[0]);
  EXPECT_EQ("", partitions[1].range_key_start());
  EXPECT_EQ("", partitions[1].range_key_end());
  EXPECT_EQ(string("\0\0\0\1", 4), partitions[1].partition_key_start());
  EXPECT_EQ(string("\0\0\0\2", 4), partitions[1].partition_key_end());
  EXPECT_EQ("hash buckets: (1)",
            partition_schema.PartitionDebugString(partitions[1], schema));

  EXPECT_EQ(2, partitions[2].hash_buckets()[0]);
  EXPECT_EQ("", partitions[2].range_key_start());
  EXPECT_EQ("", partitions[2].range_key_end());
  EXPECT_EQ(string("\0\0\0\2", 4), partitions[2].partition_key_start());
  EXPECT_EQ(string("", 0), partitions[2].partition_key_end());
  EXPECT_EQ("hash buckets: (2)",
            partition_schema.PartitionDebugString(partitions[2], schema));
}

TEST(PartitionTest, TestCreatePartitions) {
  // CREATE TABLE t (a VARCHAR, b VARCHAR, c VARCHAR, PRIMARY KEY (a, b, c))
  // PARITITION BY [HASH BUCKET (a), HASH BUCKET (b), RANGE (a, b, c)];
  Schema schema({ ColumnSchema("a", STRING),
                  ColumnSchema("b", STRING),
                  ColumnSchema("c", STRING) },
                { ColumnId(0), ColumnId(1), ColumnId(2) }, 3);

  PartitionSchemaPB schema_builder;
  AddHashBucketComponent(&schema_builder, { "a" }, 2, 0);
  AddHashBucketComponent(&schema_builder, { "b" }, 2, 0);
  PartitionSchema partition_schema;
  ASSERT_OK(PartitionSchema::FromPB(schema_builder, schema, &partition_schema));

  ASSERT_EQ("hash bucket components: [(bucket count: 2, columns: [a]), "
            "(bucket count: 2, columns: [b])], range columns: [a, b, c]",
            partition_schema.DebugString(schema));

  // Split Rows:
  //
  // { a: "a1", b: "b1", c: "c1" }
  // { b: "a2", b: "b2" }
  //
  // non-specified column values default to the logical minimum value ("").
  //
  // Encoded Partition Keys:
  //
  // [ (_, _,        _), (0, 0, "a1b1c1") )
  // [ (0, 0, "a1b1c1"), (0, 0,   "a2b2") )
  // [ (0, 0,   "a2b2"), (0, 1,        _) )
  //
  // [ (0, 1,        _), (0, 1, "a1b1c1") )
  // [ (0, 1, "a1b1c1"), (0, 1,   "a2b2") )
  // [ (0, 1,   "a2b2"), (1, _,        _) )
  //
  // [ (1, _,        _), (1, 0, "a1b1c1") )
  // [ (1, 0, "a1b1c1"), (1, 0,   "a2b2") )
  // [ (1, 0,   "a2b2"), (1, 1,        _) )
  //
  // [ (1, 1,        _), (1, 1, "a1b1c1") )
  // [ (1, 1, "a1b1c1"), (1, 1,   "a2b2") )
  // [ (1, 1,   "a2b2"), (_, _,        _) )
  //
  // _ signifies that the value is omitted from the encoded partition key.

  KuduPartialRow split_a(&schema);
  ASSERT_OK(split_a.SetStringCopy("a", "a1"));
  ASSERT_OK(split_a.SetStringCopy("b", "b1"));
  ASSERT_OK(split_a.SetStringCopy("c", "c1"));
  string partition_key_a;
  ASSERT_OK(partition_schema.EncodeKey(split_a, &partition_key_a));

  KuduPartialRow split_b(&schema);
  ASSERT_OK(split_b.SetStringCopy("a", "a2"));
  ASSERT_OK(split_b.SetStringCopy("b", "b2"));
  string partition_key_b;
  ASSERT_OK(partition_schema.EncodeKey(split_b, &partition_key_b));

  // Split keys need not be passed in sorted order.
  vector<KuduPartialRow> split_rows = { split_b, split_a };
  vector<Partition> partitions;
  ASSERT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));
  ASSERT_EQ(12, partitions.size());

  EXPECT_EQ(0, partitions[0].hash_buckets()[0]);
  EXPECT_EQ(0, partitions[0].hash_buckets()[1]);
  EXPECT_EQ(string("", 0), partitions[0].range_key_start());
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[0].range_key_end());
  EXPECT_EQ(string("", 0), partitions[0].partition_key_start());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\0" "a1\0\0b1\0\0c1", 18), partitions[0].partition_key_end());
  EXPECT_EQ("hash buckets: (0, 0), "
            "range: [(<start>), (string a=a1, string b=b1, string c=c1))",
            partition_schema.PartitionDebugString(partitions[0], schema));

  EXPECT_EQ(0, partitions[1].hash_buckets()[0]);
  EXPECT_EQ(0, partitions[1].hash_buckets()[1]);
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[1].range_key_start());
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[1].range_key_end());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\0" "a1\0\0b1\0\0c1", 18),
            partitions[1].partition_key_start());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\0" "a2\0\0b2\0\0", 16), partitions[1].partition_key_end());
  EXPECT_EQ("hash buckets: (0, 0), "
            "range: [(string a=a1, string b=b1, string c=c1), (string a=a2, string b=b2, <end>))",
            partition_schema.PartitionDebugString(partitions[1], schema));

  EXPECT_EQ(0, partitions[2].hash_buckets()[0]);
  EXPECT_EQ(0, partitions[2].hash_buckets()[1]);
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[2].range_key_start());
  EXPECT_EQ(string("", 0), partitions[2].range_key_end());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\0" "a2\0\0b2\0\0", 16), partitions[2].partition_key_start());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\1", 8), partitions[2].partition_key_end());
  EXPECT_EQ("hash buckets: (0, 0), "
            "range: [(string a=a2, string b=b2, <start>), (<end>))",
            partition_schema.PartitionDebugString(partitions[2], schema));

  EXPECT_EQ(0, partitions[3].hash_buckets()[0]);
  EXPECT_EQ(1, partitions[3].hash_buckets()[1]);
  EXPECT_EQ(string("", 0), partitions[3].range_key_start());
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[3].range_key_end());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\1", 8), partitions[3].partition_key_start());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\1" "a1\0\0b1\0\0c1", 18), partitions[3].partition_key_end());
  EXPECT_EQ("hash buckets: (0, 1), "
            "range: [(<start>), (string a=a1, string b=b1, string c=c1))",
            partition_schema.PartitionDebugString(partitions[3], schema));

  EXPECT_EQ(0, partitions[4].hash_buckets()[0]);
  EXPECT_EQ(1, partitions[4].hash_buckets()[1]);
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[4].range_key_start());
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[4].range_key_end());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\1" "a1\0\0b1\0\0c1", 18),
            partitions[4].partition_key_start());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\1" "a2\0\0b2\0\0", 16), partitions[4].partition_key_end());
  EXPECT_EQ("hash buckets: (0, 1), "
            "range: [(string a=a1, string b=b1, string c=c1), (string a=a2, string b=b2, <end>))",
            partition_schema.PartitionDebugString(partitions[4], schema));

  EXPECT_EQ(0, partitions[5].hash_buckets()[0]);
  EXPECT_EQ(1, partitions[5].hash_buckets()[1]);
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[5].range_key_start());
  EXPECT_EQ(string("", 0), partitions[5].range_key_end());
  EXPECT_EQ(string("\0\0\0\0" "\0\0\0\1" "a2\0\0b2\0\0", 16), partitions[5].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1", 4), partitions[5].partition_key_end());
  EXPECT_EQ("hash buckets: (0, 1), "
            "range: [(string a=a2, string b=b2, <start>), (<end>))",
            partition_schema.PartitionDebugString(partitions[5], schema));

  EXPECT_EQ(1, partitions[6].hash_buckets()[0]);
  EXPECT_EQ(0, partitions[6].hash_buckets()[1]);
  EXPECT_EQ(string("", 0), partitions[6].range_key_start());
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[6].range_key_end());
  EXPECT_EQ(string("\0\0\0\1", 4), partitions[6].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\0" "a1\0\0b1\0\0c1", 18), partitions[6].partition_key_end());
  EXPECT_EQ("hash buckets: (1, 0), "
            "range: [(<start>), (string a=a1, string b=b1, string c=c1))",
            partition_schema.PartitionDebugString(partitions[6], schema));

  EXPECT_EQ(1, partitions[7].hash_buckets()[0]);
  EXPECT_EQ(0, partitions[7].hash_buckets()[1]);
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[7].range_key_start());
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[7].range_key_end());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\0" "a1\0\0b1\0\0c1", 18),
            partitions[7].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\0" "a2\0\0b2\0\0", 16), partitions[7].partition_key_end());
  EXPECT_EQ("hash buckets: (1, 0), "
            "range: [(string a=a1, string b=b1, string c=c1), (string a=a2, string b=b2, <end>))",
            partition_schema.PartitionDebugString(partitions[7], schema));

  EXPECT_EQ(1, partitions[8].hash_buckets()[0]);
  EXPECT_EQ(0, partitions[8].hash_buckets()[1]);
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[8].range_key_start());
  EXPECT_EQ(string("", 0), partitions[8].range_key_end());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\0" "a2\0\0b2\0\0", 16), partitions[8].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\1", 8), partitions[8].partition_key_end());
  EXPECT_EQ("hash buckets: (1, 0), "
            "range: [(string a=a2, string b=b2, <start>), (<end>))",
            partition_schema.PartitionDebugString(partitions[8], schema));

  EXPECT_EQ(1, partitions[9].hash_buckets()[0]);
  EXPECT_EQ(1, partitions[9].hash_buckets()[1]);
  EXPECT_EQ(string("", 0), partitions[9].range_key_start());
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[9].range_key_end());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\1", 8), partitions[9].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\1" "a1\0\0b1\0\0c1", 18), partitions[9].partition_key_end());
  EXPECT_EQ("hash buckets: (1, 1), "
            "range: [(<start>), (string a=a1, string b=b1, string c=c1))",
            partition_schema.PartitionDebugString(partitions[9], schema));

  EXPECT_EQ(1, partitions[10].hash_buckets()[0]);
  EXPECT_EQ(1, partitions[10].hash_buckets()[1]);
  EXPECT_EQ(string("a1\0\0b1\0\0c1", 10), partitions[10].range_key_start());
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[10].range_key_end());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\1" "a1\0\0b1\0\0c1", 18),
            partitions[10].partition_key_start());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\1" "a2\0\0b2\0\0", 16), partitions[10].partition_key_end());
  EXPECT_EQ("hash buckets: (1, 1), "
            "range: [(string a=a1, string b=b1, string c=c1), (string a=a2, string b=b2, <end>))",
            partition_schema.PartitionDebugString(partitions[10], schema));

  EXPECT_EQ(1, partitions[11].hash_buckets()[0]);
  EXPECT_EQ(1, partitions[11].hash_buckets()[1]);
  EXPECT_EQ(string("a2\0\0b2\0\0", 8), partitions[11].range_key_start());
  EXPECT_EQ(string("", 0), partitions[11].range_key_end());
  EXPECT_EQ(string("\0\0\0\1" "\0\0\0\1" "a2\0\0b2\0\0", 16), partitions[11].partition_key_start());
  EXPECT_EQ(string("", 0), partitions[11].partition_key_end());
  EXPECT_EQ("hash buckets: (1, 1), "
            "range: [(string a=a2, string b=b2, <start>), (<end>))",
            partition_schema.PartitionDebugString(partitions[11], schema));
}

} // namespace kudu
