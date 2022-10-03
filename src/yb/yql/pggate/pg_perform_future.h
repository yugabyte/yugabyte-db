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

#ifndef YB_YQL_PGGATE_PG_PERFORM_FUTURE_H_
#define YB_YQL_PGGATE_PG_PERFORM_FUTURE_H_

#include <future>

#include "yb/common/common_fwd.h"

#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_client.h"

namespace yb {
namespace pggate {

class PgSession;

class PerformFuture {
 public:
  PerformFuture() = default;
  PerformFuture(std::future<PerformResult> future, PgSession* session, PgObjectIds&& relations);
  PerformFuture(PerformFuture&&) = default;
  PerformFuture& operator=(PerformFuture&&) = default;
  ~PerformFuture();

  bool Valid() const;
  bool Ready() const;
  Result<rpc::CallResponsePtr> Get();

 private:
  std::future<PerformResult> future_;
  PgSession* session_ = nullptr;
  PgObjectIds relations_;
};

} // namespace pggate
} // namespace yb

#endif // YB_YQL_PGGATE_PG_PERFORM_FUTURE_H_
