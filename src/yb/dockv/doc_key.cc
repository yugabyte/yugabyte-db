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

#include "yb/dockv/doc_key.h"

#include <memory>
#include <sstream>

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/compare_util.h"
#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/tostring.h"
#include "yb/util/uuid.h"

using std::ostringstream;
using std::string;

using strings::Substitute;

using yb::util::CompareVectors;
using yb::util::CompareUsingLessThan;

namespace yb::dockv {

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

constexpr auto kNumValuesNoLimit = std::numeric_limits<int>::max();

// Consumes up to n_values_limit primitive values from key until group end is found.
// Callback is called for each value and responsible for consuming this single value from slice.
template<class Callback>
Status ConsumePrimitiveValuesFromKey(
    Slice* slice, AllowSpecial allow_special, Callback callback,
    int n_values_limit = kNumValuesNoLimit) {
  const auto initial_slice(*slice);  // For error reporting.
  for (; n_values_limit > 0; --n_values_limit) {
    if (!VERIFY_RESULT(HasPrimitiveValue(slice, allow_special))) {
      return Status::OK();
    }

    RETURN_NOT_OK_PREPEND(callback(),
        Substitute("while consuming primitive values from $0",
                   initial_slice.ToDebugHexString()));
  }
  return Status::OK();
}

Status ConsumePrimitiveValuesFromKey(Slice* slice, AllowSpecial allow_special,
                                     boost::container::small_vector_base<Slice>* result,
                                     int n_values_limit = kNumValuesNoLimit) {
  return ConsumePrimitiveValuesFromKey(slice, allow_special, [slice, result]() -> Status {
    auto begin = slice->data();
    RETURN_NOT_OK(KeyEntryValue::DecodeKey(slice, /* out */ nullptr));
    if (result) {
      result->emplace_back(begin, slice->data());
    }
    return Status::OK();
  }, n_values_limit);
}

Status ConsumePrimitiveValuesFromKey(
    Slice* slice, AllowSpecial allow_special, KeyEntryValues* result,
    int n_values_limit = kNumValuesNoLimit) {
  return ConsumePrimitiveValuesFromKey(slice, allow_special, [slice, result] {
    result->emplace_back();
    return result->back().DecodeFromKey(slice);
  }, n_values_limit);
}

} // namespace

Result<bool> ConsumePrimitiveValueFromKey(Slice* slice) {
  if (!VERIFY_RESULT(HasPrimitiveValue(slice, AllowSpecial::kFalse))) {
    return false;
  }
  RETURN_NOT_OK(KeyEntryValue::DecodeKey(slice, nullptr /* out */));
  return true;
}

Status ConsumePrimitiveValuesFromKey(Slice* slice, KeyEntryValues* result) {
  return ConsumePrimitiveValuesFromKey(slice, AllowSpecial::kFalse, result);
}

// ------------------------------------------------------------------------------------------------
// DocKey
// ------------------------------------------------------------------------------------------------

DocKey::DocKey()
    : cotable_id_(Uuid::Nil()),
      colocation_id_(kColocationIdNotSet),
      hash_present_(false),
      hash_(0) {
}

DocKey::DocKey(KeyEntryValues range_components)
    : cotable_id_(Uuid::Nil()),
      colocation_id_(kColocationIdNotSet),
      hash_present_(false),
      hash_(0),
      range_group_(std::move(range_components)) {
}

DocKey::DocKey(DocKeyHash hash,
               KeyEntryValues hashed_components,
               KeyEntryValues range_components)
    : cotable_id_(Uuid::Nil()),
      colocation_id_(kColocationIdNotSet),
      hash_present_(true),
      hash_(hash),
      hashed_group_(std::move(hashed_components)),
      range_group_(std::move(range_components)) {
}

DocKey::DocKey(const Uuid& cotable_id,
               DocKeyHash hash,
               KeyEntryValues hashed_components,
               KeyEntryValues range_components)
    : cotable_id_(cotable_id),
      colocation_id_(kColocationIdNotSet),
      hash_present_(true),
      hash_(hash),
      hashed_group_(std::move(hashed_components)),
      range_group_(std::move(range_components)) {
}

DocKey::DocKey(const ColocationId colocation_id,
               DocKeyHash hash,
               KeyEntryValues hashed_components,
               KeyEntryValues range_components)
    : cotable_id_(Uuid::Nil()),
      colocation_id_(colocation_id),
      hash_present_(true),
      hash_(hash),
      hashed_group_(std::move(hashed_components)),
      range_group_(std::move(range_components)) {
}

DocKey::DocKey(const Uuid& cotable_id)
    : cotable_id_(cotable_id),
      colocation_id_(kColocationIdNotSet),
      hash_present_(false),
      hash_(0) {
}

DocKey::DocKey(const ColocationId colocation_id)
    : cotable_id_(Uuid::Nil()),
      colocation_id_(colocation_id),
      hash_present_(false),
      hash_(0) {
}

DocKey::DocKey(const Schema& schema)
    : cotable_id_(schema.cotable_id()),
      colocation_id_(schema.colocation_id()),
      hash_present_(false),
      hash_(0) {
}

DocKey::DocKey(const Schema& schema, DocKeyHash hash)
    : cotable_id_(schema.cotable_id()),
      colocation_id_(schema.colocation_id()),
      hash_present_(true),
      hash_(hash) {
}

DocKey::DocKey(const Schema& schema, KeyEntryValues range_components)
    : cotable_id_(schema.cotable_id()),
      colocation_id_(schema.colocation_id()),
      hash_present_(false),
      hash_(0),
      range_group_(std::move(range_components)) {
}

DocKey::DocKey(const Schema& schema, DocKeyHash hash,
               KeyEntryValues hashed_components,
               KeyEntryValues range_components)
    : cotable_id_(schema.cotable_id()),
      colocation_id_(schema.colocation_id()),
      hash_present_(true),
      hash_(hash),
      hashed_group_(std::move(hashed_components)),
      range_group_(std::move(range_components)) {
}

