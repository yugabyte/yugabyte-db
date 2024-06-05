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
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <boost/optional/optional.hpp>

#include <google/protobuf/repeated_field.h>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/dockv/partial_row.h"

#include "yb/util/enums.h"
#include "yb/util/status_fwd.h"
#include "yb/util/memory/arena_fwd.h"
#include "yb/util/memory/arena_list.h"
#include "yb/util/yb_partition.h"

namespace yb::dockv {

YB_DEFINE_ENUM(YBHashSchema,
               ((kMultiColumnHash, 1))
               ((kRedisHash, 2))
               ((kPgsqlHash, 3)));

// A Partition describes the set of rows that a Tablet is responsible for
// serving. Each tablet is assigned a single Partition.
//
// Partitions consist primarily of a start and end partition key. Every row with
// a partition key that falls in a Tablet's Partition will be served by that
// tablet.
//
// In addition to the start and end partition keys, a Partition holds metadata
// to determine if a scan can prune, or skip, a partition based on the scan's
// start and end primary keys, and predicates.
class Partition {
 public:

  const std::vector<int32_t>& hash_buckets() const {
    return hash_buckets_;
  }

  Slice range_key_start() const;

  Slice range_key_end() const;

  const std::string& partition_key_start() const {
    return partition_key_start_;
  }

  const std::string& partition_key_end() const {
    return partition_key_end_;
  }

  void set_partition_key_start(const std::string& partition_key_start) {
    partition_key_start_ = partition_key_start;
  }

  void set_partition_key_end(const std::string& partition_key_end) {
    partition_key_end_ = partition_key_end;
  }

  // Serializes a partition into a protobuf message.
  void ToPB(PartitionPB* pb) const;

  // Deserializes a protobuf message into a partition.
  //
  // The protobuf message is not validated, since partitions are only expected
  // to be created by the master process.
  static void FromPB(const PartitionPB& pb, Partition* partition);

  bool ContainsKey(const std::string& partition_key) const {
    return partition_key >= partition_key_start() &&
        (partition_key_end().empty() || partition_key < partition_key_end());
  }

  // Checks if this partition is a superset or is exactly the same as another partition.
  template <class T>
  bool ContainsPartition(const T& other) const {
    return other.partition_key_start() >= partition_key_start() &&
           (partition_key_end().empty() || (!other.partition_key_end().empty() &&
                                            other.partition_key_end() <= partition_key_end()));
  }

  // Checks if this and another partitions have equal bounds.
  template <class T>
  bool BoundsEqualToPartition(const T& other) const {
    return partition_key_start() == other.partition_key_start() &&
           partition_key_end() == other.partition_key_end();
  }

  // Checks if this partition is a strict superset of another partition (shouldn't be exactly the
  // same).
  template <class T>
  bool ContainsPartitionStrict(const T& other) const {
    return ContainsPartition(other) && !BoundsEqualToPartition(other);
  }

  std::string ToString() const;

 private:
  friend class PartitionSchema;

  // Helper function for accessing the range key portion of a partition key.
  Slice range_key(const std::string& partition_key) const;

  std::vector<int32_t> hash_buckets_;

  std::string partition_key_start_;
  std::string partition_key_end_;
};

// A partition schema describes how the rows of a table are distributed among
// tablets.
//
// Primarily, a table's partition schema is responsible for translating the
// primary key column values of a row into a partition key that can be used to
// determine the tablet containing the key.
//
// The partition schema is made up of zero or more hash bucket components,
// followed by a single range component.
//
// Each hash bucket component includes one or more columns from the primary key
// column set, with the restriction that an individual primary key column may
// only be included in a single hash component.
//
// To determine the hash bucket of an individual row, the values of the columns
// of the hash component are encoded into bytes (in PK or lexicographic
// preserving encoding), then hashed into a u64, then modded into an i32. When
// constructing a partition key from a row, the buckets of the row are simply
// encoded into the partition key in order (again in PK or lexicographic
// preserving encoding).
//
// The range component contains a (possibly full or empty) subset of the primary
// key columns. When encoding the partition key, the columns of the partition
// component are encoded in order.
//
// The above is true of the relationship between rows and partition keys. It
// gets trickier with partitions (tablet partition key boundaries), because the
// boundaries of tablets do not necessarily align to rows. For instance,
// currently the absolute-start and absolute-end primary keys of a table
// represented as an empty key, but do not have a corresponding row. Partitions
// are similar, but instead of having just one absolute-start and absolute-end,
// each component of a partition schema has an absolute-start and absolute-end.
// When creating the initial set of partitions during table creation, we deal
// with this by "carrying through" absolute-start or absolute-ends into lower
// significance components.
class PartitionSchema {
 public:

