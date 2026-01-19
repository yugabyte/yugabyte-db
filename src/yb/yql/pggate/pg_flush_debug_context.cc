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

#include "yb/yql/pggate/pg_flush_debug_context.h"

#include <utility>

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/slice.h"

#include "yb/yql/pggate/util/ybc_guc.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {
namespace {

class AsHexPrinter {
 public:
  explicit AsHexPrinter(std::string_view value) : value_(value) {}

  std::string ToString() const { return Slice(value_).ToDebugHexString(); }

 private:
  std::string_view value_;
};

YB_DEFINE_TYPED_ENUM(Reason, uint8_t,
  (kBeginSubTxn)
  (kEndSubTxn)
  (kGetTxnSnapshot)
  (kUnbatchableSqlStmtInSqlFunc)
  (kUnbatchablePlStmt)
  (kUnbatchableSqlStmtInPlFunc)
  (kCopyBatch)
  (kSwitchToDbCatalogVersionMode)
  (kEndOfTopLevelStmt));

struct YbcContextArgs {
  uint64_t uintarg = 0;
  PgOid oidarg = kInvalidOid;
  const char* strarg1 = nullptr;
  const char* strarg2 = nullptr;
};

auto MakeYbc(Reason reason, const YbcContextArgs& args = {}) {
  return YbcFlushDebugContext{
      .reason = std::to_underlying(reason),
      .uintarg = args.uintarg,
      .oidarg = args.oidarg,
      .strarg1 = args.strarg1,
      .strarg2 = args.strarg2};
}
} // namespace

YbcFlushDebugContext PgFlushDebugContext::YbcBeginSubTxn(
    SubTransactionId id, const char* name) {
  return MakeYbc(Reason::kBeginSubTxn, {.uintarg = id, .strarg1 = name});
}

YbcFlushDebugContext PgFlushDebugContext::YbcEndSubTxn(SubTransactionId id) {
  return MakeYbc(Reason::kEndSubTxn, {.uintarg = id});
}

YbcFlushDebugContext PgFlushDebugContext::YbcGetTxnSnapshot(uint64_t read_point) {
  return MakeYbc(Reason::kGetTxnSnapshot, {.uintarg = read_point});
}

YbcFlushDebugContext PgFlushDebugContext::YbcUnbatchableStmtInSqlFunc(
    uint64_t cmd, const char* func_name) {
  return MakeYbc(Reason::kUnbatchableSqlStmtInSqlFunc, {.uintarg = cmd, .strarg1 = func_name});
}

YbcFlushDebugContext PgFlushDebugContext::YbcUnbatchablePlStmt(
    const char* stmt_name, const char* func_name) {
  return MakeYbc(Reason::kUnbatchablePlStmt, {.strarg1 = stmt_name, .strarg2 = func_name});
}

YbcFlushDebugContext PgFlushDebugContext::YbcUnbatchableStmtInPlFunc(
    const char* cmd_name, const char* func_name) {
  return MakeYbc(Reason::kUnbatchableSqlStmtInPlFunc, {.strarg1 = cmd_name, .strarg2 = func_name});
}

YbcFlushDebugContext PgFlushDebugContext::YbcCopyBatch(
    uint64_t tuples_processed, const char* table_name) {
  return MakeYbc(Reason::kCopyBatch, {.uintarg = tuples_processed, .strarg1 = table_name});
}

YbcFlushDebugContext PgFlushDebugContext::YbcSwitchToDbCatalogVersionMode(PgOid db_oid) {
  return MakeYbc(Reason::kSwitchToDbCatalogVersionMode, {.oidarg = db_oid});
}

YbcFlushDebugContext PgFlushDebugContext::YbcEndOfTopLevelStmt() {
  return MakeYbc(Reason::kEndOfTopLevelStmt);
}

