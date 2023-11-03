// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/docdb/doc_write_batch_cache.h"

#include <vector>
#include <algorithm>
#include <sstream>

#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/dockv/primitive_value.h"
#include "yb/util/bytes_formatter.h"

using std::back_inserter;
using std::copy;
using std::endl;
using std::ostringstream;
using std::pair;
using std::sort;
using std::string;
using std::vector;

namespace yb {
namespace docdb {

void DocWriteBatchCache::Put(
    const dockv::KeyBytes& key_bytes, const DocWriteBatchCache::Entry& entry) {
  DOCDB_DEBUG_LOG(
    "Writing to DocWriteBatchCache: encoded_key_prefix=$0, gen_ht=$1, value_type=$2",
    dockv::BestEffortDocDBKeyToStr(key_bytes),
    entry.doc_hybrid_time.ToString(),
    ToString(entry.value_type));
  prefix_to_gen_ht_[key_bytes.data()] = entry;
}

boost::optional<DocWriteBatchCache::Entry> DocWriteBatchCache::Get(
    const dockv::KeyBytes& encoded_key_prefix) {
  auto iter = prefix_to_gen_ht_.find(encoded_key_prefix.data());
#ifdef DOCDB_DEBUG
  if (iter == prefix_to_gen_ht_.end()) {
    DOCDB_DEBUG_LOG("DocWriteBatchCache contained no entry for $0",
                    dockv::BestEffortDocDBKeyToStr(encoded_key_prefix));
  } else {
    DOCDB_DEBUG_LOG("DocWriteBatchCache entry found for key $0: $1",
                    dockv::BestEffortDocDBKeyToStr(encoded_key_prefix), EntryToStr(iter->second));
  }
#endif
  return iter == prefix_to_gen_ht_.end() ? boost::optional<Entry>() : iter->second;
}

string DocWriteBatchCache::ToDebugString() {
  vector<pair<KeyBuffer, Entry>> sorted_contents;
  copy(prefix_to_gen_ht_.begin(), prefix_to_gen_ht_.end(), back_inserter(sorted_contents));
  sort(sorted_contents.begin(), sorted_contents.end());
  ostringstream ss;
  ss << "DocWriteBatchCache[" << endl;
  for (const auto& kv : sorted_contents) {
    ss << "  " << dockv::BestEffortDocDBKeyToStr(kv.first.AsSlice()) << " -> "
       << EntryToStr(kv.second) << endl;
  }
  ss << "]";
  return ss.str();
}

string DocWriteBatchCache::EntryToStr(const Entry& entry) {
  ostringstream ss;
  ss << "("
     << entry.doc_hybrid_time.ToString() << ", "
     << entry.value_type << ", ";
  if (entry.user_timestamp == dockv::ValueControlFields::kInvalidTimestamp) {
    ss << "InvalidUserTimestamp";
  } else {
    ss << entry.user_timestamp;
  }
  ss << ", " << entry.found_exact_key_prefix
     << ")";
  return ss.str();
}

void DocWriteBatchCache::Clear() {
  prefix_to_gen_ht_.clear();
}

}  // namespace docdb
}  // namespace yb
