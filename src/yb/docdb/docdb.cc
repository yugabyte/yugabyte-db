// Copyright (c) YugaByte, Inc.

#include <memory>

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/logging.h"

#include "yb/docdb/value_type.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/internal_doc_iterator.h"

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

static const string kObjectValueType = EncodeValueType(ValueType::kObject);
static const string kTombstoneValueType = EncodeValueType(ValueType::kTombstone);

// ------------------------------------------------------------------------------------------------
// DocWriteBatch
// ------------------------------------------------------------------------------------------------

DocWriteBatch::DocWriteBatch(rocksdb::DB* rocksdb)
    : rocksdb_(rocksdb) {
}

Status DocWriteBatch::SetPrimitive(
    const DocPath& doc_path, const PrimitiveValue& value, Timestamp timestamp) {
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();

  InternalDocIterator doc_iter(rocksdb_);

  // Navigate to the root of the document, not including the generation timestamp. We don't yet
  // know whether the document exists or when it was last updated.
  doc_iter.SeekToDocument(encoded_doc_key);

  // This will append a timestamp only if the document is not found.
  doc_iter.AppendUpdateTimestampIfNotFound(timestamp);

  const int num_subkeys = doc_path.num_subkeys();
  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    // Invariant: at this point our key prefix ends with a timestamp, either present in RocksDB,
    // or set based on the current operation's timestamp.
    assert(doc_iter.key_prefix_ends_with_ts());
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return Status::NotSupported("Setting values at a given array index is not supported yet");
    }
    if (doc_iter.subdoc_exists()) {
      if (doc_iter.subdoc_type() != ValueType::kObject) {
        return Status::IllegalState(Substitute(
            "Cannot set values inside a subdocument of type $0",
            ValueTypeToStr(doc_iter.subdoc_type())));
      }
      doc_iter.SeekToSubDocument(subkey);
      doc_iter.AppendUpdateTimestampIfNotFound(timestamp);
    } else {
      // The subdocument that this subkey is supposed to live in does not exist. Create it.
      write_batch_.Put(doc_iter.key_prefix().AsSlice(), kObjectValueType);
      doc_iter.AppendToPrefix(subkey);
      doc_iter.AppendTimestampToPrefix(timestamp);
    }
  }

  write_batch_.Put(doc_iter.key_prefix().AsSlice(), value.ToValue());

  return Status::OK();
}

Status DocWriteBatch::DeleteSubDoc(const DocPath& doc_path, Timestamp timestamp) {
  InternalDocIterator doc_iter(rocksdb_);
  doc_iter.SeekToDocument(doc_path.encoded_doc_key());
  if (!doc_iter.subdoc_exists()) {
    // The document itself does not exist, nothing to delete.
    return Status::OK();
  }
  const int num_subkeys = doc_path.num_subkeys();
  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    assert(doc_iter.subdoc_exists());
    assert(doc_iter.key_prefix_ends_with_ts());
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return Status::NotSupported("Arrays not supported yet");
    }
    if (doc_iter.subdoc_type() != ValueType::kObject) {
      // We're being asked to remove a key from a subdocument that is not an object. We're currently
      // treating this as an error. Alternatively, we could have said that what we're trying to
      // remove does not exist, so there is nothing to do.
      return Status::IllegalState(
          Substitute(
              "Cannot remove key $0 from subdocument of type $0",
              subkey.ToString(), ValueTypeToStr(doc_iter.subdoc_type())));

    }
    doc_iter.SeekToSubDocument(subkey);
    if (!doc_iter.subdoc_exists()) {
      return Status::OK();
    }
  }

  // We can only delete at a higher timestamp than what existed before.
  assert(timestamp.CompareTo(doc_iter.subdoc_gen_ts()) > 0);
  doc_iter.ReplaceTimestampInPrefix(timestamp);
  write_batch_.Put(doc_iter.key_prefix().AsSlice(), kTombstoneValueType);

  return Status::OK();
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

  return Status::Corruption(Substitute("Invalid value type at the top level of a document: $0",
      ValueTypeToStr(top_level_value.value_type())));
}


static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* rocksdb_iter,
                                  DocVisitor* visitor) {
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
      return Status::Corruption(kErrorMsgPrefix);
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
    return Status::Corruption(
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

}
}
