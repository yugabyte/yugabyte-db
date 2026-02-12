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

#include "yb/yql/pggate/pg_dml.h"

namespace yb::pggate {

class PgDmlWrite : public PgDml {
 public:
  // Setup internal structures for binding values during prepare.
  void PrepareColumns();

  // force_non_bufferable flag indicates this operation should not be buffered.
  Status Exec(ForceNonBufferable force_non_bufferable = ForceNonBufferable::kFalse);

  void SetIsSystemCatalogChange() {
    write_req_->set_is_ysql_catalog_change_using_protobuf(true);
  }

  void SetCatalogCacheVersion(std::optional<PgOid> db_oid,
                              uint64_t catalog_cache_version) override {
    DoSetCatalogCacheVersion(write_req_.get(), db_oid, catalog_cache_version);
  }

  void SetTablespaceOid(uint32_t tablespace_oid) override {
    DoSetTablespaceOid(write_req_.get(), tablespace_oid);
  }

  [[nodiscard]] int32_t GetRowsAffectedCount() { return rows_affected_count_; }

  Status SetWriteTime(const HybridTime& write_time);

  // Set SQL query comment for CDC tracking
  void SetQueryComment(const std::string& comment) {
    query_comment_ = comment;
  }

  Status BindRow(uint64_t ybctid, YbcBindColumn* columns, int count);

  [[nodiscard]] bool packed() const { return packed_; }

 protected:
  PgDmlWrite(
      const PgSession::ScopedRefPtr& pg_session, YbcPgTransactionSetting transaction_setting,
      bool packed = false);

  // Prepare write operations.
  Status Prepare(const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info);

  // Allocate column expression.
  Result<LWPgsqlExpressionPB*> AllocColumnBindPB(PgColumn* col, PgExpr* expr) override;

  // Allocate target for selected or returned expressions.
  LWPgsqlExpressionPB* AllocTargetPB() override;

  // Allocate column expression.
  LWPgsqlExpressionPB* AllocColumnAssignPB(PgColumn* col) override;

  // Protobuf code.
  std::shared_ptr<LWPgsqlWriteRequestPB> write_req_;

  const YbcPgTransactionSetting transaction_setting_;

  int32_t rows_affected_count_ = 0;

  bool packed_;

  std::string query_comment_;

 private:
  [[nodiscard]] ArenaList<LWPgsqlColRefPB>& ColRefPBs() override;

  Status DeleteEmptyPrimaryBinds();

  virtual PgsqlWriteRequestPB::PgsqlStmtType stmt_type() const = 0;

  Status BindPackedRow(uint64_t ybctid, YbcBindColumn* columns, int count);
};

}  // namespace yb::pggate
