// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_write_batch_cache.h"

#include <vector>
#include <algorithm>
#include <sstream>

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/primitive_value.h"
#include "yb/util/bytes_formatter.h"

using std::back_inserter;
using std::copy;
using std::endl;
using std::ostringstream;
using std::pair;
using std::sort;
using std::string;
using std::vector;

using yb::util::FormatBytesAsStr;

namespace yb {
namespace docdb {

void DocWriteBatchCache::Put(const KeyBytes& key_bytes,
                             DocHybridTime gen_ht,
                             ValueType value_type) {
  DOCDB_DEBUG_LOG(
      "Writing to DocWriteBatchCache: encoded_key_prefix=$0, gen_ht=$1, value_type=$2",
      BestEffortDocDBKeyToStr(key_bytes),
      gen_ht.ToString(),
      ToString(value_type));
  prefix_to_gen_ht_[key_bytes.AsStringRef()] = std::make_pair(gen_ht, value_type);
}

boost::optional<DocWriteBatchCache::Entry> DocWriteBatchCache::Get(
    const KeyBytes& encoded_key_prefix) {
  auto iter = prefix_to_gen_ht_.find(encoded_key_prefix.AsStringRef());
#ifdef DOCDB_DEBUG
  if (iter == prefix_to_gen_ht_.end()) {
    DOCDB_DEBUG_LOG("DocWriteBatchCache contained no entry for $0",
                    BestEffortDocDBKeyToStr(encoded_key_prefix));
  } else {
    DOCDB_DEBUG_LOG("DocWriteBatchCache entry found for key $0: $1",
                    BestEffortDocDBKeyToStr(encoded_key_prefix), EntryToStr(iter->second));
  }
#endif
  return iter == prefix_to_gen_ht_.end() ? boost::optional<Entry>() : iter->second;
}

string DocWriteBatchCache::ToDebugString() {
  vector<pair<string, Entry>> sorted_contents;
  copy(prefix_to_gen_ht_.begin(), prefix_to_gen_ht_.end(), back_inserter(sorted_contents));
  sort(sorted_contents.begin(), sorted_contents.end());
  ostringstream ss;
  ss << "DocWriteBatchCache[" << endl;
  for (const auto& kv : sorted_contents) {
    ss << "  " << BestEffortDocDBKeyToStr(KeyBytes(kv.first)) << " -> "
       << EntryToStr(kv.second) << endl;
  }
  ss << "]";
  return ss.str();
}

string DocWriteBatchCache::EntryToStr(const Entry& entry) {
  ostringstream ss;
  ss << "("
     << entry.first.ToString() << ", "
     << entry.second
     << ")";
  return ss.str();
}

void DocWriteBatchCache::Clear() {
  prefix_to_gen_ht_.clear();
}

}  // namespace docdb
}  // namespace yb
