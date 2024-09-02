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
#include <vector>

#include "yb/util/result.h"

namespace yb::client {
class YBClient;
}

namespace yb::master {

// Information about a sequence; see the Postgres sequence documentation for the meaning of
// last_value and is_called.
struct YsqlSequenceInfo {
  int64_t sequence_oid;
  int64_t last_value;
  bool is_called;
};

// Scan sequences_data table for information about all sequences in database db_oid.
//
// max_rows_per_read is exposed so testing can force paging to occur.  Tests may set TEST_fail_read
// to cause the reads done by ScanSequencesDataTable to fail in order to test the error handling
// pathways.
Result<std::vector<YsqlSequenceInfo>> ScanSequencesDataTable(
    client::YBClient& client, uint32_t db_oid, uint64_t max_rows_per_read = 10000,
    bool TEST_fail_read = false);

// Ensure that there is a full update for every sequence in sequences in the WALs after the time the
// sequence info was read from the sequences_data table.
//
// That is, if a sequence S in sequences had value v at the time we scanned sequences_data to obtain
// sequences, then after this function successfully completes, there will either be an update S := v
// or some other update to S in the WAL with commit timestamp after the time the scan was done.
//
// When successful returns the number of updates it makes.
Result<int> EnsureSequenceUpdatesInWal(
    client::YBClient& client, uint32_t db_oid, const std::vector<YsqlSequenceInfo>& sequences);

}  // namespace yb::master
