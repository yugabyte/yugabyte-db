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

InternalDocIterator::InternalDocIterator(rocksdb::DB* rocksdb,
                                         DocWriteBatchCache* doc_write_batch_cache)
    : rocksdb_(rocksdb),
      doc_write_batch_cache_(doc_write_batch_cache),
      key_prefix_ends_with_ts_(false),
      subdoc_exists_(Trilean::kUnknown) {
  iter_ = CreateRocksDBIterator(rocksdb);
}

void InternalDocIterator::SeekToDocument(const KeyBytes& encoded_doc_key) {
  SetDocumentKey(encoded_doc_key);
  SeekToKeyPrefix();
}

void InternalDocIterator::SeekToSubDocument(const PrimitiveValue& subkey) {
  AppendSubkeyInExistingSubDoc(subkey);
  SeekToKeyPrefix();
}

void InternalDocIterator::AppendToPrefix(const PrimitiveValue& subkey) {
  assert(key_prefix_ends_with_ts_);
  subkey.AppendToKey(&key_prefix_);
  key_prefix_ends_with_ts_ = false;
}

void InternalDocIterator::AppendTimestampToPrefix(Timestamp timestamp) {
  assert(!key_prefix_ends_with_ts_);
  key_prefix_.AppendTimestamp(timestamp);
  key_prefix_ends_with_ts_ = true;
}

void InternalDocIterator::ReplaceTimestampInPrefix(Timestamp timestamp) {
  assert(key_prefix_ends_with_ts_);
  key_prefix_.ReplaceLastTimestamp(timestamp);
}

void InternalDocIterator::AppendUpdateTimestampIfNotFound(Timestamp update_timestamp) {
  if (subdoc_exists()) {
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
  if (subdoc_exists_ == Trilean::kTrue || subdoc_deleted()) {
    ss << "  subdoc_type: " << ValueTypeToStr(subdoc_type_) << endl;
    ss << "  subdoc_gen_ts: " << subdoc_gen_ts_.ToString() << endl;
  }
  ss << "  subdoc_exists: " << subdoc_exists_ << endl;
  return ss.str();
}

void InternalDocIterator::SeekToKeyPrefix() {
  assert(!key_prefix_ends_with_ts_);
  subdoc_exists_ = ToTrilean(false);
  subdoc_type_ = ValueType::kInvalidValueType;

  boost::optional<DocWriteBatchCache::Entry> previous_entry = doc_write_batch_cache_->Get(
      key_prefix_.AsStringRef());
  if (previous_entry) {
    subdoc_gen_ts_ = previous_entry->first;
    key_prefix_.AppendTimestamp(subdoc_gen_ts_);
    key_prefix_ends_with_ts_ = true;
    subdoc_type_ = previous_entry->second;
    subdoc_exists_ = ToTrilean(subdoc_type_ != ValueType::kTombstone);
  } else {
    iter_->Seek(key_prefix_.AsSlice());
    if (HasMoreData()) {
      const rocksdb::Slice& key = iter_->key();
      // If the first key >= key_prefix_ in RocksDB starts with key_prefix_, then a
      // document/subdocument pointed to by key_prefix_ exists.
      if (key_prefix_.IsPrefixOf(key)) {
        assert(key.size() == key_prefix_.size() + kBytesPerTimestamp);
        subdoc_type_ = DecodeValueType(iter_->value());
        subdoc_gen_ts_ = DecodeTimestampFromKey(key, key_prefix_.size());
        // Cache the results of reading from RocksDB so that we don't have to read again in a later
        // operation in the same DocWriteBatch.
        doc_write_batch_cache_->Put(key_prefix_.AsStringRef(), subdoc_gen_ts_, subdoc_type_);
        if (subdoc_type_ != ValueType::kTombstone) {
          subdoc_exists_ = ToTrilean(true);
          key_prefix_.AppendRawBytes(key.data() + key_prefix_.size(), kBytesPerTimestamp);
          key_prefix_ends_with_ts_ = true;
        }
      }
    }
  }
}

}
}
