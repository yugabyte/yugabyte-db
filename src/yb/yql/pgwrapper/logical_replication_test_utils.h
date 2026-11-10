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

#include <string>
#include <string_view>
#include <vector>

#include "yb/util/monotime.h"
#include "yb/util/result.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pgwrapper {

// Streams a logical replication slot with pgoutput over the walsender protocol, until a change
// contains needle, then acknowledges the commit of that change's transaction. conn_str connects to
// the slot's database; replication=database is added to it. If inserted_rows is set, the column
// values of each streamed insert are appended to it, as returned by DecodePgOutputInsert.
Status StreamSlotAndAcknowledge(
    const std::string& conn_str, const std::string& slot_name, const std::string& publication_name,
    const std::string& needle, MonoDelta timeout,
    std::vector<std::string>* inserted_rows);

// Returns the column values of a pgoutput insert message, joined by commas. A null value is NULL
// and an unchanged TOAST value is UNCHANGED.
Result<std::string> DecodePgOutputInsert(std::string_view message);

// Sends a standby status update reporting lsn as written, flushed and applied.
Status SendStandbyStatusUpdate(PGconn* conn, uint64_t lsn);

} // namespace yb::pgwrapper
