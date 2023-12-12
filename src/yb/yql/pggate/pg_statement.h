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

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/bytes_formatter.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/util/ybc_util.h"

namespace yb {
namespace pggate {

// Statement types.
// - Might use it for error reporting or debugging or if different operations share the same
//   CAPI calls.
// - TODO(neil) Remove StmtOp if we don't need it.
enum class StmtOp {
  STMT_NOOP = 0,
  STMT_CREATE_DATABASE,
  STMT_DROP_DATABASE,
  STMT_CREATE_SCHEMA,
  STMT_DROP_SCHEMA,
  STMT_CREATE_TABLE,
  STMT_DROP_TABLE,
  STMT_TRUNCATE_TABLE,
  STMT_CREATE_INDEX,
  STMT_DROP_INDEX,
  STMT_ALTER_TABLE,
  STMT_INSERT,
  STMT_UPDATE,
  STMT_DELETE,
  STMT_TRUNCATE,
  STMT_SELECT,
  STMT_ALTER_DATABASE,
  STMT_CREATE_TABLEGROUP,
  STMT_DROP_TABLEGROUP,
  STMT_SAMPLE,
  STMT_DROP_SEQUENCE,
  STMT_DROP_DB_SEQUENCES,
  STMT_CREATE_REPLICATION_SLOT,
  STMT_DROP_REPLICATION_SLOT,
};

class PgStatement : public PgMemctx::Registrable {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructors.
  // pg_session is the session that this statement belongs to. If PostgreSQL cancels the session
  // while statement is running, pg_session::sharedptr can still be accessed without crashing.
  explicit PgStatement(PgSession::ScopedRefPtr pg_session)
      : pg_session_(std::move(pg_session)), arena_(SharedArena()) {
  }

  virtual ~PgStatement() = default;

  const PgSession::ScopedRefPtr& pg_session() { return pg_session_; }

  // Statement type.
  virtual StmtOp stmt_op() const = 0;

  //------------------------------------------------------------------------------------------------
  static bool IsValidStmt(PgStatement* stmt, StmtOp op) {
    return (stmt != nullptr && stmt->stmt_op() == op);
  }

  const std::shared_ptr<ThreadSafeArena>& arena_ptr() const { return arena_; }

  ThreadSafeArena& arena() const { return *arena_; }

 protected:
  // YBSession that this statement belongs to.
  PgSession::ScopedRefPtr pg_session_;

  // Execution status.
  Status status_;
  std::string errmsg_;

  std::shared_ptr<ThreadSafeArena> arena_;
};

}  // namespace pggate
}  // namespace yb
