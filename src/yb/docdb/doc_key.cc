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
using yb::util::CompareUsingLessThan;

namespace yb {
namespace docdb {

Status ConsumePrimitiveValuesFromKey(rocksdb::Slice* slice, vector<PrimitiveValue>* result) {
  const auto initial_slice(*slice);  // For error reporting.
  while (true) {
    if (PREDICT_FALSE(slice->empty())) {
      return STATUS(Corruption, "Unexpected end of key when decoding document key");
    }
    ValueType current_value_type = static_cast<ValueType>(*slice->data());
    if (current_value_type == ValueType::kGroupEnd) {
      slice->ConsumeByte();
      return Status::OK();
    }
    if (PREDICT_FALSE(!IsPrimitiveValueType(current_value_type))) {
      return STATUS_SUBSTITUTE(Corruption,
          "Expected a primitive value type, got $0",
          ValueTypeToStr(current_value_type));
    }
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
    result.AppendValueType(ValueType::kUInt16Hash);
    result.AppendUInt16(hash_);
    AppendDocKeyItems(hashed_group_, &result);
  }
  AppendDocKeyItems(range_group_, &result);
  return result;
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

  if (first_value_type == ValueType::kUInt16Hash) {
    if (slice->size() >= sizeof(DocKeyHash) + 1) {
      // We'll need to update this code if we ever change the size of the hash field.
      static_assert(sizeof(DocKeyHash) == sizeof(uint16_t),
          "It looks like the DocKeyHash's size has changed -- need to update encoder/decoder.");
      hash_ = BigEndian::Load16(slice->data() + 1);
      hash_present_ = true;
      slice->remove_prefix(sizeof(DocKeyHash) + 1);
    } else {
      return STATUS_SUBSTITUTE(Corruption,
          "Could not decode a 16-bit hash component of a document key: only $0 bytes left",
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
    result += StringPrintf("0x%04x", hash_);
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
    result = CompareUsingLessThan(hash_, other.hash_);
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
      case DataType::INT8:
        new_doc_key.range_group_.emplace_back(*reinterpret_cast<const int8_t*>(raw_key));
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

DocKey DocKey::FromRedisKey(uint16_t hash, const string &key) {
  DocKey new_doc_key;
  new_doc_key.hash_present_ = true;
  new_doc_key.hash_ = hash;
  new_doc_key.hashed_group_.emplace_back(key);
  return new_doc_key;
}

// ------------------------------------------------------------------------------------------------
// SubDocKey
// ------------------------------------------------------------------------------------------------

KeyBytes SubDocKey::Encode(bool include_hybrid_time) const {
  KeyBytes key_bytes = doc_key_.Encode();
  for (const auto& subkey : subkeys_) {
    subkey.AppendToKey(&key_bytes);
  }
  if (has_hybrid_time() && include_hybrid_time) {
    key_bytes.AppendValueType(ValueType::kHybridTime);
    doc_ht_.AppendEncodedInDocDbFormat(key_bytes.mutable_data());
  }
  return key_bytes;
}

Status SubDocKey::DecodeFrom(rocksdb::Slice* slice, const bool require_hybrid_time) {
  const rocksdb::Slice original_bytes(*slice);

  Clear();
  RETURN_NOT_OK(doc_key_.DecodeFrom(slice));
  while (!slice->empty() && *slice->data() != static_cast<char>(ValueType::kHybridTime)) {
    subkeys_.emplace_back();
    RETURN_NOT_OK_PREPEND(
        subkeys_.back().DecodeFromKey(slice),
        Substitute("While decoding SubDocKey $0", ToShortDebugStr(original_bytes)));
  }
  if (slice->empty()) {
    if (!require_hybrid_time) {
      doc_ht_ = DocHybridTime::kInvalid;
      return Status::OK();
    }
    return STATUS_SUBSTITUTE(
        Corruption,
        "Found too few bytes in the end of a SubDocKey for a type-prefixed hybrid_time: $0",
        ToShortDebugStr(*slice));
  }

  // The reason the following is not handled as a Status is that the logic above (loop + emptiness
  // check) should guarantee this is the only possible case left.
  DCHECK_EQ(ValueType::kHybridTime, DecodeValueType(*slice));
  slice->ConsumeByte();

  RETURN_NOT_OK(ConsumeHybridTimeFromKey(slice, &doc_ht_));

  return Status::OK();
}

Status SubDocKey::FullyDecodeFrom(const rocksdb::Slice& slice,
                                  const bool require_hybrid_time) {
  rocksdb::Slice mutable_slice = slice;
  Status status = DecodeFrom(&mutable_slice, require_hybrid_time);
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

  if (has_hybrid_time()) {
    if (need_comma) {
      result << "; ";
    }
    result << doc_ht_.ToString();
  }
  result << "])";
  return result.str();
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

bool SubDocKey::operator ==(const SubDocKey& other) const {
  return doc_key_ == other.doc_key_ &&
         doc_ht_ == other.doc_ht_&&
         subkeys_ == other.subkeys_;
}

int SubDocKey::CompareTo(const SubDocKey& other) const {
  int result = doc_key_.CompareTo(other.doc_key_);
  if (result != 0) return result;

  // We specify reverse_second_component = true to implement inverse hybrid_time ordering.
  result = CompareVectors<PrimitiveValue>(subkeys_, other.subkeys_);
  if (result != 0) return result;

  // HybridTimes are sorted in reverse order.
  return -doc_ht_.CompareTo(other.doc_ht_);
}

string BestEffortDocDBKeyToStr(const KeyBytes &key_bytes) {
  rocksdb::Slice mutable_slice(key_bytes.AsSlice());
  SubDocKey subdoc_key;
  Status decode_status = subdoc_key.DecodeFrom(&mutable_slice, /* require_hybrid_time = */ false);
  if (decode_status.ok()) {
    ostringstream ss;
    if (!subdoc_key.has_hybrid_time() && subdoc_key.num_subkeys() == 0) {
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

  // We could not decode a SubDocKey at all, even without a hybrid_time.
  return key_bytes.ToString();
}

std::string BestEffortDocDBKeyToStr(const rocksdb::Slice& slice) {
  return BestEffortDocDBKeyToStr(KeyBytes(slice));
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

KeyBytes SubDocKey::AdvanceOutOfSubDoc() const {
  KeyBytes subdoc_key_no_ts = Encode(/* include_hybrid_time = */ false);
  subdoc_key_no_ts.AppendRawBytes("\xff", 1);
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
  doc_key_encoded.RemoveValueTypeSuffix(ValueType::kGroupEnd);
  doc_key_encoded.AppendRawBytes("\xff", 1);
  return doc_key_encoded;
}

// ------------------------------------------------------------------------------------------------
// DocDbAwareFilterPolicy
// ------------------------------------------------------------------------------------------------

class CustomFilterBitsBuilder : public rocksdb::FilterBitsBuilder {
 public:
  CustomFilterBitsBuilder() : policy_(new DocDbAwareFilterPolicy()) {}

  void AddKey(const rocksdb::Slice& key) override {
    // Copying the data in.
    string_keys_.push_back(std::string(key.data(), key.size()));
  }

  rocksdb::Slice Finish(std::unique_ptr<const char[]>* buf) override {
    CHECK_LE(string_keys_.size(), std::numeric_limits<int>::max());
    // Generate the required Slice[] input.
    const int num_input_slices = string_keys_.size();
    std::unique_ptr<rocksdb::Slice[]> raw_slices(new rocksdb::Slice[num_input_slices]);
    for (int i = 0; i < num_input_slices; ++i) {
      raw_slices[i] = rocksdb::Slice(string_keys_[i]);
    }
    // Create the actual filter using the Slice[] data and a new unique_ptr for safeguarding the
    // destination memory.
    std::string dst;
    policy_->CreateFilter(raw_slices.get(), num_input_slices, &dst);
    // Clear the current memory.
    string_keys_.clear();
    raw_slices.reset();
    // Go over the filter data and set out params.
    int32_t output_size = dst.size();
    std::unique_ptr<char[]> char_buf(new char[output_size]);
    std::copy(dst.begin(), dst.end(), char_buf.get());
    buf->reset(char_buf.release());
    return rocksdb::Slice(buf->get(), output_size);
  }

 private:
  std::vector<std::string> string_keys_;
  std::unique_ptr<DocDbAwareFilterPolicy> policy_;
};

class CustomFilterBitsReader : public rocksdb::FilterBitsReader {
 public:
  explicit CustomFilterBitsReader(const rocksdb::Slice& contents)
      : filter_(contents), policy_(new DocDbAwareFilterPolicy()) {}

  bool MayMatch(const rocksdb::Slice& entry) override {
    return policy_->KeyMayMatch(entry, filter_);
  }

 private:
  rocksdb::Slice filter_;
  std::unique_ptr<DocDbAwareFilterPolicy> policy_;
};

void DocDbAwareFilterPolicy::CreateFilter(
    const rocksdb::Slice* keys, int n, std::string* dst) const {
  CHECK_GT(n, 0);
  std::unique_ptr<rocksdb::Slice[]> decoded_keys(new rocksdb::Slice[n]);
  for (int i = 0; i < n; ++i) {
    int32_t offset = GetEncodedDocKeyPrefixSize(keys[i]);
    decoded_keys[i] = rocksdb::Slice(keys[i].data(), offset);
  }
  return builtin_policy_->CreateFilter(decoded_keys.get(), n, dst);
}

bool DocDbAwareFilterPolicy::KeyMayMatch(
    const rocksdb::Slice& key, const rocksdb::Slice& filter) const {
  int32_t offset = GetEncodedDocKeyPrefixSize(key);
  return builtin_policy_->KeyMayMatch(rocksdb::Slice(key.data(), offset), filter);
}

int32_t DocDbAwareFilterPolicy::GetEncodedDocKeyPrefixSize(const rocksdb::Slice& slice) {
  // Copy the slice.
  rocksdb::Slice copy(slice);
  // Decode the slice as a SubDocKey.
  docdb::DocKey encoded_key;
  // TODO: don't check, but return errors somehow?
  CHECK_OK(encoded_key.DecodeFrom(&copy));
  // Return the offset in the initial slice that represents the encoded DocKey only.
  return slice.size() - copy.size();
}

rocksdb::FilterBitsBuilder* DocDbAwareFilterPolicy::GetFilterBitsBuilder() const {
  return new CustomFilterBitsBuilder();
}

rocksdb::FilterBitsReader* DocDbAwareFilterPolicy::GetFilterBitsReader(
    const rocksdb::Slice& contents) const {
  return new CustomFilterBitsReader(contents);
}

}  // namespace docdb
}  // namespace yb
