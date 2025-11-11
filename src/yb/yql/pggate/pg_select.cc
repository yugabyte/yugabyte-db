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
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_select.h"

#include <memory>
#include <utility>

#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb::pggate {

PgSelect::PgSelect(const PgSession::ScopedRefPtr& pg_session)
    : BaseType(pg_session) {}

Status PgSelect::Prepare(
    const PgObjectId& table_id, const YbcPgTableLocalityInfo& locality_info,
    const std::optional<IndexQueryInfo>& index_info) {
  // Prepare target and bind descriptors.
  target_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id)));

  // Allocate READ requests to send to DocDB.
  auto read_op = ArenaMakeShared<PgsqlReadOp>(
      arena_ptr(), &arena(), *target_, locality_info, pg_session_->metrics().metrics_capture());
  read_req_ = std::shared_ptr<LWPgsqlReadRequestPB>(read_op, &read_op->read_request());

  auto doc_op = std::make_shared<PgDocReadOp>(pg_session_, &target_, std::move(read_op));

  if (!index_info) {
    bind_ = target_;
  } else {
    DCHECK(index_info->id.IsValid());
    bind_ = PgTable(nullptr);

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
    if (index_info->is_embedded) {
      index_req = std::shared_ptr<LWPgsqlReadRequestPB>(
          read_req_, read_req_->mutable_index_request());
    }

    SetSecondaryIndex(VERIFY_RESULT(PgSelectIndex::Make(
        pg_session_, index_info->id, locality_info, std::move(index_req))));
  }

  // Prepare binds for the request.
  PrepareBinds();

  doc_op_ = std::move(doc_op);
  return Status::OK();
}

Result<std::unique_ptr<PgSelect>> PgSelect::Make(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id,
    const YbcPgTableLocalityInfo& locality_info, const std::optional<IndexQueryInfo>& index_info) {
  std::unique_ptr<PgSelect> result{new PgSelect{pg_session}};
  RETURN_NOT_OK(result->Prepare(table_id, locality_info, index_info));
  return result;
}

}  // namespace yb::pggate
