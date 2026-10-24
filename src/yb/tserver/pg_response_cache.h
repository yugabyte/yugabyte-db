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

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <utility>

#include "yb/client/client_fwd.h"

#include "yb/gutil/macros.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client_session.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"

#include "yb/util/monotime.h"

namespace yb {

class MemTracker;
class MetricEntity;

namespace tserver {

class PgResponseCacheWaiter;
using PgResponseCacheWaiterPtr = std::shared_ptr<PgResponseCacheWaiter>;

class PgResponseCache {
 public:
  using KeyGroup = decltype(std::declval<PgPerformOptionsPB::CachingInfoPB>().key_group());

  // Invoked once a connection start's prefetch is wholly cached: the request that carried
  // completes_connection_prefetch succeeded and no operation in it was still paging. Not invoked
  // for the rounds before it, for any other caller's fill, or when a cached response is served.
  // The filter is here rather than in the observer so that a caller may read the callback as this
  // node now holding a whole prefetch at the version it is given.
  using FillObserver = std::function<void(KeyGroup, uint64_t)>;

  PgResponseCache(
      const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity,
      FillObserver fill_observer = {});
  ~PgResponseCache();

  struct Response {
    Response(const PgPerformResponseMsg& response_, std::vector<RefCntSlice> rows_data_);

    std::shared_ptr<LWPgPerformResponsePB> response;
    std::vector<RefCntSlice> rows_data;
    size_t response_memory_footprint = 0;
  };

  using Setter = std::function<void(Response&&)>;

  Result<Setter> Get(
      LWPgPerformOptionsPB_LWCachingInfoPB* cache_info, CoarseTimePoint deadline,
      const PgResponseCacheWaiterPtr& waiter);

  struct DisablerType;
  using Disabler = std::shared_ptr<DisablerType>;

  [[nodiscard]] Disabler Disable(KeyGroup key_group);

 private:
  class Impl;

  std::shared_ptr<Impl> impl_;

  DISALLOW_COPY_AND_ASSIGN(PgResponseCache);
};

class PgResponseCacheWaiter {
 public:
  virtual ~PgResponseCacheWaiter() = default;

  virtual void Apply(const PgResponseCache::Response& response) = 0;
};

}  // namespace tserver
}  // namespace yb
