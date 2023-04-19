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

#include <stdint.h>

#include <cstring>
#include <functional>
#include <ostream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/key_entry_value.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/string_util.h"

namespace yb::dockv {

// Identifies a particular subdocument inside the logical representation of the document database.
// By "logical representation" we mean that we are not concerned with the exact keys used in the
// underlying key-value store. This is very similar to a SubDocKey without a hybrid time, and can
// probably be merged with it.

class DocPath {
 public:
  template<class... T>
  DocPath(const KeyBytes& encoded_doc_key, T&&... subkeys)
      : subkeys_{std::forward<T>(subkeys)...} {
    encoded_doc_key_ = encoded_doc_key;
  }

  template<class... T>
  DocPath(const Slice& encoded_doc_key, T&&... subkeys)
      : encoded_doc_key_(encoded_doc_key), subkeys_{std::forward<T>(subkeys)...} {
  }

  DocPath(const KeyBytes& encoded_doc_key, const KeyEntryValues& subkeys)
      : encoded_doc_key_(encoded_doc_key),
        subkeys_(subkeys) {
  }

  const KeyBytes& encoded_doc_key() const { return encoded_doc_key_; }

  size_t num_subkeys() const { return subkeys_.size(); }

  const KeyEntryValue& subkey(size_t i) const {
    assert(i < num_subkeys());
    return subkeys_[i];
  }

  std::string ToString() const;

  void AddSubKey(const KeyEntryValue& subkey);

  void AddSubKey(KeyEntryValue&& subkey);

  const KeyEntryValue& last_subkey() const {
    assert(!subkeys_.empty());
    return subkeys_.back();
  }

  // Note: the hash is supposed to be uint16_t, but protobuf only supports uint32.
  // So this function takes in uint32_t.
  // TODO (akashnil): Add uint16 data type in docdb.
  static DocPath DocPathFromRedisKey(
      uint16_t hash, const std::string& key, const std::string& subkey = "");

  const KeyEntryValues& subkeys() const {
    return subkeys_;
  }

 private:
  // Encoded key identifying the document. This key can itself contain multiple components
  // (hash bucket, hashed components, range components).
  // TODO(mikhail): should this really be encoded?
  KeyBytes encoded_doc_key_;

  KeyEntryValues subkeys_;
};

inline std::ostream& operator << (std::ostream& out, const DocPath& doc_path) {
  return out << doc_path.ToString();
}

}  // namespace yb::dockv
