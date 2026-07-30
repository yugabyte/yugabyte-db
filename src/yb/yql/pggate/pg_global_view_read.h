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

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

class PgClient;

// Per-scan state for federated YugabyteDB global view reads.
//
// Each ForeignScan targeting a single tserver gets its own instance.
// The tserver UUID is not owned here; it is passed in by the caller
// (from the plan's fdw_private) on each ExecScan call.
//
class PgGlobalViewRead : public PgMemctx::Registrable {
 public:
  // Set text-format parameter values for parameterized queries.
  // A nullptr entry means the corresponding parameter is NULL.
  void SetParams(std::span<const char*> values);

  // Executes the query on the given tserver. On success with rows,
  // returns the serialized PgResultPB in {pgresult, pgresult_size, nullptr}.
  // On error, returns {nullptr, 0, error_message} with error_message
  // pointing to a string valid until the next ExecScan call. On success
  // with zero rows, returns {nullptr, 0, nullptr}.
  YbcRemotePgExecResult ExecScan(
      PgClient& client, std::string_view database_name, std::string_view query,
      std::string_view tserver_uuid);

 private:
  std::vector<uint8_t> serialized_result_;
  std::vector<std::optional<std::string>> params_;
  std::string last_error_;
};

}  // namespace yb::pggate
