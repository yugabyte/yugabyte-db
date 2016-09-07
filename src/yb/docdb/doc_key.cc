// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_key.h"

#include <memory>
#include <sstream>

#include "rocksdb/util/string_util.h"

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/value_type.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"

using std::ostringstream;

using strings::Substitute;

namespace yb {
namespace docdb {

Status ConsumePrimitiveValuesFromKey(rocksdb::Slice* slice, vector<PrimitiveValue>* result) {
  while (true) {
    if (slice->empty()) {
      return Status::Corruption("Unexpected end of key when decoding document key");
    }
    ValueType current_value_type = static_cast<ValueType>(*slice->data());
    if (current_value_type == ValueType::kGroupEnd) {
      slice->ConsumeByte();
      return Status::OK();
    }
    DCHECK(IsPrimitiveValueType(current_value_type))
        << "Expected a primitive value type, got " << ValueTypeToStr(current_value_type);
    result->emplace_back();
    RETURN_NOT_OK(result->back().DecodeFromKey(slice));
  }
}

namespace {

void AppendDocKeyItems(const vector<PrimitiveValue>& doc_key_items, KeyBytes* result) {
  for (const PrimitiveValue& item : doc_key_items) {
    item.AppendToKey(result);
  }
  result->AppendValueType(ValueType::kGroupEnd);
}

}  // unnamed namespace

// ------------------------------------------------------------------------------------------------
// DocKey
// ------------------------------------------------------------------------------------------------

DocKey::DocKey() : hash_present_(false) {
}

DocKey::DocKey(const vector<PrimitiveValue>& range_components)
    : hash_present_(false),
      range_group_(range_components) {
}

DocKey::DocKey(DocKeyHash hash,
               const vector<PrimitiveValue>& hashed_components,
               const vector<PrimitiveValue>& range_components)
    : hash_present_(true),
      hash_(hash),
      hashed_group_(hashed_components),
      range_group_(range_components) {
}

KeyBytes DocKey::Encode() const {
  KeyBytes result;
  if (hash_present_) {
    // We are not setting the "more items in group" bit on the hash field because it is not part
    // of "hashed" or "range" groups.
    result.AppendValueType(ValueType::kUInt32Hash);
    result.AppendUInt32(hash_);
    AppendDocKeyItems(hashed_group_, &result);
  }
  AppendDocKeyItems(range_group_, &result);
  return result;
}

yb::Status DocKey::DecodeFrom(rocksdb::Slice *slice) {
  Clear();

  if (slice->empty()) {
    return Status::Corruption("Document key is empty");
  }
  const ValueType first_value_type = static_cast<ValueType>(*slice->data());

  if (!IsPrimitiveValueType(first_value_type) && first_value_type != ValueType::kGroupEnd) {
    return Status::Corruption(Substitute(
        "Expected first value type to be primitive or GroupEnd, got $0",
        ValueTypeToStr(first_value_type)));
  }

  if (first_value_type == ValueType::kUInt32Hash) {
    if (slice->size() >= sizeof(DocKeyHash) + 1) {
      // We'll need to update this code if we ever change the size of the hash field.
      static_assert(sizeof(DocKeyHash) == sizeof(uint32_t),
          "It looks like the DocKeyHash's size has changed -- need to update encoder/decoder.");
      hash_ = BigEndian::Load32(slice->data() + 1);
      hash_present_ = true;
      slice->remove_prefix(sizeof(DocKeyHash) + 1);
    } else {
      return Status::Corruption(Substitute(
          "Could not decode a 32-bit hash component of a document key: only $0 bytes left",
          slice->size()));
    }
    RETURN_NOT_OK_PREPEND(ConsumePrimitiveValuesFromKey(slice, &hashed_group_),
        "Error when decoding hashed components of a document key");
  } else {
    hash_present_ = false;
  }

  RETURN_NOT_OK_PREPEND(ConsumePrimitiveValuesFromKey(slice, &range_group_),
      "Error when decoding range components of a document key");

  return Status::OK();
}

yb::Status DocKey::FullyDecodeFrom(const rocksdb::Slice& slice) {
  rocksdb::Slice mutable_slice = slice;
  Status status = DecodeFrom(&mutable_slice);
  if (!mutable_slice.empty()) {
    return Status::InvalidArgument(Substitute(
        "Expected all bytes of the slice to be decoded into DocKey, found $0 extra bytes",
        mutable_slice.size()));
  }
  return status;
}

string DocKey::ToString() const {
  string result = "DocKey(";
  if (hash_present_) {
    result += StringPrintf("0x%08x", hash_);
    result += ", ";
  }

  result += rocksdb::VectorToString(hashed_group_);
  result += ", ";
  result += rocksdb::VectorToString(range_group_);
  result.push_back(')');
  return result;
}

DocKey DocKey::FromKuduEncodedKey(const EncodedKey &encoded_key, const Schema &schema) {
  DocKey new_doc_key;
  for (int i = 0; i < encoded_key.num_key_columns(); ++i) {
    const auto& type_info = *schema.column(i).type_info();
    const void* const raw_key = encoded_key.raw_keys()[i];
    switch (type_info.type()) {
      case DataType::INT64:
        new_doc_key.range_group_.emplace_back(*reinterpret_cast<const int64_t*>(raw_key));
        break;
      case DataType::STRING:
        new_doc_key.range_group_.emplace_back(reinterpret_cast<const Slice*>(raw_key)->ToString());
        break;

      default:
        LOG(FATAL) << "Don't know how to decode Kudu data type " << type_info.name();
    }
  }
  return new_doc_key;
}

// ------------------------------------------------------------------------------------------------
// SubDocKey
// ------------------------------------------------------------------------------------------------

KeyBytes SubDocKey::Encode() const {
  KeyBytes key_bytes = doc_key_.Encode();
  key_bytes.AppendTimestamp(doc_gen_ts_);
  for (const auto& subkey_and_gen_ts : subkeys_) {
    subkey_and_gen_ts.first.AppendToKey(&key_bytes);
    key_bytes.AppendTimestamp(subkey_and_gen_ts.second);
  }
  return key_bytes;
}

Status SubDocKey::DecodeFrom(const rocksdb::Slice& key_bytes) {
  rocksdb::Slice slice(key_bytes);

  Clear();
  RETURN_NOT_OK(doc_key_.DecodeFrom(&slice));
  RETURN_NOT_OK(ConsumeTimestampFromKey(&slice, &doc_gen_ts_));
  while (!slice.empty()) {
    subkeys_.emplace_back();
    auto& current_key_and_timestamp = subkeys_.back();
    RETURN_NOT_OK(current_key_and_timestamp.first.DecodeFromKey(&slice));
    RETURN_NOT_OK(ConsumeTimestampFromKey(&slice, &current_key_and_timestamp.second));
  }
  return Status::OK();
}

string SubDocKey::ToString() const {
  std::stringstream result;
  result << "SubDocKey(" << doc_key_.ToString() << ", ["  << doc_gen_ts_.ToDebugString();

  for (const auto& subkey_and_ts : subkeys_) {
    result << ", " << subkey_and_ts.first << ", " << subkey_and_ts.second.ToDebugString();
  }
  result << "])";
  return result.str();
}

void SubDocKey::Clear() {
  doc_key_.Clear();
  subkeys_.clear();
  doc_gen_ts_ = Timestamp::kMin;
}

bool SubDocKey::StartsWith(const SubDocKey& other) const {
  return doc_key_ == other.doc_key_ &&
         other.num_subkeys() <= num_subkeys() &&
         // std::mismatch finds the first difference between two sequences. Prior to C++14, the
         // behavior is undefined if the second range is shorter than the first range, so we make
         // sure the potentially shorter range is first.
         std::mismatch(
             other.subkeys_.begin(), other.subkeys_.end(), subkeys_.begin()
         ).first == other.subkeys_.end();
}

bool SubDocKey::operator ==(const SubDocKey& other) const {
  return doc_key_ == other.doc_key_ &&
         doc_gen_ts_ == other.doc_gen_ts_ &&
         subkeys_ == other.subkeys_;
}

SubDocKey SubDocKey::AdvanceToNextSubkey() const {
  assert(!subkeys_.empty());
  SubDocKey next_subkey(*this);
  next_subkey.subkeys_.back().second = yb::Timestamp::kMin;
  return next_subkey;
}

int SubDocKey::CompareTo(const SubDocKey& other) const {
  int result = doc_key_.CompareTo(other.doc_key_);
  if (result != 0) return result;

  result = doc_gen_ts_.CompareTo(other.doc_gen_ts_);
  // Timestamps are sorted in reverse order.
  if (result != 0) return -result;

  // We specify reverse_second_component = true to implement inverse timestamp ordering.
  return ComparePairVectors<PrimitiveValue, Timestamp, true>(subkeys_, other.subkeys_);
}

string BestEffortKeyBytesToStr(const KeyBytes& key_bytes) {
  SubDocKey subdoc_key;
  Status subdoc_key_decode_status = subdoc_key.FullyDecodeFrom(key_bytes.AsSlice());
  if (subdoc_key_decode_status.ok()) {
    return subdoc_key.ToString();
  }
  DocKey doc_key;

  // Try to decode as a DocKey with some trailing bytes.
  rocksdb::Slice mutable_slice = key_bytes.AsSlice();
  if (doc_key.DecodeFrom(&mutable_slice).ok()) {
    ostringstream ss;
    ss << doc_key.ToString();
    if (mutable_slice.size() > 0) {
      ss << " followed by raw bytes " << FormatRocksDBSliceAsStr(mutable_slice);
      // Can append the above status of why we could not decode a SubDocKey, if needed.
    }
    return ss.str();
  }

  return key_bytes.ToString();
}

}
}