KeyBytes DocKey::Encode() const {
  KeyBytes result;
  AppendTo(&result);
  return result;
}

namespace {

// Used as cache of allocated memory by EncodeAsRefCntPrefix.
thread_local std::optional<KeyBytes> thread_local_encode_buffer;

}

RefCntPrefix DocKey::EncodeAsRefCntPrefix(Slice suffix) const {
  if (!thread_local_encode_buffer) {
    thread_local_encode_buffer.emplace();
  }
  auto& encode_buffer = *thread_local_encode_buffer;
  encode_buffer.Clear();
  AppendTo(&encode_buffer);
  encode_buffer.AppendRawBytes(suffix);
  return RefCntPrefix(encode_buffer.AsSlice());
}

void DocKey::AppendTo(KeyBytes* out) const {
  auto encoder = DocKeyEncoder(out);
  if (!cotable_id_.IsNil()) {
    encoder.CotableId(cotable_id_).Hash(hash_present_, hash_, hashed_group_).Range(range_group_);
  } else {
    encoder.ColocationId(colocation_id_)
           .Hash(hash_present_, hash_, hashed_group_)
           .Range(range_group_);
  }
}

void DocKey::Clear() {
  hash_present_ = false;
  hash_ = 0xdead;
  hashed_group_.clear();
  range_group_.clear();
}

void DocKey::ClearRangeComponents() {
  range_group_.clear();
}

void DocKey::ResizeRangeComponents(int new_size) {
  range_group_.resize(new_size);
}

namespace {

class DecodeDocKeyCallback {
 public:
  explicit DecodeDocKeyCallback(boost::container::small_vector_base<Slice>* out) : out_(out) {}

  boost::container::small_vector_base<Slice>* hashed_group() const {
    return nullptr;
  }

  boost::container::small_vector_base<Slice>* range_group() const {
    return out_;
  }

  void SetHash(...) const {}

  void SetCoTableId(const Uuid& cotable_id) const {}

  void SetColocationId(const ColocationId colocation_id) const {}

 private:
  boost::container::small_vector_base<Slice>* out_;
};

class DummyCallback {
 public:
  boost::container::small_vector_base<Slice>* hashed_group() const {
    return nullptr;
  }

  boost::container::small_vector_base<Slice>* range_group() const {
    return nullptr;
  }

  void SetHash(...) const {}

  void SetCoTableId(const Uuid& cotable_id) const {}

  void SetColocationId(const ColocationId colocation_id) const {}

  KeyEntryValue* AddSubkey() const {
    return nullptr;
  }
};

class EncodedSizesCallback {
 public:
  explicit EncodedSizesCallback(DocKeyDecoder* decoder) : decoder_(decoder) {}

  boost::container::small_vector_base<Slice>* hashed_group() const {
    return nullptr;
  }

  boost::container::small_vector_base<Slice>* range_group() const {
    range_group_start_ = decoder_->left_input().data();
    return nullptr;
  }

  void SetHash(...) const {}

  void SetCoTableId(const Uuid& cotable_id) const {}

  void SetColocationId(const ColocationId colocation_id) const {}

  KeyEntryValue* AddSubkey() const {
    return nullptr;
  }

  const uint8_t* range_group_start() {
    return range_group_start_;
  }

 private:
  DocKeyDecoder* decoder_;
  mutable const uint8_t* range_group_start_ = nullptr;
};

} // namespace

yb::Status DocKey::PartiallyDecode(Slice *slice,
                                   boost::container::small_vector_base<Slice>* out) {
  CHECK_NOTNULL(out);
  DocKeyDecoder decoder(*slice);
  RETURN_NOT_OK(DoDecode(
      &decoder, DocKeyPart::kWholeDocKey, AllowSpecial::kFalse, DecodeDocKeyCallback(out)));
  *slice = decoder.left_input();
  return Status::OK();
}

Result<DocKeyHash> DocKey::DecodeHash(const Slice& slice) {
  DocKeyDecoder decoder(slice);
  RETURN_NOT_OK(decoder.DecodeCotableId());
  RETURN_NOT_OK(decoder.DecodeColocationId());
  uint16_t hash;
  RETURN_NOT_OK(decoder.DecodeHashCode(&hash));
  return hash;
}

Result<size_t> DocKey::EncodedSize(Slice slice, DocKeyPart part, AllowSpecial allow_special) {
  auto initial_begin = slice.cdata();
  DocKeyDecoder decoder(slice);
  RETURN_NOT_OK(DoDecode(&decoder, part, allow_special, DummyCallback()));
  return decoder.left_input().cdata() - initial_begin;
}

Result<std::pair<size_t, bool>> DocKey::EncodedSizeAndHashPresent(Slice slice, DocKeyPart part) {
  class HashPresenceAwareDummyCallback : public DummyCallback {
   public:
    explicit HashPresenceAwareDummyCallback(bool* hash_present) : hash_present_(hash_present) {}

    void SetHash(const bool hash_present, const DocKeyHash hash = 0) const {
      *hash_present_ = hash_present;
    }

   private:
    bool* hash_present_;
  };

  auto initial_begin = slice.cdata();
  DocKeyDecoder decoder(slice);
  bool hash_present = false;
  HashPresenceAwareDummyCallback callback(&hash_present);
  RETURN_NOT_OK(DoDecode(&decoder, part, AllowSpecial::kFalse, callback));
  // TODO: left_input() should be called remaining_input().
  return std::make_pair(decoder.left_input().cdata() - initial_begin, hash_present);
}

Result<DocKeySizes> DocKey::EncodedHashPartAndDocKeySizes(
    Slice slice,
    AllowSpecial allow_special) {
  auto initial_begin = slice.data();
  DocKeyDecoder decoder(slice);
  EncodedSizesCallback callback(&decoder);
  RETURN_NOT_OK(DoDecode(
      &decoder, DocKeyPart::kWholeDocKey, allow_special, callback));
  return DocKeySizes {
    .hash_part_size = static_cast<size_t>(callback.range_group_start() - initial_begin),
    .doc_key_size = static_cast<size_t>(decoder.left_input().data() - initial_begin),
  };
}

