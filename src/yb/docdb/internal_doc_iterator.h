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

#ifndef YB_DOCDB_INTERNAL_DOC_ITERATOR_H_
#define YB_DOCDB_INTERNAL_DOC_ITERATOR_H_

#include <memory>

#include "yb/rocksdb/db.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/docdb/doc_write_batch_cache.h"
#include "yb/docdb/key_bytes.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"
#include "yb/util/trilean.h"

namespace yb {
namespace docdb {

// A wrapper around a RocksDB iterator that helps navigating the document structure. This is a
// relatively low-level interface that allows manipulating key prefixes directly. The primary use
// case for this is as a utility in the implementation of DocWriteBatch operations. The iterator
// keeps the following pieces of state:
//
// - A key prefix for the current document/subdocument. We append to this prefix or truncate it when
//   traversing the nested document DB structure.
//
// - Whether or not the subdocument pointed to by the prefix exists. In many cases, such as when
//   setting a nested value (e.g. "set a.b.c to d"), it is necessary to create intermediate
//   documents/subdocuments on the path from the root to the user-specified document path. This
//   field provides a way to decide between creating those missing [sub]documents or reusing
//   existing ones.
//
// - A flag indicating whether or not the current key prefix ends with a document/subdocument
//   generation hybrid_time (the hybrid_time at which that document/subdocument was last replaced or
//   deleted). We use this for sanity checking.
//
// This class is not thread-safe.
class InternalDocIterator {
 public:
  // @param rocksdb RocksDB database to operate on.
  // @param doc_write_batch_cache A utility that allows us to avoid redundant lookups.
  // @param filter_key Only SST files containing at least one key with the same hashed
  // components are considered.
  // WARNING: filter_key should survive the InternalDocIterator lifetime, because we store
  // reference to it in order to avoid copying.
  InternalDocIterator(rocksdb::DB* rocksdb,
                      DocWriteBatchCache* doc_write_batch_cache,
                      BloomFilterMode bloom_filter_mode,
                      const KeyBytes& filter_key,
                      rocksdb::QueryId query_id,
                      int* seek_counter = nullptr);

  // Positions this iterator at the root of a document identified by the given encoded document key.
  // The key must not end with a generation hybrid_time.
  //
  // @param encoded_doc_key The encoded key pointing to the document.
  CHECKED_STATUS SeekToDocument(const KeyBytes& encoded_doc_key);

  // Sets the iterator to a state where it is positioned at the top of a document, but does not
  // actually know whether that subdocument exists in the underlying RocksDB database.
  void SetDocumentKey(const KeyBytes& encoded_doc_key) {
    key_prefix_ = encoded_doc_key;
    subdoc_exists_ = Trilean::kUnknown;
    subdoc_type_ = ValueType::kInvalidValueType;
  }

  // Go one level deeper in the document hierarchy. This assumes the iterator is already positioned
  // inside an existing object-type subdocument, but the prefix does not yet end with a hybrid_time.
  // Note: in our MVCC data model with no intermediate generation hybrid_times in the key, only the
  // last component of a SubDocKey can be a hybrid_time.
  //
  // @param subkey The key identifying the subdocument within the current document to navigate to.
  CHECKED_STATUS SeekToSubDocument(const PrimitiveValue& subkey);

  // Append the given subkey to the document. We are assuming that we have already made sure that
  // the iterator is positioned inside an existing subdocument.
  void AppendSubkeyInExistingSubDoc(const PrimitiveValue &subkey) {
    CHECK(subdoc_exists());
    CHECK(IsObjectType(subdoc_type_));
    AppendToPrefix(subkey);
  }

  // @return Whether the subdocument pointed to by this iterator exists.
  bool subdoc_exists() const {
    CHECK_NE(subdoc_exists_, Trilean::kUnknown);
    return static_cast<bool>(subdoc_exists_);
  }

  // @return The type of subdocument pointed to by this iterator, if it exists.
  ValueType subdoc_type() const {
    CHECK(subdoc_exists());
    return subdoc_type_unchecked();
  }

