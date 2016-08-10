// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_WRITE_BATCH_CACHE_H_
#define YB_DOCDB_DOC_WRITE_BATCH_CACHE_H_

#include <unordered_set>
#include <string>

#include <boost/optional.hpp>

#include "yb/common/timestamp.h"
#include "yb/docdb/value_type.h"

namespace yb {
namespace docdb {

// A utility used by DocWriteBatch. Caches generation timestamps (timestamps of full overwrite
// or deletion) for key prefixes that were read from RocksDB or created by previous operations
// performed on the DocWriteBatch.
//
// This class is not thread-safe.
class DocWriteBatchCache {
 public:
  using Entry = std::pair<yb::Timestamp, ValueType>;

  // Records the generation timestamp corresponding to the given encoded key prefix, which is
  // assumed not to include the timestamp at the end.
  void Put(const std::string& encoded_key_prefix, yb::Timestamp gen_ts, ValueType value_type) {
    prefix_to_gen_ts_[encoded_key_prefix] = std::make_pair(gen_ts, value_type);
  }

  // Returns the latest generation timestamp for the document/subdocument identified by the given
  // encoded key prefix.
  // TODO: switch to taking a slice as an input to avoid making a copy on lookup.
  boost::optional<Entry> Get(const std::string& encoded_key_prefix) {
    std::string slice_as_str = encoded_key_prefix;
    auto iter = prefix_to_gen_ts_.find(encoded_key_prefix);
    return iter == prefix_to_gen_ts_.end() ? boost::optional<Entry>() : iter->second;
  };

 private:
  std::unordered_map<std::string, Entry> prefix_to_gen_ts_;
};


};
}

#endif