class DocKey::DecodeFromCallback {
 public:
  explicit DecodeFromCallback(DocKey* key) : key_(key) {
  }

  KeyEntryValues* hashed_group() const {
    return &key_->hashed_group_;
  }

  KeyEntryValues* range_group() const {
    return &key_->range_group_;
  }

  void SetHash(bool present, DocKeyHash hash = 0) const {
    key_->hash_present_ = present;
    if (present) {
      key_->hash_ = hash;
    }
  }

  void SetCoTableId(const Uuid& cotable_id) const {
    key_->cotable_id_ = cotable_id;
  }

  void SetColocationId(const ColocationId colocation_id) const {
    key_->colocation_id_ = colocation_id;
  }

 private:
  DocKey* key_;
};

Status DocKey::DecodeFrom(Slice *slice, DocKeyPart part_to_decode, AllowSpecial allow_special) {
  Clear();
  DocKeyDecoder decoder(*slice);
  RETURN_NOT_OK(DoDecode(&decoder, part_to_decode, allow_special, DecodeFromCallback(this)));
  *slice = decoder.left_input();
  return Status::OK();
}

Result<size_t> DocKey::DecodeFrom(
    const Slice& slice, DocKeyPart part_to_decode, AllowSpecial allow_special) {
  Slice copy = slice;
  RETURN_NOT_OK(DecodeFrom(&copy, part_to_decode, allow_special));
  return slice.size() - copy.size();
}

namespace {

// Return limit on number of range components to decode based on part_to_decode and whether hash
// component are present in key (hash_present).
int MaxRangeComponentsToDecode(const DocKeyPart part_to_decode, const bool hash_present) {
  switch (part_to_decode) {
    case DocKeyPart::kUpToId:
      LOG(FATAL) << "Internal error: unexpected to have DocKeyPart::kUpToId here";
    case DocKeyPart::kWholeDocKey:
      return kNumValuesNoLimit;
    case DocKeyPart::kUpToHashCode: FALLTHROUGH_INTENDED;
    case DocKeyPart::kUpToHash:
      return 0;
    case DocKeyPart::kUpToHashOrFirstRange:
      return hash_present ? 0 : 1;
  }
  FATAL_INVALID_ENUM_VALUE(DocKeyPart, part_to_decode);
}

} // namespace

template<class Callback>
yb::Status DocKey::DoDecode(DocKeyDecoder* decoder,
                            DocKeyPart part_to_decode,
                            AllowSpecial allow_special,
                            const Callback& callback) {
  Uuid cotable_id;
  ColocationId colocation_id;
  if (VERIFY_RESULT(decoder->DecodeCotableId(&cotable_id))) {
    callback.SetCoTableId(cotable_id);
  } else if (VERIFY_RESULT(decoder->DecodeColocationId(&colocation_id))) {
    callback.SetColocationId(colocation_id);
  }

  switch (part_to_decode) {
    case DocKeyPart::kUpToId:
      return Status::OK();
    case DocKeyPart::kUpToHashCode: FALLTHROUGH_INTENDED;
    case DocKeyPart::kUpToHash: FALLTHROUGH_INTENDED;
    case DocKeyPart::kUpToHashOrFirstRange: FALLTHROUGH_INTENDED;
    case DocKeyPart::kWholeDocKey:
      uint16_t hash_code;
      const auto hash_present = VERIFY_RESULT(decoder->DecodeHashCode(&hash_code, allow_special));
      if (hash_present) {
        callback.SetHash(/* present */ true, hash_code);
        if (part_to_decode == DocKeyPart::kUpToHashCode) {
          return Status::OK();
        }
        RETURN_NOT_OK_PREPEND(
            ConsumePrimitiveValuesFromKey(
                decoder->mutable_input(), allow_special, callback.hashed_group()),
            "Error when decoding hashed components of a document key");
      } else {
        callback.SetHash(/* present */ false);
      }
      if (decoder->left_input().empty()) {
        return Status::OK();
      }
      // The rest are range components.
      const auto max_components_to_decode =
          MaxRangeComponentsToDecode(part_to_decode, hash_present);
      if (max_components_to_decode > 0) {
        RETURN_NOT_OK_PREPEND(
            ConsumePrimitiveValuesFromKey(
                decoder->mutable_input(), allow_special, callback.range_group(),
                max_components_to_decode),
            "Error when decoding range components of a document key");
      }
      return Status::OK();
  }
  FATAL_INVALID_ENUM_VALUE(DocKeyPart, part_to_decode);
}

yb::Status DocKey::FullyDecodeFrom(const rocksdb::Slice& slice) {
  rocksdb::Slice mutable_slice = slice;
  Status status = DecodeFrom(&mutable_slice);
  if (!mutable_slice.empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Expected all bytes of the slice to be decoded into DocKey, found $0 extra bytes",
        mutable_slice.size());
  }
  return status;
}

namespace {

// We need a special implementation of converting a vector to string because we need to pass the
// auto_decode_keys flag to PrimitiveValue::ToString.
void AppendVectorToString(
    std::string* dest,
    const KeyEntryValues& vec,
    AutoDecodeKeys auto_decode_keys) {
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
    std::string* dest,
    const KeyEntryValues& vec,
    AutoDecodeKeys auto_decode_keys) {
  dest->push_back('[');
  AppendVectorToString(dest, vec, auto_decode_keys);
  dest->push_back(']');
}

}  // namespace

string DocKey::ToString(AutoDecodeKeys auto_decode_keys) const {
  string result = "DocKey(";
  if (!cotable_id_.IsNil()) {
    result += "CoTableId=";
    result += cotable_id_.ToString();
    result += ", ";
  } else if (colocation_id_ != kColocationIdNotSet) {
    result += "ColocationId=";
    result += std::to_string(colocation_id_);
    result += ", ";
  }

  if (hash_present_) {
    result += StringPrintf("0x%04x", hash_);
    result += ", ";
  }

  AppendVectorToStringWithBrackets(&result, hashed_group_, auto_decode_keys);
  result += ", ";
  AppendVectorToStringWithBrackets(&result, range_group_, auto_decode_keys);
  result.push_back(')');
  return result;
}

