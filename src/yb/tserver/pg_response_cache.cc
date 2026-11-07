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

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <boost/multi_index/member.hpp>

#include "yb/client/yb_op.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/sidecars.h"

#include "yb/tserver/pg_client.messages.h"

#include "yb/util/async_util.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/lru_cache.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/scope_exit.h"
#include "yb/util/write_buffer.h"

METRIC_DEFINE_counter(server, pg_response_cache_hits,
                      "PgClientService Response Cache Hits",
                      yb::MetricUnit::kCacheHits,
                      "Total number of hits in PgClientService response cache");
DEFINE_test_flag(bool, pg_response_cache_force_lookup_only_miss, false,
    "Treat every lookup-only response cache query as a miss, as though the entry had been "
    "evicted. Lets a test exercise the fallback from reading an older catalog version to a full "
    "prefetch without having to arrange an eviction.");

DEFINE_test_flag(bool, pg_response_cache_force_lookup_only_miss_on_final_request, false,
    "Treat only the lookup-only query that completes a connection start's prefetch as a miss. "
    "The earlier requests still hit, so this reproduces a connection that has already built its "
    "relcache from the older version by the time it has to fall back.");

DEFINE_test_flag(bool, pg_response_cache_pause_fill, false,
    "Hold every response cache fill open before it stores its response, for as long as this is "
    "set. The entries stay claimed and unanswered meanwhile, which is the window another "
    "connection has to arrive and find a fill already under way. A test clears it once it has "
    "seen what it was waiting for, so the window is bounded by the test's own progress rather "
    "than by a sleep.");

METRIC_DEFINE_counter(server, pg_response_cache_lookup_only_hits,
                      "PgClientService Response Cache Lookup Only Hits",
                      yb::MetricUnit::kCacheHits,
                      "Number of lookup-only queries to PgClientService response cache that were "
                      "served from a catalog version this node already held rather than fetched "
                      "from the master leader. A connection start issues one per prefetch "
                      "request, so this counts requests rather than connections");
METRIC_DEFINE_counter(server, pg_response_cache_lookup_only_misses,
                      "PgClientService Response Cache Lookup Only Misses",
                      yb::MetricUnit::kCacheQueries,
                      "Number of lookup-only queries to PgClientService response cache that found "
                      "nothing cached, after which the connection falls back to a full catalog "
                      "prefetch. Counts requests rather than connections");
METRIC_DEFINE_counter(server, pg_response_cache_send_away_asks,
                      "PgClientService Response Cache Send-Away Asks",
                      yb::MetricUnit::kCacheQueries,
                      "Number of requests that arrived asking to be sent away from a fill "
                      "already under way. One per connection start that had an older catalog "
                      "version to fall back on and found the master leader not shedding "
                      "prefetches, so this is the denominator for "
                      "pg_response_cache_send_aways: how often one of those connections "
                      "actually met a fill. Zero while connections queue means none of them "
                      "asked, which places the cause on the PG side, the commonest being the "
                      "feature turned off, no older version recorded for the database yet, or "
                      "a leader reporting itself busy");
METRIC_DEFINE_counter(server, pg_response_cache_send_aways,
                      "PgClientService Response Cache Send-Aways From An In-Flight Fill",
                      yb::MetricUnit::kCacheQueries,
                      "Number of queries to PgClientService response cache that found a fill "
                      "already under way -- another connection filling their database, or any "
                      "session filling the very entry they wanted -- and were told to read a "
                      "catalog version this node already holds instead of waiting for it. One "
                      "that cannot use that version reruns its prefetch without the ask and "
                      "proceeds as it would have without it, so this counts the connections "
                      "turned away, not the ones that went on to read the older version. "
                      "Counts requests rather than connections");
METRIC_DEFINE_counter(server, pg_response_cache_queries,
                      "PgClientService Response Cache Queries",
                      yb::MetricUnit::kCacheQueries,
                      "Total number of queries to PgClientService response cache");
METRIC_DEFINE_counter(server, pg_response_cache_renew_soft,
                      "PgClientService Response Cache Renewed Soft",
                      yb::MetricUnit::kCacheQueries,
                      "Total number of PgClientService response cache entries renewed soft");
METRIC_DEFINE_counter(server, pg_response_cache_renew_hard,
                      "PgClientService Response Cache Renewed Hard",
                      yb::MetricUnit::kCacheQueries,
                      "Total number of PgClientService response cache entries renewed hard");
METRIC_DEFINE_counter(server, pg_response_cache_gc_calls,
                      "PgClientService Response Cache GC",
                      yb::MetricUnit::kRequests,
                      "Number of PgClientService response cache GC calls");
METRIC_DEFINE_counter(server, pg_response_cache_entries_removed_by_gc,
                      "PgClientService Response Cache Renewed Hard",
                      yb::MetricUnit::kEntries,
                      "Number of PgClientService response cache entries removed by GC calls");
METRIC_DEFINE_counter(server, pg_response_cache_disable_calls,
                      "PgClientService Response Cache Disabled",
                      yb::MetricUnit::kRequests,
                      "Total number of Disable() calls for PgClientService response cache");
METRIC_DEFINE_gauge_uint32(server, pg_response_cache_entries,
                      "PgClientService Response Cache Entries",
                      yb::MetricUnit::kEntries,
                      "Number of entries in PgClientService response cache");

DEFINE_NON_RUNTIME_uint64(pg_response_cache_capacity, 1024,
    "PgClientService response cache capacity.");

