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

#ifndef YB_YQL_PGGATE_PG_CLIENT_H
#define YB_YQL_PGGATE_PG_CLIENT_H

#include <memory>

#include "yb/rpc/proxy.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/yql/pggate/pg_env.h"

namespace yb {
namespace pggate {

class PgClient {
 public:
  PgClient();
  ~PgClient();

  void Start(rpc::ProxyCache* proxy_cache,
             const tserver::TServerSharedObject& tserver_shared_object);
  void Shutdown();

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_CLIENT_H
