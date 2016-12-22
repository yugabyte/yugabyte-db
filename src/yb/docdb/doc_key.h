// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_KEY_H_
#define YB_DOCDB_DOC_KEY_H_

#include <ostream>
#include <vector>

#include "rocksdb/slice.h"

#include "yb/common/encoded_key.h"
#include "yb/common/schema.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

using DocKeyHash = uint32_t;

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
//     * The byte ValueType::kUnit32Hash, followed by four bytes of the hash prefix.
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
         const std::vector<PrimitiveValue>& range_components);

  KeyBytes Encode() const;

  // Resets the state to an empty document key.
  void Clear();

  const std::vector<PrimitiveValue>& range_group() const {
    return range_group_;
  }

  // Decodes a document key from the given RocksDB key.
  // slice (in/out) - a slice corresponding to a RocksDB key. Any consumed bytes are removed.
  Status DecodeFrom(rocksdb::Slice* slice);

  // Decode the current document key from the given slice, but expect all bytes to be consumed, and
  // return an error status if that is not the case.
  Status FullyDecodeFrom(const rocksdb::Slice& slice);

  // Converts the document key to a human-readable representation.
  std::string ToString() const;

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
  static DocKey FromRedisStringKey(const string& key);

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
// and an optional timestamp of when the subdocument (which may itself be a primitive value) was
// last fully overwritten or deleted.
//
// Keys stored in RocksDB should always have the timestamp field set. However, it is useful to make
// the timestamp field optional while a SubDocKey is being constructed. If the timestamp is not set,
// it is omitted from the encoded representation of a SubDocKey.
//
// Implementation note: we use Timestamp::kInvalidTimestamp to represent an omitted timestamp.  We
// rely on that being the default-constructed value of a Timestamp.
//
// TODO: this should be renamed to something more generic, e.g. Key or LogicalKey, to reflect that
// this is actually the logical representation of keys that we store in the RocksDB key-value store.
class SubDocKey {
 public:
  SubDocKey() {}
  explicit SubDocKey(const DocKey& doc_key) : doc_key_(doc_key) {}
  explicit SubDocKey(DocKey&& doc_key) : doc_key_(std::move(doc_key)) {}

  SubDocKey(const DocKey& doc_key, Timestamp timestamp)
      : doc_key_(doc_key),
        timestamp_(timestamp) {
  }

  SubDocKey(DocKey&& doc_key,
            Timestamp timestamp)
      : doc_key_(std::move(doc_key)),
        timestamp_(timestamp) {
  }

  SubDocKey(const DocKey& doc_key,
            Timestamp timestamp,
            std::vector<PrimitiveValue> subkeys)
      : doc_key_(doc_key),
        timestamp_(timestamp),
        subkeys_(subkeys) {
  }

  template <class ...T>
  SubDocKey(const DocKey& doc_key, T... subkeys_and_maybe_timestamp)
      : doc_key_(doc_key),
        timestamp_(Timestamp::kInvalidTimestamp) {
    AppendSubKeysAndMaybeTimestamp(subkeys_and_maybe_timestamp...);
  }

  // Append a sequence of sub-keys to this key. We require that the timestamp is not set, because
  // we append it last.
  template<class ...T>
  void AppendSubKeysAndMaybeTimestamp(PrimitiveValue subdoc_key,
                                      T... subkeys_and_maybe_timestamp) {
    EnsureHasNoTimestampYet();
    subkeys_.emplace_back(subdoc_key);
    AppendSubKeysAndMaybeTimestamp(subkeys_and_maybe_timestamp...);
  }

  template<class ...T>
  void AppendSubKeysAndMaybeTimestamp(PrimitiveValue subdoc_key) {
    EnsureHasNoTimestampYet();
    subkeys_.emplace_back(subdoc_key);
  }

  template<class ...T>
  void AppendSubKeysAndMaybeTimestamp(PrimitiveValue subdoc_key, Timestamp timestamp) {
    DCHECK_EQ(Timestamp::kInvalidTimestamp, timestamp_);
    subkeys_.emplace_back(subdoc_key);
    DCHECK_NE(Timestamp::kInvalidTimestamp, timestamp);
    timestamp_ = timestamp;
  }

  void RemoveLastSubKey() {
    DCHECK(!subkeys_.empty());
    subkeys_.pop_back();
  }

  void Clear();

  KeyBytes Encode(bool include_timestamp = true) const;

  // Decodes a SubDocKey from the given slice, typically retrieved from a RocksDB key.
  // @param slice
  //     A pointer to the slice containing the bytes to decode the SubDocKey from. This slice is
  //     modified, with consumed bytes being removed.
  // @param require_timestamp
  //     Whether a timestamp is required in the end of the SubDocKey. If this is true, we require
  //     a ValueType::kTimestamp byte followed by a timestamp to be present in the input slice.
  //     Otherwise, we allow decoding an incomplete SubDocKey without a timestamp in the end. Note
  //     that we also allow input that has a few bytes in the end but not enough to represent a
  //     timestamp.
  Status DecodeFrom(rocksdb::Slice* slice,
                    bool require_timestamp = true);