  struct RangeSplit {
    explicit RangeSplit(const std::string& bounds) : column_bounds(bounds) {}

    std::string column_bounds;

    friend bool operator==(const RangeSplit&, const RangeSplit&) = default;
  };

  struct RangeSchema {
    std::vector<ColumnId> column_ids;
    std::vector<RangeSplit> splits;

    friend bool operator==(const RangeSchema&, const RangeSchema&) = default;

    size_t DynamicMemoryUsage() const {
      size_t size = sizeof(*this);
      for (auto& split : splits) {
        size += split.column_bounds.size();
      }
      size += (column_ids.size() * sizeof(ColumnId));
      return size;
    }
  };

  static constexpr int32_t kPartitionKeySize = 2;
  static constexpr uint16_t kMaxPartitionKey = std::numeric_limits<uint16_t>::max();

  // Deserializes a protobuf message into a partition schema.
  static Status FromPB(const PartitionSchemaPB& pb,
                       const Schema& schema,
                       PartitionSchema* partition_schema);

  static bool IsHashPartitioning(const PartitionSchemaPB& pb);

  // Serializes a partition schema into a protobuf message.
  void ToPB(PartitionSchemaPB* pb) const;

  Status EncodeRedisKey(const YBPartialRow& row, std::string* buf) const;
  Status EncodeRedisKey(const ConstContiguousRow& row, std::string* buf) const;
  Status EncodeRedisKey(const Slice& slice, std::string* buf) const;

  Status EncodeKey(const google::protobuf::RepeatedPtrField<QLExpressionPB>& hash_values,
                   std::string* buf) const;

  template <class Collection>
  Result<uint16_t> PgsqlHashColumnCompoundValue(const Collection& hash_values) const {
    SCHECK(hash_schema_ && *hash_schema_ == YBHashSchema::kPgsqlHash,
           InvalidArgument,
           "Unexpected hash schema in PgsqlHashColumnCompoundValue");
    std::string tmp;
    for (const auto& value : hash_values) {
      ProcessHashKeyEntry(value, &tmp);
    }
    return YBPartition::HashColumnCompoundValue(tmp);
  }

  template <class Collection>
  Result<std::string> EncodePgsqlHash(const Collection& hash_values) const {
    if (!hash_schema_) {
      return std::string();
    }
    auto hash_value = VERIFY_RESULT(PgsqlHashColumnCompoundValue(hash_values));
    return EncodeMultiColumnHashValue(hash_value);
  }

  // Appends the row's encoded partition key into the provided buffer.
  // On failure, the buffer may have data partially appended.
  Status EncodeKey(const YBPartialRow& row, std::string* buf) const;

  // Appends the row's encoded partition key into the provided buffer.
  // On failure, the buffer may have data partially appended.
  Status EncodeKey(const ConstContiguousRow& row, std::string* buf) const;

  bool IsHashPartitioning() const;

  YBHashSchema hash_schema() const;

  const RangeSchema& range_schema() const;

  bool IsRangePartitioning() const {
    return range_schema_.column_ids.size() > 0;
  }

  // Encodes the given uint16 value into a 2 byte string.
  static std::string EncodeMultiColumnHashValue(uint16_t hash_value);

  // Decode the given partition_key to a 2-byte integer.
  static uint16_t DecodeMultiColumnHashValue(Slice partition_key);

  // Decode the given partition inclusive left bound to a 2-byte inclusive left bound integer.
  static uint16_t DecodeMultiColumnHashLeftBound(Slice partition_key);

  // Decode the given partition exclusive right bound to a 2-byte inclusive right bound integer.
  static uint16_t DecodeMultiColumnHashRightBound(Slice partition_key);

  // Does [partition_key_start, partition_key_end] form a valid range.
  static Status IsValidHashPartitionRange(const std::string& partition_key_start,
                                          const std::string& partition_key_end);

  static bool IsValidHashPartitionKeyBound(const std::string& partition_key);

  // Returns the lexicographically ordered middle key between two key bounds.
  static Result<std::string> GetLexicographicMiddleKey(
      const std::string& key_start, const std::string& key_end);

  // Get if there is overlap between two key ranges. This can be done without decoding, by just
  // performing string comparisons on keys.
  static bool HasOverlap(
      const std::string& key_start,
      const std::string& key_end,
      const std::string& other_key_start,
      const std::string& other_key_end);