  // Retrieves the type without checking for existence of the doc. This is useful when we
  // encounter a tombstone and would like to still expose information about the subdoc we found.
  ValueType subdoc_type_unchecked() const {
    return subdoc_type_;
  }

  // Retrieves whether the key_prefix only lacks hybrid time from the key found without checking for
  // existence of the doc. This is useful when we encounter a tombstone and would like to still
  // expose information about the subdoc we found.
  bool found_exact_key_prefix_unchecked() const {
    return found_exact_key_prefix_;
  }

  const DocHybridTime& subdoc_ht() const {
    CHECK(subdoc_exists());
    return subdoc_ht_unchecked();
  }

  // Retrieves the hybrid time without checking for existence of the doc. This is useful when we
  // encounter a tombstone and would like to still expose information about the subdoc we found.
  const DocHybridTime& subdoc_ht_unchecked() const {
    return subdoc_ht_;
  }

  // Retrieves the user timestamp without checking for existence of the doc. This is useful when we
  // encounter a tombstone and would like to still expose information about the subdoc we found.
  const UserTimeMicros subdoc_user_timestamp_unchecked() const {
    return subdoc_user_timestamp_;
  }

  bool subdoc_deleted() {
    return subdoc_exists_ != Trilean::kUnknown && subdoc_type_ == ValueType::kTombstone;
  }

  const KeyBytes& key_prefix() { return key_prefix_; }
  KeyBytes* mutable_key_prefix() { return &key_prefix_; }

  // Encode and append the given primitive value to the current key prefix. We are assuming the
  // current key prefix already ends with a hybrid_time, but we don't assume it corresponds to an
  // existing subdocument.
  void AppendToPrefix(const PrimitiveValue& subkey);

  void AppendHybridTimeToPrefix(const DocHybridTime& ht);

  std::string ToDebugString();

  bool HasMoreData() {
    return iter_->Valid();
  }

  // A helper method that seeks the RocksDB iterator to the current document/subdocument
  // key prefix (which is assumed not to end with a generation hybrid_time), and checks whether or
  // not that document/subdocument actually exists. There is an important assumption that this
  // method makes in terms of seeking for subkeys. If we are seeking for a key prefix a.b.c, this
  // method assumes that we have performed a seek for a and a.b immediately before this seek or
  // haven't performed any seeks at all.
  CHECKED_STATUS SeekToKeyPrefix();

  rocksdb::Iterator* iterator() {
    return iter_.get();
  }

 private:

  rocksdb::DB* db_;
  BloomFilterMode bloom_filter_mode_;
  // User key to be used for filtering SST files. Only files which contains the same hashed
  // components are used.
  const KeyBytes& filter_key_;

  // The iterator is created lazily as seek is needed.
  std::unique_ptr<rocksdb::Iterator> iter_;

  DocWriteBatchCache* doc_write_batch_cache_;

  // Current key prefix. This corresponds to all keys belonging to a top-level document or a
  // subdocument.
  KeyBytes key_prefix_;

  ValueType subdoc_type_;

  // The "generation hybrid time" of the current subdocument, i.e. the hybrid time at which the
  // document was last fully overwritten or deleted. The notion of "last" may mean "last as of the
  // hybrid time we're scanning at". Only valid if subdoc_exists() or subdoc_deleted().
  DocHybridTime subdoc_ht_;

  // The user supplied timestamp in the value portion.
  UserTimeMicros subdoc_user_timestamp_;

  // We found a key which matched the exact key_prefix_ we were searching for (excluding the
  // hybrid time). Since we search for a key prefix, we could search for a.b.c, but end up
  // finding a key like a.b.c.d.e. This field indicates that we searched for something like a.b.c
  // and found a key for a.b.c.
  bool found_exact_key_prefix_;

  Trilean subdoc_exists_;

  // Query id that created this iterator.
  const rocksdb::QueryId query_id_;

  // A count to increment to count RocksDB seeks. Not using an atomic here because this is not
  // thread-safe.
  int* num_rocksdb_seeks_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_INTERNAL_DOC_ITERATOR_H_
