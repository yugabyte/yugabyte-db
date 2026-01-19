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

#include "yb/util/result.h"

#include "yb/yql/pggate/pg_dml_write.h"

namespace yb::pggate {

class PgUpdate final : public PgStatementLeafBase<PgDmlWrite, StmtOp::kUpdate> {
 public:
  static Result<std::unique_ptr<PgUpdate>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info,
      YbcPgTransactionSetting transaction_setting) {
    std::unique_ptr<PgUpdate> result{new PgUpdate{pg_session, transaction_setting}};
    RETURN_NOT_OK(result->Prepare(table_id, locality_info));
    return result;
  }

 private:
  PgUpdate(const PgSession::ScopedRefPtr& pg_session, YbcPgTransactionSetting transaction_setting)
      : BaseType(pg_session, transaction_setting) {}

  PgsqlWriteRequestPB::PgsqlStmtType stmt_type() const override {
    return PgsqlWriteRequestPB::PGSQL_UPDATE;
  }
};

}  // namespace yb::pggate
