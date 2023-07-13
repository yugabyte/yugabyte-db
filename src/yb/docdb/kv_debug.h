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

#include <string>

#include "yb/util/slice.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_types.h"
#include "yb/dockv/doc_key.h"

namespace yb {
namespace docdb {

Result<std::string> DocDBKeyToDebugStr(
    Slice key_slice, StorageDbType db_type,
    dockv::HybridTimeRequired htRequired = dockv::HybridTimeRequired::kTrue);

Result<std::string> DocDBValueToDebugStr(
    KeyType key_type, Slice key, Slice value,
    SchemaPackingProvider* schema_packing_provider /*null ok*/);

Result<std::string> DocDBValueToDebugStr(
    Slice key, StorageDbType db_type, Slice value,
    SchemaPackingProvider* schema_packing_provider /*null ok*/);

}  // namespace docdb
}  // namespace yb
