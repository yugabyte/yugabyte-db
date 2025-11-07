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

#include "yb/util/result.h"

#include "yb/yql/pggate/pg_dml_read.h"

namespace yb::pggate {

class PgSelect : public PgStatementLeafBase<PgDmlRead, StmtOp::kSelect> {
 public:
  struct IndexQueryInfo {
    IndexQueryInfo(PgObjectId id_, bool is_embedded_)
        : id(id_), is_embedded(is_embedded_) {}

    PgObjectId id;
    bool is_embedded;
  };

  static Result<std::unique_ptr<PgSelect>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info,
      const std::optional<IndexQueryInfo>& index_info = std::nullopt);

 protected:
  explicit PgSelect(const PgSession::ScopedRefPtr& pg_session);

  Status Prepare(
      const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info,
      const std::optional<IndexQueryInfo>& index_info = std::nullopt);
};

}  // namespace yb::pggate
