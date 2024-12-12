//
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
//

#pragma once

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/common/pgsql_protocol.pb.h"

namespace yb {

constexpr char kPgAdvisoryLocksTableName[] = "pg_advisory_locks";

// Helper class for the advisory locks table.
class YsqlAdvisoryLocksTable {
 public:
  explicit YsqlAdvisoryLocksTable(client::YBClient& client);
  ~YsqlAdvisoryLocksTable();

  Result<client::YBPgsqlLockOpPtr> CreateLockOp(
      uint32_t db_oid, uint32_t class_oid, uint32_t objid, uint32_t objsubid,
      PgsqlLockRequestPB::PgsqlAdvisoryLockMode mode, bool wait,
      rpc::Sidecars* sidecars) EXCLUDES(mutex_);

 private:
  friend class AdvisoryLockTest;

  Result<client::YBTablePtr> GetTable() EXCLUDES(mutex_);

  std::mutex mutex_;
  client::YBTablePtr table_ GUARDED_BY(mutex_);;
  client::YBClient& client_;
};

} // namespace yb
