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

#pragma once

#include <unordered_map>
#include <string>

#include "yb/common/hybrid_time.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value_type.h"
#include "yb/dockv/value.h"

namespace yb {
namespace docdb {

// A utility used by DocWriteBatch. Caches generation hybrid_times (hybrid_times of full overwrite
// or deletion) for key prefixes that were read from RocksDB or created by previous operations
// performed on the DocWriteBatch.
//
// This class is not thread-safe.
class DocWriteBatchCache {
 public:
  struct Entry {
    EncodedDocHybridTime doc_hybrid_time;
    dockv::ValueEntryType value_type = dockv::ValueEntryType::kInvalid;
    UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp;
    // We found a key which matched the exact key_prefix_ we were searching for (excluding the
    // hybrid time). Since we search for a key prefix, we could search for a.b.c, but end up
    // finding a key like a.b.c.d.e. This field indicates that we searched for something like a.b.c
    // and found a key for a.b.c.
    bool found_exact_key_prefix = false;

    bool operator<(const Entry& other) const {
      return (doc_hybrid_time < other.doc_hybrid_time);
    }
  };

  // Records the generation hybrid_time corresponding to the given encoded key prefix, which is
  // assumed not to include the hybrid_time at the end.
  void Put(const dockv::KeyBytes& key_bytes, const Entry& entry);

  // Same thing, but doesn't use an already created entry.
  void Put(const dockv::KeyBytes& key_bytes,
           const EncodedDocHybridTime& gen_ht,
           dockv::ValueEntryType key_entry_type,
           UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp,
           bool found_exact_key_prefix = true) {
    Put(key_bytes, {gen_ht, key_entry_type, user_timestamp, found_exact_key_prefix});
  }

  // Returns the latest generation hybrid_time for the document/subdocument identified by the given
  // encoded key prefix.
  // TODO: switch to taking a slice as an input to avoid making a copy on lookup.
  std::optional<Entry> Get(const dockv::KeyBytes& encoded_key_prefix);

  std::string ToDebugString();

  static std::string EntryToStr(const Entry& entry);

  void Clear();

 private:
  std::unordered_map<KeyBuffer, Entry, ByteBufferHash> prefix_to_gen_ht_;
};


}  // namespace docdb
}  // namespace yb
