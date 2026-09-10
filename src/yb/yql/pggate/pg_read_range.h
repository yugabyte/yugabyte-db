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

#pragma once

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"

namespace yb::pggate {

//--------------------------------------------------------------------------------------------------
// PgReadRange is a helper class that represents a scanning range for read operations.
// It can take bounds in various formats, store them internally and apply them to a read request.
// The PgReadRange object is mutable, but it only allows to update the bounds to make them stricter.
// In other words, the lower bound can only be increased, and the upper bound can only be decreased.
// Hence if the object represents an empty range, it remains empty. Empty scan range means no
// execution is needed, and allows execution to take a fast path and skip many execution tasks.
//--------------------------------------------------------------------------------------------------
class PgReadRange {
 public:
  explicit PgReadRange(const PgTable& table) : table_(table) {}
  bool IsEmpty() const { return empty_; }
  bool Equals(const PgReadRange& other) const;
  bool Intersects(const PgReadRange& other) const;
  bool operator==(const PgReadRange& other) const {
    return Equals(other);
  }
  bool operator!=(const PgReadRange& other) const {
    return !Equals(other);
  }

  // The SetXXX methods update the range bounds. After the change the empty_ flag is updated, so
  // it is not required to compare bounds to check if the range is empty.
  // Set the hash code as the range bound.
  void SetHashCodeBound(uint16_t hash_code, bool is_inclusive, bool is_lower);
  // Set the doc key as the range bound.
  template <class T>
  void SetDocKeyBound(const T& doc_key, bool is_inclusive, bool is_lower);

  template <class Collection>
  Status SetHashAndRangeValuesBound(
      uint16_t hash, const Collection& values, bool is_inclusive, bool is_lower) {
    auto num_hash_key_columns = table_->schema().num_hash_key_columns();
    auto num_range_key_columns = table_->schema().num_range_key_columns();
    DCHECK_GT(num_hash_key_columns, 0);
    auto null_type = is_lower ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest;
    bool null_found = false;
    auto hashed_group = VERIFY_RESULT(MakeGroup(
        values, 0, num_hash_key_columns, null_type, &null_found));
    dockv::KeyBytes out;
    dockv::DocKeyEncoderAfterTableIdStep encoder =
        dockv::DocKeyEncoder(&out).Schema(table_->schema());
    auto after_hash = encoder.Hash(hash, hashed_group);
    auto range_group = VERIFY_RESULT(MakeGroup(
        values, num_hash_key_columns, num_range_key_columns, null_type, &null_found));
    after_hash.Range(range_group);
    SetBound(std::move(out), is_inclusive && !null_found, is_lower);
    return Status::OK();
  }

  template <class Collection>
  Status SetRangeValuesBound(const Collection& values, bool is_inclusive, bool is_lower) {
    auto num_range_key_columns = table_->schema().num_range_key_columns();
    DCHECK_EQ(table_->schema().num_hash_key_columns(), 0);
    auto null_type = is_lower ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest;
    bool null_found = false;
    auto range_group = VERIFY_RESULT(MakeGroup(
        values, 0, num_range_key_columns, null_type, &null_found));
    dockv::KeyBytes out;
    dockv::DocKeyEncoderAfterTableIdStep encoder =
        dockv::DocKeyEncoder(&out).Schema(table_->schema());
    encoder.NoHash().Range(range_group);
    SetBound(std::move(out), is_inclusive && !null_found, is_lower);
    return Status::OK();
  }

  // Set the partition's bounds as the range bounds.
  void SetPartitionBounds(size_t partition);
  // Set the request's bounds as the range bounds.
  void SetRequestBounds(const LWPgsqlReadRequestPB& req);

