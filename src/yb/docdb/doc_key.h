// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_KEY_H_
#define YB_DOCDB_DOC_KEY_H_

#include <ostream>
#include <vector>

#include "rocksdb/filter_policy.h"
#include "rocksdb/slice.h"

#include "yb/common/encoded_key.h"
#include "yb/common/schema.h"
#include "yb/docdb/primitive_value.h"

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
class DocKey {
 public:
  // Constructs an empty document key with no hash component.
  DocKey();

  // Construct a document key with only a range component, but no hashed component.
  explicit DocKey(const std::vector<PrimitiveValue>& range_components);

  // Construct a document key including a hashed component and a range component. The hash value has
  // to be calculated outside of the constructor, and we're not assuming any specific hash function
  // here.
  // @param hash A hash value calculated using the appropriate hash function based on
  //             hashed_components.
  // @param hashed_components Components of the key that go into computing the hash prefix.
  // @param range_components Components of the key that we want to be able to do range scans on.
  DocKey(DocKeyHash hash,
         const std::vector<PrimitiveValue>& hashed_components,
         const std::vector<PrimitiveValue>& range_components = std::vector<PrimitiveValue>());

  KeyBytes Encode() const;

  // Resets the state to an empty document key.
  void Clear();

  // Clear the range components of the document key only.
  void ClearRangeComponents();

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
  CHECKED_STATUS DecodeFrom(rocksdb::Slice* slice);

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

  int CompareTo(const DocKey& other) const;

  bool operator <(const DocKey& other) const {
    return CompareTo(other) < 0;
  }

  bool operator >(const DocKey& other) const {
    return CompareTo(other) > 0;
  }

  // Converts the given Kudu encoded key to a DocKey. It looks like Kudu's EncodedKey assumes all
  // fields are non-null, so we have the same assumption here. In fact, there does not seem to
  // even be a way to encode null fields in an EncodedKey.
  static DocKey FromKuduEncodedKey(const EncodedKey& encoded_key, const Schema& schema);

  // Converts a redis string key to a doc key
  static DocKey FromRedisKey(uint16_t hash, const string& key);

 private:

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
// Implementation note: we use HybridTime::kInvalidHybridTime to represent an omitted hybrid_time.
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

  static SubDocKey MakeSeekKey(const DocKey& doc_key, HybridTime hybrid_time) {
    return SubDocKey(doc_key, DocHybridTime(hybrid_time, kMaxWriteId));
  }

  SubDocKey(const DocKey& doc_key,
            HybridTime hybrid_time,
            std::vector<PrimitiveValue> subkeys)
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

  // Return the subkeys within this SubDocKey
  const std::vector<PrimitiveValue>& subkeys() const {
    return subkeys_;
  }

  // Append a sequence of sub-keys to this key. We require that the hybrid_time is not set, because
  // we append it last.
  template<class ...T>
  void AppendSubKeysAndMaybeHybridTime(PrimitiveValue subdoc_key,
                                       T... subkeys_and_maybe_hybrid_time) {
    EnsureHasNoHybridTimeYet();
    subkeys_.emplace_back(subdoc_key);
    AppendSubKeysAndMaybeHybridTime(subkeys_and_maybe_hybrid_time...);
  }

  template<class ...T>
  void AppendSubKeysAndMaybeHybridTime(PrimitiveValue subdoc_key) {
    EnsureHasNoHybridTimeYet();
    subkeys_.emplace_back(subdoc_key);
  }

  template<class ...T>
  void AppendSubKeysAndMaybeHybridTime(PrimitiveValue subdoc_key, HybridTime hybrid_time) {
    DCHECK(!has_hybrid_time());
    subkeys_.emplace_back(subdoc_key);
    DCHECK_NE(HybridTime::kInvalidHybridTime, hybrid_time);
    doc_ht_ = DocHybridTime(hybrid_time);
  }

  void RemoveLastSubKey() {
    DCHECK(!subkeys_.empty());
    subkeys_.pop_back();
  }

  void remove_hybrid_time() {
    doc_ht_ = DocHybridTime::kInvalid;
  }

  void Clear();

  KeyBytes Encode(bool include_hybrid_time = true) const;

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
  CHECKED_STATUS DecodeFrom(rocksdb::Slice* slice, bool require_hybrid_time = true);

  // Similar to DecodeFrom, but requires that the entire slice is decoded, and thus takes a const
  // reference to a slice. This still respects the require_hybrid_time parameter, but in case a
  // hybrid_time is omitted, we don't allow any extra bytes to be present in the slice.
  CHECKED_STATUS FullyDecodeFrom(
      const rocksdb::Slice& slice,
      bool require_hybrid_time = true);

  CHECKED_STATUS FullyDecodeFromKeyWithoutHybridTime(const rocksdb::Slice& slice) {
    return FullyDecodeFrom(slice, /* require_hybrid_time = */ false);
  }

  std::string ToString() const;

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

  void RemoveHybridTime() {
    doc_ht_ = DocHybridTime::kInvalid;
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
  DocKey doc_key_;
  DocHybridTime doc_ht_;
  std::vector<PrimitiveValue> subkeys_;

  void EnsureHasNoHybridTimeYet() {
    DCHECK(!has_hybrid_time())
        << "Trying to append a primitive value to a SubDocKey " << ToString()
        << " that already has a hybrid_time set: " << doc_ht_.ToString();
  }
};

inline std::ostream& operator <<(std::ostream& out, const SubDocKey& subdoc_key) {
  out << subdoc_key.ToString();
  return out;
}

// A best-effort to decode the given sequence of key bytes as either a DocKey or a SubDocKey.
// If not possible to decode, return the key_bytes directly as a readable string.
std::string BestEffortDocDBKeyToStr(const KeyBytes &key_bytes);
std::string BestEffortDocDBKeyToStr(const rocksdb::Slice &slice);

class DocDbAwareFilterPolicy : public rocksdb::FilterPolicy {
 public:
  // Use the full file bloom filter and 10 bits, by default.
  DocDbAwareFilterPolicy() { builtin_policy_.reset(rocksdb::NewBloomFilterPolicy(10, false)); }

  const char* Name() const override { return "DocDbAwareFilterPolicy"; }

  void CreateFilter(const rocksdb::Slice* keys, int n, std::string* dst) const override;

  bool KeyMayMatch(const rocksdb::Slice& key, const rocksdb::Slice& filter) const override;

  rocksdb::FilterBitsBuilder* GetFilterBitsBuilder() const override;

  rocksdb::FilterBitsReader* GetFilterBitsReader(const rocksdb::Slice& contents) const override;

 private:
  static int32_t GetEncodedDocKeyPrefixSize(const rocksdb::Slice& slice);

  std::unique_ptr<const rocksdb::FilterPolicy> builtin_policy_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_KEY_H_
