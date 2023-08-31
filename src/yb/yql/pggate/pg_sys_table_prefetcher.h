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

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
struct PgObjectId;

namespace pggate {
class PgSession;

YB_DEFINE_ENUM(PrefetchingCacheMode, (NO_CACHE)(TRUST_CACHE)(RENEW_CACHE_SOFT)(RENEW_CACHE_HARD));

using PrefetchedDataHolder =
    std::shared_ptr<const boost::container::small_vector<rpc::SidecarHolder, 8>>;

struct PrefetcherOptions {
  struct VersionInfo {
    uint64_t version;
    bool is_db_catalog_version_mode;

    std::string ToString() const;
  };

  VersionInfo version_info;
  PrefetchingCacheMode cache_mode;
  uint64_t fetch_row_limit;

  std::string ToString() const;
};

// PgSysTablePrefetcher class allows to register multiple sys tables and read all of them in
// a single RPC (Almost single, actual number of RPCs depends on sys table size).
// GetData method is used to access particular table data.
class PgSysTablePrefetcher {
 public:
  explicit PgSysTablePrefetcher(const PrefetcherOptions& options);
  ~PgSysTablePrefetcher();

  // Register new sys table to be read on a first GetData method call.
  void Register(
      const PgObjectId& table_id, const PgObjectId& index_id, int row_oid_filtering_attr);

  // Load registered tables
  Status Prefetch(PgSession* session);

  // GetData of previously registered table.
  PrefetchedDataHolder GetData(const LWPgsqlReadRequestPB& read_req, bool index_check_required);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pggate
} // namespace yb
