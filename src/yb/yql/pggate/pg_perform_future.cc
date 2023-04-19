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

#include "yb/util/flags.h"
#include "yb/yql/pggate/pg_session.h"

DEFINE_test_flag(bool, use_monotime_for_rpc_wait_time, false,
                 "Flag to enable use of MonoTime::Now() instead of CoarseMonoClock::Now() "
                 "in order to avoid 0 timings in the tests.");

using namespace std::literals;

namespace yb {
namespace pggate {
namespace {

Status PatchStatus(const Status& status, const PgObjectIds& relations) {
  if (PgsqlRequestStatus(status) != PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR) {
    return status;
  }
  auto op_index = OpIndex::ValueFromStatus(status);
  if (op_index && *op_index < relations.size()) {
    return STATUS(AlreadyPresent, PgsqlError(YBPgErrorCode::YB_PG_UNIQUE_VIOLATION))
        .CloneAndAddErrorCode(RelationOid(relations[*op_index].object_oid));
  }
  return status;
}

} // namespace

PerformFuture::PerformFuture(
    std::future<PerformResult> future, PgSession* session, PgObjectIds&& relations)
    : future_(std::move(future)), session_(session), relations_(std::move(relations)) {
}

PerformFuture::~PerformFuture() {
  if (Valid()) {
    // In case object is valid nobody got the result from it.
    // This is possible in case of error handling. Transaction will be rolled back in this case.
    // We have to be sure that all requests are completed before performing rollback.
    future_.wait();
  }
}

bool PerformFuture::Valid() const {
  return future_.valid();
}

bool PerformFuture::Ready() const {
  return Valid() && future_.wait_for(0ms) == std::future_status::ready;
}

Result<PerformFuture::Data> PerformFuture::Get() {
  // Make sure Valid method will return false before thread will be blocked on call future.get()
  // This requirement is not necessary after fixing of #12884.
  auto future = std::move(future_);
  auto result = future.get();
  RETURN_NOT_OK(PatchStatus(result.status, relations_));
  session_->TrySetCatalogReadPoint(result.catalog_read_time);
  return Data{
      .response = std::move(result.response),
      .used_in_txn_limit = result.used_in_txn_limit};
}

Result<PerformFuture::Data> PerformFuture::Get(MonoDelta* wait_time) {
  if (PREDICT_FALSE(FLAGS_TEST_use_monotime_for_rpc_wait_time)) {
    auto start_time = MonoTime::Now();
    auto response = Get();
    *wait_time += MonoTime::Now() - start_time;
    return response;
  }

  auto start_time = CoarseMonoClock::Now();
  auto response = Get();
  *wait_time += CoarseMonoClock::Now() - start_time;
  return response;
}

} // namespace pggate
} // namespace yb
