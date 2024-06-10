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

#include "yb/client/client_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_ddl.fwd.h"

#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"

namespace yb {
namespace tserver {

class PgTableCache {
 public:
  explicit PgTableCache(std::shared_future<client::YBClient*> client_future);
  ~PgTableCache();

  Status GetInfo(
      const TableId& table_id,
      client::YBTablePtr* table,
      master::GetTableSchemaResponsePB* schema);

  Result<client::YBTablePtr> Get(const TableId& table_id);

  void Invalidate(const TableId& table_id);
  void InvalidateAll(CoarseTimePoint invalidation_time);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace tserver
}  // namespace yb