DEFINE_NON_RUNTIME_uint64(pg_response_cache_size_bytes, 0,
                          "Size in bytes of the PgClientService response cache. "
                          "0 value (default) means that cache size is not limited by this flag.");

DEFINE_NON_RUNTIME_uint32(pg_response_cache_size_percentage, 5,
    "Percentage of process' hard memory limit to use by the PgClientService response cache. "
    "Default value is 5, max is 100, min is 0 means that cache size is not limited by this flag.");


DEFINE_NON_RUNTIME_uint32(pg_response_cache_num_key_group_bucket, 512,
    "Number of buckets for key group values which are used as part of response cache key. "
    "Response cache can be disabled for particular key group, but actually it will be disabled for "
    "all key groups in same bucket");

DEFINE_test_flag(uint64, pg_response_cache_catalog_read_time_usec, 0,
                 "Value to substitute original catalog_read_time in cached responses");

namespace yb::tserver {
namespace {
// Whether the caller asked to be sent away from a fill already under way, and whether the
// request is one of a connection start's prefetch.
YB_STRONGLY_TYPED_BOOL(SendAway);
YB_STRONGLY_TYPED_BOOL(PartOfConnectionPrefetch);


void TEST_UpdateCatalogReadTime(
    PgPerformResponseMsg* resp, const ReadHybridTime& new_catalog_read_time) {
  ReadHybridTime original_catalog_read_time;
  original_catalog_read_time.FromPB(resp->catalog_read_time());
  LOG(INFO) << "Substitute original catalog_read_time " << original_catalog_read_time
            << " with " << new_catalog_read_time;
  new_catalog_read_time.ToPB(resp->mutable_catalog_read_time());
  for (auto& r : *resp->mutable_responses()) {
    if (r.has_paging_state()) {
      new_catalog_read_time.ToPB(r.mutable_paging_state()->mutable_read_time());
    }
  }
}

[[nodiscard]] bool IsOk(const PgResponseCache::Response& resp) {
  return !resp.response->has_status();
}

class MetricContext {
 public:
  MetricContext(std::shared_ptr<MemTracker> mem_tracker,
                scoped_refptr<AtomicGauge<uint32_t>> num_entries)
      : mem_tracker_(std::move(mem_tracker)),
        num_entries_(std::move(num_entries)) {}

  MemTracker& mem_tracker() { return *mem_tracker_; }
  AtomicGauge<uint32_t>& num_entries() {return *num_entries_; }

 private:
  std::shared_ptr<MemTracker> mem_tracker_;
  scoped_refptr<AtomicGauge<uint32_t>> num_entries_;

  DISALLOW_COPY_AND_ASSIGN(MetricContext);
};

class MetricUpdater {
 public:
  explicit MetricUpdater(MetricContext* metric_context)
      : metric_context_(*metric_context) {
    num_entries().Increment();
  }

  ~MetricUpdater() {
    num_entries().Decrement();
    const auto bytes = consumption();
    if (bytes) {
      mem_tracker().Release(bytes);
    }
  }

  void Consume(size_t bytes) {
    DCHECK(bytes);
    if (mem_tracker().TryConsume(bytes)) {
      size_t expected = 0;
      [[maybe_unused]] const auto changed = consumption_.compare_exchange_strong(
          expected, bytes, std::memory_order::acq_rel);
      DCHECK(changed);
    }
  }

  [[nodiscard]] uint64_t consumption() const {
    return consumption_.load(std::memory_order_acquire);
  }

 private:
  [[nodiscard]] MemTracker& mem_tracker() { return metric_context_.mem_tracker(); }
  [[nodiscard]] AtomicGauge<uint32_t>& num_entries() {return metric_context_.num_entries(); }

  std::atomic<size_t> consumption_ = {0};

  MetricContext& metric_context_;

  DISALLOW_COPY_AND_ASSIGN(MetricUpdater);
};

YB_DEFINE_ENUM(DataState, (kNotReady)(kReadyOK)(kReadyFailure));

class Data {
 public:
  Data(uint64_t version,
       std::weak_ptr<MetricUpdater> metric_updater,
       CoarseTimePoint creation_time,
       CoarseTimePoint readiness_deadline)
      : metric_updater_(std::move(metric_updater)),
        version_(version),
        creation_time_(creation_time),
        readiness_deadline_(readiness_deadline) {}

  void Set(
      PgResponseCache::Response&& value, PgResponseCache::KeyGroup group,
      uint64_t catalog_version) {
    if (PREDICT_FALSE(FLAGS_TEST_pg_response_cache_catalog_read_time_usec > 0) &&
        value.response->has_catalog_read_time()) {
      TEST_UpdateCatalogReadTime(
          value.response.get(),
          ReadHybridTime::SingleTime(HybridTime::FromMicros(
              FLAGS_TEST_pg_response_cache_catalog_read_time_usec)));
    }
    auto new_state = DataState::kReadyFailure;
    size_t sz = 0;
    if (IsOk(value)) {
      for (const auto& data : value.rows_data) {
        sz += data.size();
      }
      new_state = DataState::kReadyOK;
    }
    decltype(waiters_) waiters;
    // Since response_ is not changed after assignment, we could store pointer to it to make
    // thread safety analysis happy, and then use it.
    const PgResponseCache::Response* response = nullptr;
    {
      std::lock_guard lock(mutex_);
      response_ = std::move(value);
      waiters.swap(waiters_);
      response = &*response_;
      state_.store(new_state, std::memory_order_release);
    }
    // Logged before the loop, not after, because a node that is about to run out of memory dies
    // inside it.
    //
    // The waiter count is the point: every waiter receives the response here, and connections that
    // arrived one at a time go on to build their catalog caches together. The byte count is the
    // response itself, not what a backend grows to holding it -- a few MB here has been seen to
    // become a few hundred MB of relcache. The fill duration is here because anything that delays
    // the filler, such as the master shedding its prefetch, lengthens the queue behind it. The
    // group and the catalog version say which database refilled and at which version. Neither
    // identifies one fill: a paged prefetch caches every page under a key of its own, and a global
    // catalog version bump moves every database to the same version at once.
    //
    // The outcome separates a fill that stored a catalog from one that did not: a read that
    // failed, or a filler that went away without storing anything (see SetAbandoned). All three
    // release their waiters here; only the first hands them a catalog.
    const auto* const outcome =
        new_state == DataState::kReadyOK ? "Fill complete" : "Fill failed";
    VLOG(1) << outcome << " for group " << group << " catalog version " << catalog_version
            << " after " << MonoDelta(CoarseMonoClock::Now() - creation_time_) << ", releasing "
            << waiters.size() << " waiters, " << sz << " bytes each";
    for (auto waiter : waiters) {
      waiter->Apply(*response);
    }
    if (sz) {
      auto updater = metric_updater_.lock();
      if (updater) {
        updater->Consume(sz);
      }
    }
  }