bool DocKey::operator ==(const DocKey& other) const {
  return cotable_id_ == other.cotable_id_ &&
         colocation_id_ == other.colocation_id_ &&
         HashedComponentsEqual(other) &&
         range_group_ == other.range_group_;
}

bool DocKey::HashedComponentsEqual(const DocKey& other) const {
  return hash_present_ == other.hash_present_ &&
      // Only compare hashes and hashed groups if the hash presence flag is set.
      (!hash_present_ || (hash_ == other.hash_ && hashed_group_ == other.hashed_group_));
}

void DocKey::AddRangeComponent(const KeyEntryValue& val) {
  range_group_.push_back(val);
}

void DocKey::SetRangeComponent(const KeyEntryValue& val, int idx) {
  DCHECK_LT(idx, range_group_.size());
  range_group_[idx] = val;
}

int DocKey::CompareTo(const DocKey& other) const {
  int result = CompareUsingLessThan(cotable_id_, other.cotable_id_);
  if (result != 0) return result;

  result = CompareUsingLessThan(colocation_id_, other.colocation_id_);
  if (result != 0) return result;

  result = CompareUsingLessThan(hash_present_, other.hash_present_);
  if (result != 0) return result;

  if (hash_present_) {
    result = CompareUsingLessThan(hash_, other.hash_);
    if (result != 0) return result;
  }

  result = CompareVectors(hashed_group_, other.hashed_group_);
  if (result != 0) return result;

  return CompareVectors(range_group_, other.range_group_);
}

DocKey DocKey::FromRedisKey(uint16_t hash, const string &key) {
  DocKey new_doc_key;
  new_doc_key.hash_present_ = true;
  new_doc_key.hash_ = hash;
  new_doc_key.hashed_group_.emplace_back(key);
  return new_doc_key;
}

KeyBytes DocKey::EncodedFromRedisKey(uint16_t hash, const std::string &key) {
  KeyBytes result;
  result.AppendKeyEntryType(KeyEntryType::kUInt16Hash);
  result.AppendUInt16(hash);
  result.AppendKeyEntryType(KeyEntryType::kString);
  result.AppendString(key);
  result.AppendKeyEntryType(KeyEntryType::kGroupEnd);
  result.AppendKeyEntryType(KeyEntryType::kGroupEnd);
  DCHECK_EQ(result, FromRedisKey(hash, key).Encode());
  return result;
}

std::string DocKey::DebugSliceToString(Slice slice) {
  DocKey key;
  auto decoded_size = key.DecodeFrom(slice, DocKeyPart::kWholeDocKey, AllowSpecial::kTrue);
  if (!decoded_size.ok()) {
    return decoded_size.status().ToString() + ": " + slice.ToDebugHexString();
  }
  slice.remove_prefix(*decoded_size);
  auto result = key.ToString();
  if (!slice.empty()) {
    result += " + ";
    result += slice.ToDebugHexString();
  }
  return result;
}

bool DocKey::BelongsTo(const Schema& schema) const {
  if (!cotable_id_.IsNil()) {
    return cotable_id_ == schema.cotable_id();
  } else if (colocation_id_ != kColocationIdNotSet) {
    return colocation_id_ == schema.colocation_id();
  }
  return schema.cotable_id().IsNil() && schema.colocation_id() == kColocationIdNotSet;
}

// ------------------------------------------------------------------------------------------------
// SubDocKey
// ------------------------------------------------------------------------------------------------

KeyBytes SubDocKey::DoEncode(bool include_hybrid_time) const {
  KeyBytes key_bytes = doc_key_.Encode();
  for (const auto& subkey : subkeys_) {
    subkey.AppendToKey(&key_bytes);
  }
  if (has_hybrid_time() && include_hybrid_time) {
    AppendDocHybridTime(doc_ht_, &key_bytes);
  }
  return key_bytes;
}

namespace {

class DecodeSubDocKeyCallback {
 public:
  explicit DecodeSubDocKeyCallback(boost::container::small_vector_base<Slice>* out) : out_(out) {}

  Status DecodeDocKey(Slice* slice, AllowSpecial) const {
    return DocKey::PartiallyDecode(slice, out_);
  }

  // We don't need subkeys in partial decoding.
  KeyEntryValue* AddSubkey() const {
    return nullptr;
  }

  DocHybridTime& doc_hybrid_time() const {
    return doc_hybrid_time_;
  }

  void DocHybridTimeSlice(Slice slice) const {
    out_->push_back(slice);
  }
 private:
  boost::container::small_vector_base<Slice>* out_;
  mutable DocHybridTime doc_hybrid_time_;
};

} // namespace

Status SubDocKey::PartiallyDecode(Slice* slice, boost::container::small_vector_base<Slice>* out) {
  CHECK_NOTNULL(out);
  return DoDecode(slice, HybridTimeRequired::kTrue, AllowSpecial::kFalse,
                  DecodeSubDocKeyCallback(out));
}

class SubDocKey::DecodeCallback {
 public:
  explicit DecodeCallback(SubDocKey* key) : key_(key) {}

  Status DecodeDocKey(Slice* slice, AllowSpecial allow_special) const {
    return key_->doc_key_.DecodeFrom(slice, DocKeyPart::kWholeDocKey, allow_special);
  }

  KeyEntryValue* AddSubkey() const {
    key_->subkeys_.emplace_back();
    return &key_->subkeys_.back();
  }

  DocHybridTime& doc_hybrid_time() const {
    return key_->doc_ht_;
  }

  void DocHybridTimeSlice(Slice slice) const {
  }
 private:
  SubDocKey* key_;
};

Status SubDocKey::DecodeFrom(
    Slice* slice, HybridTimeRequired require_hybrid_time, AllowSpecial allow_special) {
  Clear();
  return DoDecode(slice, require_hybrid_time, allow_special, DecodeCallback(this));
}

