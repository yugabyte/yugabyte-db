//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <memory>
#include <vector>

#include "yb/util/result.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_select.h"

namespace yb::pggate {

class PgSelectIndex : public PgSelect {
 public:
  PgSelectIndex(
      PgSession::ScopedRefPtr pg_session, const PgObjectId& index_id,
      bool is_region_local, const PrepareParameters& prepare_params = {});

  // Prepare NESTED query for secondary index. This function is called when Postgres layer is
  // accessing the IndexTable via an outer select (Sequential or primary scans)
  Status PrepareSubquery(std::shared_ptr<LWPgsqlReadRequestPB> read_req);

  virtual Result<const std::vector<Slice>*> FetchYbctidBatch();

 private:
  // Get next batch of ybctids from either PgGate::cache or server.
  Result<bool> GetNextYbctidBatch();

};

}  // namespace yb::pggate