  // Complete an entry whose filler went away without storing anything. Set() is the only thing
  // that drains waiters_, and PgResponseCacheWaiter takes nothing but a response, so an error
  // response is the only way to release callers already queued. Handing them one is what a fill
  // whose read failed already does: pg_client_session calls the setter with the failed status
  // and Set() passes it on. Leaving them queued instead costs a postgres backend apiece until
  // their own deadlines expire, and leaves them to be destroyed un-responded if the entry's
  // readiness deadline passes first.
  //
  // Completing as a failure also leaves the entry in the state that already means "do not serve
  // this": IsValid() is false for kReadyFailure, so the next caller rebuilds it and fills afresh
  // rather than inheriting the failure.
  void SetAbandoned(PgResponseCache::KeyGroup group, uint64_t catalog_version) {
    auto msg = rpc::MakeSharedMessage<PgPerformResponseMsg>();
    StatusToPB(
        STATUS(Aborted, "Catalog response cache fill was abandoned by its filler"),
        msg->mutable_status());
    Set(PgResponseCache::Response(*msg, {}), group, catalog_version);
  }

  [[nodiscard]] bool IsValid(CoarseTimePoint now, uint64_t version) const {
    if (PREDICT_FALSE(version != version_)) {
      return false;
    }
    const auto state = state_.load(std::memory_order_acquire);
    switch (state) {
      case DataState::kNotReady:
        return now < readiness_deadline_;
      case DataState::kReadyOK:
        return true;
      case DataState::kReadyFailure:
        return false;
    }
    FATAL_INVALID_ENUM_VALUE(DataState, state);
  }

  enum class JoinState {
    // No fill has started for this entry, so this query performs it.
    kFill,
    // A fill is under way and this query is queued behind it.
    kWaiting,
    // The response is already here.
    kReady,
    // A fill is under way and this query asked not to wait for one.
    kSentAway,
  };

  struct JoinResult {
    JoinState state;
    // Set for kReady and nothing else.
    const PgResponseCache::Response* response = nullptr;
  };

  [[nodiscard]] JoinResult RegisterWaiter(
      const PgResponseCacheWaiterPtr& waiter, SendAway send_away) {
    std::lock_guard lock(mutex_);
    if (response_) {
      return {.state = JoinState::kReady, .response = &*response_};
    }
    if (!running_) {
      running_ = true;
      return {.state = JoinState::kFill};
    }
    if (send_away) {
      // Deliberately not enqueued. This query is going to read an older version instead, and a
      // waiter left behind would be handed a response nothing is waiting for any more.
      return {.state = JoinState::kSentAway};
    }
    waiters_.push_back(waiter);
    return {.state = JoinState::kWaiting};
  }

  // The cached response, or null while one is still being filled. Unlike RegisterWaiter this
  // neither claims the entry for filling nor enqueues the waiter, so a query that only looks
  // leaves no trace on the entry.
  [[nodiscard]] const PgResponseCache::Response* ReadyResponse() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return response_ ? &*response_ : nullptr;
  }

  [[nodiscard]] CoarseTimePoint creation_time() const {
    return creation_time_;
  }

 private:
  std::weak_ptr<MetricUpdater> metric_updater_;
  const uint64_t version_;
  const CoarseTimePoint creation_time_;
  const CoarseTimePoint readiness_deadline_;
  std::mutex mutex_;
  std::atomic<DataState> state_{DataState::kNotReady};
  bool running_ GUARDED_BY(mutex_) = false;
  std::optional<PgResponseCache::Response> response_ GUARDED_BY(mutex_);
  std::vector<PgResponseCacheWaiterPtr> waiters_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(Data);
};

class Value {
 public:
  Value() = default;
  Value(uint64_t version,
        MetricContext* metric_context,
        CoarseTimePoint creation_time,
        CoarseTimePoint readiness_deadline)
      : metric_updater_(std::make_shared<MetricUpdater>(metric_context)),
        data_(std::make_shared<Data>(version, metric_updater_, creation_time, readiness_deadline)) {
        }

  [[nodiscard]] size_t Consumption() const {
    DCHECK(metric_updater_);
    return metric_updater_->consumption();
  }

  [[nodiscard]] const std::shared_ptr<Data>& data() const { return data_; }

