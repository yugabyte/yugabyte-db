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

#include "yb/dockv/doc_path.h"

#include "yb/dockv/doc_key.h"

using std::string;

namespace yb::dockv {

std::string DocPath::ToString() const {
  return strings::Substitute("DocPath($0, $1)",
      BestEffortDocDBKeyToStr(encoded_doc_key_), rocksdb::VectorToString(subkeys_));
}

void DocPath::AddSubKey(const KeyEntryValue& subkey) {
  subkeys_.emplace_back(subkey);
}

void DocPath::AddSubKey(KeyEntryValue&& subkey) {
  subkeys_.emplace_back(std::move(subkey));
}

DocPath DocPath::DocPathFromRedisKey(uint16_t hash, const string& key, const string& subkey) {
  DocPath doc_path(DocKey::FromRedisKey(hash, key).Encode());
  if (!subkey.empty()) {
    doc_path.AddSubKey(KeyEntryValue(subkey));
  }
  return doc_path;
}

}  // namespace yb::dockv
