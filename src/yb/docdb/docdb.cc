// Copyright (c) YugaByte, Inc.

#include <string>
#include <memory>

#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/value_type.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/logging.h"

using std::endl;
using std::string;
using std::stringstream;
using std::unique_ptr;

using yb::Timestamp;
using yb::util::FormatBytesAsStr;
using yb::FormatRocksDBSliceAsStr;
using strings::Substitute;


namespace yb {
namespace docdb {

namespace {
// This a zero-terminated string for safety, even though we only intend to use one byte.
const char kObjectValueType[] = { static_cast<char>(ValueType::kObject), 0 };
}

// ------------------------------------------------------------------------------------------------
// DocWriteBatch
// ------------------------------------------------------------------------------------------------

DocWriteBatch::DocWriteBatch(rocksdb::DB* rocksdb)
    : rocksdb_(rocksdb) {
}

// This codepath is used to handle both primitive upserts and deletions.
Status DocWriteBatch::SetPrimitive(
    const DocPath& doc_path, const PrimitiveValue& value, const Timestamp timestamp) {
  // TODO(mbautin): add a check that this method is always being called with non-decreasing
  //                timestamps.
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1, timestamp=$2",
      doc_path.ToString(), value.ToString(), timestamp.ToDebugString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.value_type() == ValueType::kTombstone;

  InternalDocIterator doc_iter(rocksdb_, &cache_);

  if (num_subkeys > 0 || is_deletion) {
    // Navigate to the root of the document, not including the generation timestamp. We don't yet
    // know whether the document exists or when it was last updated.
    doc_iter.SeekToDocument(encoded_doc_key);
    DOCDB_DEBUG_LOG("Top-level document exists: $0", doc_iter.subdoc_exists());
    if (!doc_iter.subdoc_exists()) {
      if (is_deletion) {
        DOCDB_DEBUG_LOG("We're performing a deletion, and the document is not present. "
                        "Nothing to do.");
        return Status::OK();
      }
    }
    doc_iter.AppendUpdateTimestampIfNotFound(timestamp);
  } else {
    // If we are overwriting an entire document with a primitive value (not deleting it), we don't
    // need to perform any reads from RocksDB at all.
    doc_iter.SetDocumentKey(encoded_doc_key);
    doc_iter.AppendTimestampToPrefix(timestamp);
  }

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    assert(doc_iter.key_prefix_ends_with_ts());
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return STATUS(NotSupported, "Setting values at a given array index is not supported yet");
    }
    if (doc_iter.subdoc_exists()) {
      if (doc_iter.subdoc_type() != ValueType::kObject) {
        return STATUS(IllegalState, Substitute(
            "Cannot set values inside a subdocument of type $0",
            ValueTypeToStr(doc_iter.subdoc_type())));
      }
      if (subkey_index == num_subkeys - 1 && !is_deletion) {
        // We don't need to perform a RocksDB read at the last level for upserts, we just
        // overwrite the value within the last subdocument with what we're trying to write.
        // We still perform the read for deletions, because we try to avoid writing a new tombstone
        // if the data is not there anyway.
        doc_iter.AppendSubkeyInExistingSubDoc(subkey);
        doc_iter.AppendTimestampToPrefix(timestamp);
      } else {
        // We need to check if the subdocument at this subkey exists.
        doc_iter.SeekToSubDocument(subkey);
        if (is_deletion) {
          if (!doc_iter.subdoc_exists()) {
            // A parent subdocument of the value we're trying to delete, or that value itself,
            // does not exist, nothing to do.
            DOCDB_DEBUG_LOG("Subdocument does not exist at subkey level $0 (subkey: $1)",
                            subkey_index, subkey.ToString());
            return Status::OK();
          }
          if (subkey_index == num_subkeys - 1) {
            // Replace the last timestamp only at the final level as we're about to write the
            // tombstone.
            doc_iter.ReplaceTimestampInPrefix(timestamp);
          }
        } else {
          doc_iter.AppendUpdateTimestampIfNotFound(timestamp);
        }
      }
    } else {
      if (is_deletion) {
        // A parent subdocument of the subdocument we're trying to delete does not exist, nothing
        // to do.
        return Status::OK();
      }

      // The document/subdocument that this subkey is supposed to live in does not exist, create it.
      write_batch_.Put(doc_iter.key_prefix().AsSlice(), rocksdb::Slice(kObjectValueType, 1));

      // Record the fact that we're adding this subdocument in our local cache so that future
      // operations in this document write batch don't have to add it or look for it in RocksDB.
      // Note that the key we're using in the cache does not have the timestamp at the end.
      cache_.Put(doc_iter.key_prefix().AsSliceWithoutTimestamp().ToString(),
                 timestamp, ValueType::kObject);

      doc_iter.AppendToPrefix(subkey);
      doc_iter.AppendTimestampToPrefix(timestamp);
    }
  }


  write_batch_.Put(doc_iter.key_prefix().AsSlice(), value.ToValue());

  // The key we use in the DocWriteBatchCache does not have a final timestamp, because that's the
  // key we expect to look up.
  cache_.Put(doc_iter.key_prefix().AsSliceWithoutTimestamp().ToString(),
      timestamp, value.value_type());

