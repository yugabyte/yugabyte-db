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

class PgDelete final : public PgStatementLeafBase<PgDmlWrite, StmtOp::kDelete> {
 public:
  void SetIsPersistNeeded(bool is_persist_needed) {
    write_req_->set_is_delete_persist_needed(is_persist_needed);
  }

  static Result<std::unique_ptr<PgDelete>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
      const YbcPgTableLocalityInfo& locality_info,
      YbcPgTransactionSetting transaction_setting) {
    std::unique_ptr<PgDelete> result{new PgDelete{pg_session, transaction_setting}};
    RETURN_NOT_OK(result->Prepare(table_id, locality_info));
    return result;
  }

 private:
  PgDelete(
      const PgSession::ScopedRefPtr& pg_session, YbcPgTransactionSetting transaction_setting)
      : BaseType(pg_session, transaction_setting) {}

  PgsqlWriteRequestPB::PgsqlStmtType stmt_type() const override {
    return PgsqlWriteRequestPB::PGSQL_DELETE;
  }
};

}  // namespace yb::pggate
