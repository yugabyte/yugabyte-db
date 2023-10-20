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
#include <utility>

#include "yb/yql/pggate/pg_dml_write.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DELETE
//--------------------------------------------------------------------------------------------------

class PgDelete : public PgDmlWrite {
 public:
  PgDelete(PgSession::ScopedRefPtr pg_session,
           const PgObjectId& table_id,
           bool is_region_local,
           YBCPgTransactionSetting transaction_setting)
      : PgDmlWrite(std::move(pg_session), table_id, is_region_local, transaction_setting) {}

  StmtOp stmt_op() const override { return StmtOp::STMT_DELETE; }

  void SetIsPersistNeeded(const bool is_persist_needed) {
    write_req_->set_is_delete_persist_needed(is_persist_needed);
  }

 private:
  PgsqlWriteRequestPB::PgsqlStmtType stmt_type() const override {
    return PgsqlWriteRequestPB::PGSQL_DELETE;
  }
};

}  // namespace pggate
}  // namespace yb
