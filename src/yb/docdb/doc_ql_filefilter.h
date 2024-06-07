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

// Utilities for encoding and decoding key/value pairs that are used in the DocDB code.

#pragma once

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/rocksdb/rocksdb_fwd.h"

namespace yb::docdb {

std::shared_ptr<rocksdb::ReadFileFilter> CreateFileFilter(const qlexpr::YQLScanSpec& scan_spec);
std::shared_ptr<rocksdb::ReadFileFilter> CreateHybridTimeFileFilter(HybridTime min_hybrid_Time);
// Create a file filter for intentsdb using the given min running hybrid time. Filtering is done
// based on intent hybrid time stored in the intent key, not commit time of the transaction.
std::shared_ptr<rocksdb::ReadFileFilter> CreateIntentHybridTimeFileFilter(
    HybridTime min_running_ht);

}  // namespace yb::docdb
