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

#include "yb/yql/pggate/pg_dml.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DML WRITE - Insert, Update, Delete.
//--------------------------------------------------------------------------------------------------

class PgDmlWrite : public PgDml {
 public:
  // Abstract class without constructors.
  virtual ~PgDmlWrite();

  // Prepare write operations.
  virtual Status Prepare();

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

  int32_t GetRowsAffectedCount() {
    return rows_affected_count_;
  }

  Status SetWriteTime(const HybridTime& write_time);

  Status BindRow(uint64_t ybctid, YBCBindColumn* columns, int count);

  bool packed() const {
    return packed_;
  }

 protected:
  // Constructor.
  PgDmlWrite(PgSession::ScopedRefPtr pg_session,
             const PgObjectId& table_id,
             bool is_region_local,
             YBCPgTransactionSetting transaction_setting,
             bool packed = false);

  // Allocate write request.
  void AllocWriteRequest();

  // Allocate column expression.
  Result<LWPgsqlExpressionPB*> AllocColumnBindPB(PgColumn* col, PgExpr* expr) override;

  // Allocate target for selected or returned expressions.
  LWPgsqlExpressionPB *AllocTargetPB() override;

  // Allocate protobuf for a qual in the write request's where_clauses list.
  LWPgsqlExpressionPB *AllocQualPB() override;

  // Allocate protobuf for a column reference in the write request's col_refs list.
  LWPgsqlColRefPB *AllocColRefPB() override;

  // Clear the write request's col_refs list.
  void ClearColRefPBs() override;

  // Allocate column expression.
  LWPgsqlExpressionPB* AllocColumnAssignPB(PgColumn *col) override;

  // Protobuf code.
  std::shared_ptr<LWPgsqlWriteRequestPB> write_req_;

  const YBCPgTransactionSetting transaction_setting_;

  int32_t rows_affected_count_ = 0;

  bool packed_;

 private:
  Status DeleteEmptyPrimaryBinds();

  virtual PgsqlWriteRequestPB::PgsqlStmtType stmt_type() const = 0;

  Status BindPackedRow(uint64_t ybctid, YBCBindColumn* columns, int count);
};

}  // namespace pggate
}  // namespace yb