  // Update the bounds on the specified read request. If requests already has bounds, they are
  // updated if the respective new bound is stricter. If the resulting bounds represent an empty
  // range, the function returns false, and the execution can be skipped.
  bool ApplyBounds(LWPgsqlReadRequestPB& req) const;
  // Older (before GHI#28219) DocDB version treated bounds on the requests to the hash distributed
  // relations specially. They expected the bounds to be two-byte hash codes, and did not perform
  // any validations on them. The newer versions expect the bounds to be encoded doc keys, but
  // recognize the old format. Therefore, in "mixed" mode, when cluster nodes of different version
  // work side by side during rolling upgrade, we convert the bounds set with the SetHashCodeBound
  // method to be two-byte hash code, or error out if the bound is not derived from hash code.
  static Status ConvertBoundsToHashCode(LWPgsqlReadRequestPB& req);
 private:
  static Result<dockv::KeyEntryValue> AsKeyEntryValue(
      const LWQLValuePB* value, const PgColumn& column, const dockv::KeyEntryType& null_type,
      bool* null_found) {
    if (value == nullptr || yb::IsNull(*value)) {
      if (null_found) {
        *null_found = true;
      }
      return dockv::KeyEntryValue(null_type);
    }
    return dockv::KeyEntryValue::FromQLValuePB(*value, column.desc().sorting_type());
  }
  static Result<dockv::KeyEntryValue> AsKeyEntryValue(
      PgExpr* value, const PgColumn& column, const dockv::KeyEntryType& null_type,
      bool* null_found) {
    return AsKeyEntryValue(
        value ? VERIFY_RESULT(value->Eval()) : nullptr, column, null_type, null_found);
  }
  template <class Collection>
  Result<std::vector<dockv::KeyEntryValue>> MakeGroup(
      const Collection& values, size_t offset, size_t size,
      const dockv::KeyEntryType& null_type, bool* null_found) {
    std::vector<dockv::KeyEntryValue> group;
    group.reserve(size);
    const auto& columns = table_.columns();
    DCHECK_LE(offset + size, columns.size());
    for (size_t i = offset; i < offset + size; ++i) {
      if ((null_found && *null_found) || i >= columns.size() || i > values.size()) {
        break;
      }
      // Missing trailing values are allowed, they are equivalent to NULLs.
      group.emplace_back(VERIFY_RESULT(AsKeyEntryValue(
          i < values.size() ? values[i] : nullptr, columns[i], null_type, null_found)));
    }
    return group;
  }
  // This function makes sure that the different representations of a hash partition key are
  // treated as the same thing. The base representation of a partition key with hash code h is a
  // three bytes sequence [kUInt16Hash, h], where kUInt16Hash is as defined in value_type.h and h
  // is a 16-bit unsigned integer. Other representations are [kUInt16Hash, h, kLowest], also
  // referenced as h+0 is used to represent the lower bound of the partition, and
  // [kUInt16Hash, h-1, kHighest], also referenced as h-0 is used to represent the upper bound of
  // the previous partition.
  // All representations are binary comparable to each other and to the DocKeys. The
  // representations are not valid DocKeys, and no valid DocKey can be between h-0 and h and h+0.
  // The h-0 and h+0 can be parsed as special DocKeys.
  // The function returns the value of h if the bound is h-0 or h or h+0, nullopt otherwise.
  // The function is important for parallel query where the hash partition keys are used as
  // parallel range bounds. The value is stored and handled as the base representation, but when it
  // is sent down to DocDB, it has to be converted to either h-0 or h+0 as DocDB parses the bounds.
  // While this function makes some functionally equivalent bounds equal, it does not make all
  // functionally equivalent bounds equal. For example, partition key of the first partition is
  // empty. Empty key is functionally equivalent to the [kUInt16Hash, 0] key, but they are not
  // equal, moreover, empty key is not recognized as an encoded hash partition key by this function.
  static std::optional<uint16_t> DecodeEncodedHashPartitionKeyBound(Slice bound);
  dockv::KeyBytes HashCodeToBound(uint16_t hash_code, bool is_lower) const;
  // After bounds change, check if the range is empty and update the empty flag.
  void ComputeEmpty();
  // Set the new lower bound.
  void SetLowerBound(dockv::KeyBytes&& bound, bool is_inclusive);
  // Set the new upper bound.
  void SetUpperBound(dockv::KeyBytes&& bound, bool is_inclusive);
  void SetBound(dockv::KeyBytes&& bound, bool is_inclusive, bool is_lower) {
    if (is_lower) {
      SetLowerBound(std::move(bound), is_inclusive);
    } else {
      SetUpperBound(std::move(bound), is_inclusive);
    }
    ComputeEmpty();
  }
  // Check if boundaries set on the request define valid (not empty) range.
  static bool CheckScanBounds(const LWPgsqlReadRequestPB& req);
  // Set the lower bound on the request.
  void ApplyLowerBound(LWPgsqlReadRequestPB& req) const;
  // Set the upper bound on the request.
  void ApplyUpperBound(LWPgsqlReadRequestPB& req) const;
  const PgTable& table_;
  dockv::KeyBytes lower_bound_;
  bool lower_bound_is_inclusive_ = false;
  dockv::KeyBytes upper_bound_;
  bool upper_bound_is_inclusive_ = false;
  bool empty_ = false;
};

}  // namespace yb::pggate