  template <class T>
  static void ProcessHashKeyEntry(const T* value_pb, std::string* out) {
    if (value_pb) {
      ProcessHashKeyEntry(*value_pb, out);
    }
  }

  static void ProcessHashKeyEntry(const LWQLValuePB& value_pb, std::string* out);
  static void ProcessHashKeyEntry(const LWPgsqlExpressionPB& expr, std::string* out);
  static void ProcessHashKeyEntry(const PgsqlExpressionPB& expr, std::string* out);

  // For range-based sharded tables, encoded partition key is the same as partition_key.
  // For hash-based sharded tables, encoded partition key consists of KeyEntryType::kUInt16Hash
  // prefix followed by partition_key.
  //
  // Encoded (sub)doc keys that belong to partition are starting with encoded partition_key_start
  // or greater than it and lower than encoded partition_key_end.
  //
  // If partition_key is empty, encoded partition key is also empty and that means corresponding
  // partition boundary is absent (tablet is the first/last in table key range).
  Result<std::string> GetEncodedPartitionKey(const std::string& partition_key);
  static Result<std::string> GetEncodedPartitionKey(
    const std::string& partition_key, const PartitionSchemaPB& partition_schema);

  // Returns inclusive min and max hash code for the partition.
  static std::pair<uint16_t, uint16_t> GetHashPartitionBounds(const Partition& partition);

  // YugaByte partition creation
  // Creates the set of table partitions using multi column hash schema. In this schema, we divide
  // the [0, max_partition_key] range into the requested number of intervals.
  // - Inputs are from SQL and CQL.
  // - YugaByte methods are used for hash and range partitioning to create tablets.
  // 'num_tablets' is just a recommendation for the target number of partitions. The final
  // partitions are in the result variable 'partitions', so the final number of tablets is
  // 'partitions.size()' value.
  //
  // TODO(neil) Investigate partitions to support both hash and range schema.
  // - First, use range schema to split the table.
  // - Second, use hash schema to partition each split.
  Status CreatePartitions(int32_t num_tablets, std::vector<Partition>* partitions) const;

  // Kudu partition creation
  // NOTE: The following function from Kudu is to support a C++ API instead of SQL or CQL. They
  // also create partitions differently. There are code in this function that shouldn't be apply
  // to YugaByte's database except for metadata in master, which is using Kudu's DB.
  //
  // Creates the set of table partitions for a partition schema and collection
  // of split rows.
  //
  // The number of resulting partitions is the product of the number of hash
  // buckets for each hash bucket component, multiplied by
  // (split_rows.size() + 1).
  Status CreatePartitions(const std::vector<YBPartialRow>& split_rows,
                          const Schema& schema,
                          std::vector<Partition>* partitions) const;

  // Tests if the partition contains the row.
  Status PartitionContainsRow(const Partition& partition,
                              const YBPartialRow& row,
                              bool* contains) const;

  // Tests if the partition contains the row.
  Status PartitionContainsRow(const Partition& partition,
                              const ConstContiguousRow& row,
                              bool* contains) const;

  // Returns a text description of the partition suitable for debug printing.
  std::string PartitionDebugString(const Partition& partition, const Schema& schema) const;

  // Returns a text description of a range partition suitable for debug printing.
  std::string RangePartitionDebugString(const Partition& partition, const Schema& schema) const;

  // Returns a text description of the partial row's partition key suitable for debug printing.
  std::string RowDebugString(const YBPartialRow& row) const;

  // Returns a text description of the row's partition key suitable for debug printing.
  std::string RowDebugString(const ConstContiguousRow& row) const;

  // Returns a text description of the encoded partition key suitable for debug printing.
  std::string PartitionKeyDebugString(const std::string& key, const Schema& schema) const;

  // Returns a text description of this partition schema suitable for debug printing.
  std::string DebugString(const Schema& schema) const;

  // Returns true if the other partition schema is equivalent to this one.
  bool Equals(const PartitionSchema& other) const;

  // Return true if the partitioning scheme simply range-partitions on the full primary key,
  // with no bucketing components, etc.
  bool IsSimplePKRangePartitioning(const Schema& schema) const;

  // Returns two hash-partitions covering the range of the passed in hash-partiion, or
  // std::nullopt if the passed in partition covers only one value.
  // This does not attempt to split partition evenly based on tablet data, and is only suitable
  // for tablets of the transaction status table, which have no data.
  static boost::optional<std::pair<Partition, Partition>> SplitHashPartitionForStatusTablet(
      const Partition& partition);

