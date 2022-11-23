// Copyright (c) YugaByte, Inc.
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

#include <ostream>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/optional/optional.hpp>

#include "yb/common/constants.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/key_bytes.h"
#include "yb/docdb/primitive_value.h"

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/filter_policy.h"

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/uuid.h"

namespace yb {
namespace docdb {

// Whether to allow parts of the range component of a doc key that should not be present in stored
// doc key, but could be used during read, for instance kLowest and kHighest.
YB_STRONGLY_TYPED_BOOL(AllowSpecial)

class DocKeyBase {
 public:
  virtual ~DocKeyBase() = default;

  // Constructs an empty key with no hash component.
  DocKeyBase();

  // Construct a key with only a range component, but no hashed component.
  explicit DocKeyBase(std::vector<KeyEntryValue> range_components);

  // Construct a key including a hashed component and a range component. The hash value has
  // to be calculated outside of the constructor, and we're not assuming any specific hash function
  // here.
  // @param hash A hash value calculated using the appropriate hash function based on
  //             hashed_components.
  // @param hashed_components Components of the key that go into computing the hash prefix.
  // @param range_components Components of the key that we want to be able to do range scans on.
  DocKeyBase(
      DocKeyHash hash,
      std::vector<KeyEntryValue> hashed_components,
      std::vector<KeyEntryValue> range_components = std::vector<KeyEntryValue>());

  KeyBytes Encode() const;
  virtual void AppendTo(KeyBytes* out) const = 0;

  // Resets the state to an empty document key.
  void Clear();

  // Clear the range components of the document key only.
  void ClearRangeComponents();

  // Resize the range components:
  //  - drop elements (primitive values) from the end if new_size is smaller than the old size.
  //  - append default primitive values (kNullLow) if new_size is bigger than the old size.
  void ResizeRangeComponents(int new_size);

  DocKeyHash hash() const {
    return hash_;
  }

  const std::vector<KeyEntryValue>& hashed_group() const {
    return hashed_group_;
  }

  const std::vector<KeyEntryValue>& range_group() const {
    return range_group_;
  }

  std::vector<KeyEntryValue>& hashed_group() {
    return hashed_group_;
  }

  std::vector<KeyEntryValue>& range_group() {
    return range_group_;
  }

  // Check if it is an empty key.
  bool empty() const {
    return !hash_present_ && range_group_.empty();
  }

  bool HashedComponentsEqual(const DocKeyBase& other) const;

  void AddRangeComponent(const KeyEntryValue& val);

  void SetRangeComponent(const KeyEntryValue& val, int idx);

  void set_hash(DocKeyHash hash) {
    hash_ = hash;
    hash_present_ = true;
  }

  bool has_hash() const {
    return hash_present_;
  }

  // Decode just the hash code of a key.
  static Result<DocKeyHash> DecodeHash(const Slice& slice);

  static constexpr auto kNumValuesNoLimit = std::numeric_limits<int>::max();

  // Converts the document key to a human-readable representation.
  virtual std::string ToString(AutoDecodeKeys auto_decode_keys = AutoDecodeKeys::kFalse) const = 0;

 protected:
  // TODO: can we get rid of this field and just use !hashed_group_.empty() instead?
  bool hash_present_;

  DocKeyHash hash_;
  std::vector<KeyEntryValue> hashed_group_;
  std::vector<KeyEntryValue> range_group_;
};

template <class Collection>
void AppendDocKeyItems(const Collection& doc_key_items, KeyBytes* result) {
  for (const auto& item : doc_key_items) {
    item.AppendToKey(result);
  }
  result->AppendGroupEnd();
}

class DocKeyEncoderAfterHashStep {
 public:
  explicit DocKeyEncoderAfterHashStep(KeyBytes* out) : out_(out) {}

  template <class Collection>
  void Range(const Collection& range_group) {
    AppendDocKeyItems(range_group, out_);
  }

