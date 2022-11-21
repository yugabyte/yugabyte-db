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

#include "yb/docdb/doc_key_base.h"

#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/util/compare_util.h"

using strings::Substitute;

namespace yb {
namespace docdb {

namespace {

// Checks whether slice starts with primitive value.
// Valid cases are end of group or primitive value starting with value type.
Result<bool> HasPrimitiveValue(Slice* slice, AllowSpecial allow_special) {
  if (PREDICT_FALSE(slice->empty())) {
    return STATUS(Corruption, "Unexpected end of key when decoding document key");
  }
  KeyEntryType current_value_type = static_cast<KeyEntryType>(*slice->data());
  if (current_value_type == KeyEntryType::kGroupEnd) {
    slice->consume_byte();
    return false;
  }

  if (allow_special || !IsSpecialKeyEntryType(current_value_type)) {
    return true;
  }

  return STATUS_FORMAT(Corruption, "Expected a primitive value type, got $0", current_value_type);
}

// Consumes up to n_values_limit primitive values from key until group end is found.
// Callback is called for each value and responsible for consuming this single value from slice.
template <class Callback>
Status ConsumePrimitiveValuesFromKeyWithCallback(
    Slice* slice, AllowSpecial allow_special, Callback callback,
    int n_values_limit = DocKeyBase::kNumValuesNoLimit) {
  const auto initial_slice(*slice);  // For error reporting.
  for (; n_values_limit > 0; --n_values_limit) {
    if (!VERIFY_RESULT(HasPrimitiveValue(slice, allow_special))) {
      return Status::OK();
    }

    RETURN_NOT_OK_PREPEND(
        callback(),
        Substitute("while consuming primitive values from $0", initial_slice.ToDebugHexString()));
  }
  return Status::OK();
}

}  // namespace

Status ConsumePrimitiveValuesFromKey(
    Slice* slice, AllowSpecial allow_special, boost::container::small_vector_base<Slice>* result,
    int n_values_limit) {
  return ConsumePrimitiveValuesFromKeyWithCallback(
      slice, allow_special,
      [slice, result]() -> Status {
        auto begin = slice->data();
        RETURN_NOT_OK(KeyEntryValue::DecodeKey(slice, /* out */ nullptr));
        if (result) {
          result->emplace_back(begin, slice->data());
        }
        return Status::OK();
      },
      n_values_limit);
}

Status ConsumePrimitiveValuesFromKey(
    Slice* slice, AllowSpecial allow_special, std::vector<KeyEntryValue>* result,
    int n_values_limit) {
  return ConsumePrimitiveValuesFromKeyWithCallback(
      slice, allow_special,
      [slice, result] {
        result->emplace_back();
        return result->back().DecodeFromKey(slice);
      },
      n_values_limit);
}

Result<bool> ConsumePrimitiveValueFromKey(Slice* slice) {
  if (!VERIFY_RESULT(HasPrimitiveValue(slice, AllowSpecial::kFalse))) {
    return false;
  }
  RETURN_NOT_OK(KeyEntryValue::DecodeKey(slice, nullptr /* out */));
  return true;
}

Status ConsumePrimitiveValuesFromKey(Slice* slice, std::vector<KeyEntryValue>* result) {
  return ConsumePrimitiveValuesFromKey(slice, AllowSpecial::kFalse, result);
}

// We need a special implementation of converting a vector to string because we need to pass the
// auto_decode_keys flag to PrimitiveValue::ToString.
void AppendVectorToString(
    std::string* dest, const std::vector<KeyEntryValue>& vec, AutoDecodeKeys auto_decode_keys) {
  bool need_comma = false;
  for (const auto& pv : vec) {
    if (need_comma) {
      dest->append(", ");
    }
    need_comma = true;
    dest->append(pv.ToString(auto_decode_keys));
  }
}

void AppendVectorToStringWithBrackets(
    std::string* dest, const std::vector<KeyEntryValue>& vec, AutoDecodeKeys auto_decode_keys) {
  dest->push_back('[');
  AppendVectorToString(dest, vec, auto_decode_keys);
  dest->push_back(']');
}

// ------------------------------------------------------------------------------------------------
// DocKeyBase
// ------------------------------------------------------------------------------------------------

DocKeyBase::DocKeyBase() : hash_present_(false), hash_(0) {}

DocKeyBase::DocKeyBase(std::vector<KeyEntryValue> range_components)
    : hash_present_(false), hash_(0), range_group_(std::move(range_components)) {}

DocKeyBase::DocKeyBase(
    DocKeyHash hash,
    std::vector<KeyEntryValue> hashed_components,
    std::vector<KeyEntryValue> range_components)
    : hash_present_(true),
      hash_(hash),
      hashed_group_(std::move(hashed_components)),
      range_group_(std::move(range_components)) {}

KeyBytes DocKeyBase::Encode() const {
  KeyBytes result;
  AppendTo(&result);
  return result;
}

void DocKeyBase::Clear() {
  hash_present_ = false;
  hash_ = 0xdead;
  hashed_group_.clear();
  range_group_.clear();
}

void DocKeyBase::ClearRangeComponents() {
  range_group_.clear();
}

void DocKeyBase::ResizeRangeComponents(int new_size) {
  range_group_.resize(new_size);
}

bool DocKeyBase::HashedComponentsEqual(const DocKeyBase& other) const {
  return hash_present_ == other.hash_present_ &&
         // Only compare hashes and hashed groups if the hash presence flag is set.
         (!hash_present_ || (hash_ == other.hash_ && hashed_group_ == other.hashed_group_));
}

void DocKeyBase::AddRangeComponent(const KeyEntryValue& val) {
  range_group_.push_back(val);
}

void DocKeyBase::SetRangeComponent(const KeyEntryValue& val, int idx) {
  DCHECK_LT(idx, range_group_.size());
  range_group_[idx] = val;
}

Result<DocKeyHash> DocKeyBase::DecodeHash(const Slice& slice) {
  DocKeyBaseDecoder decoder(slice);
  uint16_t hash;
  RETURN_NOT_OK(decoder.DecodeHashCode(&hash));
  return hash;
}

// ------------------------------------------------------------------------------------------------
// DocKeyBaseDecoder
// ------------------------------------------------------------------------------------------------

Result<bool> DocKeyBaseDecoder::DecodeHashCode(uint16_t* out, AllowSpecial allow_special) {
  if (input_.empty()) {
    return false;
  }

  auto first_value_type = static_cast<KeyEntryType>(input_[0]);

  if (first_value_type == KeyEntryType::kGroupEnd) {
    return false;
  }

  auto good_value_type = allow_special || !IsSpecialKeyEntryType(first_value_type);
  if (!good_value_type) {
    return STATUS_FORMAT(
        Corruption,
        "Expected first value type to be primitive or GroupEnd, got $0 in $1",
        first_value_type, input_.ToDebugHexString());
  }

  if (input_.empty() || input_[0] != KeyEntryTypeAsChar::kUInt16Hash) {
    return false;
  }

  if (input_.size() < sizeof(DocKeyHash) + 1) {
    return STATUS_FORMAT(
        Corruption,
        "Could not decode a 16-bit hash component of a document key: only $0 bytes left",
        input_.size());
  }

  // We'll need to update this code if we ever change the size of the hash field.
  static_assert(
      sizeof(DocKeyHash) == sizeof(uint16_t),
      "It looks like the DocKeyHash's size has changed -- need to update encoder/decoder.");
  if (out) {
    *out = BigEndian::Load16(input_.data() + 1);
  }
  input_.remove_prefix(sizeof(DocKeyHash) + 1);
  return true;
}

Status DocKeyBaseDecoder::DecodeKeyEntryValue(AllowSpecial allow_special) {
  return DecodeKeyEntryValue(nullptr /* out */, allow_special);
}

Status DocKeyBaseDecoder::DecodeKeyEntryValue(KeyEntryValue* out, AllowSpecial allow_special) {
  if (allow_special && !input_.empty() &&
      (input_[0] == KeyEntryTypeAsChar::kLowest || input_[0] == KeyEntryTypeAsChar::kHighest)) {
    input_.consume_byte();
    return Status::OK();
  }
  return KeyEntryValue::DecodeKey(&input_, out);
}

Status DocKeyBaseDecoder::ConsumeGroupEnd() {
  if (input_.empty() || input_[0] != KeyEntryTypeAsChar::kGroupEnd) {
    return STATUS_FORMAT(Corruption, "Group end expected but $0 found", input_.ToDebugHexString());
  }
  input_.consume_byte();
  return Status::OK();
}

bool DocKeyBaseDecoder::GroupEnded() const {
  return input_.empty() || input_[0] == KeyEntryTypeAsChar::kGroupEnd;
}

Result<bool> DocKeyBaseDecoder::HasPrimitiveValue(AllowSpecial allow_special) {
  return docdb::HasPrimitiveValue(&input_, allow_special);
}

Status DocKeyBaseDecoder::DecodeToRangeGroup() {
  if (VERIFY_RESULT(DecodeHashCode())) {
    while (VERIFY_RESULT(HasPrimitiveValue())) {
      RETURN_NOT_OK(DecodeKeyEntryValue());
    }
  }

  return Status::OK();
}

Result<bool> DocKeyBaseDecoder::DecodeHashCode(AllowSpecial allow_special) {
  return DecodeHashCode(nullptr /* out */, allow_special);
}

}  // namespace docdb
}  // namespace yb
