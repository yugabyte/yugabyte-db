// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_INTERNAL_DOC_ITERATOR_H
#define YB_DOCDB_INTERNAL_DOC_ITERATOR_H

#include <memory>

#include "rocksdb/db.h"

#include "yb/docdb/key_bytes.h"
#include "yb/docdb/value_type.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// A wrapper around a RocksDB iterator that helps navigating the document structure. This is a
// relatively low-level interface that allows manipulating key prefixes directly. The iterator keeps
// the following pieces of state:
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
//   generation timestamp (the timestamp at which that document/subdocument was last replaced or
//   deleted). We use this for sanity checking.
//
// This class is not thread-safe.
class InternalDocIterator {
 public:
  InternalDocIterator(rocksdb::DB* rocksdb);

  // Positions this iterator at the root of a document identified by the given encoded document key.
  // The key must not end with a generation timestamp.
  //
  // @param encoded_doc_key The encoded key pointing to the document.
  void SeekToDocument(const KeyBytes& encoded_doc_key);

  // Similar to SeekToDocument, but positions the iterator at the given timestamp.
  void SeekToDocumentAtTimestamp(const KeyBytes& encoded_doc_key, Timestamp timestamp);

  // Go one level deeper in the document hierarchy. This assumes the iterator is already positioned
  // inside an existing object-type subdocument, and that the prefix ends with a generation
  // timestamp, effectively pointing to a particular "revision" of that subdocument.
  //
  // @param subkey The key identifying the subdocument within the current document to navigate to.
  void SeekToSubDocument(const PrimitiveValue& subkey);

  // Positions this iterator at the document pointed to by the underlying RocksDB iterator. This is
  // useful when we have attempted to SeekToDocument and did not find an exact match, but we still
  // want to continue scanning from this point. We are assuming that the RocksDB iterator is
  // positioned at the beginning of a document, which will happen naturally if we seek at a dcoument
  // key immediately preceding a call to this method.
  void ResetToCurrentDocument();

  // @return Whether the subdocument pointed to by this iterator exists.
  bool subdoc_exists() { return subdoc_exists_; }

  // @return The type of subdocument pointed to by this iterator, if it exists.
  ValueType subdoc_type() {
    assert(subdoc_exists_);
    return subdoc_type_;
  }

  bool subdoc_deleted() {
    return subdoc_type_ == ValueType::kTombstone;
  }

  const KeyBytes& key_prefix() { return key_prefix_; }
  bool key_prefix_ends_with_ts() { return key_prefix_ends_with_ts_; }

  Timestamp subdoc_gen_ts() {
    assert(subdoc_exists_);
    return subdoc_gen_ts_;
  }

  void AppendToPrefix(const PrimitiveValue& subkey);
  void AppendTimestampToPrefix(Timestamp ts);
  void AppendUpdateTimestampIfNotFound(Timestamp ts);
  void ReplaceTimestampInPrefix(Timestamp ts);
  std::string ToDebugString();

  bool HasMoreData() {
    return iter_->Valid();
  }

  static std::unique_ptr<rocksdb::Iterator> CreateRocksDBIterator(rocksdb::DB* rocksdb);

 private:
  // An internal helper method that seeks the RocksDB iterator to the current document/subdocument
  // key prefix (which is assumed not to end with a generation timestamp), and checks whether or not
  // that document/subdocument actually exists.
  void SeekToKeyPrefix();

  rocksdb::DB* rocksdb_;
  std::unique_ptr<rocksdb::Iterator> iter_;

  // Current key prefix. This corresponds to all keys belonging to a top-level document or a
  // subdocument.
  KeyBytes key_prefix_;

  bool key_prefix_ends_with_ts_;

  ValueType subdoc_type_;

  // The "generation timestamp" of the current subdocument, i.e. the timestamp at which the document
  // was last been fully overwritten or deleted. The notion of "last" may mean "last as of the
  // timestamp we're scanning at". Only valid if subdoc_exists() or subdoc_deleted().
  Timestamp subdoc_gen_ts_;

  bool subdoc_exists_;
};

}
}

#endif
