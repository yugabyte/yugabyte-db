// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_KEY_H_
#define YB_DOCDB_DOC_KEY_H_

#include <ostream>
#include <vector>

#include "rocksdb/slice.h"
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
  DocKey(const std::vector<PrimitiveValue>& range_components);

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

  // Decodes a document key from the given RocksDB key.
  // slice (in/out) - a slice corresponding to a RocksDB key. Any consumed bytes are removed.
  yb::Status DecodeFrom(rocksdb::Slice *slice);

  // Decode the current document key from the given slice, but expect all bytes to be consumed, and
  // return an error status if that is not the case.
  yb::Status FullyDecodeFrom(const rocksdb::Slice& slice);

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

 private:

  bool hash_present_;
  DocKeyHash hash_;
  std::vector<PrimitiveValue> hashed_group_;
  std::vector<PrimitiveValue> range_group_;
};

// Consume a group of document key components, ending with ValueType::kGroupEnd.
// @param slice - the current point at which we are decoding a key
// @param result - vector to append decoded values to.
yb::Status ConsumePrimitiveValuesFromKey(rocksdb::Slice* slice,
                                         std::vector<PrimitiveValue>* result);

inline std::ostream& operator <<(std::ostream& out, const DocKey& doc_key) {
  out << doc_key.ToString();
  return out;
}

// ------------------------------------------------------------------------------------------------
// SubDocKey
// ------------------------------------------------------------------------------------------------

// A key pointing to a subdocument. Consists of a DocKey identifying the document and a set of
// (primitive_value, generation_timestamp) pairs leading to the subdocument in question.
// "Generation timestamp" is a timestamp at which a particular subdocument was completely
// overwritten or deleted.
class SubDocKey {
 public:
  SubDocKey() {}
  SubDocKey(const DocKey& doc_key,
            Timestamp doc_gen_ts)
      : doc_key_(doc_key),
        doc_gen_ts_(doc_gen_ts) {
  }

  SubDocKey(const DocKey& doc_key,
            Timestamp doc_gen_ts,
            std::vector<std::pair<PrimitiveValue, yb::Timestamp>> subkeys)
      : doc_key_(doc_key),
        doc_gen_ts_(doc_gen_ts),
        subkeys_(subkeys) {
  }

  template <class ...T>
  SubDocKey(const DocKey& doc_key, Timestamp doc_gen_ts, T... subkeys_and_timestamps)
      : doc_key_(doc_key),
        doc_gen_ts_(doc_gen_ts) {
    AppendSubKeysAndTimestamps(subkeys_and_timestamps...);
  }

  void AppendSubKeysAndTimestamps() {}

  template <class ...T>
  void AppendSubKeysAndTimestamps(PrimitiveValue subdoc_key, Timestamp gen_ts,
                                  T... subkeys_and_timestamps) {
    subkeys_.emplace_back(subdoc_key, gen_ts);
    AppendSubKeysAndTimestamps(subkeys_and_timestamps...);
  }

  void Clear();

  KeyBytes Encode() const;

  // Decodes a SubDocKey from the given slice, typically retrieved from a RocksDB key. All bytes
  // of the slice have to be successfully decoded.
  yb::Status DecodeFrom(const rocksdb::Slice& slice);

  // Unlike the DocKey case, this is the same as DecodeFrom, because a "subdocument key" occupies
  // the entire RocksDB key. This is here for compatibilty with templates that can operate both on
  // DocKeys and SubDocKeys.
  yb::Status FullyDecodeFrom(const rocksdb::Slice& slice) { return DecodeFrom(slice); }

  std::string ToString() const;

  const DocKey& doc_key() const {
    return doc_key_;
  }

  int num_subkeys() const {
    return subkeys_.size();
  }

  bool StartsWith(const SubDocKey& other) const;

  bool operator ==(const SubDocKey& other) const;

  bool operator !=(const SubDocKey& other) const {
    return !(*this == other);
  }

  const PrimitiveValue& last_subkey() const {
    assert(!subkeys_.empty());
    return subkeys_.back().first;
  }

  // Produces a subdocument key that could be used to seek to the next subkey in an object. We do
  // this by setting the generation timestamp to its minimum value.
  SubDocKey AdvanceToNextSubkey() const;

  int CompareTo(const SubDocKey& other) const;

 private:
  DocKey doc_key_;
  yb::Timestamp doc_gen_ts_;
  std::vector<std::pair<PrimitiveValue, yb::Timestamp>> subkeys_;
};

inline std::ostream& operator <<(std::ostream& out, const SubDocKey& subdoc_key) {
  out << subdoc_key.ToString();
  return out;
}

}
}

#endif
