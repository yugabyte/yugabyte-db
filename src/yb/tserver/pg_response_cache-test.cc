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

#include "yb/tserver/pg_response_cache.h"

#include <memory>
#include <string>

#include "yb/rpc/lightweight_message.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

using namespace std::literals;

METRIC_DECLARE_entity(server);

namespace yb::tserver {
namespace {

constexpr PgResponseCache::KeyGroup kKeyGroup = 16384;
constexpr auto kCatalogVersion = 42ULL;

// Records whether the cache ever handed this waiter a response.
class CountingWaiter : public PgResponseCacheWaiter {
 public:
  void Apply(const PgResponseCache::Response& response) override { ++applied_; }

  [[nodiscard]] size_t applied() const { return applied_; }

 private:
  size_t applied_ = 0;
};

class PgResponseCacheTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    mem_tracker_ = MemTracker::CreateTracker("pg_response_cache-test");
    metric_entity_ = METRIC_ENTITY_server.Instantiate(&registry_, "pg_response_cache-test");
    cache_ = std::make_unique<PgResponseCache>(mem_tracker_, metric_entity_.get());
  }

  // A request for the one key every caller in these tests contends on.
  LWPgPerformOptionsPB::LWCachingInfoPB& MakeCachingInfo(ThreadSafeArena& arena) {
    auto& info = *arena.NewArenaObject<LWPgPerformOptionsPB::LWCachingInfoPB>();
    info.set_key_group(kKeyGroup);
    info.dup_key_value("the-only-key"sv);
    info.set_catalog_version(kCatalogVersion);
    return info;
  }

  // A minimal successful fill: no operations, so no rows data to hand back.
  PgResponseCache::Response MakeResponse() {
    response_msg_ = rpc::MakeSharedMessage<PgPerformResponseMsg>();
    return PgResponseCache::Response(*response_msg_, {});
  }

  MetricRegistry registry_;
  std::shared_ptr<PgPerformResponseMsg> response_msg_;
  std::shared_ptr<MemTracker> mem_tracker_;
  scoped_refptr<MetricEntity> metric_entity_;
  std::unique_ptr<PgResponseCache> cache_;
};

// A caller that claims an entry for filling can go away without storing a response:
// pg_client_session.cc returns early between taking the setter and flushing, on SetupSession or
// PrepareOperations. Without the abandon guard on the setter, nothing completes that entry, so
// everything queued on it waits out its own deadline, and is destroyed un-responded if the
// entry's readiness deadline passes first. SetAbandoned completes it as a failure instead, which
// releases them at once.
TEST_F(PgResponseCacheTest, AbandonedFillReleasesItsWaiters) {
  ThreadSafeArena arena;
  auto& filler_info = MakeCachingInfo(arena);
  auto& waiter_info = MakeCachingInfo(arena);
  auto& next_info = MakeCachingInfo(arena);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto filler_waiter = std::make_shared<CountingWaiter>();
  auto setter = ASSERT_RESULT(cache_->Get(&filler_info, deadline, filler_waiter));
  ASSERT_TRUE(setter) << "the first caller on an empty entry must be asked to fill it";

  // A second caller finds the fill under way and is queued behind it.
  auto queued_waiter = std::make_shared<CountingWaiter>();
  auto queued_setter = ASSERT_RESULT(cache_->Get(&waiter_info, deadline, queued_waiter));
  ASSERT_FALSE(queued_setter) << "the second caller must not also be asked to fill";
  ASSERT_EQ(queued_waiter->applied(), 0) << "nothing to apply while the fill is under way";

  // The filler abandons the fill: the setter is destroyed without ever being invoked.
  setter = {};
  ASSERT_EQ(queued_waiter->applied(), 1)
      << "a caller queued behind an abandoned fill was never told the fill is not coming, so it "
      << "waits out its own deadline holding a backend, and is destroyed un-responded if the "
      << "entry is replaced first";

  // The entry is left in the state that means "do not serve this", so the next caller rebuilds
  // it and fills afresh rather than inheriting the failure.
  auto next_waiter = std::make_shared<CountingWaiter>();
  auto next_setter = ASSERT_RESULT(cache_->Get(&next_info, deadline, next_waiter));
  ASSERT_TRUE(next_setter) << "an entry completed as a failure must not be served or reused";
}

// A fill can outlive the readiness deadline of the entry it is filling. The filler's setter holds
// that entry, so the deadline passing does not take it away: GetEntryData() hands later callers a
// fresh entry to fill, while the older one lives on and still serves everyone queued on it when
// the late fill lands. Its response is never cached, since the cache now keeps the fresh entry.
TEST_F(PgResponseCacheTest, AFillOutlivingItsDeadlineStillServesItsWaiters) {
  ThreadSafeArena arena;
  auto& filler_info = MakeCachingInfo(arena);
  auto& waiter_info = MakeCachingInfo(arena);
  auto& later_info = MakeCachingInfo(arena);
  // Short enough to wait out, long enough that the caller below queues rather than rebuilding.
  constexpr auto kReadiness = 2s;

  auto filler_waiter = std::make_shared<CountingWaiter>();
  auto setter = ASSERT_RESULT(
      cache_->Get(&filler_info, CoarseMonoClock::Now() + kReadiness, filler_waiter));
  ASSERT_TRUE(setter) << "the first caller on an empty entry must be asked to fill it";

  auto queued_waiter = std::make_shared<CountingWaiter>();
  auto queued_setter = ASSERT_RESULT(
      cache_->Get(&waiter_info, CoarseMonoClock::Now() + kReadiness, queued_waiter));
  ASSERT_FALSE(queued_setter) << "the second caller must queue behind the fill under way";

  // Past the deadline the entry is no longer handed out, so the next caller is given a fresh one
  // to fill rather than queued behind a fill it would have no way to wait for.
  SleepFor(MonoDelta::FromMilliseconds(2500));
  auto later_waiter = std::make_shared<CountingWaiter>();
  auto later_setter = ASSERT_RESULT(
      cache_->Get(&later_info, CoarseMonoClock::Now() + 60s, later_waiter));
  ASSERT_TRUE(later_setter) << "once the readiness deadline has passed the entry must be rebuilt";

  // The first fill lands late. Its setter still holds the older entry, so the caller queued on
  // that entry is served even though the cache has moved on to the fresh one.
  setter(MakeResponse());
  ASSERT_EQ(queued_waiter->applied(), 1)
      << "a fill that outlived its entry's readiness deadline left the caller queued on it "
      << "unserved; the setter holds that entry, so the fill must still reach its waiters";
}

} // namespace
} // namespace yb::tserver
