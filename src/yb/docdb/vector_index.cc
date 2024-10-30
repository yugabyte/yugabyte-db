// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/vector_index.h"

namespace yb::docdb {

namespace detail {

void AppendSubkey(dockv::KeyBytes& key, VectorIndexLevel level) {
  key.AppendKeyEntryType(dockv::KeyEntryType::kUInt32);
  key.AppendUInt32(level);
}

void AppendSubkey(dockv::KeyBytes& key, VertexId id) {
  key.AppendKeyEntryType(dockv::KeyEntryType::kVertexId);
  key.AppendUInt64(id);
}

}  // namespace detail

}  // namespace yb::docdb