Status SubDocKey::FullyDecodeFromKeyWithOptionalHybridTime(const rocksdb::Slice& slice) {
  return FullyDecodeFrom(slice, HybridTimeRequired::kFalse);
}

Result<bool> SubDocKey::DecodeSubkey(Slice* slice) {
  return DecodeSubkey(slice, DummyCallback());
}

template<class Callback>
Result<bool> SubDocKey::DecodeSubkey(Slice* slice, const Callback& callback) {
  if (!slice->empty() && *slice->data() != KeyEntryTypeAsChar::kHybridTime) {
    RETURN_NOT_OK(KeyEntryValue::DecodeKey(slice, callback.AddSubkey()));
    return true;
  }
  return false;
}

template<class Callback>
Status SubDocKey::DoDecode(rocksdb::Slice* slice,
                           const HybridTimeRequired require_hybrid_time,
                           AllowSpecial allow_special,
                           const Callback& callback) {
  if (allow_special && require_hybrid_time) {
    return STATUS(NotSupported,
                  "Not supported to have both require_hybrid_time and allow_special");
  }
  const rocksdb::Slice original_bytes(*slice);

  RETURN_NOT_OK(callback.DecodeDocKey(slice, allow_special));
  for (;;) {
    if (allow_special && !slice->empty() &&
        IsSpecialKeyEntryType(static_cast<KeyEntryType>(slice->cdata()[0]))) {
      callback.doc_hybrid_time() = DocHybridTime::kInvalid;
      return Status::OK();
    }
    auto decode_result = DecodeSubkey(slice, callback);
    RETURN_NOT_OK_PREPEND(
        decode_result,
        Substitute("While decoding SubDocKey $0", ToShortDebugStr(original_bytes)));
    if (!decode_result.get()) {
      break;
    }
  }
  if (slice->empty()) {
    if (!require_hybrid_time) {
      callback.doc_hybrid_time() = DocHybridTime::kInvalid;
      return Status::OK();
    }
    return STATUS_SUBSTITUTE(
        Corruption,
        "Found too few bytes in the end of a SubDocKey for a type-prefixed hybrid_time: $0",
        ToShortDebugStr(*slice));
  }

  // The reason the following is not handled as a Status is that the logic above (loop + emptiness
  // check) should guarantee this is the only possible case left.
  DCHECK_EQ(KeyEntryType::kHybridTime, DecodeKeyEntryType(*slice));
  slice->consume_byte();

  auto begin = slice->data();
  callback.doc_hybrid_time() = VERIFY_RESULT(DocHybridTime::DecodeFrom(slice));
  callback.DocHybridTimeSlice(Slice(begin, slice->data()));

  return Status::OK();
}

Status SubDocKey::FullyDecodeFrom(const rocksdb::Slice& slice,
                                  HybridTimeRequired require_hybrid_time) {
  rocksdb::Slice mutable_slice = slice;
  RETURN_NOT_OK(DecodeFrom(&mutable_slice, require_hybrid_time));
  if (!mutable_slice.empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Expected all bytes of the slice to be decoded into SubDocKey, found $0 extra bytes: $1",
        mutable_slice.size(), mutable_slice.ToDebugHexString());
  }
  return Status::OK();
}

Status SubDocKey::DecodePrefixLengths(
    Slice slice, boost::container::small_vector_base<size_t>* out) {
  auto begin = slice.data();
  auto hashed_part_size = VERIFY_RESULT(DocKey::EncodedSize(slice, DocKeyPart::kUpToHash));
  if (hashed_part_size != 0) {
    slice.remove_prefix(hashed_part_size);
    out->push_back(hashed_part_size);
  }
  while (VERIFY_RESULT(ConsumePrimitiveValueFromKey(&slice))) {
    out->push_back(slice.data() - begin);
  }
  if (!out->empty()) {
    if (begin[out->back()] != KeyEntryTypeAsChar::kGroupEnd) {
      return STATUS_FORMAT(Corruption, "Range keys group end expected at $0 in $1",
                           out->back(), Slice(begin, slice.end()).ToDebugHexString());
    }
    ++out->back(); // Add range key group end to last prefix
  }
  while (VERIFY_RESULT(SubDocKey::DecodeSubkey(&slice))) {
    out->push_back(slice.data() - begin);
  }

  return Status::OK();
}

Status SubDocKey::DecodeDocKeyAndSubKeyEnds(
    Slice slice, boost::container::small_vector_base<size_t>* out) {
  auto begin = slice.data();
  if (out->empty()) {
    auto id_size = VERIFY_RESULT(DocKey::EncodedSize(slice, DocKeyPart::kUpToId));
    out->push_back(id_size);
  }
  if (out->size() == 1) {
    auto id_size = out->front();
    SCHECK_GE(slice.size(), id_size + 1, Corruption,
              Format("Cannot have exclusively ID in key $0", slice.ToDebugHexString()));
    // Identify table tombstone.
    if ((slice[0] == KeyEntryTypeAsChar::kColocationId || slice[0] == KeyEntryTypeAsChar::kTableId)
        && slice[id_size] == KeyEntryTypeAsChar::kGroupEnd) {
      SCHECK_GE(slice.size(), id_size + 2, Corruption,
                Format("Space for kHybridTime expected in key $0", slice.ToDebugHexString()));
      SCHECK_EQ(slice[id_size + 1], KeyEntryTypeAsChar::kHybridTime, Corruption,
                Format("Hybrid time expected in key $0", slice.ToDebugHexString()));
      // Consume kGroupEnd without pushing to out because the empty key of a table tombstone
      // shouldn't count as an end.
      slice.remove_prefix(id_size + 1);
    } else {
      slice.remove_prefix(id_size);
      auto doc_key_size = VERIFY_RESULT(DocKey::EncodedSize(slice, DocKeyPart::kWholeDocKey));
      slice.remove_prefix(doc_key_size);
      out->push_back(id_size + doc_key_size);
    }
  } else {
    slice.remove_prefix(out->back());
  }
  while (VERIFY_RESULT(SubDocKey::DecodeSubkey(&slice))) {
    out->push_back(slice.data() - begin);
  }

  return Status::OK();
}

