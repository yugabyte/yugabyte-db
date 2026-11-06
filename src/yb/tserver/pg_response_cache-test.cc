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

#include "yb/util/countdown_latch.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

using namespace std::literals;

METRIC_DECLARE_entity(server);

namespace yb::tserver {
namespace {

constexpr PgResponseCache::KeyGroup kKeyGroup = 16384;
constexpr auto kCatalogVersion = 42ULL;
constexpr uint64_t kSessionId = 7;
constexpr uint64_t kOtherSessionId = 8;

YB_STRONGLY_TYPED_BOOL(SendAway);
YB_STRONGLY_TYPED_BOOL(PartOfConnectionPrefetch);

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

  // A request for a key in the group these tests contend on. Rounds of one prefetch differ only
  // by key, so a distinct key stands for a distinct round.
  LWPgPerformOptionsPB::LWCachingInfoPB& MakeCachingInfo(
      ThreadSafeArena& arena, std::string_view key = "the-only-key"sv,
      SendAway send_away = SendAway::kFalse,
      PartOfConnectionPrefetch part_of_connection_prefetch = PartOfConnectionPrefetch::kFalse) {
    auto& info = *arena.NewArenaObject<LWPgPerformOptionsPB::LWCachingInfoPB>();
    info.set_key_group(kKeyGroup);
    info.dup_key_value(key);
    info.set_catalog_version(kCatalogVersion);
    info.set_send_away_if_fill_in_flight(send_away);
    info.set_part_of_connection_prefetch(part_of_connection_prefetch);
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
  auto setter = ASSERT_RESULT(
      cache_->Get(&filler_info, deadline, filler_waiter, kSessionId, nullptr));
  ASSERT_TRUE(setter) << "the first caller on an empty entry must be asked to fill it";

  // A second caller finds the fill under way and is queued behind it.
  auto queued_waiter = std::make_shared<CountingWaiter>();
  auto queued_setter = ASSERT_RESULT(
      cache_->Get(&waiter_info, deadline, queued_waiter, kSessionId, nullptr));
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
  auto next_setter = ASSERT_RESULT(
      cache_->Get(&next_info, deadline, next_waiter, kSessionId, nullptr));
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
      cache_->Get(&filler_info, CoarseMonoClock::Now() + kReadiness, filler_waiter, kSessionId,
                  nullptr));
  ASSERT_TRUE(setter) << "the first caller on an empty entry must be asked to fill it";

  auto queued_waiter = std::make_shared<CountingWaiter>();
  auto queued_setter = ASSERT_RESULT(
      cache_->Get(&waiter_info, CoarseMonoClock::Now() + kReadiness, queued_waiter, kSessionId,
                  nullptr));
  ASSERT_FALSE(queued_setter) << "the second caller must queue behind the fill under way";

  // Past the deadline the entry is no longer handed out, so the next caller is given a fresh one
  // to fill rather than queued behind a fill it would have no way to wait for.
  SleepFor(MonoDelta::FromMilliseconds(2500));
  auto later_waiter = std::make_shared<CountingWaiter>();
  auto later_setter = ASSERT_RESULT(
      cache_->Get(&later_info, CoarseMonoClock::Now() + 60s, later_waiter, kSessionId, nullptr));
  ASSERT_TRUE(later_setter) << "once the readiness deadline has passed the entry must be rebuilt";

  // The first fill lands late. Its setter still holds the older entry, so the caller queued on
  // that entry is served even though the cache has moved on to the fresh one.
  setter(MakeResponse());
  ASSERT_EQ(queued_waiter->applied(), 1)
      << "a fill that outlived its entry's readiness deadline left the caller queued on it "
      << "unserved; the setter holds that entry, so the fill must still reach its waiters";
}

// The group check skips the asking session's own counts. Without that a session would be sent
// away from a fill of its own: a round's token outlives the response that lets the next round be
// sent, so two of one session's rounds are counted at once, and the second would find the first
// and abandon the prefetch part-way -- when a prefetch that finishes is what records a version
// for the next connection.
TEST_F(PgResponseCacheTest, AConnectionIsNotSentAwayFromAFillOfItsOwn) {
  ThreadSafeArena arena;
  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kTrue);
  auto& round2 = MakeCachingInfo(arena, "round-2"sv, SendAway::kTrue);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto first_waiter = std::make_shared<CountingWaiter>();
  auto first = ASSERT_RESULT(cache_->Get(&round1, deadline, first_waiter, kSessionId, nullptr));
  ASSERT_TRUE(first) << "the first round must be asked to fill";

