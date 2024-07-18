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
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_select_index.h"

#include "yb/util/status_format.h"

#include "yb/yql/pggate/util/pg_doc_data.h"


namespace yb::pggate {

PgSelectIndex::PgSelectIndex(
    PgSession::ScopedRefPtr pg_session, const PgObjectId& index_id,
    bool is_region_local, const PrepareParameters& prepare_params)
    : PgSelect(std::move(pg_session), index_id, is_region_local, prepare_params) {
}

Status PgSelectIndex::PrepareSubquery(std::shared_ptr<LWPgsqlReadRequestPB> read_req) {
  if (!read_req) {
    return PgSelect::Prepare();
  }

  RSTATUS_DCHECK(!prepare_params_.index_only_scan, InvalidArgument, "Unexpected Index scan type");
  // For (system and user) colocated tables, SelectIndex is a part of Select and being sent
  // together with the SELECT protobuf request. A read doc_op and request is not needed in this
  // case.
  RSTATUS_DCHECK(
      prepare_params_.querying_colocated_table, InvalidArgument, "Read request invalid");

  // Setup target and bind descriptor.
  target_ = bind_ = PgTable(VERIFY_RESULT(LoadTable()));

  read_req_ = std::move(read_req);
  read_req_->dup_table_id(table_id_.GetYbTableId()); // TODO(LW_PERFORM)

  // Prepare index key columns.
  PrepareBinds();

  return Status::OK();
}

Result<const std::vector<Slice>*> PgSelectIndex::FetchYbctidBatch() {
  // Keep reading until we get one batch of ybctids or EOF.
  while (!VERIFY_RESULT(GetNextYbctidBatch())) {
    if (!VERIFY_RESULT(FetchDataFromServer())) {
      // Server returns no more rows.
      return nullptr;
    }
  }

  // Got the next batch of ybctids.
  DCHECK(!rowsets_.empty());
  return &rowsets_.front().ybctids();
}

Result<bool> PgSelectIndex::GetNextYbctidBatch() {
  for (auto rowset_iter = rowsets_.begin(); rowset_iter != rowsets_.end();) {
    if (rowset_iter->is_eof()) {
      rowset_iter = rowsets_.erase(rowset_iter);
    } else {
      // Write all found rows to ybctid array.
      RETURN_NOT_OK(rowset_iter->ProcessSystemColumns());
      return true;
    }
  }

  return false;
}

}  // namespace yb::pggate