std::string SubDocKey::DebugSliceToString(Slice slice) {
  auto r = DebugSliceToStringAsResult(slice);
  if (r.ok()) {
    return r.get();
  }
  return r.status().ToString();
}

Result<std::string> SubDocKey::DebugSliceToStringAsResult(Slice slice) {
  SubDocKey key;
  auto status = key.DecodeFrom(&slice, HybridTimeRequired::kFalse, AllowSpecial::kTrue);
  if (status.ok()) {
    if (slice.empty()) {
      return key.ToString();
    }
    return key.ToString() + "+" + slice.ToDebugHexString();
  }
  return status;
}

string SubDocKey::ToString(AutoDecodeKeys auto_decode_keys) const {
  std::string result("SubDocKey(");
  result.append(doc_key_.ToString(auto_decode_keys));
  result.append(", [");

  AppendVectorToString(&result, subkeys_, auto_decode_keys);

  if (has_hybrid_time()) {
    if (!subkeys_.empty()) {
      result.append("; ");
    }
    result.append(doc_ht_.ToString());
  }
  result.append("])");
  return result;
}

Status SubDocKey::FromDocPath(const DocPath& doc_path) {
  RETURN_NOT_OK(doc_key_.FullyDecodeFrom(doc_path.encoded_doc_key().AsSlice()));
  subkeys_ = doc_path.subkeys();
  return Status::OK();
}

void SubDocKey::Clear() {
  doc_key_.Clear();
  subkeys_.clear();
  doc_ht_ = DocHybridTime::kInvalid;
}

bool SubDocKey::StartsWith(const SubDocKey& prefix) const {
  return doc_key_ == prefix.doc_key_ &&
         // Subkeys precede the hybrid_time field in the encoded representation, so the hybrid_time
         // either has to be undefined in the prefix, or the entire key must match, including
         // subkeys and the hybrid_time (in this case the prefix is the same as this key).
         (!prefix.has_hybrid_time() ||
          (doc_ht_ == prefix.doc_ht_ && prefix.num_subkeys() == num_subkeys())) &&
         prefix.num_subkeys() <= num_subkeys() &&
         // std::mismatch finds the first difference between two sequences. Prior to C++14, the
         // behavior is undefined if the second range is shorter than the first range, so we make
         // sure the potentially shorter range is first.
         std::mismatch(
             prefix.subkeys_.begin(), prefix.subkeys_.end(), subkeys_.begin()
         ).first == prefix.subkeys_.end();
}

bool SubDocKey::operator==(const SubDocKey& other) const {
  if (doc_key_ != other.doc_key_ ||
      subkeys_ != other.subkeys_)
    return false;

  const bool ht_is_valid = doc_ht_.is_valid();
  const bool other_ht_is_valid = other.doc_ht_.is_valid();
  if (ht_is_valid != other_ht_is_valid)
    return false;
  if (ht_is_valid) {
    return doc_ht_ == other.doc_ht_;
  } else {
    // Both keys don't have a hybrid time.
    return true;
  }
}

int SubDocKey::CompareTo(const SubDocKey& other) const {
  int result = CompareToIgnoreHt(other);
  if (result != 0) return result;

  const bool ht_is_valid = doc_ht_.is_valid();
  const bool other_ht_is_valid = other.doc_ht_.is_valid();
  if (ht_is_valid) {
    if (other_ht_is_valid) {
      // HybridTimes are sorted in reverse order.
      return -doc_ht_.CompareTo(other.doc_ht_);
    } else {
      // This key has a hybrid time and the other one is identical but lacks the hybrid time, so
      // this one is greater.
      return 1;
    }
  } else {
    if (other_ht_is_valid) {
      // This key is a "prefix" of the other key, which has a hybrid time, so this one is less.
      return -1;
    } else {
      // Neither key has a hybrid time.
      return 0;
    }
  }

}

int SubDocKey::CompareToIgnoreHt(const SubDocKey& other) const {
  int result = doc_key_.CompareTo(other.doc_key_);
  if (result != 0) return result;

  result = CompareVectors(subkeys_, other.subkeys_);
  return result;
}

string BestEffortDocDBKeyToStr(Slice key) {
  Slice mutable_slice = key;
  SubDocKey subdoc_key;
  Status decode_status = subdoc_key.DecodeFrom(
      &mutable_slice, HybridTimeRequired::kFalse, AllowSpecial::kTrue);
  if (decode_status.ok()) {
    ostringstream ss;
    if (!subdoc_key.has_hybrid_time() && subdoc_key.num_subkeys() == 0) {
      // This is really just a DocKey.
      ss << subdoc_key.doc_key().ToString();
    } else {
      ss << subdoc_key.ToString();
    }
    if (mutable_slice.size() > 0) {
      ss << "+" << mutable_slice.ToDebugString();
      // Can append the above status of why we could not decode a SubDocKey, if needed.
    }
    return ss.str();
  }

  // We could not decode a SubDocKey at all, even without a hybrid_time.
  return key.ToDebugString();
}

std::string BestEffortDocDBKeyToStr(const KeyBytes& key_bytes) {
  return BestEffortDocDBKeyToStr(key_bytes.AsSlice());
}

KeyBytes SubDocKey::AdvanceOutOfSubDoc() const {
  KeyBytes subdoc_key_no_ts = EncodeWithoutHt();
  subdoc_key_no_ts.AppendKeyEntryType(KeyEntryType::kMaxByte);
  return subdoc_key_no_ts;
}