 private:
  std::shared_ptr<MetricUpdater> metric_updater_;
  std::shared_ptr<Data> data_;
};

struct Key {
  PgResponseCache::KeyGroup group;
  std::string value;

  Key(PgResponseCache::KeyGroup group, std::string_view key_value)
      : group(group), value(key_value) {}

  friend bool operator==(const Key&, const Key&) = default;
};

inline size_t hash_value(const Key& key) {
  size_t value = 0;
  boost::hash_combine(value, key.group);
  boost::hash_range(value, key.value.begin(), key.value.end());
  return value;
}

struct Entry {
  Entry(PgResponseCache::KeyGroup group, std::string_view key_value)
      : key(group, key_value) {}

  Key key;
  Value value;
};

[[nodiscard]] uint64_t GetCacheSizeLimitInBytes() {
  uint64_t result = 0;
  if (FLAGS_pg_response_cache_size_bytes) {
    result = FLAGS_pg_response_cache_size_bytes;
  }
  auto total_memory = MemTracker::GetRootTracker()->limit();
  if (FLAGS_pg_response_cache_size_percentage && total_memory > 100) {
    auto percentage = FLAGS_pg_response_cache_size_percentage;
    CHECK(percentage > 0 && percentage <= 100)
        << Format(
            "Flag pg_response_cache_size_percentage must be between 0 and 100. Current value: $0",
            percentage);

    auto new_limit = static_cast<uint64_t>(total_memory) * percentage / 100;
    if (!result || new_limit < result) {
      result = new_limit;
    }
  }
  return result;
}

[[nodiscard]] std::shared_ptr<MemTracker> GetCacheMemoryTracker(
    const std::shared_ptr<MemTracker>& parent) {
  const auto memory_limit = GetCacheSizeLimitInBytes();
  constexpr auto* kId = "PgResponseCache";
  if (memory_limit) {
    VLOG(1) << "Building memory tracker with " << memory_limit << " bytes limit";
    return MemTracker::FindOrCreateTracker(memory_limit, kId, parent);
  }
  VLOG(1) << "Building unlimited memory tracker";
  return MemTracker::FindOrCreateTracker(kId, parent);
}

struct KeyGroupBucket {
  std::weak_ptr<PgResponseCache::DisablerType> disabler;
  uint64_t version{0};
};

} // namespace

struct PgResponseCache::DisablerType {};

// Counting a fill starts with this and ends when it is released, which its destructor does if
// nothing else has; the two always pair and nothing else moves the count.
//
// What the two spans are and why, is on the declaration in pg_response_cache.h. What they rest
// on is here: an ordinary fill's token belongs to the setter, so the count ends with the request
// whether the read completes, fails, or is dropped before the flush, and a backend that dies
// mid-prefetch cannot leave its database marked busy for ever.
class PgResponseCache::FillToken {
 public:
  FillToken(Impl* impl, KeyGroup group, uint64_t session_id);

  FillToken(const FillToken&) = delete;
  FillToken& operator=(const FillToken&) = delete;

  ~FillToken();

  // Idempotent, and safe to call from two threads at once: see the definition.
  void Release();

  [[nodiscard]] bool released() const { return released_.load(std::memory_order_acquire); }
  [[nodiscard]] KeyGroup group() const { return group_; }

 private:
  Impl* const impl_;
  std::atomic<bool> released_{false};
  const KeyGroup group_;
  const uint64_t session_id_;
};

class PgResponseCache::Impl : private GarbageCollector {
  friend class PgResponseCache::FillToken;
  [[nodiscard]] auto& GetKeyGroupBucket(KeyGroup group) REQUIRES(mutex_) {
    const auto sz = key_group_buckets_.size();
    DCHECK_EQ(sz, FLAGS_pg_response_cache_num_key_group_bucket);
    return key_group_buckets_[group % sz];
  }

  // Whether a connection other than this one is filling an entry of this key group. The session
  // id is the only handle on "another connection" a Perform carries, so a session's own counts
  // are skipped.
  //
  // Within one backend the skip decides nothing: pggate asks only on a prefetch's opening round
  // and clears the ask before any rerun, and at that round the session has filled nothing yet,
  // so there is no count of its own to find. What it decides is the parallel-worker case below,
  // where siblings share a session id -- there the skip is why they do not send each other away.
  //
  // Counted per session rather than per entry because a connection start wants a different entry
  // from the fill it is sent away from -- it pinned a version of its own -- so there is no entry
  // the two have in common to count.
  //
  // Parallel workers adopt their leader's session id, so this check does not separate siblings
  // of one query. The entry-level check in RegisterWaiter still can: they start at one catalog
  // version and want the same entries, so the one that arrives second is sent away and reads
  // the version this node holds, which it then bridges to the same version its leader is on.
  [[nodiscard]] bool AnotherFillInFlight(KeyGroup group, uint64_t session_id) REQUIRES(mutex_) {
    auto it = fills_in_flight_.find(group);
    if (it == fills_in_flight_.end()) {
      return false;
    }
    return std::ranges::any_of(
        it->second, [session_id](const auto& e) { return e.first != session_id; });
  }

  // A connection start's prefetch reuses the session's token; every other caching read takes one
  // of its own, which the setter drops with the request. A released token is one whose prefetch
  // has ended, so it is replaced rather than reused. The group check beside it is defensive: a
  // session's prefetch keys on the database it connected to, so today it never holds a token on
  // another.
  [[nodiscard]] FillTokenPtr TakeFillToken(
      PartOfConnectionPrefetch part_of_connection_prefetch, KeyGroup group,
      uint64_t session_id,
      FillTokenPtr* token) {
    if (!part_of_connection_prefetch || !token) {
      return std::make_shared<FillToken>(this, group, session_id);
    }
    if (!*token || (*token)->released() || (*token)->group() != group) {
      *token = std::make_shared<FillToken>(this, group, session_id);
    }
    return *token;
  }

