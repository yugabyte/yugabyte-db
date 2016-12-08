// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_key.h"

#include <memory>
#include <sstream>

#include "rocksdb/util/string_util.h"

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/value_type.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/enums.h"
#include "yb/util/compare_util.h"

using std::ostringstream;

using strings::Substitute;

using yb::util::to_underlying;
using yb::util::CompareVectors;

namespace yb {
namespace docdb {

Status ConsumePrimitiveValuesFromKey(rocksdb::Slice* slice, vector<PrimitiveValue>* result) {
  const auto initial_slice(*slice);  // For error reporting.
  while (true) {
    if (slice->empty()) {
      return STATUS(Corruption, "Unexpected end of key when decoding document key");
    }
    ValueType current_value_type = static_cast<ValueType>(*slice->data());
    if (current_value_type == ValueType::kGroupEnd) {
      slice->ConsumeByte();
      return Status::OK();
    }
    DCHECK(IsPrimitiveValueType(current_value_type))
        << "Expected a primitive value type, got " << ValueTypeToStr(current_value_type);
    result->emplace_back();
    RETURN_NOT_OK_PREPEND(result->back().DecodeFromKey(slice),
                          Substitute("while consuming primitive values from $0",
                                     ToShortDebugStr(initial_slice)));
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

void DocKey::Clear() {
  hash_present_ = false;
  hash_ = 0xdeadbeef;
  hashed_group_.clear();
  range_group_.clear();
}

yb::Status DocKey::DecodeFrom(rocksdb::Slice *slice) {
  Clear();

  if (slice->empty()) {
    return STATUS(Corruption, "Document key is empty");
  }
  const ValueType first_value_type = static_cast<ValueType>(*slice->data());

  if (!IsPrimitiveValueType(first_value_type) && first_value_type != ValueType::kGroupEnd) {
    return STATUS_SUBSTITUTE(Corruption,
        "Expected first value type to be primitive or GroupEnd, got $0",
        ValueTypeToStr(first_value_type));
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
      return STATUS_SUBSTITUTE(Corruption,
          "Could not decode a 32-bit hash component of a document key: only $0 bytes left",
          slice->size());
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
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Expected all bytes of the slice to be decoded into DocKey, found $0 extra bytes",
        mutable_slice.size());
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

bool DocKey::operator ==(const DocKey& other) const {
  return hash_present_ == other.hash_present_ &&
         // Only compare hashes and hashed groups if the hash presence flag is set.
         (!hash_present_ || (hash_ == other.hash_ && hashed_group_ == other.hashed_group_)) &&
         range_group_ == other.range_group_;
}

int DocKey::CompareTo(const DocKey& other) const {
  // Each table will only contain keys with hash present or absent, so we should never compare
  // keys from both categories.
  //
  // TODO: see how we can prevent this from ever happening in production. This might change
  //       if we decide to rethink DocDB's implementation of hash components as part of end-to-end
  //       integration of CQL's hash partition keys in December 2016.
  DCHECK_EQ(hash_present_, other.hash_present_);

  int result = 0;
  if (hash_present_) {
    result = GenericCompare(hash_, other.hash_);
    if (result != 0) return result;
  }
  result = CompareVectors(hashed_group_, other.hashed_group_);
  if (result != 0) return result;

  return CompareVectors(range_group_, other.range_group_);
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
      case DataType::INT32:
        new_doc_key.range_group_.emplace_back(*reinterpret_cast<const int32_t*>(raw_key));
        break;
      case DataType::STRING: FALLTHROUGH_INTENDED;
      case DataType::BINARY:
        new_doc_key.range_group_.emplace_back(reinterpret_cast<const Slice*>(raw_key)->ToString());
        break;

      default:
        LOG(FATAL) << "Decoding kudu data type " << type_info.name() << " is not supported";
    }
  }
  return new_doc_key;
}

DocKey DocKey::FromRedisStringKey(const string& key) {
  DocKey new_doc_key;
  new_doc_key.range_group_.emplace_back(key);
  return new_doc_key;
}

// ------------------------------------------------------------------------------------------------
// SubDocKey
// ------------------------------------------------------------------------------------------------

KeyBytes SubDocKey::Encode(bool include_timestamp) const {
  KeyBytes key_bytes = doc_key_.Encode();
  for (const auto& subkey : subkeys_) {
    subkey.AppendToKey(&key_bytes);
  }
  if (has_timestamp() && include_timestamp) {
    key_bytes.AppendValueType(ValueType::kTimestamp);
    key_bytes.AppendTimestamp(timestamp_);
  }
  return key_bytes;
}

Status SubDocKey::DecodeFrom(rocksdb::Slice* slice,
                             const bool require_timestamp) {
  const rocksdb::Slice original_bytes(*slice);

  Clear();
  RETURN_NOT_OK(doc_key_.DecodeFrom(slice));
  while (!slice->empty() &&
         *slice->data() != static_cast<char>(ValueType::kTimestamp)) {
    if (*slice->data() == '\xff' && !require_timestamp) {
      // A special case for easier debugging. In SubDocKey::AdvanceOutOfSubDoc we add '\xff' after
      // the last subkey to an encoded SubDocKey without a timestamp to seek to the next vector of
      // subkeys (or "jump out" of the current subdocument). We want such special-case keys to be
      // successfully decoded here as a SubDocKey with no timestamp followed by some raw bytes.
      return Status::OK();
    }
    subkeys_.emplace_back();
    auto& current_subkey = subkeys_.back();
    RETURN_NOT_OK_PREPEND(
        current_subkey.DecodeFromKey(slice),
        Substitute("While decoding SubDocKey $0", ToShortDebugStr(original_bytes)));
  }
  if (slice->size() < kBytesPerTimestamp + 1) {
    if (!require_timestamp) {
      return Status::OK();
    }
    return STATUS_SUBSTITUTE(
        Corruption,
        "Found too few bytes in the end of a SubDocKey for a type-prefixed timestamp: $0",
        ToShortDebugStr(*slice));
  }
  CHECK_EQ(to_underlying(ValueType::kTimestamp), slice->ConsumeByte());
  RETURN_NOT_OK(ConsumeTimestampFromKey(slice, &timestamp_));

  return Status::OK();
}

Status SubDocKey::FullyDecodeFrom(const rocksdb::Slice& slice,
                                  const bool require_timestamp) {
  rocksdb::Slice mutable_slice = slice;
  Status status = DecodeFrom(&mutable_slice, require_timestamp);
  if (!mutable_slice.empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Expected all bytes of the slice to be decoded into DocKey, found $0 extra bytes: $1",
        mutable_slice.size(), ToShortDebugStr(mutable_slice));
  }
  return status;
}

string SubDocKey::ToString() const {
  std::stringstream result;
  result << "SubDocKey(" << doc_key_.ToString() << ", [";

  bool need_comma = false;
  for (const auto& subkey : subkeys_) {
    if (need_comma) {
      result << ", ";
    }
    need_comma = true;
    result << subkey.ToString();
  }

  if (has_timestamp()) {
    if (need_comma) {
      result << "; ";
    }
    result << timestamp_.ToDebugString();
  }
  result << "])";
  return result.str();
}

void SubDocKey::Clear() {
  doc_key_.Clear();
  subkeys_.clear();
  timestamp_ = Timestamp::kInvalidTimestamp;
}

bool SubDocKey::StartsWith(const SubDocKey& prefix) const {
  return doc_key_ == prefix.doc_key_ &&
         // Subkeys precede the timestamp field in the encoded representation, so the timestamp
         // either has to be undefined in the prefix, or the entire key must match, including
         // subkeys and the timestamp (in this case the prefix is the same as this key).
         (!prefix.has_timestamp() ||
          (timestamp_ == prefix.timestamp_ && prefix.num_subkeys() == num_subkeys())) &&
         prefix.num_subkeys() <= num_subkeys() &&
         // std::mismatch finds the first difference between two sequences. Prior to C++14, the
         // behavior is undefined if the second range is shorter than the first range, so we make
         // sure the potentially shorter range is first.
         std::mismatch(
             prefix.subkeys_.begin(), prefix.subkeys_.end(), subkeys_.begin()
         ).first == prefix.subkeys_.end();
}

bool SubDocKey::operator ==(const SubDocKey& other) const {
  return doc_key_ == other.doc_key_ &&
         timestamp_ == other.timestamp_&&
         subkeys_ == other.subkeys_;
}

int SubDocKey::CompareTo(const SubDocKey& other) const {
  int result = doc_key_.CompareTo(other.doc_key_);
  if (result != 0) return result;

  // We specify reverse_second_component = true to implement inverse timestamp ordering.
  result = CompareVectors<PrimitiveValue>(subkeys_, other.subkeys_);
  if (result != 0) return result;

  // Timestamps are sorted in reverse order.
  return -timestamp_.CompareTo(other.timestamp_);
}

string BestEffortDocDBKeyToStr(const KeyBytes &key_bytes) {
  rocksdb::Slice mutable_slice(key_bytes.AsSlice());
  SubDocKey subdoc_key;
  Status decode_status = subdoc_key.DecodeFrom(&mutable_slice, /* require_timestamp = */ false);
  if (decode_status.ok()) {
    ostringstream ss;
    if (!subdoc_key.has_timestamp() && subdoc_key.num_subkeys() == 0) {
      // This is really just a DocKey.
      ss << subdoc_key.doc_key().ToString();
    } else {
      ss << subdoc_key.ToString();
    }
    if (mutable_slice.size() > 0) {
      ss << " followed by raw bytes " << FormatRocksDBSliceAsStr(mutable_slice);
      // Can append the above status of why we could not decode a SubDocKey, if needed.
    }
    return ss.str();
  }

  VLOG(4) << __func__ << ": could not decode " << key_bytes.ToString() << ", error: "
          << decode_status.ToString();

  // We could not decode a SubDocKey at all, even without a timestamp.
  return key_bytes.ToString();
}

std::string BestEffortDocDBKeyToStr(const rocksdb::Slice& slice) {
  return BestEffortDocDBKeyToStr(KeyBytes(slice));
}

void SubDocKey::ReplaceMaxTimestampWith(Timestamp timestamp) {
  if (timestamp_ == Timestamp::kMax) {
    timestamp_ = timestamp;
  }
}

int SubDocKey::NumSharedPrefixComponents(const SubDocKey& other) const {
  if (doc_key_ != other.doc_key_) {
    return 0;
  }
  const int min_num_subkeys = min(num_subkeys(), other.num_subkeys());
  for (int i = 0; i < min_num_subkeys; ++i) {
    if (subkeys_[i] != other.subkeys_[i]) {
      // If we found a mismatch at the first subkey (i = 0), but the DocKey matches, we return 1.
      // If one subkey matches but the second one (i = 1) is a mismatch, we return 2, etc.
      return i + 1;
    }
  }
  // The DocKey and all subkeys match up until the subkeys in one of the SubDocKeys are exhausted.
  return min_num_subkeys + 1;
}

KeyBytes SubDocKey::AdvanceOutOfSubDoc() {
  KeyBytes subdoc_key_no_ts = Encode(/* include_timestamp = */ false);
  subdoc_key_no_ts.AppendRawBytes("\xff", 1);
  return subdoc_key_no_ts;
}

}  // namespace docdb
}  // namespace yb
