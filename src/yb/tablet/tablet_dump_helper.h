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


#pragma once

#include "yb/util/status.h"
#include "yb/util/env.h"

#include "yb/tablet/tablet.h"

namespace yb::tablet {

// Computes an XOR hash and row count over the tablet's rows at read_ht (or safe time if read_ht is
// 0). If target_table_id is non-empty, the scan is restricted to that single table within the
// tablet; this is the way to hash one table of a colocated tablet rather than the whole tablet.
// An empty target_table_id hashes every (non-parent, non-vector-index) table in the tablet, which
// for a colocated tablet means all colocated tables sharing it.
Status DumpTabletData(
    Tablet& tablet, std::shared_future<client::YBClient*> client_future, WritableFile* file,
    uint64_t read_ht, CoarseTimePoint deadline, uint64_t& xor_hash, uint64_t& row_count,
    const TableId& target_table_id = "");

}  // namespace yb::tablet
