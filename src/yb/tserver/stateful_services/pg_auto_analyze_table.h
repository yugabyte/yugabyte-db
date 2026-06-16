// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
// except in compliance with the License.  You may obtain a copy of the License at
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
#include <span>

#include "yb/common/entity_ids_types.h"

#include "yb/util/status_fwd.h"

namespace yb::client {
class TableHandle;
class YBSession;
}

namespace yb::stateful_service {

struct PgAutoAnalyzeMutationSnapshot {
  TableId table_id;
  int64_t mutations;
};

// Sets the persisted mutation count to 0 for each of `table_ids` in the PG_AUTO_ANALYZE
// stateful-service YCQL table. Each table's row is updated with a single conditional write
// (`IF EXISTS`), so rows that do not yet exist are left untouched.
Status ResetPgAutoAnalyzeMutationCounts(
    const client::TableHandle& table, client::YBSession& session,
    std::span<const TableId> table_ids);

// Subtracts each snapshot's `mutations` from the persisted mutation count of the corresponding
// table, clamping the result at 0 (i.e. computes max(current - snapshot, 0)).
//
// Consistency note: this is intentionally NOT a single atomic read-modify-write across the pair of
// writes it emits per table. It relies on each individual YCQL conditional UPDATE being an atomic
// compare-and-set on the (single-partition) row:
//   * a clamp-to-zero write guarded by `mutations < snapshot`, and
//   * a `mutations -= snapshot` write guarded by `mutations >= snapshot`.
// Because the subtract is guarded by `mutations >= snapshot` and evaluates `mutations - snapshot`
// against the same committed value atomically, the stored count can never go negative regardless
// of how these writes interleave with concurrent writers (e.g. a racing manual ANALYZE reset or a
// mutation-count persist). The trade-off is accuracy, not safety: under a race the final count may
// drift slightly (over- or under-counting recently reported mutations), which is acceptable for
// auto-analyze's heuristic mutation tracking.
Status SubtractPgAutoAnalyzeMutationCounts(
    const client::TableHandle& table, client::YBSession& session,
    std::span<const PgAutoAnalyzeMutationSnapshot> snapshots);

}  // namespace yb::stateful_service
