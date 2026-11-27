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

#include <cstdint>

#include "yb/yql/pggate/ybc_latency_ledger.h"

namespace yb {

class LatencyLedgerPB;

namespace pggate {

// Adds the ledger the local tserver returned for a request sent at sent_nanos and whose response
// was read at received_nanos, to the current round trip. shared_memory tells whether the request
// went through the shared memory exchange or over an RPC.
void PgLedgerAddRequest(
    int64_t sent_nanos, int64_t received_nanos, const LatencyLedgerPB& ledger,
    bool shared_memory);

} // namespace pggate
} // namespace yb
