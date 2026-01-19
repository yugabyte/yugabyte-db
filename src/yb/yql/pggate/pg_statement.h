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

#include <stdint.h>

#include <concepts>
#include <memory>
#include <string>
#include <utility>

#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/util/ybc_util.h"

namespace yb::pggate {

YB_DEFINE_TYPED_ENUM(StmtOp, uint8_t,
    (kCreateDatabase)
    (kDropDatabase)
    (kCreateTable)
    (kDropTable)
    (kTruncateTable)
    (kCreateIndex)
    (kDropIndex)
    (kAlterTable)
    (kInsert)
    (kUpdate)
    (kDelete)
    (kTruncate)
    (kSelect)
    (kAlterDatabase)
    (kCreateTablegroup)
    (kDropTablegroup)
    (kSample)
    (kDropSequence)
    (kDropDbSequences)
    (kCreateReplicationSlot)
    (kDropReplicationSlot)
);

class PgStatement : public PgMemctx::Registrable {
 public:
  explicit PgStatement(const PgSession::ScopedRefPtr& pg_session)
      : pg_session_(pg_session), arena_(SharedThreadSafeArena()) {
  }

  virtual ~PgStatement() = default;

  [[nodiscard]] virtual StmtOp stmt_op() const = 0;

  const std::shared_ptr<ThreadSafeArena>& arena_ptr() const { return arena_; }

  ThreadSafeArena& arena() const { return *arena_; }

 protected:
  // YBSession that this statement belongs to.
  PgSession::ScopedRefPtr pg_session_;

 private:
  std::shared_ptr<ThreadSafeArena> arena_;
};

template <class T>
concept PgStatementSubclass = std::derived_from<T, PgStatement> && !std::same_as<T, PgStatement>;

template <PgStatementSubclass Base, StmtOp T>
class PgStatementLeafBase : public Base {
 public:
  using BaseParam = Base;

  static constexpr StmtOp kStmtOp = T;
  [[nodiscard]] StmtOp stmt_op() const override final { return kStmtOp; }

 protected:
  using BaseType = PgStatementLeafBase<Base, T>;

  template <class... Args>
  explicit PgStatementLeafBase(Args&&... args) : Base(std::forward<Args>(args)...) {}
};

}  // namespace yb::pggate
