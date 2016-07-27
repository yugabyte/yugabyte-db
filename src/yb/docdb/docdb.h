// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_H
#define YB_DOCDB_DOCDB_H

#include <cstdint>
#include <ostream>

#include "rocksdb/db.h"

#include "yb/common/timestamp.h"
#include "yb/util/status.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"

// Document DB mapping on top of the key-value map in RocksDB:
// <document_key> <doc_gen_ts> -> <doc_type>
// <document_key> <doc_gen_ts> <key_a> <gen_ts_a> -> <subdoc_a_type_or_value>
//
// Assuming the type of subdocument corresponding to key_a in the above example is "object", the
// contents of that subdocument are stored in a similar way:
// <document_key> <doc_gen_ts> <key_a> <gen_ts_a> <key_aa> <gen_ts_aa> -> <subdoc_aa_type_or_value>
// <document_key> <doc_gen_ts> <key_a> <gen_ts_a> <key_ab> <gen_ts_ab> -> <subdoc_ab_type_or_value>
// ...
//
// See doc_key.h for the encoding of the <document_key> part.
//
// <key_a>, <key_aa> are subkeys indicating a path inside a document.
// Their encoding is as follows:
//   <value_type> -- one byte, see the ValueType enum.
//   <value_specific_encoding> -- e.g. a big-endian 8-byte integer, or a string in a "zero encoded"
//                                format. This is empty for null or true/false values.
//
// <doc_gen_ts>, <gen_ts_a>, <gen_ts_ab> are "generation timestamps" corresponding to hybrid clock
// timestamps of the last time a particular top-level document / subdocument was fully overwritten
// or deleted.
//
// <subdoc_a_type_or_value>, <subdoc_aa_type_or_value>, <subdoc_ab_type_or_value> are values of the
// following form:
//   - One-byte value type (see the ValueType enum below).
//   - For primitive values, the encoded value. Note: the value encoding may be different from the
//     key encoding for the same data type. E.g. we only flip the sign bit for signed 64-bit
//     integers when encoded as part of a RocksDB key, not value.
//
// Also see this document for a high-level overview of how we lay out JSON documents on top of
// RocksDB:
// https://docs.google.com/document/d/1uEOHUqGBVkijw_CGD568FMt8UOJdHtiE3JROUOppYBU/edit

namespace yb {
namespace docdb {

class DocWriteBatch {
 public:
  DocWriteBatch(rocksdb::DB* rocksdb);

  // Set the primitive at the given path to the given value. Intermediate subdocuments are created
  // if necessary and possible.
  Status SetPrimitive(const DocPath& doc_path, const PrimitiveValue& value, Timestamp timestamp);
  Status DeleteSubDoc(const DocPath& doc_path, Timestamp timestamp);

  std::string ToDebugString();
  rocksdb::WriteBatch* write_batch() { return &write_batch_; }
  void Clear();

 private:
  rocksdb::DB* rocksdb_;
  rocksdb::WriteBatch write_batch_;
};

// A visitor class that could be overridden to consume results of scanning of one or more document.
class DocVisitor {
 public:
  DocVisitor() {}
  virtual ~DocVisitor() {}

	virtual void StartDocument(const DocKey& key) = 0;
	virtual void EndDocument() = 0;

	virtual void VisitKey(const PrimitiveValue& key) = 0;
  virtual void VisitValue(const PrimitiveValue& value) = 0;

	virtual void StartObject() = 0;
	virtual void EndObject() = 0;

	virtual void StartArray() = 0;
	virtual void EndArray() = 0;
};

class DocScanner {
 public:
  // Creates a scanner to scan the document database at the given timestamp.
  DocScanner(rocksdb::DB* rocksdb,
             Timestamp timestamp,
             const DocKey& start_key,
             const DocKey& end_key,
             bool end_key_included)
      : rocksdb_(rocksdb),
				timestamp_(timestamp),
				start_key_(start_key),
				end_key_(end_key),
				end_key_included_(end_key_included) {
	}

 private:
  rocksdb::DB* rocksdb_;
  Timestamp timestamp_;
  DocKey start_key_;
  DocKey end_key_;
  bool end_key_included_;
};

yb::Status ScanDocument(rocksdb::DB* rocksdb,
												const KeyBytes& document_key,
												DocVisitor* visitor);

// Create a debug dump of the document database. Tries to decode all keys/values despite failures.
// Reports all errors to the output stream and returns the status of the first failed operation,
// if any.
yb::Status DocDBDebugDump(rocksdb::DB* rocksdb, std::ostream& out);

}
}

#endif