  // Similar to DecodeFrom, but requires that the entire slice is decoded, and thus takes a const
  // reference to a slice. This still respects the require_timestamp parameter, but in case a
  // timestamp is omitted, we don't allow any extra bytes to be present in the slice.
  Status FullyDecodeFrom(const rocksdb::Slice& slice,
                         bool require_timestamp = true);

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

  Timestamp timestamp() const {
    DCHECK(has_timestamp());
    return timestamp_;
  }

  void set_timestamp(Timestamp timestamp) {
    DCHECK_NE(timestamp, Timestamp::kInvalidTimestamp);
    timestamp_ = timestamp;
  }

  // When we come up with a batch of DocDB updates, we don't yet know the timestamp, because the
  // timestamp is only determined at the time the write operation is appended to the Raft log.
  // Therefore, we initially use Timestamp::kMax, and we have to replace it with the actual
  // timestamp later.
  void ReplaceMaxTimestampWith(Timestamp timestamp);

  bool has_timestamp() const {
    return timestamp_ != Timestamp::kInvalidTimestamp;
  }

  void RemoveTimestamp() {
    timestamp_ = Timestamp::kInvalidTimestamp;
  }

  // @return The number of initial components (including document key and subkeys) that this
  //         SubDocKey shares with another one. This does not care about the timestamp field.
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
  // 1. SubDocKey(DocKey([], ["a"]), [TS(1)]) -> {}
  // 2. SubDocKey(DocKey([], ["a"]), ["x", TS(1)]) -> {} ---------------------------.
  // 3. SubDocKey(DocKey([], ["a"]), ["x", "x", TS(2)]) -> null                     |
  // 4. SubDocKey(DocKey([], ["a"]), ["x", "x", TS(1)]) -> {}                       |
  // 5. SubDocKey(DocKey([], ["a"]), ["x", "x", "y", TS(1)]) -> {}                  |
  // 6. SubDocKey(DocKey([], ["a"]), ["x", "x", "y", "x", TS(1)]) -> true           |
  // 7. SubDocKey(DocKey([], ["a"]), ["y", TS(3)]) -> {}                  <---------
  // 8. SubDocKey(DocKey([], ["a"]), ["y", "y", TS(3)]) -> {}
  // 9. SubDocKey(DocKey([], ["a"]), ["y", "y", "x", TS(3)]) ->
  //
  // This is achieved by simply appending a byte that is higher than any ValueType in an encoded
  // representation of a SubDocKey that extends the vector of subkeys present in the current one,
  // or has the same vector of subkeys, i.e. key/value pairs #3-6 in the above example. Timestamp
  // is omitted from the resulting encoded representation.
  KeyBytes AdvanceOutOfSubDoc();

  // Similar to AdvanceOutOfSubDoc, but seeks to the smallest SubDocKey that has a
  // lexicographically higher document key, excluding any subkeys.
  //
  // E.g. assuming the SubDocKey this is being called on is #2 from the following example,
  // performing a RocksDB seek on the return value of this takes us to #10.
  //
  //  1. SubDocKey(DocKey([], ["a"]), [TS(1)]) -> {}
  //  2. SubDocKey(DocKey([], ["a"]), ["x", TS(1)]) -> {} ----------------------------.
  //  3. SubDocKey(DocKey([], ["a"]), ["x", "x", TS(2)]) -> null                      |
  //  4. SubDocKey(DocKey([], ["a"]), ["x", "x", TS(1)]) -> {}                        |
  //  5. SubDocKey(DocKey([], ["a"]), ["x", "x", "y", TS(1)]) -> {}                   |
  //  6. SubDocKey(DocKey([], ["a"]), ["x", "x", "y", "x", TS(1)]) -> true            |
  //  7. SubDocKey(DocKey([], ["a"]), ["y", TS(3)]) -> {}                             |
  //  8. SubDocKey(DocKey([], ["a"]), ["y", "y", TS(3)]) -> {}                        |
  //  9. SubDocKey(DocKey([], ["a"]), ["y", "y", "x", TS(3)]) ->                      |
  // 10. SubDocKey(DocKey([], ["b"]), [TS(1)]) -> {}                    <-------------.
  // 11. SubDocKey(DocKey([], ["b"]), ["z", TS(1)]) -> "value"
  KeyBytes AdvanceToNextDocKey();

 private:
  DocKey doc_key_;
  Timestamp timestamp_;
  std::vector<PrimitiveValue> subkeys_;

  void EnsureHasNoTimestampYet() {
    DCHECK(!has_timestamp())
        << "Trying to append a primitive value to a SubDocKey " << ToString()
        << " that already has a timestamp set: " << timestamp_.ToDebugString();
  }
};

inline std::ostream& operator <<(std::ostream& out, const SubDocKey& subdoc_key) {
  out << subdoc_key.ToString();
  return out;
}

// A best-effort to decode the given sequence of key bytes as either a DocKey or a SubDocKey.
std::string BestEffortDocDBKeyToStr(const KeyBytes &key_bytes);
std::string BestEffortDocDBKeyToStr(const rocksdb::Slice &slice);

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_KEY_H_
