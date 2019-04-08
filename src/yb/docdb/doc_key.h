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

#ifndef YB_DOCDB_DOC_KEY_H_
#define YB_DOCDB_DOC_KEY_H_

#include <ostream>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/filter_policy.h"

#include "yb/common/schema.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/primitive_value.h"

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {

using DocKeyHash = uint16_t;

// ------------------------------------------------------------------------------------------------
// DocKey
// ------------------------------------------------------------------------------------------------

// A key that allows us to locate a document. This is the prefix of all RocksDB keys of records
// inside this document. A document key contains:
//   - An optional fixed-width hash prefix.
//   - A group of primitive values representing "hashed" components (this is what the hash is
//     computed based on, so this group is present/absent together with the hash).
//   - A group of "range" components suitable for doing ordered scans.
//
// The encoded representation of the key is as follows:
//   - Optional fixed-width hash prefix, followed by hashed components:
//     * The byte ValueType::kUInt16Hash, followed by two bytes of the hash prefix.
//     * Hashed components:
//       1. Each hash component consists of a type byte (ValueType) followed by the encoded
//          representation of the respective type (see PrimitiveValue's key encoding).
//       2. ValueType::kGroupEnd terminates the sequence.
//   - Range components are stored similarly to the hashed components:
//     1. Each range component consists of a type byte (ValueType) followed by the encoded
//        representation of the respective type (see PrimitiveValue's key encoding).
//     2. ValueType::kGroupEnd terminates the sequence.
enum class DocKeyPart {
  WHOLE_DOC_KEY,
  HASHED_PART_ONLY
};

YB_STRONGLY_TYPED_BOOL(HybridTimeRequired)

class DocKey {
 public:
  // Constructs an empty document key with no hash component.
  DocKey();

  // Construct a document key with only a range component, but no hashed component.
  explicit DocKey(std::vector<PrimitiveValue> range_components);

  // Construct a document key including a hashed component and a range component. The hash value has
  // to be calculated outside of the constructor, and we're not assuming any specific hash function
  // here.
  // @param hash A hash value calculated using the appropriate hash function based on
  //             hashed_components.
  // @param hashed_components Components of the key that go into computing the hash prefix.
  // @param range_components Components of the key that we want to be able to do range scans on.
  DocKey(DocKeyHash hash,
         std::vector<PrimitiveValue> hashed_components,
         std::vector<PrimitiveValue> range_components = std::vector<PrimitiveValue>());

  // Constructors to create a DocKey for the given schema to support co-located tables.
  explicit DocKey(const Schema& schema);
  DocKey(const Schema& schema, DocKeyHash hash);
  DocKey(const Schema& schema, std::vector<PrimitiveValue> range_components);
  DocKey(const Schema& schema, DocKeyHash hash,
         std::vector<PrimitiveValue> hashed_components,
         std::vector<PrimitiveValue> range_components = std::vector<PrimitiveValue>());

  KeyBytes Encode() const;
  void AppendTo(KeyBytes* out) const;

  // Encodes DocKey to binary representation returning result as RefCntPrefix.
  RefCntPrefix EncodeAsRefCntPrefix() const;

  // Resets the state to an empty document key.
  void Clear();

  // Clear the range components of the document key only.
  void ClearRangeComponents();

  // Resize the range components:
  //  - drop elements (primitive values) from the end if new_size is smaller than the old size.
  //  - append kNull primitive values (default constructor) if new_size is bigger than the old size.
  void ResizeRangeComponents(int new_size);

  DocKeyHash hash() const {
    return hash_;
  }

  const std::vector<PrimitiveValue>& hashed_group() const {
    return hashed_group_;
  }

  const std::vector<PrimitiveValue>& range_group() const {
    return range_group_;
  }

  // Decodes a document key from the given RocksDB key.
  // slice (in/out) - a slice corresponding to a RocksDB key. Any consumed bytes are removed.
  // part_to_decode specifies which part of key to decode.
  CHECKED_STATUS DecodeFrom(rocksdb::Slice* slice,
      DocKeyPart part_to_decode = DocKeyPart::WHOLE_DOC_KEY);

  // Decodes a document key from the given RocksDB key similar to the above but return the number
  // of bytes decoded from the input slice.
  Result<size_t> DecodeFrom(const rocksdb::Slice& slice,
      DocKeyPart part_to_decode = DocKeyPart::WHOLE_DOC_KEY);

  // Splits given RocksDB key into vector of slices that forms range_group of document key.
  static CHECKED_STATUS PartiallyDecode(Slice* slice,
                                        boost::container::small_vector_base<Slice>* out);

  static Result<size_t> EncodedSize(Slice slice, DocKeyPart part);

  // Decode the current document key from the given slice, but expect all bytes to be consumed, and
  // return an error status if that is not the case.
  CHECKED_STATUS FullyDecodeFrom(const rocksdb::Slice& slice);

  // Converts the document key to a human-readable representation.
  std::string ToString() const;

  // Check if it is an empty key.
  bool empty() const {
    return !hash_present_ && range_group_.empty();
  }

  bool operator ==(const DocKey& other) const;

  bool operator !=(const DocKey& other) const {
    return !(*this == other);
  }

  bool HashedComponentsEqual(const DocKey& other) const;

  void AddRangeComponent(const PrimitiveValue& val);

  void SetRangeComponent(const PrimitiveValue& val, int idx);

  int CompareTo(const DocKey& other) const;

  bool operator <(const DocKey& other) const {
    return CompareTo(other) < 0;
  }

  bool operator <=(const DocKey& other) const {
    return CompareTo(other) <= 0;
  }

  bool operator >(const DocKey& other) const {
    return CompareTo(other) > 0;
  }

  bool operator >=(const DocKey& other) const {
    return CompareTo(other) >= 0;
  }

  bool BelongsTo(const Schema& schema) const {
    return cotable_id_ == schema.cotable_id();
  }

  void SwitchTo(const Schema& schema) {
    cotable_id_ = schema.cotable_id();
  }

  // Converts a redis string key to a doc key
  static DocKey FromRedisKey(uint16_t hash, const string& key);
  static KeyBytes EncodedFromRedisKey(uint16_t hash, const std::string &key);

 private:
  class DecodeFromCallback;
  friend class DecodeFromCallback;

  template<class Callback>
  static CHECKED_STATUS DoDecode(rocksdb::Slice* slice,
                                 DocKeyPart part_to_decode,
                                 const Callback& callback);

  // Uuid of the non-primary table this DocKey belongs to co-located in a tablet. Nil for the
  // primary or single-tenant table.
  Uuid cotable_id_;

  bool hash_present_;
  DocKeyHash hash_;
  std::vector<PrimitiveValue> hashed_group_;
  std::vector<PrimitiveValue> range_group_;
};

// Consume a group of document key components, ending with ValueType::kGroupEnd.
// @param slice - the current point at which we are decoding a key
// @param result - vector to append decoded values to.
Status ConsumePrimitiveValuesFromKey(rocksdb::Slice* slice,
                                     std::vector<PrimitiveValue>* result);

inline std::ostream& operator <<(std::ostream& out, const DocKey& doc_key) {
  out << doc_key.ToString();
  return out;
}

// ------------------------------------------------------------------------------------------------
// SubDocKey
// ------------------------------------------------------------------------------------------------

// A key pointing to a subdocument. Consists of a DocKey identifying the document, a list of
// primitive values leading to the subdocument in question, from the outermost to innermost order,
// and an optional hybrid_time of when the subdocument (which may itself be a primitive value) was
// last fully overwritten or deleted.
//
// Keys stored in RocksDB should always have the hybrid_time field set. However, it is useful to
// make the hybrid_time field optional while a SubDocKey is being constructed. If the hybrid_time
// is not set, it is omitted from the encoded representation of a SubDocKey.
//
// Implementation note: we use HybridTime::kInvalid to represent an omitted hybrid_time.
// We rely on that being the default-constructed value of a HybridTime.
//
// TODO: this should be renamed to something more generic, e.g. Key or LogicalKey, to reflect that
// this is actually the logical representation of keys that we store in the RocksDB key-value store.
class SubDocKey {
 public:
  SubDocKey() {}
  explicit SubDocKey(const DocKey& doc_key) : doc_key_(doc_key) {}
  explicit SubDocKey(DocKey&& doc_key) : doc_key_(std::move(doc_key)) {}

  SubDocKey(const DocKey& doc_key, HybridTime hybrid_time)
      : doc_key_(doc_key),
        doc_ht_(DocHybridTime(hybrid_time)) {
  }

  SubDocKey(DocKey&& doc_key,
            HybridTime hybrid_time)
      : doc_key_(std::move(doc_key)),
        doc_ht_(DocHybridTime(hybrid_time)) {
  }

  SubDocKey(const DocKey& doc_key, const DocHybridTime& hybrid_time)
      : doc_key_(doc_key),
        doc_ht_(std::move(hybrid_time)) {
  }

  SubDocKey(const DocKey& doc_key,
            DocHybridTime doc_hybrid_time,
            const std::vector<PrimitiveValue>& subkeys)
      : doc_key_(doc_key),
        doc_ht_(doc_hybrid_time),
        subkeys_(subkeys) {
  }

  SubDocKey(const DocKey& doc_key,
            HybridTime hybrid_time,
            const std::vector<PrimitiveValue>& subkeys)
      : doc_key_(doc_key),
        doc_ht_(DocHybridTime(hybrid_time)),
        subkeys_(subkeys) {
  }

  template <class ...T>
  SubDocKey(const DocKey& doc_key, T... subkeys_and_maybe_hybrid_time)
      : doc_key_(doc_key),
        doc_ht_(DocHybridTime::kInvalid) {
    AppendSubKeysAndMaybeHybridTime(subkeys_and_maybe_hybrid_time...);
  }

  CHECKED_STATUS FromDocPath(const DocPath& doc_path);

  // Return the subkeys within this SubDocKey
  const std::vector<PrimitiveValue>& subkeys() const {
    return subkeys_;
  }

  // Append a sequence of sub-keys to this key.
  template<class ...T>
  void AppendSubKeysAndMaybeHybridTime(PrimitiveValue subdoc_key,
                                       T... subkeys_and_maybe_hybrid_time) {
    subkeys_.emplace_back(subdoc_key);
    AppendSubKeysAndMaybeHybridTime(subkeys_and_maybe_hybrid_time...);
  }

  template<class ...T>
  void AppendSubKeysAndMaybeHybridTime(PrimitiveValue subdoc_key) {
    subkeys_.emplace_back(subdoc_key);
  }

  template<class ...T>
  void AppendSubKeysAndMaybeHybridTime(PrimitiveValue subdoc_key, HybridTime hybrid_time) {
    DCHECK(!has_hybrid_time());
    subkeys_.emplace_back(subdoc_key);
    DCHECK(hybrid_time.is_valid());
    doc_ht_ = DocHybridTime(hybrid_time);
  }

  void RemoveLastSubKey() {
    DCHECK(!subkeys_.empty());
    subkeys_.pop_back();
  }

  void KeepPrefix(int num_sub_keys_to_keep) {
    if (subkeys_.size() > num_sub_keys_to_keep) {
      subkeys_.resize(num_sub_keys_to_keep);
    }
  }

  void remove_hybrid_time() {
    doc_ht_ = DocHybridTime::kInvalid;
  }

  void Clear();

  bool IsValid() const {
    return !doc_key_.empty();
  }

  KeyBytes Encode() const { return DoEncode(true /* include_hybrid_time */); }
  KeyBytes EncodeWithoutHt() const { return DoEncode(false /* include_hybrid_time */); }

  // Decodes a SubDocKey from the given slice, typically retrieved from a RocksDB key.
  // @param slice
  //     A pointer to the slice containing the bytes to decode the SubDocKey from. This slice is
  //     modified, with consumed bytes being removed.
  // @param require_hybrid_time
  //     Whether a hybrid_time is required in the end of the SubDocKey. If this is true, we require
  //     a ValueType::kHybridTime byte followed by a hybrid_time to be present in the input slice.
  //     Otherwise, we allow decoding an incomplete SubDocKey without a hybrid_time in the end. Note
  //     that we also allow input that has a few bytes in the end but not enough to represent a
  //     hybrid_time.
  CHECKED_STATUS DecodeFrom(rocksdb::Slice* slice,
                            HybridTimeRequired require_hybrid_time = HybridTimeRequired::kTrue);

  // Similar to DecodeFrom, but requires that the entire slice is decoded, and thus takes a const
  // reference to a slice. This still respects the require_hybrid_time parameter, but in case a
  // hybrid_time is omitted, we don't allow any extra bytes to be present in the slice.
  CHECKED_STATUS FullyDecodeFrom(
      const rocksdb::Slice& slice,
      HybridTimeRequired hybrid_time_required = HybridTimeRequired::kTrue);

  // Splits given RocksDB key into vector of slices that forms range_group of document key and
  // hybrid_time.
  static CHECKED_STATUS PartiallyDecode(Slice* slice,
                                        boost::container::small_vector_base<Slice>* out);

  // Splits the given RocksDB sub key into a vector of slices that forms the range group of document
  // key and sub keys.
  //
  // We don't use Result<...> to be able to reuse memory allocated by out.
  //
  // When key does not have hash component first returned prefix would contain first range
  // component.
  //
  // For instance for (h, r1, r2, s1) doc key the following values will be returned:
  // encoded_length(h), encoded_length(h, r1), encoded_length(h, r1, r2),
  // encoded_length(h, r1, r2, s1).
  static CHECKED_STATUS DecodePrefixLengths(
      Slice slice, boost::container::small_vector_base<size_t>* out);

  static Result<bool> DecodeSubkey(Slice* slice);

  CHECKED_STATUS FullyDecodeFromKeyWithOptionalHybridTime(const rocksdb::Slice& slice) {
    return FullyDecodeFrom(slice, HybridTimeRequired::kFalse);
  }

  std::string ToString() const;
  static std::string DebugSliceToString(Slice slice);

  const DocKey& doc_key() const {
    return doc_key_;
  }

  int num_subkeys() const {
    return subkeys_.size();
  }

  bool StartsWith(const SubDocKey& prefix) const;

  bool operator ==(const SubDocKey& other) const;

  bool operator !=(const SubDocKey& other) const {
    return !(*this == other);
  }

  const PrimitiveValue& last_subkey() const {
    assert(!subkeys_.empty());
    return subkeys_.back();
  }

  int CompareTo(const SubDocKey& other) const;
  int CompareToIgnoreHt(const SubDocKey& other) const;

  bool operator <(const SubDocKey& other) const {
    return CompareTo(other) < 0;
  }

  bool operator <=(const SubDocKey& other) const {
    return CompareTo(other) <= 0;
  }

  bool operator >(const SubDocKey& other) const {
    return CompareTo(other) > 0;
  }

  bool operator >=(const SubDocKey& other) const {
    return CompareTo(other) >= 0;
  }

  HybridTime hybrid_time() const {
    DCHECK(has_hybrid_time());
    return doc_ht_.hybrid_time();
  }

  const DocHybridTime& doc_hybrid_time() const {
    DCHECK(has_hybrid_time());
    return doc_ht_;
  }

  void set_hybrid_time(const DocHybridTime& hybrid_time) {
    DCHECK(hybrid_time.is_valid());
    doc_ht_ = hybrid_time;
  }

  // Sets HybridTime with the maximum write id so that the reader can see the values written at
  // exactly the given hybrid time. Useful when constructing a seek key.
  void SetHybridTimeForReadPath(HybridTime hybrid_time) {
    DCHECK(hybrid_time.is_valid());
    doc_ht_ = DocHybridTime(hybrid_time, kMaxWriteId);
  }

  bool has_hybrid_time() const {
    return doc_ht_.is_valid();
  }

  // @return The number of initial components (including document key and subkeys) that this
  //         SubDocKey shares with another one. This does not care about the hybrid_time field.
  int NumSharedPrefixComponents(const SubDocKey& other) const;

  // Generate a RocksDB key that would allow us to seek to the smallest SubDocKey that has a
  // lexicographically higher sequence of subkeys than this one, but is not an extension of this
  // sequence of subkeys.  In other words, ensure we advance to the next field (subkey) either
  // within the object (subdocument) we are currently scanning, or at any higher level, including
  // advancing to the next document key.
  //
  // E.g. assuming the SubDocKey this is being called on is #2 from the following example,
  // performing a RocksDB seek on the return value of this takes us to #7.
  //
  // 1. SubDocKey(DocKey([], ["a"]), [HT(1)]) -> {}
  // 2. SubDocKey(DocKey([], ["a"]), ["x", HT(1)]) -> {} ---------------------------.
  // 3. SubDocKey(DocKey([], ["a"]), ["x", "x", HT(2)]) -> null                     |
  // 4. SubDocKey(DocKey([], ["a"]), ["x", "x", HT(1)]) -> {}                       |
  // 5. SubDocKey(DocKey([], ["a"]), ["x", "x", "y", HT(1)]) -> {}                  |
  // 6. SubDocKey(DocKey([], ["a"]), ["x", "x", "y", "x", HT(1)]) -> true           |
  // 7. SubDocKey(DocKey([], ["a"]), ["y", HT(3)]) -> {}                  <---------
  // 8. SubDocKey(DocKey([], ["a"]), ["y", "y", HT(3)]) -> {}
  // 9. SubDocKey(DocKey([], ["a"]), ["y", "y", "x", HT(3)]) ->
  //
  // This is achieved by simply appending a byte that is higher than any ValueType in an encoded
  // representation of a SubDocKey that extends the vector of subkeys present in the current one,
  // or has the same vector of subkeys, i.e. key/value pairs #3-6 in the above example. HybridTime
  // is omitted from the resulting encoded representation.
  KeyBytes AdvanceOutOfSubDoc() const;

  // Similar to AdvanceOutOfSubDoc, but seek to the smallest key that skips documents with this
  // DocKey and DocKeys that have the same hash components but add more range components to it.
  //
  // E.g. assuming the SubDocKey this is being called on is #2 from the following example:
  //
  //  1. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), [HT(1)]) -> {}
  //  2. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["x", HT(1)]) -> {} <----------------.
  //  3. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["x", "x", HT(2)]) -> null           |
  //  4. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["x", "x", HT(1)]) -> {}             |
  //  5. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["x", "x", "y", HT(1)]) -> {}        |
  //  6. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["x", "x", "y", "x", HT(1)]) -> true |
  //  7. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["y", HT(3)]) -> {}                  |
  //  8. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["y", "y", HT(3)]) -> {}             |
  //  9. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"]), ["y", "y", "x", HT(3)]) -> {}        |
  // ...                                                                                        |
  // 20. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d", "e"]), ["y", HT(3)]) -> {}             |
  // 21. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d", "e"]), ["z", HT(3)]) -> {}             |
  // 22. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "f"]), [HT(1)]) -> {}      <--- (*** 1 ***)-|
  // 23. SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "f"]), ["x", HT(1)]) -> {}                  |
  // ...                                                                                        |
  // 30. SubDocKey(DocKey(0x2345, ["a", "c"], ["c", "f"]), [HT(1)]) -> {}      <--- (*** 2 ***)-
  // 31. SubDocKey(DocKey(0x2345, ["a", "c"], ["c", "f"]), ["x", HT(1)]) -> {}
  //
  // SubDocKey(DocKey(0x1234, ["a", "b"], ["c", "d"])).AdvanceOutOfDocKeyPrefix() will seek to #22
  // (*** 1 ***), pass doc keys with additional range components when they are present.
  //
  // And when given a doc key without range component like below, it can help seek pass all doc
  // keys with the same hash components, e.g.
  // SubDocKey(DocKey(0x1234, ["a", "b"], [])).AdvanceOutOfDocKeyPrefix() will seek to #30
  // (*** 2 ***).

  KeyBytes AdvanceOutOfDocKeyPrefix() const;

 private:
  class DecodeCallback;
  friend class DecodeCallback;

  // Attempts to decode and consume a subkey from the beginning of the given slice.
  // A non-error false result means e.g. that the slice is empty or if the next thing is an encoded
  // hybrid time.
  template<class Callback>
  static Result<bool> DecodeSubkey(Slice* slice, const Callback& callback);

  template<class Callback>
  static Status DoDecode(rocksdb::Slice* slice,
                         HybridTimeRequired require_hybrid_time,
                         const Callback& callback);

  KeyBytes DoEncode(bool include_hybrid_time) const;

  DocKey doc_key_;
  DocHybridTime doc_ht_;

  // TODO: make this a small_vector.
  std::vector<PrimitiveValue> subkeys_;
};

inline std::ostream& operator <<(std::ostream& out, const SubDocKey& subdoc_key) {
  out << subdoc_key.ToString();
  return out;
}

// A best-effort to decode the given sequence of key bytes as either a DocKey or a SubDocKey.
// If not possible to decode, return the key_bytes directly as a readable string.
std::string BestEffortDocDBKeyToStr(const KeyBytes &key_bytes);
std::string BestEffortDocDBKeyToStr(const rocksdb::Slice &slice);

// This filter policy only takes into account hashed components of keys for filtering.
class DocDbAwareFilterPolicy : public rocksdb::FilterPolicy {
 public:
  explicit DocDbAwareFilterPolicy(size_t filter_block_size_bits, rocksdb::Logger* logger) {
    builtin_policy_.reset(rocksdb::NewFixedSizeFilterPolicy(
        filter_block_size_bits, rocksdb::FilterPolicy::kDefaultFixedSizeFilterErrorRate, logger));
  }

  const char* Name() const override { return "DocKeyHashedComponentsFilter"; }

  void CreateFilter(const rocksdb::Slice* keys, int n, std::string* dst) const override;

  bool KeyMayMatch(const rocksdb::Slice& key, const rocksdb::Slice& filter) const override;

  rocksdb::FilterBitsBuilder* GetFilterBitsBuilder() const override;

  rocksdb::FilterBitsReader* GetFilterBitsReader(const rocksdb::Slice& contents) const override;

  FilterType GetFilterType() const override;

  const KeyTransformer* GetKeyTransformer() const override;

 private:
  std::unique_ptr<const rocksdb::FilterPolicy> builtin_policy_;
};

// Combined DB to store regular records and intents.
struct DocDB {
  rocksdb::DB* regular;
  rocksdb::DB* intents;

  static DocDB FromRegular(rocksdb::DB* regular) {
    return {regular, nullptr /* intents */};
  }
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_KEY_H_
