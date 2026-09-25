// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the
// License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
// either express or implied. See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/tserver/pg_response_cache.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "yb/tserver/pg_client.messages.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"

METRIC_DECLARE_entity(server);

namespace yb::tserver {
namespace {

using namespace std::chrono_literals;

class TestResponseWaiter : public PgResponseCacheWaiter {
 public:
  void Apply(const PgResponseCache::Response&) override {
    ++apply_count_;
  }

  size_t apply_count() const {
    return apply_count_;
  }

 private:
  size_t apply_count_ = 0;
};

// Entry memory (key bytes and the copied response) must be reflected in the cache's MemTracker
// and released when the entry is destroyed.
TEST(PgResponseCacheTest, EntryMemoryIsTracked) {
  MetricRegistry metric_registry;
  auto metric_entity = METRIC_ENTITY_server.Instantiate(&metric_registry, "response-cache-test");
  auto parent_tracker = MemTracker::CreateTracker(-1, "response-cache-test");

  constexpr size_t kKeySize = 1_MB;
  const std::string key(kKeySize, 'k');
  auto waiter = std::make_shared<TestResponseWaiter>();
  std::shared_ptr<MemTracker> cache_tracker;

  {
    PgResponseCache cache(parent_tracker, metric_entity.get());

    ThreadSafeArena first_arena;
    LWPgPerformOptionsPB_LWCachingInfoPB first_cache_info(&first_arena);
    first_cache_info.set_key_group(1);
    first_cache_info.dup_key_value(Slice(key));
    auto setter = ASSERT_RESULT(cache.Get(
        &first_cache_info, CoarseMonoClock::Now() + 1min, waiter));
    ASSERT_TRUE(setter);

    LWPgPerformResponsePB response(&first_arena);
    setter(PgResponseCache::Response(response, {}));

    cache_tracker = parent_tracker->FindChild("PgResponseCache");
    ASSERT_TRUE(cache_tracker);
    ASSERT_GE(cache_tracker->consumption(), static_cast<int64_t>(kKeySize));

    ThreadSafeArena second_arena;
    LWPgPerformOptionsPB_LWCachingInfoPB second_cache_info(&second_arena);
    second_cache_info.set_key_group(1);
    second_cache_info.dup_key_value(Slice(key));
    auto second_setter = ASSERT_RESULT(cache.Get(
        &second_cache_info, CoarseMonoClock::Now() + 1min, waiter));

    ASSERT_FALSE(second_setter);
    ASSERT_EQ(waiter->apply_count(), 1);
  }

  ASSERT_EQ(cache_tracker->consumption(), 0);
}

}  // namespace
}  // namespace yb::tserver
