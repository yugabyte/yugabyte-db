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

#pragma once

#include <memory>
#include <string>

#include <boost/container/small_vector.hpp>

#include "yb/common/pgsql_protocol.fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/result.h"

namespace yb {
struct PgObjectId;

namespace pggate {
class PgSession;

using PrefetchedDataHolder =
    std::shared_ptr<const boost::container::small_vector<rpc::SidecarHolder, 8>>;

// PgSysTablePrefetcher class allows to register multiple sys tables and read all of them in
// a single RPC (Almost single, actual number of RPCs depends on sys table size).
// GetData method is used to access particular table data.
class PgSysTablePrefetcher {
 public:
  explicit PgSysTablePrefetcher(uint64_t latest_known_ysql_catalog_version);
  ~PgSysTablePrefetcher();

  // Register new sys table to be read on a first GetData method call.
  void Register(const PgObjectId& table_id, const PgObjectId& index_id);
  // GetData of previously registered table.
  Result<PrefetchedDataHolder> GetData(
    PgSession* session, const LWPgsqlReadRequestPB& read_req, bool index_check_required);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pggate
} // namespace yb
