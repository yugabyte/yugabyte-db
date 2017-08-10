// Copyright (c) YugaByte, Inc.

#include "yb/docdb/internal_doc_iterator.h"

#include <sstream>
#include <string>

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"

using std::endl;
using std::string;
using std::stringstream;
using std::unique_ptr;

using strings::Substitute;

using yb::util::to_underlying;

namespace yb {
namespace docdb {

InternalDocIterator::InternalDocIterator(rocksdb::DB* rocksdb,
                                         DocWriteBatchCache* doc_write_batch_cache,
                                         BloomFilterMode bloom_filter_mode,
                                         const rocksdb::QueryId query_id,
                                         int* num_rocksdb_seeks)
    : db_(rocksdb),
      bloom_filter_mode_(bloom_filter_mode),
      iter_(nullptr),
      doc_write_batch_cache_(doc_write_batch_cache),
      subdoc_exists_(Trilean::kUnknown),
      query_id_(query_id),
      num_rocksdb_seeks_(num_rocksdb_seeks) {}

Status InternalDocIterator::SeekToDocument(const KeyBytes& encoded_doc_key) {
  SetDocumentKey(encoded_doc_key);
  return SeekToKeyPrefix();
}

Status InternalDocIterator::SeekToSubDocument(const PrimitiveValue& subkey) {
  DOCDB_DEBUG_LOG("Called with subkey=$0", subkey.ToString());
  AppendSubkeyInExistingSubDoc(subkey);
  return SeekToKeyPrefix();
}

void InternalDocIterator::AppendToPrefix(const PrimitiveValue& subkey) {
  subkey.AppendToKey(&key_prefix_);
}

void InternalDocIterator::AppendHybridTimeToPrefix(
    const DocHybridTime& hybrid_time) {
  key_prefix_.AppendHybridTime(hybrid_time);
}

string InternalDocIterator::ToDebugString() {
  stringstream ss;
  ss << "InternalDocIterator:" << endl;
  ss << "  key_prefix: " << BestEffortDocDBKeyToStr(key_prefix_) << endl;
  if (subdoc_exists_ == Trilean::kTrue || subdoc_deleted()) {
    ss << "  subdoc_type: " << ToString(subdoc_type_) << endl;
    ss << "  subdoc_gen_ht: " << subdoc_ht_.ToString() << endl;
  }
  ss << "  subdoc_exists: " << subdoc_exists_ << endl;
  return ss.str();
}

Status InternalDocIterator::SeekToKeyPrefix() {
  const auto prev_subdoc_exists = subdoc_exists_;
  const auto prev_subdoc_ht = subdoc_ht_;

  subdoc_exists_ = Trilean::kFalse;
  subdoc_type_ = ValueType::kInvalidValueType;

  DOCDB_DEBUG_LOG("key_prefix=$0", BestEffortDocDBKeyToStr(key_prefix_));
  boost::optional<DocWriteBatchCache::Entry> cached_ht_and_type =
      doc_write_batch_cache_->Get(KeyBytes(key_prefix_.AsStringRef()));
  if (cached_ht_and_type) {
    subdoc_ht_ = cached_ht_and_type->first;
    subdoc_type_ = cached_ht_and_type->second;
    subdoc_exists_ = ToTrilean(subdoc_type_ != ValueType::kTombstone);
  } else {
    if (!iter_) {
      // If iter hasn't been created yet, do so now.
      iter_ = CreateRocksDBIterator(db_, bloom_filter_mode_, query_id_);
    }
    ROCKSDB_SEEK(iter_.get(), key_prefix_.AsSlice());
    if (num_rocksdb_seeks_ != nullptr) {
      (*num_rocksdb_seeks_)++;
    }
    if (!HasMoreData()) {
      DOCDB_DEBUG_LOG("No more data found in RocksDB when trying to seek at prefix $0",
                      BestEffortDocDBKeyToStr(key_prefix_));
      subdoc_exists_ = Trilean::kFalse;
    } else {
      const rocksdb::Slice& key = iter_->key();
      // If the first key >= key_prefix_ in RocksDB starts with key_prefix_, then a
      // document/subdocument pointed to by key_prefix_ exists, or has been recently deleted.
      if (key_prefix_.IsPrefixOf(key)) {
        // TODO: make this return a Status and propagate it to the caller.
        subdoc_type_ = DecodeValueType(iter_->value());

        // TODO: with optional init markers we can find something that is more than one level
        //       deep relative to the current prefix.

        RETURN_NOT_OK(DecodeHybridTimeFromEndOfKey(key, &subdoc_ht_));

        // Cache the results of reading from RocksDB so that we don't have to read again in a later
        // operation in the same DocWriteBatch.
        DOCDB_DEBUG_LOG("Writing to DocWriteBatchCache: $0",
                        BestEffortDocDBKeyToStr(key_prefix_));
        if (prev_subdoc_exists != Trilean::kUnknown && prev_subdoc_ht > subdoc_ht_) {
          // We already saw an object init marker or a tombstone one level higher with a higher
          // hybrid_time, so just ignore this key/value pair. This had to be added when we switched
          // from a format with intermediate hybrid_times to our current format without them.
          //
          // Example (from a real test case):
          //
          // SubDocKey(DocKey([], ["a"]), [HT(38)]) -> {}
          // SubDocKey(DocKey([], ["a"]), [HT(37)]) -> DEL
          // SubDocKey(DocKey([], ["a"]), [HT(36)]) -> false
          // SubDocKey(DocKey([], ["a"]), [HT(1)]) -> {}
          // SubDocKey(DocKey([], ["a"]), ["y", HT(35)]) -> "lD\x97\xaf^m\x0a1\xa0\xfc\xc8YM"
          //
          // Caveat (04/17/2017): the HybridTime encoding in the above example is outdated.
          //
          // In the above layout, if we try to set "a.y.x" to a new value, we first seek to the
          // document key "a" and find that it exists, but then we seek to "a.y" and find that it
          // also exists as a primitive value (assuming we don't check the hybrid_time), and
          // therefore we can't create "a.y.x", which would be incorrect.
          subdoc_exists_ = Trilean::kFalse;
        } else {
          doc_write_batch_cache_->Put(key_prefix_, subdoc_ht_, subdoc_type_);
          if (subdoc_type_ != ValueType::kTombstone) {
            subdoc_exists_ = ToTrilean(true);
          }
        }
      } else {
        DOCDB_DEBUG_LOG("Actual RocksDB key found ($0) does not start with $1",
                        BestEffortDocDBKeyToStr(KeyBytes(key.ToString())),
                        BestEffortDocDBKeyToStr(key_prefix_));
        subdoc_exists_ = Trilean::kFalse;
      }
    }

  }
  DOCDB_DEBUG_LOG("New InternalDocIterator state: $0", ToDebugString());
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