  // A round of the request that completes a prefetch ends the session's claim even when it takes
  // no fill of its own. A filling round releases from its setter; a round served from the cache,
  // or queued behind another session's fill, never gets one. Without this the claim would outlive
  // the connection start that took it and wait for whatever that session sent next.
  //
  // Every round of that last request carries the flag, so a round before the last one can end the
  // claim early. The next round that fills takes a fresh token, so the database is uncounted only
  // between the two -- against a claim held for the whole connection start if this never fired.
  //
  // A round that failed is not one of those, and the callers that can see the outcome check it
  // before calling: the setter, and the arm that serves a response already stored. Two callers
  // cannot. A round queued behind another session's fill has no response yet, and a round
  // answered while the database's cache is disabled has not read anything at all. Both end the
  // claim whatever the round goes on to do, and both leave the retry to take a fresh token.
  static void EndPrefetchClaimIfComplete(
      const LWPgPerformOptionsPB::LWCachingInfoPB& cache_info, FillTokenPtr* token) {
    if (cache_info.completes_connection_prefetch() && token && *token) {
      (*token)->Release();
    }
  }

  // Counted, not a set, because a request's token outlives its response, so the next round of
  // the same read can be counted before the previous one lets go. In a set that release would
  // erase the mark the next round had just made, and a connection arriving then would start a
  // whole-catalog read of its own. A connection start never exceeds one, since its rounds
  // share a token.
  void NoteFillStarted(KeyGroup group, uint64_t session_id) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    ++fills_in_flight_[group][session_id];
  }

  void NoteFillFinished(KeyGroup group, uint64_t session_id) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    auto it = fills_in_flight_.find(group);
    if (it == fills_in_flight_.end()) {
      return;
    }
    auto session_it = it->second.find(session_id);
    if (session_it == it->second.end() || --session_it->second > 0) {
      return;
    }
    it->second.erase(session_it);
    if (it->second.empty()) {
      fills_in_flight_.erase(it);
    }
  }

  [[nodiscard]] std::shared_ptr<Data> GetEntryData(
      LWPgPerformOptionsPB::LWCachingInfoPB* cache_info,
      CoarseTimePoint deadline) EXCLUDES(mutex_) {
    auto now = CoarseMonoClock::Now();
    Counter* renew_metric = nullptr;
    auto metric_updater = ScopeExit([&renew_metric] {
      if (renew_metric) {
        renew_metric->Increment();
      }
    });
    std::lock_guard lock(mutex_);
    const auto group = cache_info->key_group();
    const auto& bucket = GetKeyGroupBucket(group);
    if (bucket.disabler.lock()) {
      return nullptr;
    }
    auto actual_version = bucket.version;
    const auto& value = entries_.emplace(
        group, std::move(*cache_info->mutable_key_value()))->value;
    auto data = value.data();
    if (data && !IsRenewRequired(data->creation_time(), now, *cache_info, &renew_metric) &&
        data->IsValid(now, actual_version)) {
      return data;
    }
    VLOG(5) << "(Re)Building element group=" << group << " actual_version=" << actual_version;
    const_cast<Value&>(value) = Value(actual_version, &metric_context_, now, deadline);
    return value.data();
  }

  // The data for an already cached entry, or null when there is none to serve. Unlike
  // GetEntryData this never inserts.
  [[nodiscard]] std::shared_ptr<Data> FindEntryData(
      LWPgPerformOptionsPB::LWCachingInfoPB* cache_info) EXCLUDES(mutex_) {
    const auto now = CoarseMonoClock::Now();
    std::lock_guard lock(mutex_);
    const auto group = cache_info->key_group();
    const auto& bucket = GetKeyGroupBucket(group);
    if (bucket.disabler.lock()) {
      return nullptr;
    }
    const auto i = entries_.find(Key(group, cache_info->key_value()));
    if (i == entries_.end()) {
      return nullptr;
    }
    const auto& data = i->value.data();
    return data && data->IsValid(now, bucket.version) ? data : nullptr;
  }

 public:
  Result<Setter> Get(
      LWPgPerformOptionsPB::LWCachingInfoPB* cache_info, CoarseTimePoint deadline,
      const PgResponseCacheWaiterPtr& waiter, uint64_t session_id,
      FillTokenPtr* token) EXCLUDES(mutex_) {
    // The last request of a connection start's prefetch is one of its requests, so the two fields
    // have three valid combinations between them and the wire allows a fourth. Only a bug on the
    // PG side can send it -- the two ends are the same build -- and what it would do is record a
    // version for a prefetch this node has no reason to believe it holds whole. A connection
    // reading back at that version then misses an entry and falls back to a full prefetch, so the
    // cost is a wasted attempt rather than a catalog read at the wrong version. DFATAL rather
    // than FATAL: a debug build fails on it, since only a bug produces it, while a release
    // tserver logs it and carries on.
    LOG_IF(DFATAL, cache_info->completes_connection_prefetch() &&
                       !cache_info->part_of_connection_prefetch())
        << "Request completes a connection prefetch it is not part of, key group "
        << cache_info->key_group() << ", catalog version " << cache_info->catalog_version();

    if (cache_info->lookup_only()) {
      return LookupOnlyGet(cache_info, waiter);
    }
    auto data = GetEntryData(cache_info, deadline);
    queries_->Increment();
    // Before the disabled-cache return below, so the count stays one per connection start that
    // asked. A connection whose opening round lands in a disabled window met every condition on
    // the PG side and is simply unanswerable here; leaving it out would drop it from the
    // denominator and make send-aways look rarer than the asks warranted.
    if (cache_info->send_away_if_fill_in_flight()) {
      send_away_asks_->Increment();
    }
    if (!data) {
      // The cache is disabled for this database, as it is across a DDL commit, so nothing is
      // served or filled. This return comes before both of the places that would end a claim,
      // so end it here: otherwise a connection start whose completing round landed in this
      // window would keep its database counted busy until the session sent something that is
      // not part of a prefetch, or ended.
      //
      // Nothing has been read yet, so unlike the setter and the arm below this cannot tell a
      // round that is finishing the prefetch from one that will fail and be retried. It ends
      // the claim either way, and the retry's first fill takes a fresh token.
      EndPrefetchClaimIfComplete(*cache_info, token);
      return [](Response&& response) {};
    }
    const auto group = cache_info->key_group();
    // While another session is filling this database, send the asking connection away, whatever
    // state the entry it asked for is in. A connection starting during a run of DDLs pins a
    // version of its own, so the entry it wants is nobody else's and the per-entry check below
    // would not see that fill at all. A connection whose opening round hits the cache is not
    // spared the wait either: the rounds after it carry no ask of their own and queue behind
    // the rest of the same fill. What it reads instead is a version this node already holds and
    // can bridge from. When the fill ends nothing is counted and connections are served as usual.
    //
    // The count read here is taken a moment later, when the setter takes its token, so two
    // connections on keys of their own that both reach this point inside that window both go on
    // to fill. The per-entry check below closes it where they want the same entry; on different
    // keys nothing does, so a burst costs a few whole-catalog reads rather than one. Closing it
    // means taking a token speculatively under this lock for every request that might fill and
    // undoing it on the three paths that do not, which is more machinery in this path than a
    // window of a few instructions is worth.
    if (cache_info->send_away_if_fill_in_flight()) {
      bool another_fill;
      {
        std::lock_guard lock(mutex_);
        another_fill = AnotherFillInFlight(group, session_id);
      }
      if (another_fill) {
        send_aways_->Increment();
        // The lookup above inserts the entry, replacing one that was due for renewal, and a
        // send-away leaves nothing to fill it. Harmless: no fill is claimed on it, so the next
        // query for that key fills it as usual -- unlike LookupOnlyGet, where an entry left
        // claimed for a fill that is not coming makes every later query wait out its deadline.
        return STATUS(Incomplete, "A fill for this database is already under way");
      }
    }
    auto join = data->RegisterWaiter(waiter, SendAway(cache_info->send_away_if_fill_in_flight()));
    switch (join.state) {
      case Data::JoinState::kReady:
        hits_->Increment();
        // Only a round that succeeded ends the claim, the rule the setter follows: a failure
        // does not mean the prefetch is over, since PG retries it. A failure that is already
        // stored sends the next caller to a rebuilt entry, so what reaches here is a fill this
        // caller joined between the entry being read and this line, which has just failed.
        if (IsOk(*join.response)) {
          EndPrefetchClaimIfComplete(*cache_info, token);
        }
        waiter->Apply(*join.response);
        return Setter();
      case Data::JoinState::kWaiting:
        hits_->Increment();
        // No response yet, so the outcome cannot be checked as it is above, and a completing
        // round that goes on to fail ends the claim here all the same.
        EndPrefetchClaimIfComplete(*cache_info, token);
        return Setter();
      case Data::JoinState::kSentAway:
        // Not a hit: nothing was served. The caller reads the version this node already holds
        // and catches up from there, and asks again without send_away_if_fill_in_flight if it
        // cannot, so this cannot be answered the same way twice.
        //
        // The group check above covers another session's fill, but only once that fill is
        // counted, which its setter does a moment after the entry is marked as filling. This
        // arm covers that gap, and the case of a session asking for an entry it is filling
        // itself, which parallel workers sharing a session can reach.
        send_aways_->Increment();
        return STATUS(Incomplete, "A fill for this key is already under way");
      case Data::JoinState::kFill:
        break;
    }
    const auto version = cache_info->catalog_version();
    // pg_client_session takes this setter and can return early before the flush -- SetupSession
    // and PrepareOperations both can -- destroying it without ever storing a response. Completing
    // the entry then is what releases the callers queued behind this fill.
    auto abandon = [data, group, version] { data->SetAbandoned(group, version); };
    return [data = std::move(data),
            abandon_guard =
                std::make_shared<CancelableScopeExit<decltype(abandon)>>(std::move(abandon)),
            fill = TakeFillToken(
                PartOfConnectionPrefetch(cache_info->part_of_connection_prefetch()),
                group, session_id, token),
            observer = fill_observer_,
            group,
            version,
            completes_connection_prefetch =
                cache_info->completes_connection_prefetch()](Response&& response) {
      TEST_PAUSE_IF_FLAG(TEST_pg_response_cache_pause_fill);
      // A prefetch is paged. pggate keeps issuing reads while any operation still returns paging
      // state, and each round is cached under a key of its own, so an observer told about the
      // first round would learn of a version whose later rounds are not cached yet. The round
      // where nothing is left to page is the one after which this node holds all of them.
      // A failed round holds no rows and no paging state, so it must not be mistaken for the
      // last one. Computed before the move below, which empties the response.
      bool is_final_round = completes_connection_prefetch && IsOk(response);
      if (is_final_round) {
        for (const auto& op_response : response.response->responses()) {
          if (op_response.has_paging_state()) {
            is_final_round = false;
            break;
          }
        }
      }
      abandon_guard->Cancel();
      data->Set(std::move(response), group, version);
      // After the entry is set, so an observer can rely on the entry being readable.
      if (observer && is_final_round) {
        observer(group, version);
      }
      // The prefetch is done, so the session's claim on the database ends here rather than
      // waiting for the session to drop it. An ordinary fill's token is nobody else's and is
      // released when this setter dies, which is the request's end.
      //
      // One prefetch per session is the usual case but not the only one: parallel workers share
      // their leader's session, and so this token, where they run connection starts of their
      // own. The first of them to finish ends the claim for all, leaving a sibling that is
      // still filling uncounted until its next round takes a fresh token. For that round only
      // the per-entry check stands between an arriving connection and a catalog read of its
      // own. Telling the two apart would need a per-prefetch identifier the wire does not
      // carry, and the case needs preload_rel_cache to arise at all.
      //
      // Last, not before the two above: a connection arriving while this round publishes is
      // still sent away by this claim, where releasing first would leave nothing to send it
      // away and it would read the whole catalog itself. One sent away between Set and the
      // observer aims at the version the observer has not replaced yet, which is older than
      // the one just stored and bridgeable like any other, so that costs nothing.
      // TheClaimOutlivesThePublish covers this.
      //
      // A failed round does not end the claim, because a failure does not mean the prefetch is
      // over: PG retries a failed attempt, and the gap before the next one is where the claim
      // matters most, since preparing the older read can wait for a heartbeat. Holding it too
      // long costs other connections a version they can bridge from; ending it too early costs
      // them the wait this change exists to remove. The session ends it instead, on its next
      // request that is not part of a prefetch or when it goes away.
      if (is_final_round) {
        fill->Release();
      }
    };
  }

  // A lookup-only query answers whether a response is cached here and never fills the cache; its
  // caller reads from the master leader instead when nothing is. It must leave no entry behind on
  // a miss: an entry claimed for a fill that is not coming makes every later query on that key
  // wait out the RPC deadline.
  Result<Setter> LookupOnlyGet(
      LWPgPerformOptionsPB::LWCachingInfoPB* cache_info,
      const PgResponseCacheWaiterPtr& waiter) EXCLUDES(mutex_) {
    auto data = FindEntryData(cache_info);
    queries_->Increment();
    // A response another query is still filling is not one this can serve, since waiting for it
    // would mean registering as a waiter on an entry this query does not own.
    const auto* response = data ? data->ReadyResponse() : nullptr;
    // An entry whose fill failed still holds a response, since Set stores it before recording the
    // failure and ReadyResponse does not consult the state. Serving one counts a hit for something
    // the caller cannot start from, and it falls back either way.
    if (!response || !IsOk(*response) ||
        PREDICT_FALSE(FLAGS_TEST_pg_response_cache_force_lookup_only_miss) ||
        PREDICT_FALSE(FLAGS_TEST_pg_response_cache_force_lookup_only_miss_on_final_request &&
                      cache_info->completes_connection_prefetch())) {
      lookup_only_misses_->Increment();
      return STATUS(Incomplete, "Response is not cached");
    }
    hits_->Increment();
    lookup_only_hits_->Increment();
    waiter->Apply(*response);
    return Setter();
  }

  [[nodiscard]] Disabler Disable(KeyGroup group) {
    VLOG(5) << "Disabling cache for " << group;
    disable_calls_->Increment();
    std::lock_guard lock(mutex_);
    auto& bucket = GetKeyGroupBucket(group);
    auto disabler = bucket.disabler.lock();
    if (!disabler) {
      disabler = std::make_shared<DisablerType>();
      bucket.disabler = disabler;
      ++bucket.version;
    }
    return disabler;
  }

  [[nodiscard]] static std::shared_ptr<Impl> Make(
      const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity,
      PgResponseCache::FillObserver fill_observer) {
    struct PrivateConstructorAccessor : public Impl {
      PrivateConstructorAccessor(
          const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity,
          PgResponseCache::FillObserver fill_observer)
          : Impl(parent_mem_tracker, metric_entity, std::move(fill_observer)) {}
    };
    auto result = std::make_shared<PrivateConstructorAccessor>(
        parent_mem_tracker, metric_entity, std::move(fill_observer));
    result->metric_context_.mem_tracker().AddGarbageCollector(
        std::shared_ptr<GarbageCollector>(result, result.get()));
    return result;
  }

 private:
  Impl(const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity,
       PgResponseCache::FillObserver fill_observer)
      : queries_(METRIC_pg_response_cache_queries.Instantiate(metric_entity)),
        hits_(METRIC_pg_response_cache_hits.Instantiate(metric_entity)),
        lookup_only_hits_(
            METRIC_pg_response_cache_lookup_only_hits.Instantiate(metric_entity)),
        lookup_only_misses_(
            METRIC_pg_response_cache_lookup_only_misses.Instantiate(metric_entity)),
        send_away_asks_(
            METRIC_pg_response_cache_send_away_asks.Instantiate(metric_entity)),
        send_aways_(
            METRIC_pg_response_cache_send_aways.Instantiate(metric_entity)),
        renew_soft_(METRIC_pg_response_cache_renew_soft.Instantiate(metric_entity)),
        renew_hard_(METRIC_pg_response_cache_renew_hard.Instantiate(metric_entity)),
        gc_calls_(METRIC_pg_response_cache_gc_calls.Instantiate(metric_entity)),
        disable_calls_(METRIC_pg_response_cache_disable_calls.Instantiate(metric_entity)),
        entries_removed_by_gc_(
            METRIC_pg_response_cache_entries_removed_by_gc.Instantiate(metric_entity)),
        metric_context_(
            GetCacheMemoryTracker(parent_mem_tracker),
            METRIC_pg_response_cache_entries.Instantiate(metric_entity, 0)),
        entries_(FLAGS_pg_response_cache_capacity),
        key_group_buckets_(FLAGS_pg_response_cache_num_key_group_bucket),
        fill_observer_(std::move(fill_observer))
  {}

  void CollectGarbage(size_t required) override {
    size_t entries_removed = 0;
    {
      std::lock_guard lock(mutex_);
      const auto end = entries_.end();
      auto i = std::make_reverse_iterator(end);
      const auto rend = std::make_reverse_iterator(entries_.begin());
      for (size_t free_amount = 0; free_amount < required && i != rend; ++i) {
        const auto& entry = *i;
        free_amount += entry.value.Consumption();
      }
      entries_removed = std::distance(i.base(), end);
      entries_.erase(i.base(), end);
    }
    VLOG(1) << entries_removed <<  " entries removed during GC for " << required << " bytes";
    gc_calls_->Increment();
    entries_removed_by_gc_->IncrementBy(entries_removed);
  }

  [[nodiscard]] bool IsRenewRequired(
      CoarseTimePoint creation_time, CoarseTimePoint now,
      const LWPgPerformOptionsPB::LWCachingInfoPB& cache_info, Counter** renew_metric) const {
    if (!cache_info.has_lifetime_threshold_ms()) {
      return false;
    }
    const auto threshold = cache_info.lifetime_threshold_ms().value();
    if (!threshold) {
      *renew_metric = renew_hard_.get();
      return true;
    }

    if (creation_time < now - std::chrono::milliseconds(threshold)) {
      *renew_metric = renew_soft_.get();
      return true;
    }

    return false;
  }

  scoped_refptr<Counter> queries_;
  scoped_refptr<Counter> hits_;
  scoped_refptr<Counter> lookup_only_hits_;
  scoped_refptr<Counter> lookup_only_misses_;
  scoped_refptr<Counter> send_away_asks_;
  scoped_refptr<Counter> send_aways_;
  scoped_refptr<Counter> renew_soft_;
  scoped_refptr<Counter> renew_hard_;
  scoped_refptr<Counter> gc_calls_;
  scoped_refptr<Counter> disable_calls_;
  scoped_refptr<Counter> entries_removed_by_gc_;
  MetricContext metric_context_;
  std::mutex mutex_;
  LRUCache<
      Entry,
      boost::multi_index::member<Entry, Key, &Entry::key>
  > entries_ GUARDED_BY(mutex_);

  std::vector<KeyGroupBucket> key_group_buckets_ GUARDED_BY(mutex_);
  // Fills under way, by key group and by the session performing them. The FillToken the filling
  // caller holds takes the count and drops it, so an abandoned fill clears itself.
  std::unordered_map<KeyGroup, std::unordered_map<uint64_t, size_t>> fills_in_flight_
      GUARDED_BY(mutex_);

  const PgResponseCache::FillObserver fill_observer_;

  DISALLOW_COPY_AND_ASSIGN(Impl);
};