  return Status::OK();
}

Status DocWriteBatch::DeleteSubDoc(const DocPath& doc_path, const Timestamp timestamp) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone), timestamp);
}

string DocWriteBatch::ToDebugString() {
  WriteBatchFormatter formatter;
  rocksdb::Status iteration_status = write_batch_.Iterate(&formatter);
  CHECK(iteration_status.ok());
  return formatter.str();
}

void DocWriteBatch::Clear() {
  write_batch_.Clear();
}

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* rocksdb_iter,
                                  DocVisitor* visitor);

static yb::Status ScanPrimitiveValueOrObject(const SubDocKey& higher_level_key,
                                             rocksdb::Iterator* rocksdb_iter,
                                             DocVisitor* visitor) {
  PrimitiveValue top_level_value;
  RETURN_NOT_OK(top_level_value.DecodeFromValue(rocksdb_iter->value()));

  if (top_level_value.value_type() == ValueType::kTombstone) {
    return Status::OK();  // the document does not exist
  }

  if (top_level_value.IsPrimitive()) {
    visitor->VisitValue(top_level_value);
    return Status::OK();
  }

  if (top_level_value.value_type() == ValueType::kObject) {
    rocksdb_iter->Next();
    visitor->StartObject();
    ScanSubDocument(higher_level_key, rocksdb_iter, visitor);
    visitor->EndObject();
    return Status::OK();
  }

  return STATUS(Corruption, Substitute("Invalid value type at the top level of a document: $0",
      ValueTypeToStr(top_level_value.value_type())));
}


static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* const rocksdb_iter,
                                  DocVisitor* const visitor) {
  while (rocksdb_iter->Valid()) {
    SubDocKey subdoc_key;
    RETURN_NOT_OK(subdoc_key.DecodeFrom(rocksdb_iter->key()));
    if (!subdoc_key.StartsWith(higher_level_key)) {
      break;
    }

    if (subdoc_key.num_subkeys() != higher_level_key.num_subkeys() + 1) {
      static const char* kErrorMsgPrefix =
          "A subdocument key must be nested exactly one level under the parent subdocument.";
      // Log more details in debug mode.
      DLOG(WARNING) << Substitute(
          "$0. Got parent subdocument key: $1, subdocument key: $2.",
          kErrorMsgPrefix, higher_level_key.ToString(), subdoc_key.ToString());
      return STATUS(Corruption, kErrorMsgPrefix);
    }

    if (DecodeValueType(rocksdb_iter->value()) != ValueType::kTombstone) {
      visitor->VisitKey(subdoc_key.last_subkey());
    }
    ScanPrimitiveValueOrObject(subdoc_key, rocksdb_iter, visitor);

    rocksdb_iter->Seek(subdoc_key.AdvanceToNextSubkey().Encode().AsSlice());
  }
  return Status::OK();
}

yb::Status ScanDocument(rocksdb::DB* rocksdb,
                        const KeyBytes& document_key,
                        DocVisitor* visitor) {
  auto rocksdb_iter = InternalDocIterator::CreateRocksDBIterator(rocksdb);
  KeyBytes key_prefix(document_key);
  rocksdb_iter->Seek(key_prefix.AsSlice());
  if (!rocksdb_iter->Valid() || !document_key.IsPrefixOf(rocksdb_iter->key())) {
    return Status::OK();
  }

  SubDocKey doc_key;
  RETURN_NOT_OK(doc_key.DecodeFrom(rocksdb_iter->key()));
  if (doc_key.num_subkeys() > 0) {
    return STATUS(Corruption,
        Substitute("A top-level document key is not supposed to contain any sub-keys: $0",
                   doc_key.ToString()));
  }

  visitor->StartDocument(doc_key.doc_key());
  RETURN_NOT_OK(ScanPrimitiveValueOrObject(doc_key, rocksdb_iter.get(), visitor));
  visitor->EndDocument();

  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

Status DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out) {
  rocksdb::ReadOptions read_opts;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();
  auto result_status = Status::OK();
  while (iter->Valid()) {
    SubDocKey subdoc_key;
    rocksdb::Slice key_slice(iter->key());
    Status subdoc_key_decode_status = subdoc_key.DecodeFrom(key_slice);
    if (!subdoc_key_decode_status.ok()) {
      out << "Error: failed decoding RocksDB key " << FormatRocksDBSliceAsStr(iter->key()) << ": "
          << subdoc_key_decode_status.ToString() << endl;
      if (result_status.ok()) {
        result_status = subdoc_key_decode_status;
      }
      iter->Next();
      continue;
    }

    PrimitiveValue value;
    Status value_decode_status = value.DecodeFromValue(iter->value());
    if (!value_decode_status.ok()) {
      out << "Error: failed to decode value for key " << subdoc_key.ToString() << endl;
      if (result_status.ok()) {
        result_status = value_decode_status;
      }
      iter->Next();
      continue;
    }

    out << subdoc_key.ToString() << " -> " << value.ToString() << endl;

    iter->Next();
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