  // first is still held, so round 1 is still recorded as being filled.
  auto second_waiter = std::make_shared<CountingWaiter>();
  auto second = ASSERT_RESULT(cache_->Get(&round2, deadline, second_waiter, kSessionId, nullptr));
  ASSERT_TRUE(second)
      << "a connection was sent away from a fill of its own, so nothing finishes its prefetch";
}

// The same fill seen by a different connection, which is what the check is for.
TEST_F(PgResponseCacheTest, AnotherConnectionIsSentAwayFromAFillUnderWay) {
  ThreadSafeArena arena;
  auto& mine = MakeCachingInfo(arena, "round-1"sv, SendAway::kTrue);
  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine_setter = ASSERT_RESULT(cache_->Get(&mine, deadline, my_waiter, kSessionId, nullptr));
  ASSERT_TRUE(mine_setter);

  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_result = cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr);
  ASSERT_NOK(theirs_result);
  // Incomplete is how the cache says a fill is already under way. The caller is not queued
  // behind it, and reads the older version this node already holds instead.
  ASSERT_TRUE(theirs_result.status().IsIncomplete()) << theirs_result.status();
}

// Fills are counted per connection, so releasing an earlier round while a later one is still
// filling leaves the group busy. Erasing on the first release would let a connection arriving
// inside the overlap start a whole-catalog read of its own.
TEST_F(PgResponseCacheTest, AGroupStaysBusyAcrossOverlappingRounds) {
  ThreadSafeArena arena;
  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kTrue);
  auto& round2 = MakeCachingInfo(arena, "round-2"sv, SendAway::kTrue);
  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto first_waiter = std::make_shared<CountingWaiter>();
  auto first = ASSERT_RESULT(cache_->Get(&round1, deadline, first_waiter, kSessionId, nullptr));
  ASSERT_TRUE(first);
  auto second_waiter = std::make_shared<CountingWaiter>();
  auto second = ASSERT_RESULT(cache_->Get(&round2, deadline, second_waiter, kSessionId, nullptr));
  ASSERT_TRUE(second);

  // The setter owns the round's fill token, so clearing it is what ends round 1, as releasing
  // the request does in pg_client_session.
  first = {};

  // Round 1 is gone but round 2 is still filling, so the group is still counted busy and this
  // caller is sent away rather than queued.
  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_result = cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr);
  ASSERT_NOK(theirs_result);
  ASSERT_TRUE(theirs_result.status().IsIncomplete()) << theirs_result.status();
}

// A connection start's prefetch is several requests with the backend building its relcache in
// between, holding nothing outstanding. The session's token is what keeps the database counted
// across that gap; without it the next connection finds no fill and reads the whole catalog
// from the master leader, which is the fan-out this is all meant to stop.
TEST_F(PgResponseCacheTest, AConnectionPrefetchStaysCountedBetweenItsRequests) {
  ThreadSafeArena arena;
  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kTrue,
                                 PartOfConnectionPrefetch::kTrue);
  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  PgResponseCache::FillTokenPtr token;
  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine = ASSERT_RESULT(cache_->Get(&round1, deadline, my_waiter, kSessionId, &token));
  ASSERT_TRUE(mine);
  ASSERT_TRUE(token) << "a connection start's fill must leave its claim with the session";

  // The request ends. Its setter goes with it, as it does once the response has been sent.
  mine = {};

  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_result = cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr);
  ASSERT_NOK(theirs_result);
  ASSERT_TRUE(theirs_result.status().IsIncomplete()) << theirs_result.status();

  // Dropping the session's token is what ends the claim, so the next caller fills.
  token.reset();
  auto later_waiter = std::make_shared<CountingWaiter>();
  auto later = ASSERT_RESULT(cache_->Get(&theirs, deadline, later_waiter, kOtherSessionId,
                                         nullptr));
  ASSERT_TRUE(later) << "the database stayed counted after its last claim was dropped";
}