PgResponseCache::PgResponseCache(
    const std::shared_ptr<MemTracker>& parent_mem_tracker,
    MetricEntity* metric_entity,
    FillObserver fill_observer)
    : impl_(Impl::Make(parent_mem_tracker, metric_entity, std::move(fill_observer))) {
}

PgResponseCache::~PgResponseCache() = default;

Result<PgResponseCache::Setter> PgResponseCache::Get(
    LWPgPerformOptionsPB::LWCachingInfoPB* cache_info,
    CoarseTimePoint deadline,
    const PgResponseCacheWaiterPtr& waiter,
    uint64_t session_id, FillTokenPtr* token) {
  return impl_->Get(cache_info, deadline, waiter, session_id, token);
}

PgResponseCache::FillToken::FillToken(Impl* impl, KeyGroup group, uint64_t session_id)
    : impl_(impl), group_(group), session_id_(session_id) {
  impl->NoteFillStarted(group_, session_id_);
}

PgResponseCache::FillToken::~FillToken() {
  Release();
}

void PgResponseCache::FillToken::Release() {
  // Two threads can reach this for one token. The setter releases the round that completes a
  // prefetch from QueryData::FlushDone, which runs on a client thread with the session lock
  // already dropped, while the session releases the same token under that lock -- and parallel
  // workers share a session, so they share its token. Whoever claims the latch owes the single
  // NoteFillFinished this token carries and the other does nothing, so a late release can only
  // remove the count it put there itself, never one a newer token has taken since.
  if (!released_.exchange(true, std::memory_order_acq_rel)) {
    impl_->NoteFillFinished(group_, session_id_);
  }
}

PgResponseCache::Disabler PgResponseCache::Disable(KeyGroup key_group) {
  return impl_->Disable(key_group);
}

PgResponseCache::Response::Response(
    const PgPerformResponseMsg& response_, std::vector<RefCntSlice> rows_data_)
    : response(rpc::SharedMessage<PgPerformResponseMsg>(response_)),
      rows_data(std::move(rows_data_)) {
  DCHECK_EQ(response->responses().size(), rows_data.size());
}

} // namespace yb::tserver