KeyBytes SubDocKey::AdvanceOutOfDocKeyPrefix() const {
  // To construct key bytes that will seek past this DocKey and DocKeys that have the same hash
  // components but add more range components to it, we will strip the group-end of the range
  // components and append 0xff, which will be lexicographically higher than any key bytes
  // with the same hash and range component prefix. For example,
  //
  // DocKey(0x1234, ["aa", "bb"], ["cc", "dd"])
  // Encoded: H\0x12\0x34$aa\x00\x00$bb\x00\x00!$cc\x00\x00$dd\x00\x00!
  // Result:  H\0x12\0x34$aa\x00\x00$bb\x00\x00!$cc\x00\x00$dd\x00\x00\xff
  // This key will also skip all DocKeys that have additional range components, e.g.
  // DocKey(0x1234, ["aa", "bb"], ["cc", "dd", "ee"])
  // (encoded as H\0x12\0x34$aa\x00\x00$bb\x00\x00!$cc\x00\x00$dd\x00\x00$ee\x00\00!). That should
  // make no difference to DocRowwiseIterator in a valid database, because all keys actually stored
  // in DocDB will have exactly the same number of range components.
  //
  // Now, suppose there are no range components in the key passed to us (note: that does not
  // necessarily mean there are no range components in the schema, just the doc key being passed to
  // us is a custom-constructed DocKey with no range components because the caller wants a key
  // that will skip pass all doc keys with the same hash components prefix). Example:
  //
  // DocKey(0x1234, ["aa", "bb"], [])
  // Encoded: H\0x12\0x34$aa\x00\x00$bb\x00\x00!!
  // Result: H\0x12\0x34$aa\x00\x00$bb\x00\x00!\xff
  KeyBytes doc_key_encoded = doc_key_.Encode();
  doc_key_encoded.RemoveKeyEntryTypeSuffix(KeyEntryType::kGroupEnd);
  doc_key_encoded.AppendKeyEntryType(KeyEntryType::kMaxByte);
  return doc_key_encoded;
}

void SubDocKey::AppendSubKey(KeyEntryValue subkey) {
  subkeys_.emplace_back(std::move(subkey));
}

void SubDocKey::RemoveLastSubKey() {
  DCHECK(!subkeys_.empty());
  subkeys_.pop_back();
}

void SubDocKey::KeepPrefix(size_t num_sub_keys_to_keep) {
  if (subkeys_.size() > num_sub_keys_to_keep) {
    subkeys_.resize(num_sub_keys_to_keep);
  }
}

DocKeyEncoderAfterTableIdStep DocKeyEncoder::CotableId(const Uuid& cotable_id) {
  if (!cotable_id.IsNil()) {
    std::string bytes;
    cotable_id.EncodeToComparable(&bytes);
    out_->AppendKeyEntryType(KeyEntryType::kTableId);
    out_->AppendRawBytes(bytes);
  }
  return DocKeyEncoderAfterTableIdStep(out_);
}

DocKeyEncoderAfterTableIdStep DocKeyEncoder::ColocationId(const yb::ColocationId colocation_id) {
  if (colocation_id != kColocationIdNotSet) {
    out_->AppendKeyEntryType(KeyEntryType::kColocationId);
    out_->AppendUInt32(colocation_id);
  }
  return DocKeyEncoderAfterTableIdStep(out_);
}

DocKeyEncoderAfterTableIdStep DocKeyEncoder::Schema(const class Schema& schema) {
  if (schema.colocation_id() != kColocationIdNotSet) {
    return ColocationId(schema.colocation_id());
  } else {
    return CotableId(schema.cotable_id());
  }
}

Result<bool> DocKeyDecoder::DecodeCotableId(Uuid* uuid) {
  if (!input_.TryConsumeByte(KeyEntryTypeAsChar::kTableId)) {
    return false;
  }

  if (input_.size() < kUuidSize) {
    return STATUS_FORMAT(
        Corruption, "Not enough bytes for cotable id: $0", input_.ToDebugHexString());
  }

  if (uuid) {
    *uuid = VERIFY_RESULT(Uuid::FromComparable(input_.Prefix(kUuidSize)));
  }
  input_.remove_prefix(kUuidSize);

  return true;
}

Result<bool> DocKeyDecoder::DecodeColocationId(ColocationId* colocation_id) {
  if (input_.empty() || input_[0] != KeyEntryTypeAsChar::kColocationId) {
    return false;
  }

  input_.consume_byte();

  if (input_.size() < sizeof(ColocationId)) {
    return STATUS_FORMAT(
        Corruption, "Not enough bytes for a colocation id: $0", input_.ToDebugHexString());
  }

  static_assert(
      sizeof(ColocationId) == sizeof(uint32_t),
      "It looks like the colocation ID's size has changed -- need to update encoder/decoder.");
  if (colocation_id) {
    *colocation_id = BigEndian::Load32(input_.data());
  }
  input_.remove_prefix(sizeof(ColocationId));

  return true;
}

