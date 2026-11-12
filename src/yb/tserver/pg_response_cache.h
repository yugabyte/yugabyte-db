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
  };

  using Setter = std::function<void(Response&&)>;

  // One connection's claim on a database, held while it fills. Counting it is what sends other
  // connections to the older version this node holds. A request that fills takes one for the
  // length of that request; a connection start's prefetch instead keeps one across all of its
  // requests, so the gaps between them -- the backend building its relcache, with nothing
  // outstanding -- do not read as an idle database.
  class FillToken;
  using FillTokenPtr = std::shared_ptr<FillToken>;

  // session_id is what the in-flight count is kept under, and is what a connection start's
  // rounds share a claim by.
  //
  // token is the caller's slot for a connection start's claim, kept across that prefetch's
  // requests and released on its last one. The claim ends when its last holder lets go, so a
  // session that goes away cannot leave its database looking busy.
  Result<Setter> Get(
      LWPgPerformOptionsPB_LWCachingInfoPB* cache_info, CoarseTimePoint deadline,
      const PgResponseCacheWaiterPtr& waiter, uint64_t session_id, FillTokenPtr* token);

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
