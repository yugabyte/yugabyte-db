//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <memory>
#include <optional>

#include <boost/container/small_vector.hpp>

#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_select.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_tools.h"

namespace yb::pggate {

class PgSelectIndex : public PgSelect {
 public:
  Result<std::optional<YbctidBatch>> FetchYbctidBatch();

  [[nodiscard]] bool IsPgSelectIndex() const override { return true; }

  static Result<std::unique_ptr<PgSelectIndex>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& index_id,
      const YbcPgTableLocalityInfo& locality_info,
      std::shared_ptr<LWPgsqlReadRequestPB>&& read_req = {});

 protected:
  explicit PgSelectIndex(const PgSession::ScopedRefPtr& pg_session);

 private:
  // Prepare NESTED query for secondary index. This function is called when Postgres layer is
  // accessing the IndexTable via an outer select (Sequential or primary scans)
  Status PrepareSubquery(
      const PgObjectId& index_id, std::shared_ptr<LWPgsqlReadRequestPB>&& read_req);

  boost::container::small_vector<Slice, 8> ybctids_;
};

}  // namespace yb::pggate
