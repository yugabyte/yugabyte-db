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
                             Timestamp gen_ts,
                             ValueType value_type) {
  DOCDB_DEBUG_LOG(
      "Writing to DocWriteBatchCache: encoded_key_prefix=$0, gen_ts=$1, value_type=$2",
      BestEffortKeyBytesToStr(key_bytes),
      gen_ts.ToDebugString(),
      ValueTypeToStr(value_type));
  prefix_to_gen_ts_[key_bytes.AsStringRef()] = std::make_pair(gen_ts, value_type);
}

boost::optional<DocWriteBatchCache::Entry> DocWriteBatchCache::Get(
    const KeyBytes& encoded_key_prefix) {
  auto iter = prefix_to_gen_ts_.find(encoded_key_prefix.AsStringRef());
#ifdef DOCDB_DEBUG
  if (iter == prefix_to_gen_ts_.end()) {
    DOCDB_DEBUG_LOG("DocWriteBatchCache contained no entry for $0",
        BestEffortKeyBytesToStr(encoded_key_prefix));
  } else {
    DOCDB_DEBUG_LOG("DocWriteBatchCache entry found for key $0: $1",
        BestEffortKeyBytesToStr(encoded_key_prefix), EntryToStr(iter->second));
  }
#endif
  return iter == prefix_to_gen_ts_.end() ? boost::optional<Entry>() : iter->second;
}

string DocWriteBatchCache::ToDebugString() {
  vector<pair<string, Entry>> sorted_contents;
  copy(prefix_to_gen_ts_.begin(), prefix_to_gen_ts_.end(), back_inserter(sorted_contents));
  sort(sorted_contents.begin(), sorted_contents.end());
  ostringstream ss;
  ss << "DocWriteBatchCache[" << endl;
  for (const auto& kv : sorted_contents) {
    ss << "  " << BestEffortKeyBytesToStr(KeyBytes(kv.first)) << " -> "
       << EntryToStr(kv.second) << endl;
  }
  ss << "]";
  return ss.str();
}

string DocWriteBatchCache::EntryToStr(const Entry& entry) {
  ostringstream ss;
  ss << "("
     << entry.first.ToDebugString() << ", "
     << ValueTypeToStr(entry.second)
     << ")";
  return ss.str();
}

void DocWriteBatchCache::Clear() {
  prefix_to_gen_ts_.clear();
}

}  // namespace docdb
}  // namespace yb
