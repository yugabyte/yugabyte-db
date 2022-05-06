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

#include "yb/yql/pggate/pg_session.h"

using namespace std::literals;

namespace yb {
namespace pggate {

PerformFuture::PerformFuture(
    std::future<PerformResult> future, PgSession* session, PgObjectIds relations)
    : future_(std::move(future)), session_(session), relations_(std::move(relations)) {
}

bool PerformFuture::Valid() const {
  return session_ != nullptr;
}

bool PerformFuture::Ready() const {
  return Valid() && future_.wait_for(0ms) == std::future_status::ready;
}

Result<rpc::CallResponsePtr> PerformFuture::Get() {
  auto result = future_.get();
  auto session = session_;
  session_ = nullptr;
  session->TrySetCatalogReadPoint(result.catalog_read_time);
  RETURN_NOT_OK(session->PatchStatus(result.status, relations_));
  return result.response;
}

} // namespace pggate
} // namespace yb
