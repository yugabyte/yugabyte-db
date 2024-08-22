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

#include "yb/yql/pggate/pg_perform_future.h"

#include <chrono>
#include <utility>

#include "yb/common/pgsql_error.h"

#include "yb/yql/pggate/pg_session.h"

namespace yb::pggate {
namespace {

Status PatchStatus(const Status& status, PgSession& session, const PgObjectIds& relations) {
  if (PgsqlRequestStatus(status) != PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR) {
    return status;
  }
  auto op_index = OpIndex::ValueFromStatus(status);
  if (op_index && *op_index < relations.size()) {
    auto table = VERIFY_RESULT(session.LoadTable(relations[*op_index]));
    return STATUS(AlreadyPresent, PgsqlError(YBPgErrorCode::YB_PG_UNIQUE_VIOLATION))
        .CloneAndAddErrorCode(RelationOid(table->pg_table_id()));
  }
  return status;
}

} // namespace

PerformFuture::PerformFuture(PerformResultFuture&& future, PgObjectIds&& relations)
    : future_(std::move(future)), relations_(std::move(relations)) {
}

PerformFuture::~PerformFuture() {
  if (Valid()) {
    // In case object is valid nobody got the result from it.
    // This is possible in case of error handling. Transaction will be rolled back in this case.
    // We have to be sure that all requests are completed before performing rollback.
    Wait(future_);
  }
}

bool PerformFuture::Valid() const {
  return pggate::Valid(future_);
}

bool PerformFuture::Ready() const {
  return Valid() && pggate::Ready(future_);
}

Result<PerformFuture::Data> PerformFuture::Get(PgSession& session) {
  // Make sure Valid method will return false before thread will be blocked on call future.get()
  // This requirement is not necessary after fixing of #12884.
  auto future = std::move(future_);
  auto result = pggate::Get(&future);
  RETURN_NOT_OK(PatchStatus(result.status, session, relations_));
  session.TrySetCatalogReadPoint(result.catalog_read_time);
  return Data{
      .response = std::move(result.response),
      .used_in_txn_limit = result.used_in_txn_limit};
}

} // namespace yb::pggate
