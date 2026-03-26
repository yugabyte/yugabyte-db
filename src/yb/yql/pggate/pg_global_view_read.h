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
#include <string>
#include <string_view>
#include <vector>

#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

class PgClient;

// Encapsulates scan state for federated YugabyteDB global view reads.
//
// Each scan iterates over tservers, executing a remote SQL query on each one
// via RPC. This class isolates per-scan state so that multiple concurrent scans
// (e.g. from joins or subqueries) don't interfere with each other.
//
class PgGlobalViewRead : public PgMemctx::Registrable {
 public:
  PgGlobalViewRead(
      PgClient& pg_client, const char* query, std::vector<std::string> tserver_uuids);
  ~PgGlobalViewRead() = default;

  void ResetScan();

  // Set text-format parameter values for parameterized queries.
  // A nullptr entry means the corresponding parameter is NULL.
  void SetParams(int num_params, const char** values);

  // Executes the query on the next tserver and returns the serialized
  // PgResultPB as a byte buffer. Advances the tserver index by one.
  // Returns {nullptr, 0, false} when all tservers are exhausted.
  YbcRemotePgExecResult ExecScan();

  bool is_eof() const { return next_tserver_idx_ >= tserver_uuids_.size(); }

 private:
  PgClient& pg_client_;
  std::string_view query_;
  std::vector<std::string> tserver_uuids_;
  size_t next_tserver_idx_ = 0;
  // Holds the serialized PgResultPB between ExecScan and the caller's
  // deserialization.
  std::string serialized_result_;

  std::vector<std::optional<std::string>> params_;
};

}  // namespace yb::pggate