// Only a prefetch's opening round asks to be sent away. A later round that asked and was sent
// away would discard the rounds already fetched, so the cache must leave a request that does
// not ask alone, even while another connection is filling the same database.
TEST_F(PgResponseCacheTest, ARequestThatDoesNotAskIsNotSentAway) {
  ThreadSafeArena arena;
  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  auto& later_round = MakeCachingInfo(arena, "round-2"sv, SendAway::kFalse,
                                      PartOfConnectionPrefetch::kTrue);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_setter = ASSERT_RESULT(
      cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr));
  ASSERT_TRUE(theirs_setter);

  PgResponseCache::FillTokenPtr token;
  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine = ASSERT_RESULT(cache_->Get(&later_round, deadline, my_waiter, kSessionId, &token));
  ASSERT_TRUE(mine) << "a round that does not ask must fill rather than be sent away";
}

// The round that completes a connection start's prefetch can be served from the cache, in which
// case no setter runs for it and the release that normally ends the session's claim never
// happens. The claim has to end anyway: the prefetch is over and nothing this session started is
// still filling. Left held it deflects every later connection start on this database to an older
// version, and no timeout takes it back -- the backend's heartbeat keeps its session alive.
TEST_F(PgResponseCacheTest, AConnectionPrefetchEndsItsClaimOnAFinalRoundServedFromCache) {
  ThreadSafeArena arena;
  const auto deadline = CoarseMonoClock::Now() + 60s;

  // Another connection has already cached the entry this one's last round will want.
  auto& cached = MakeCachingInfo(arena, "final-round"sv);
  auto cached_waiter = std::make_shared<CountingWaiter>();
  auto cached_setter = ASSERT_RESULT(
      cache_->Get(&cached, deadline, cached_waiter, kOtherSessionId, nullptr));
  ASSERT_TRUE(cached_setter);
  cached_setter(MakeResponse());
  cached_setter = {};

  // This connection fills an earlier round, which takes its session's claim.
  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kFalse,
                                 PartOfConnectionPrefetch::kTrue);
  PgResponseCache::FillTokenPtr token;
  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine = ASSERT_RESULT(cache_->Get(&round1, deadline, my_waiter, kSessionId, &token));
  ASSERT_TRUE(mine);
  ASSERT_TRUE(token) << "a connection start's fill must leave its claim with the session";
  mine(MakeResponse());
  mine = {};

  // Its last round hits that cached entry, so it is answered without a setter.
  auto& final_round = MakeCachingInfo(arena, "final-round"sv, SendAway::kFalse,
                                      PartOfConnectionPrefetch::kTrue);
  final_round.set_completes_connection_prefetch(true);
  auto final_waiter = std::make_shared<CountingWaiter>();
  auto final_setter = ASSERT_RESULT(
      cache_->Get(&final_round, deadline, final_waiter, kSessionId, &token));
  ASSERT_FALSE(final_setter) << "the final round was expected to be served from the cache";
  ASSERT_EQ(final_waiter->applied(), 1);

  // The prefetch is over, so the next connection fills rather than being sent away from it.
  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_setter = ASSERT_RESULT(
      cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr));
  ASSERT_TRUE(theirs_setter)
      << "the claim outlived the prefetch, so every later connection start on this database is "
      << "sent away to an older version until the session goes away";
}

// The token has two owners: the session holds it across the gaps between a prefetch's requests,
// and the setter of a round still in flight holds it too. Letting go of the session's half while
// a round is outstanding must not end the claim -- that round is still filling the catalog this
// database would otherwise be read as idle for. Only the last owner to let go ends it, which is
// what lets the two let go on different threads: the session under its lock, the setter from the
// flush callback with that lock already dropped.
TEST_F(PgResponseCacheTest, AClaimOutlivesTheSessionsHalfWhileARoundIsStillFilling) {
  ThreadSafeArena arena;
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kFalse,
                                 PartOfConnectionPrefetch::kTrue);
  PgResponseCache::FillTokenPtr token;
  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine = ASSERT_RESULT(cache_->Get(&round1, deadline, my_waiter, kSessionId, &token));
  ASSERT_TRUE(mine);
  ASSERT_TRUE(token) << "a connection start's fill must leave its claim with the session";

  // The session lets go of its half while the round it started is still outstanding.
  token.reset();

  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_result = cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr);
  ASSERT_NOK(theirs_result);
  ASSERT_TRUE(theirs_result.status().IsIncomplete())
      << "a round still filling stopped being counted when the session let go of its half";

  // The round ends, which is the last owner letting go.
  mine = {};
  auto later_waiter = std::make_shared<CountingWaiter>();
  auto later = ASSERT_RESULT(
      cache_->Get(&theirs, deadline, later_waiter, kOtherSessionId, nullptr));
  ASSERT_TRUE(later) << "the claim outlived every owner of the token";
}

