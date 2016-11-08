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

template<typename T>
int CompareVectors(const std::vector<T>& a, const std::vector<T>& b) {
  auto a_iter = a.begin();
  auto b_iter = b.begin();
  while (a_iter != a.end() && b_iter != b.end()) {
    int result = a_iter->CompareTo(*b_iter);
    if (result != 0) {
      return result;
    }
    ++a_iter;
    ++b_iter;
  }
  if (a_iter == a.end()) {
    return b_iter == b.end() ? 0 : -1;
  }
  DCHECK(b_iter == b.end());  // This follows from the while loop condition.
  return 1;
}

// A template for comparing vectors of pairs of classes implementing CompareTo. An optional flag
// allows to reverse the sort order of the second component (used for timestamp ordering).
template<typename T1, typename T2, bool reverse_second_component = false>
int ComparePairVectors(const std::vector<std::pair<T1, T2>>& a,
                       const std::vector<std::pair<T1, T2>>& b) {
  auto a_iter = a.begin();
  auto b_iter = b.begin();
  while (a_iter != a.end() && b_iter != b.end()) {
    int result = a_iter->first.CompareTo(b_iter->first);
    if (result != 0) return result;

    result = a_iter->second.CompareTo(b_iter->second);
    if (result != 0) {
      return (reverse_second_component ? -1 : 1) * result;
    }

    ++a_iter;
    ++b_iter;
  }
  if (a_iter == a.end()) {
    return b_iter == b.end() ? 0 : -1;
  }
  DCHECK(b_iter == b.end());  // This follows from the while loop condition.
  return 1;
}

// A key that allows us to locate a document. This is the prefix of all RocksDB keys of records
// inside this document. A document key contains:
//   - An optional fixed-width hash prefix.
//   - A group of primitive values representing "hashed" components (this is what the hash is
//     computed based on, so this group is present/absent together with the hash).
//   - A group of "range" components suitable for doing ordered scans.
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
  void Clear() {
    hash_present_ = false;
    hash_ = 0xdeadbeef;
    hashed_group_.clear();
    range_group_.clear();
  }

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

  bool operator ==(const DocKey& other) const {
    return hash_present_ == other.hash_present_ &&
           // Only compare hashes and hashed groups if the hash presence flag is set.
           (!hash_present_ || (hash_ == other.hash_ && hashed_group_ == other.hashed_group_)) &&
           range_group_ == other.range_group_;
  }

  bool operator !=(const DocKey& other) const {
    return !(*this == other);
  }

  int CompareTo(const DocKey& other) const {
    // Each table will only contain keys with hash present or absent, so we should never compare
    // keys from both categories.
    assert(hash_present_ == other.hash_present_);
    int result = 0;
    if (hash_present_) {
      result = GenericCompare(hash_, other.hash_);
      if (result != 0) return result;
    }
    result = CompareVectors(hashed_group_, other.hashed_group_);
    if (result != 0) return result;

    return CompareVectors(range_group_, other.range_group_);
  }

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
// Implementation note: we use Timestamp::kInvalidTimestamp to represent an omitted timestamp.
// We heavily rely on that being the default-constructed value of a Timestamp.
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

  void AppendSubKeys() {}

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
  // @param require_timestamp
  //     Whether a timestamp is required in the end of the SubDocKey. If this is set to false, we
  //     allow decoding an incomplete SubDocKey without a timestamp.
  Status DecodeFrom(const rocksdb::Slice& original_bytes,
                    bool require_timestamp = true);

  // Unlike the DocKey case, this is the same as DecodeFrom, because a "subdocument key" occupies
  // the entire RocksDB key. This is here for compatibility with templates that can operate both on
  // DocKeys and SubDocKeys.
  Status FullyDecodeFrom(const rocksdb::Slice& slice) { return DecodeFrom(slice); }

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
