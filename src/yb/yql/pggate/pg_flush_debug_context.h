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
//

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/gutil/macros.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"


namespace yb::pggate {

class PgFlushDebugContext {
 public:
  [[nodiscard]] static YbcFlushDebugContext YbcBeginSubTxn(SubTransactionId id, const char* name);
  [[nodiscard]] static YbcFlushDebugContext YbcEndSubTxn(SubTransactionId id);
  [[nodiscard]] static YbcFlushDebugContext YbcGetTxnSnapshot(uint64_t read_point);
  [[nodiscard]] static YbcFlushDebugContext YbcUnbatchableStmtInSqlFunc(
      uint64_t cmd, const char* func_name);
  [[nodiscard]] static YbcFlushDebugContext YbcUnbatchablePlStmt(
      const char* stmt_name, const char* func_name);
  [[nodiscard]] static YbcFlushDebugContext YbcUnbatchableStmtInPlFunc(
      const char* cmd_name, const char* func_name);
  [[nodiscard]] static YbcFlushDebugContext YbcCopyBatch(
      uint64_t tuples_processed, const char* table_name);
  [[nodiscard]] static YbcFlushDebugContext YbcSwitchToDbCatalogVersionMode(PgOid db_oid);
  [[nodiscard]] static YbcFlushDebugContext YbcEndOfTopLevelStmt();

  [[nodiscard]] static PgFlushDebugContext Make(const YbcFlushDebugContext& ybc_context);
  [[nodiscard]] static PgFlushDebugContext CatalogTablePrefetch();
  [[nodiscard]] static PgFlushDebugContext BufferFull(uint64_t sz_bytes);
  [[nodiscard]] static PgFlushDebugContext CommitTxn(std::optional<PgOid> ddl_db_oid);
  [[nodiscard]] static PgFlushDebugContext ActivateSubTxn(SubTransactionId id);
  [[nodiscard]] static PgFlushDebugContext ChangeTxnSnapshot(uint64_t read_point);
  [[nodiscard]] static PgFlushDebugContext ExportSnapshot(
      PgOid oid, std::optional<uint64_t> read_point);
  [[nodiscard]] static PgFlushDebugContext ImportSnapshot(std::string_view snapshot_id);
  [[nodiscard]] static PgFlushDebugContext EndOperationsBuffering();
  [[nodiscard]] static PgFlushDebugContext EnterDdlTxnMode();
  [[nodiscard]] static PgFlushDebugContext ExitDdlTxnMode();
  [[nodiscard]] static PgFlushDebugContext ExecuteDdl();
  [[nodiscard]] static PgFlushDebugContext AcquireLock(std::string_view lock_id);
  [[nodiscard]] static PgFlushDebugContext ConflictingRead(
      PgOid table_oid, std::string_view table_name);
  [[nodiscard]] static PgFlushDebugContext ConflictingKeyWrite(
      PgOid table_oid, std::string_view table_name, std::string_view key = {});

  [[nodiscard]] const std::string& ToString() const { return value_; }

 private:
  PgFlushDebugContext() {}
  explicit PgFlushDebugContext(std::string&& value) : value_(std::move(value)) {}

  template <class... Args>
  [[nodiscard]] static PgFlushDebugContext DoMake(const char* format, const Args&... args);

  std::string value_;

  DISALLOW_COPY_AND_ASSIGN(PgFlushDebugContext);
};

} // namespace yb::pggate