// The other half of the send-away: the caller wants the very entry being filled, so the group
// check passes it -- that check skips the asking session's own fills -- and the entry check
// turns it away instead. Parallel workers reach this, since they share their leader's session
// and start at the same catalog version, so they want the same keys.
TEST_F(PgResponseCacheTest, ACallerOnTheEntryBeingFilledIsSentAway) {
  ThreadSafeArena arena;
  auto& filling = MakeCachingInfo(arena, "round-1"sv, SendAway::kTrue);
  auto& sibling = MakeCachingInfo(arena, "round-1"sv, SendAway::kTrue);
  const auto deadline = CoarseMonoClock::Now() + 60s;

  auto filler_waiter = std::make_shared<CountingWaiter>();
  auto filler = ASSERT_RESULT(
      cache_->Get(&filling, deadline, filler_waiter, kSessionId, nullptr));
  ASSERT_TRUE(filler);

  // Same session, so the group check finds no fill but its own and lets this through to the
  // entry, which is being filled.
  auto sibling_waiter = std::make_shared<CountingWaiter>();
  auto sibling_result = cache_->Get(&sibling, deadline, sibling_waiter, kSessionId, nullptr);
  ASSERT_NOK(sibling_result);
  ASSERT_TRUE(sibling_result.status().IsIncomplete()) << sibling_result.status();
  ASSERT_EQ(sibling_waiter->applied(), 0)
      << "a caller sent away from an entry must not be queued on it as well";
}

// The two owners of a connection start's token let go on different threads: the setter from the
// flush callback, with the session lock already dropped, and the session itself under that lock.
// Release has to admit exactly one of them, or the database loses a count it still owes. Both
// arrive here through a round that completes the prefetch and finds its entry already cached,
// which releases the token on the spot, each through a slot of its own -- production serializes
// slot access under the session lock, so only the token is ever shared.
//
// An ordinary read of this session holds a second, separate token throughout, so the group goes
// in counted twice and must come out counted once: a stray second release would take that count
// too, and no release at all would leave both. Run under tsan for the data race.
TEST_F(PgResponseCacheTest, ATokenIsSafeToReleaseFromTwoThreads) {
  ThreadSafeArena arena;
  const auto deadline = CoarseMonoClock::Now() + 60s;

  // Two entries another connection has already cached, so both completing rounds below are
  // answered without a fill and release on the spot.
  for (const auto& key : {"cached-a"sv, "cached-b"sv}) {
    auto& info = MakeCachingInfo(arena, key);
    auto waiter = std::make_shared<CountingWaiter>();
    auto setter = ASSERT_RESULT(cache_->Get(&info, deadline, waiter, kOtherSessionId, nullptr));
    ASSERT_TRUE(setter);
    setter(MakeResponse());
  }

  // This connection fills an earlier round, which takes its session's token.
  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kFalse,
                                 PartOfConnectionPrefetch::kTrue);
  PgResponseCache::FillTokenPtr token;
  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine = ASSERT_RESULT(cache_->Get(&round1, deadline, my_waiter, kSessionId, &token));
  ASSERT_TRUE(mine);
  ASSERT_TRUE(token);

  // An ordinary read of the same session, counted separately and held open throughout.
  auto& ordinary = MakeCachingInfo(arena, "ordinary"sv);
  auto ordinary_waiter = std::make_shared<CountingWaiter>();
  auto ordinary_setter = ASSERT_RESULT(
      cache_->Get(&ordinary, deadline, ordinary_waiter, kSessionId, nullptr));
  ASSERT_TRUE(ordinary_setter);

  auto& completing_a = MakeCachingInfo(arena, "cached-a"sv, SendAway::kFalse,
                                       PartOfConnectionPrefetch::kTrue);
  completing_a.set_completes_connection_prefetch(true);
  auto& completing_b = MakeCachingInfo(arena, "cached-b"sv, SendAway::kFalse,
                                       PartOfConnectionPrefetch::kTrue);
  completing_b.set_completes_connection_prefetch(true);

  auto slot_a = token;
  auto slot_b = token;
  CountDownLatch go(1);
  {
    TestThreadHolder threads;
    threads.AddThreadFunctor([this, &completing_a, &slot_a, &go, deadline] {
      auto waiter = std::make_shared<CountingWaiter>();
      go.Wait();
      ASSERT_RESULT(cache_->Get(&completing_a, deadline, waiter, kSessionId, &slot_a));
    });
    threads.AddThreadFunctor([this, &completing_b, &slot_b, &go, deadline] {
      auto waiter = std::make_shared<CountingWaiter>();
      go.Wait();
      ASSERT_RESULT(cache_->Get(&completing_b, deadline, waiter, kSessionId, &slot_b));
    });
    go.CountDown();
  }

  // One release landed, not two: the ordinary read's token still counts the database.
  auto& theirs = MakeCachingInfo(arena, "their-key"sv, SendAway::kTrue);
  auto their_waiter = std::make_shared<CountingWaiter>();
  auto theirs_result = cache_->Get(&theirs, deadline, their_waiter, kOtherSessionId, nullptr);
  ASSERT_NOK(theirs_result) << "both threads released the one token the connection start held";
  ASSERT_TRUE(theirs_result.status().IsIncomplete()) << theirs_result.status();

  // And it did land: with the ordinary read finished too, nothing is left counting.
  ordinary_setter = {};
  auto later_waiter = std::make_shared<CountingWaiter>();
  auto later = ASSERT_RESULT(
      cache_->Get(&theirs, deadline, later_waiter, kOtherSessionId, nullptr));
  ASSERT_TRUE(later) << "neither thread released the token the connection start held";
}


