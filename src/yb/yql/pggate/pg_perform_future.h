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

#pragma once

#include <future>

#include "yb/common/common_fwd.h"
#include "yb/common/hybrid_time.h"

#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_client.h"

namespace yb {
namespace pggate {

class PgSession;

class PerformFuture {
 public:
  struct Data {
    rpc::CallResponsePtr response;
    HybridTime used_in_txn_limit;
  };

  PerformFuture() = default;
  PerformFuture(PerformResultFuture future, PgSession* session, PgObjectIds&& relations);
  PerformFuture(PerformFuture&&) = default;
  PerformFuture& operator=(PerformFuture&&) = default;
  ~PerformFuture();

  bool Valid() const;
  bool Ready() const;
  Result<Data> Get();

 private:
  PerformResultFuture future_;
  PgSession* session_ = nullptr;
  PgObjectIds relations_;
};

} // namespace pggate
} // namespace yb