  size_t DynamicMemoryUsage() const {
    size_t size = sizeof(*this) + range_schema_.DynamicMemoryUsage();
    for (auto& hash_bucket_schema : hash_bucket_schemas_) {
      size += hash_bucket_schema.DynamicMemoryUsage();
    }
    if (hash_schema_) {
      size += sizeof(hash_schema_);
    }
    return size;
  }

 private:

  struct HashBucketSchema {
    std::vector<ColumnId> column_ids;
    int32_t num_buckets;
    uint32_t seed;

    friend bool operator==(const HashBucketSchema&, const HashBucketSchema&) = default;

    size_t DynamicMemoryUsage() const {
      return sizeof(*this) + (column_ids.size() * sizeof(ColumnId));
    }
  };

  // Convertion between PB and partition schema.
  static Status KuduFromPB(const PartitionSchemaPB& pb,
                           const Schema& schema,
                           PartitionSchema* partition_schema);
  void KuduToPB(PartitionSchemaPB* pb) const;

  // Creates the set of table partitions using multi column hash schema. In this schema, we divide
  // the [ hash(0), hash(max_partition_key) ] range equally into the requested number of intervals.
  Status CreateHashPartitions(int32_t num_tablets,
                              std::vector<Partition>* partitions,
                              uint16_t max_partition_key = kMaxPartitionKey) const;

  // Creates the set of table partitions using primary-key range schema. In this schema, we divide
  // the table by given ranges in the partitions vector.
  Status CreateRangePartitions(std::vector<Partition>* partitions) const;

  // Encodes the specified columns of a row into lexicographic sort-order preserving format.
  static Status EncodeColumns(const YBPartialRow& row,
                              const std::vector<ColumnId>& column_ids,
                              std::string* buf);

  // Encodes the specified columns of a row into lexicographic sort-order preserving format.
  static Status EncodeColumns(const ConstContiguousRow& row,
                              const std::vector<ColumnId>& column_ids,
                              std::string* buf);

  // Hashes a compound string of all columns into a 16-bit integer.
  static uint16_t HashColumnCompoundValue(std::string_view compound);

  // Encodes the specified columns of a row into 2-byte partition key using the multi column
  // hashing scheme.
  static Status EncodeColumns(const YBPartialRow& row, std::string* buf);

  // Encodes the specified columns of a row into 2-byte partition key using the multi column
  // hashing scheme.
  static Status EncodeColumns(const ConstContiguousRow& row, std::string* buf);

  // Assigns the row to a hash bucket according to the hash schema.
  template<typename Row>
  static Status BucketForRow(const Row& row,
                             const HashBucketSchema& hash_bucket_schema,
                             int32_t* bucket);

  // Private templated helper for PartitionContainsRow.
  template<typename Row>
  Status PartitionContainsRowImpl(const Partition& partition,
                                  const Row& row,
                                  bool* contains) const;

  // Appends the stringified range partition components of a partial row to a
  // vector.
  //
  // If any columns of the range partition do not exist in the partial row,
  // processing stops and the provided default string piece is appended to the vector.
  void AppendRangeDebugStringComponentsOrString(const YBPartialRow& row,
                                                GStringPiece default_string,
                                                std::vector<std::string>* components) const;

  // Appends the stringified range partition components of a partial row to a
  // vector.
  //
  // If any columns of the range partition do not exist in the partial row, the
  // logical minimum value for that column will be used instead.
  void AppendRangeDebugStringComponentsOrMin(const YBPartialRow& row,
                                             std::vector<std::string>* components) const;

  // Decodes a range partition key into a partial row, with variable-length
  // fields stored in the arena.
  Status DecodeRangeKey(Slice* encode_key,
                        YBPartialRow* partial_row,
                        Arena* arena) const;

  // Decodes the hash bucket component of a partition key into its buckets.
  //
  // This should only be called with partition keys created from a row, not with
  // partition keys from a partition.
  Status DecodeHashBuckets(Slice* partition_key, std::vector<int32_t>* buckets) const;

  // Clears the state of this partition schema.
  void Clear();

  // Validates that this partition schema is valid. Returns OK, or an
  // appropriate error code for an invalid partition schema.
  Status Validate(const Schema& schema) const;

  std::vector<HashBucketSchema> hash_bucket_schemas_;
  RangeSchema range_schema_;
  boost::optional<YBHashSchema> hash_schema_; // Defined only for table that is hash-partitioned.
};

}  // namespace yb::dockv
