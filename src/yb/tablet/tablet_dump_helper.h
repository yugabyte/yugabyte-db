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

#include <optional>

#include "yb/common/column_id.h"
#include "yb/common/value.fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"
#include "yb/util/env.h"
#include "yb/util/slice.h"

#include "yb/tablet/tablet.h"

namespace yb::tablet {

// Identifies the scheme DumpTabletData hashes rows under. Reported in
// DumpTabletDataResponsePB::hash_scheme_version. Hashes are comparable only within one version, and
// nothing pins a cluster to a single version: a rolling upgrade runs both binaries, and an xCluster
// source and target upgrade independently.
//
// BUMP THIS when anything that feeds a hash changes: the mixing, the salt, or which values are
// hashed. This includes the QLValuePB wire format, because a value is hashed as its serialized
// QLValuePB. A field number or encoding change upstream rehashes identical data without touching
// this file.
//
// Version 1 hashes each value with MurmurHash2_64 salted by its column id, xors the values of a
// row, seals the row with an avalanche step, and xors the rows. Version 0 is what a tserver that
// reports no version used: a plain xor of every value's serialization.
constexpr uint32_t kTabletDataHashSchemeVersion = 1;

// Builds the contribution one row makes to a table's xor_hash. The YSQL and YCQL scan paths both
// use it, so they agree on the scheme.
//
// Each value is hashed with the id of its column as salt. Without the salt a value carries only its
// type, because a serialized QLValuePB is tagged by type and not by column. Two same-typed columns
// with swapped values, or two columns holding one value, would then cancel, and a damaged row would
// hash like an undamaged one. The row is sealed before the caller xors it into the table total,
// which stops contributions cancelling across rows and keeps whole rows commutative.
//
// The salt costs cross-cluster comparability: two clusters' hashes mean the same thing only if
// their column ids agree, so a caller comparing hashes across clusters compares the schemas first.
// Scheme version 0 hashed no column ids; version 1 adds them.
class RowHashAccumulator {
 public:
  // Adds one non-NULL column value. A NULL is not added: in DocDB a NULL is the absence of a value,
  // and a column absent from the schema is a schema difference, which comparing schemas catches.
  void AddValue(ColumnId column_id, const QLValuePB& value);

  // What this row contributes to the table's xor_hash.
  uint64_t RowHash() const;

 private:
  uint64_t column_xor_ = 0;
};

// Computes an xor hash and row count over the tablet's rows at read_ht, or at safe time if read_ht
// is 0. A non-empty target_table_id restricts the scan to that one table, which is how a single
// table of a colocated tablet is hashed. An empty target_table_id hashes every table in the tablet
// except colocation parents and vector indexes.
//
// An explicit read_ht is waited for, bounded by max_read_time_wait (nullopt falls back to
// FLAGS_dump_tablet_data_max_read_time_wait_ms, zero fails immediately) and never outliving the RPC
// deadline. A replica that stays behind fails with READ_TIME_NOT_REACHED rather than reporting a
// partially applied state as the state at read_ht. A read_ht more than
// FLAGS_dump_tablet_data_max_read_time_ahead_ms ahead of this server's clock is rejected with
// InvalidArgument instead of waited for.
//
// start_partition_key / end_partition_key narrow the scan within each scanned table's own key space
// (raw PartitionPB encoding, start inclusive, end exclusive, empty = the table's natural bound).
// For a colocated table the cotable_id/colocation_id prefix is prepended automatically, so the
// range addresses that table's slice of the shared tablet.
//
// max_rows > 0 stops after that many rows. next_key (when non-null) receives the exclusive
// continuation key of the first unhashed row, or empty if the range was fully hashed. max_rows
// requires a concrete target_table_id: a colocation parent covers several tables with independent
// key spaces, so one continuation key could not say where to resume.
Status DumpTabletData(
    Tablet& tablet, std::shared_future<client::YBClient*> client_future, WritableFile* file,
    uint64_t read_ht, std::optional<MonoDelta> max_read_time_wait, CoarseTimePoint deadline,
    uint64_t& xor_hash, uint64_t& row_count, const TableId& target_table_id = "",
    Slice start_partition_key = Slice(), Slice end_partition_key = Slice(), uint64_t max_rows = 0,
    std::string* next_key = nullptr);

}  // namespace yb::tablet