PgFlushDebugContext PgFlushDebugContext::Make(const YbcFlushDebugContext& ctx) {
  switch (static_cast<Reason>(ctx.reason)) {
    case Reason::kBeginSubTxn:
      return DoMake(
          "due to begin of new subtransaction for $0 (current SubTransactionId: $1)",
          ctx.strarg1 ? ctx.strarg1 : "unnamed txn", ctx.uintarg);
    case Reason::kEndSubTxn:
      return DoMake("due to end of subtransaction with SubTransactionId $0", ctx.uintarg);
    case Reason::kGetTxnSnapshot:
      return DoMake(
          "before getting a new transaction snapshot (current read point: $0) in "
          "Read Committed isolation", ctx.uintarg);
    case Reason::kUnbatchableSqlStmtInSqlFunc:
      return DoMake(
          "before executing non-DML statement $0 (see CmdType in nodes.h) in SQL function '$1'",
          ctx.uintarg, ctx.strarg1);
    case Reason::kUnbatchablePlStmt:
      return DoMake(
          "before executing PL statement of type '$0' in function '$1'", ctx.strarg1, ctx.strarg2);
    case Reason::kUnbatchableSqlStmtInPlFunc:
      return DoMake(
          "before executing SQL statement with command tag '$0' in PL function '$1'",
          ctx.strarg1, ctx.strarg2);
    case Reason::kCopyBatch:
      return DoMake(
          "after copying batch of tuples for table '$0' (total tuples processed: $1)",
          ctx.strarg1, ctx.uintarg);
    case Reason::kSwitchToDbCatalogVersionMode:
      return DoMake(
          "switch from global to per-database catalog version mode (current DB OID: $0)",
          ctx.oidarg);
    case Reason::kEndOfTopLevelStmt:
      return DoMake("at the end of top-level statement");
  }
  DCHECK(false);
  return DoMake("for unknown reason");
}

PgFlushDebugContext PgFlushDebugContext::CatalogTablePrefetch() {
  return DoMake("before prefetching catalog tables");
}

PgFlushDebugContext PgFlushDebugContext::ConflictingRead(
    PgOid table_oid, std::string_view table_name) {
  return DoMake(
      "before performing a non-bufferable read operation on table '$0' with OID $1",
      table_name, table_oid);
}

PgFlushDebugContext PgFlushDebugContext::BufferFull(uint64_t sz_bytes) {
  return DoMake("due to buffer being full (ops size: $0 bytes)", sz_bytes);
}

PgFlushDebugContext PgFlushDebugContext::CommitTxn(std::optional<PgOid> ddl_db_oid) {
  return DoMake(
      ddl_db_oid
          ? "due to commit of plain transaction (contains DDL ops on database with OID $0)"
          : "due to commit of plain transaction", ddl_db_oid);
}

PgFlushDebugContext PgFlushDebugContext::ActivateSubTxn(SubTransactionId id) {
  return DoMake("due to activation of subtransaction with SubTransactionId $0", id);
}

PgFlushDebugContext PgFlushDebugContext::ChangeTxnSnapshot(uint64_t read_point) {
  return DoMake("before restoring transaction snapshot corresponding to read point $0", read_point);
}

PgFlushDebugContext PgFlushDebugContext::ExportSnapshot(
    PgOid oid, std::optional<uint64_t> read_point) {
  return DoMake(
      read_point
          ? "before exporting transaction snapshot (read point: $1) on database with OID $0"
          : "before exporting transaction snapshot on database with OID $0",
      oid, read_point);
}

PgFlushDebugContext PgFlushDebugContext::ImportSnapshot(std::string_view snapshot_id) {
  return DoMake("before importing snapshot '$0'", snapshot_id);
}

PgFlushDebugContext PgFlushDebugContext::EndOperationsBuffering() {
  return DoMake("at the end of operations buffering");
}

PgFlushDebugContext PgFlushDebugContext::EnterDdlTxnMode() {
  return DoMake("due to entering DDL mode");
}

PgFlushDebugContext PgFlushDebugContext::ExitDdlTxnMode() {
  return DoMake("due to exiting DDL mode");
}

PgFlushDebugContext PgFlushDebugContext::ExecuteDdl() {
  return DoMake("before executing DDL statement");
}

PgFlushDebugContext PgFlushDebugContext::AcquireLock(std::string_view lock_id) {
  return DoMake("before acquiring lock on $0", lock_id);
}

PgFlushDebugContext PgFlushDebugContext::ConflictingKeyWrite(
      PgOid table_oid, std::string_view table_name, std::string_view key) {
  return DoMake(
      key.empty()
          ? "before enqueueing a conflicting write operation on table '$0' with OID $1"
          : "before enqueueing a conflicting write operation on table '$0' with OID $1 (key: $2)",
      table_oid, table_name, AsHexPrinter{key});
}

template <class... Args>
PgFlushDebugContext PgFlushDebugContext::DoMake(const char* format, const Args&... args) {
  if (PREDICT_TRUE(!yb_debug_log_docdb_requests)) {
    return {};
  }
  return PgFlushDebugContext{
      std::invoke<std::string(*)(const char*, const Args&...)>(&Format, format, args...)};
}

} // namespace yb::pggate