Result<bool> DocKeyDecoder::DecodeHashCode(uint16_t* out, AllowSpecial allow_special) {
  if (input_.empty()) {
    return false;
  }

  auto first_value_type = static_cast<KeyEntryType>(input_[0]);

  if (first_value_type == KeyEntryType::kGroupEnd) {
    return false;
  }

  auto good_value_type = allow_special || !IsSpecialKeyEntryType(first_value_type);
  if (!good_value_type) {
    return STATUS_FORMAT(Corruption,
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
  static_assert(sizeof(DocKeyHash) == sizeof(uint16_t),
      "It looks like the DocKeyHash's size has changed -- need to update encoder/decoder.");
  if (out) {
    *out = BigEndian::Load16(input_.data() + 1);
  }
  input_.remove_prefix(sizeof(DocKeyHash) + 1);
  return true;
}

Status DocKeyDecoder::DecodeKeyEntryValue(AllowSpecial allow_special) {
  return DecodeKeyEntryValue(nullptr /* out */, allow_special);
}

Status DocKeyDecoder::DecodeKeyEntryValue(KeyEntryValue* out, AllowSpecial allow_special) {
  if (allow_special &&
      !input_.empty() &&
      (input_[0] == KeyEntryTypeAsChar::kLowest || input_[0] == KeyEntryTypeAsChar::kHighest)) {
    input_.consume_byte();
    return Status::OK();
  }
  return KeyEntryValue::DecodeKey(&input_, out);
}

Status DocKeyDecoder::ConsumeGroupEnd() {
  if (input_.empty() || input_[0] != KeyEntryTypeAsChar::kGroupEnd) {
    return STATUS_FORMAT(Corruption, "Group end expected but $0 found", input_.ToDebugHexString());
  }
  input_.consume_byte();
  return Status::OK();
}

bool DocKeyDecoder::GroupEnded() const {
  return input_.empty() || input_[0] == KeyEntryTypeAsChar::kGroupEnd;
}

Result<bool> DocKeyDecoder::HasPrimitiveValue(AllowSpecial allow_special) {
  return dockv::HasPrimitiveValue(&input_, allow_special);
}

Status DocKeyDecoder::DecodeToKeys() {
  RETURN_NOT_OK(DecodeCotableId());
  RETURN_NOT_OK(DecodeColocationId());
  return Status::OK();
}

Result<bool> DocKeyDecoder::DecodeHashCode(AllowSpecial allow_special) {
  return DecodeHashCode(nullptr /* out */, allow_special);
}

Result<bool> ClearRangeComponents(KeyBytes* out, AllowSpecial allow_special) {
  auto prefix_size = VERIFY_RESULT(
      DocKey::EncodedSize(out->AsSlice(), DocKeyPart::kUpToHash, allow_special));
  auto& str = *out->mutable_data();
  if (str.size() == prefix_size + 1 && str[prefix_size] == KeyEntryTypeAsChar::kGroupEnd) {
    return false;
  }
  if (str.size() > prefix_size) {
    str[prefix_size] = KeyEntryTypeAsChar::kGroupEnd;
    str.Truncate(prefix_size + 1);
  } else {
    str.PushBack(KeyEntryTypeAsChar::kGroupEnd);
  }
  return true;
}

Result<bool> HashedOrFirstRangeComponentsEqual(const Slice& lhs, const Slice& rhs) {
  DocKeyDecoder lhs_decoder(lhs);
  DocKeyDecoder rhs_decoder(rhs);
  RETURN_NOT_OK(lhs_decoder.DecodeCotableId());
  RETURN_NOT_OK(rhs_decoder.DecodeCotableId());
  RETURN_NOT_OK(lhs_decoder.DecodeColocationId());
  RETURN_NOT_OK(rhs_decoder.DecodeColocationId());

  const bool hash_present = VERIFY_RESULT(lhs_decoder.DecodeHashCode(AllowSpecial::kTrue));
  if (hash_present != VERIFY_RESULT(rhs_decoder.DecodeHashCode(AllowSpecial::kTrue))) {
    return false;
  }

  size_t consumed = lhs_decoder.ConsumedSizeFrom(lhs.data());
  if (consumed != rhs_decoder.ConsumedSizeFrom(rhs.data()) ||
      !strings::memeq(lhs.data(), rhs.data(), consumed)) {
    return false;
  }

  // Check all hashed components if present or first range component otherwise.
  int num_components_to_check = hash_present ? kNumValuesNoLimit : 1;

  while (!lhs_decoder.GroupEnded() && num_components_to_check > 0) {
    auto lhs_start = lhs_decoder.left_input().data();
    auto rhs_start = rhs_decoder.left_input().data();
    auto value_type = lhs_start[0];
    if (rhs_decoder.GroupEnded() || rhs_start[0] != value_type) {
      return false;
    }

    RETURN_NOT_OK(lhs_decoder.DecodeKeyEntryValue(AllowSpecial::kTrue));
    RETURN_NOT_OK(rhs_decoder.DecodeKeyEntryValue(AllowSpecial::kTrue));
    consumed = lhs_decoder.ConsumedSizeFrom(lhs_start);
    if (consumed != rhs_decoder.ConsumedSizeFrom(rhs_start) ||
        !strings::memeq(lhs_start, rhs_start, consumed)) {
      return false;
    }
    --num_components_to_check;
  }
  if (num_components_to_check == 0) {
    // We don't care about difference in rest of range components.
    return true;
  }

  return rhs_decoder.GroupEnded();
}

bool DocKeyBelongsTo(Slice doc_key, const Schema& schema) {
  bool has_table_id = !doc_key.empty() &&
       (doc_key[0] == KeyEntryTypeAsChar::kTableId ||
        doc_key[0] == KeyEntryTypeAsChar::kColocationId);

  if (schema.cotable_id().IsNil() && schema.colocation_id() == kColocationIdNotSet) {
    return !has_table_id;
  }

  if (!has_table_id) {
    return false;
  }

  if (doc_key[0] == KeyEntryTypeAsChar::kTableId) {
    doc_key.consume_byte();

    uint8_t bytes[kUuidSize];
    schema.cotable_id().EncodeToComparable(bytes);
    return doc_key.starts_with(Slice(bytes, kUuidSize));
  } else {
    DCHECK(doc_key[0] == KeyEntryTypeAsChar::kColocationId);
    doc_key.consume_byte();
    char buf[sizeof(ColocationId)];
    BigEndian::Store32(buf, schema.colocation_id());
    return doc_key.starts_with(Slice(buf, sizeof(ColocationId)));
  }
}

Result<bool> IsColocatedTableTombstoneKey(Slice doc_key) {
  DocKeyDecoder decoder(doc_key);
  if (VERIFY_RESULT(decoder.DecodeColocationId())) {
    RETURN_NOT_OK(decoder.ConsumeGroupEnd());

    if (decoder.left_input().size() == 0) {
      return true;
    }
  }

  return false;
}

Result<boost::optional<DocKeyHash>> DecodeDocKeyHash(const Slice& encoded_key) {
  DocKey key;
  RETURN_NOT_OK(key.DecodeFrom(encoded_key, DocKeyPart::kUpToHashCode));
  return key.has_hash() ? key.hash() : boost::optional<DocKeyHash>();
}

}  // namespace yb::dockv
