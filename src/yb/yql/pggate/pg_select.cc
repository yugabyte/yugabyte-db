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

#include "yb/yql/pggate/pg_select.h"

#include <memory>
#include <utility>

#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb::pggate {

PgSelect::PgSelect(
    PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id,
    bool is_region_local, const PrepareParameters& prepare_params, const PgObjectId& index_id)
    : PgDmlRead(pg_session, table_id, is_region_local, prepare_params, index_id) {}

Status PgSelect::Prepare() {
  // Prepare target and bind descriptors.
  target_ = PgTable(VERIFY_RESULT(LoadTable()));

  // Allocate READ requests to send to DocDB.
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, is_region_local_, pg_session_->metrics().metrics_capture());
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());

  auto doc_op = std::make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

  if (!secondary_index_id_.IsValid()) {
    bind_ = target_;
  } else {
    bind_ = PgTable(nullptr);

    secondary_index_query_ = std::make_unique<PgSelectIndex>(
        pg_session_, secondary_index_id_, is_region_local_, prepare_params_);

    // Prepare the index operation to read ybctids from the index table. There are two different
    // scenarios on how ybctids are requested.
    // - Due to an optimization in DocDB, for colocated tables (both system and user colocated),
    //   index request is sent as a part of the actual read request using protobuf field
    //   "PgsqlReadRequestPB::index_request". For this case, "mutable_index_request" is allocated
    //   here and passed to PgSelectIndex node to fill in with bind-values when necessary.
    //
    // - For regular tables, the index subquery will send separate request to tablet servers collect
    //   batches of ybctids which is then used by 'this' outer select to query actual data.
    std::shared_ptr<LWPgsqlReadRequestPB> index_req;
    if (prepare_params_.querying_colocated_table) {
      // Allocate "index_request" and pass to PgSelectIndex.
      index_req = std::shared_ptr<LWPgsqlReadRequestPB>(
          read_req_, read_req_->mutable_index_request());
    }

    // Prepare subquery. When index_req is not null, it is part of 'this' SELECT request. When it
    // is nullptr, the subquery will create its own doc_op to run a separate read request.
    RETURN_NOT_OK(secondary_index_query_->PrepareSubquery(index_req));
  }

  // Prepare binds for the request.
  PrepareBinds();

  doc_op_ = std::move(doc_op);
  return Status::OK();
}

}  // namespace yb::pggate