 private:
  KeyBytes* out_;
};

class DocKeyEncoderAfterTableIdStep {
 public:
  explicit DocKeyEncoderAfterTableIdStep(KeyBytes* out) : out_(out) {}

  template <class Collection>
  DocKeyEncoderAfterHashStep Hash(
      bool hash_present, uint16_t hash, const Collection& hashed_group) {
    if (!hash_present) {
      return DocKeyEncoderAfterHashStep(out_);
    }

    return Hash(hash, hashed_group);
  }

  template <class Collection>
  DocKeyEncoderAfterHashStep Hash(uint16_t hash, const Collection& hashed_group) {
    // We are not setting the "more items in group" bit on the hash field because it is not part
    // of "hashed" or "range" groups.
    AppendHash(hash, out_);
    AppendDocKeyItems(hashed_group, out_);

    return DocKeyEncoderAfterHashStep(out_);
  }

  template <class HashCollection, class RangeCollection>
  void HashAndRange(
      uint16_t hash, const HashCollection& hashed_group, const RangeCollection& range_collection) {
    Hash(hash, hashed_group).Range(range_collection);
  }

  void HashAndRange(
      uint16_t hash, const std::initializer_list<KeyEntryValue>& hashed_group,
      const std::initializer_list<KeyEntryValue>& range_collection) {
    Hash(hash, hashed_group).Range(range_collection);
  }

 private:
  KeyBytes* out_;
};

class DocKeyBaseDecoder {
 public:
  explicit DocKeyBaseDecoder(const Slice& input) : input_(input) {}

  virtual ~DocKeyBaseDecoder() {}

  Result<bool> HasPrimitiveValue(AllowSpecial allow_special = AllowSpecial::kFalse);

  Result<bool> DecodeHashCode(
      uint16_t* out = nullptr, AllowSpecial allow_special = AllowSpecial::kFalse);

  Result<bool> DecodeHashCode(AllowSpecial allow_special);

  Status DecodeKeyEntryValue(
      KeyEntryValue* out = nullptr, AllowSpecial allow_special = AllowSpecial::kFalse);

  Status DecodeKeyEntryValue(AllowSpecial allow_special);

  Status ConsumeGroupEnd();

  bool GroupEnded() const;

  const Slice& left_input() const {
    return input_;
  }

  size_t ConsumedSizeFrom(const uint8_t* start) const {
    return input_.data() - start;
  }

  Slice* mutable_input() {
    return &input_;
  }

  virtual Status DecodeToRangeGroup();

 protected:
  Slice input_;
};

// Consumes single primitive value from start of slice.
// Returns true when value was consumed, false when group end is found. The group end byte is
// consumed in the latter case.
Result<bool> ConsumePrimitiveValueFromKey(Slice* slice);

// Consume a group of document key components, ending with ValueType::kGroupEnd.
// @param slice - the current point at which we are decoding a key
// @param result - vector to append decoded values to.
Status ConsumePrimitiveValuesFromKey(Slice* slice, std::vector<KeyEntryValue>* result);

Status ConsumePrimitiveValuesFromKey(
    Slice* slice, AllowSpecial allow_special, boost::container::small_vector_base<Slice>* result,
    int n_values_limit = DocKeyBase::kNumValuesNoLimit);

Status ConsumePrimitiveValuesFromKey(
    Slice* slice, AllowSpecial allow_special, std::vector<KeyEntryValue>* result,
    int n_values_limit = DocKeyBase::kNumValuesNoLimit);

// Special string functions to handle auto_decode_keys.
void AppendVectorToString(
    std::string* dest, const std::vector<KeyEntryValue>& vec, AutoDecodeKeys auto_decode_keys);

void AppendVectorToStringWithBrackets(
    std::string* dest, const std::vector<KeyEntryValue>& vec,
    AutoDecodeKeys auto_decode_keys = AutoDecodeKeys::kFalse);

}  // namespace docdb
}  // namespace yb