// The claim outlives the publish. A round that completes a prefetch stores its response and
// records the version before it lets the claim go, so a connection arriving while it does that
// is still sent away rather than left to read the whole catalog for itself. The fill observer
// runs in that window -- after the response is stored, before the release -- so asking from
// there is asking at exactly the moment the ordering is about.
TEST_F(PgResponseCacheTest, TheClaimOutlivesThePublish) {
  ThreadSafeArena arena;
  const auto deadline = CoarseMonoClock::Now() + 60s;

  PgResponseCache* observed = nullptr;
  Status sent_away_while_publishing;
  PgResponseCache cache(
      mem_tracker_, metric_entity_.get(),
      [this, &observed, &sent_away_while_publishing](PgResponseCache::KeyGroup, uint64_t) {
        ThreadSafeArena probe_arena;
        auto& theirs = MakeCachingInfo(probe_arena, "their-key"sv, SendAway::kTrue);
        auto waiter = std::make_shared<CountingWaiter>();
        sent_away_while_publishing = ResultToStatus(observed->Get(
            &theirs, CoarseMonoClock::Now() + 60s, waiter, kOtherSessionId, nullptr));
      });
  observed = &cache;

  // A connection start takes its session's token on an earlier round.
  auto& round1 = MakeCachingInfo(arena, "round-1"sv, SendAway::kFalse,
                                 PartOfConnectionPrefetch::kTrue);
  PgResponseCache::FillTokenPtr token;
  auto my_waiter = std::make_shared<CountingWaiter>();
  auto mine = ASSERT_RESULT(cache.Get(&round1, deadline, my_waiter, kSessionId, &token));
  ASSERT_TRUE(mine);
  mine(MakeResponse());
  mine = {};

  // The round that completes it. Storing its response fires the observer above.
  auto& final_round = MakeCachingInfo(arena, "final-round"sv, SendAway::kFalse,
                                      PartOfConnectionPrefetch::kTrue);
  final_round.set_completes_connection_prefetch(true);
  auto final_waiter = std::make_shared<CountingWaiter>();
  auto final_setter = ASSERT_RESULT(
      cache.Get(&final_round, deadline, final_waiter, kSessionId, &token));
  ASSERT_TRUE(final_setter);
  final_setter(MakeResponse());

  ASSERT_NOK(sent_away_while_publishing)
      << "the claim was already gone while the round that completes the prefetch was still "
      << "publishing, so a connection arriving then reads the whole catalog itself";
  ASSERT_TRUE(sent_away_while_publishing.IsIncomplete()) << sent_away_while_publishing;
}

} // namespace
} // namespace yb::tserver
