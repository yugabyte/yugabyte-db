// Copyright (c) YugaByte, Inc.

#include "yb/docdb/internal_doc_iterator.h"

#include <string>
#include <sstream>

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/doc_key.h"

using std::endl;
using std::string;
using std::stringstream;
using std::unique_ptr;

namespace yb {
namespace docdb {

std::unique_ptr<rocksdb::Iterator> InternalDocIterator::CreateRocksDBIterator(
    rocksdb::DB* rocksdb) {
  // TODO: avoid instantiating ReadOptions every time.
  rocksdb::ReadOptions read_opts;
  return unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
}

InternalDocIterator::InternalDocIterator(rocksdb::DB* rocksdb)
    : rocksdb_(rocksdb),
      key_prefix_ends_with_ts_(false),
      subdoc_exists_(false) {
  iter_ = CreateRocksDBIterator(rocksdb);
}

void InternalDocIterator::SeekToDocument(const KeyBytes& encoded_doc_key) {
  key_prefix_ = encoded_doc_key;
  key_prefix_ends_with_ts_ = false;
  SeekToKeyPrefix();
}

void InternalDocIterator::SeekToSubDocument(const PrimitiveValue& subkey) {
  assert(subdoc_exists_);
  assert(subdoc_type_ == ValueType::kObject);
  assert(key_prefix_ends_with_ts_);
  subkey.AppendToKey(&key_prefix_);
  key_prefix_ends_with_ts_ = false;
  SeekToKeyPrefix();
}

void InternalDocIterator::AppendToPrefix(const PrimitiveValue& subkey) {
  assert(key_prefix_ends_with_ts_);
  subkey.AppendToKey(&key_prefix_);
  key_prefix_ends_with_ts_ = false;
}

void InternalDocIterator::AppendTimestampToPrefix(Timestamp timestamp) {
  assert(!subdoc_exists_);
  assert(!key_prefix_ends_with_ts_);
  key_prefix_.AppendTimestamp(timestamp);
  key_prefix_ends_with_ts_ = true;
}

void InternalDocIterator::ReplaceTimestampInPrefix(Timestamp timestamp) {
  assert(key_prefix_ends_with_ts_);
  key_prefix_.ReplaceLastTimestamp(timestamp);
}

void InternalDocIterator::AppendUpdateTimestampIfNotFound(Timestamp update_timestamp) {
  if (subdoc_exists_) {
    // We can only add updates at a later timestamp.
    assert(update_timestamp.CompareTo(subdoc_gen_ts_) > 0);
  } else {
    AppendTimestampToPrefix(update_timestamp);
  }
}

string InternalDocIterator::ToDebugString() {
  stringstream ss;
  ss << "DocIterator:" << endl;
  ss << "  key_prefix: " << key_prefix_.ToString() << endl;
  ss << "  key_prefix_ends_with_ts: " << key_prefix_ends_with_ts_ << endl;
  if (subdoc_exists_ || subdoc_deleted()) {
    ss << "  subdoc_type: " << ValueTypeToStr(subdoc_type_) << endl;
    ss << "  subdoc_gen_ts: " << subdoc_gen_ts_.ToString() << endl;
  }
  ss << "  subdoc_exists: " << subdoc_exists_ << endl;
  return ss.str();
}

void InternalDocIterator::SeekToKeyPrefix() {
  assert(!key_prefix_ends_with_ts_);
  subdoc_exists_ = false;
  subdoc_type_ = ValueType::kInvalidValueType;
  iter_->Seek(key_prefix_.AsSlice());
  if (HasMoreData()) {
    const rocksdb::Slice& key = iter_->key();
    // If the first key >= key_prefix_ in RocksDB starts with key_prefix_, then a
    // document/subdocument pointed to by key_prefix_ exists.
    if (key_prefix_.IsPrefixOf(key)) {
      assert(key.size() == key_prefix_.size() + kBytesPerTimestamp);
      subdoc_type_ = DecodeValueType(iter_->value());
      subdoc_gen_ts_ = DecodeTimestampFromKey(key, key_prefix_.size());
      if (subdoc_type_ != ValueType::kTombstone) {
        subdoc_exists_ = true;
        key_prefix_.AppendRawBytes(key.data() + key_prefix_.size(), kBytesPerTimestamp);
        key_prefix_ends_with_ts_ = true;
      }
    }
  }
}

}
}
