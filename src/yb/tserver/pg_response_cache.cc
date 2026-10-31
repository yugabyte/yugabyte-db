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

#include <atomic>
#include <future>
#include <mutex>

#include <boost/functional/hash.hpp>
#include <boost/multi_index/member.hpp>

#include "yb/client/yb_op.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/sidecars.h"

#include "yb/util/async_util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/lru_cache.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
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

DEFINE_NON_RUNTIME_uint64(
    pg_response_cache_capacity, 1024, "PgClientService response cache capacity.");

DEFINE_NON_RUNTIME_uint64(pg_response_cache_size_bytes, 0,
                          "Size in bytes of the PgClientService response cache. "
                          "0 value (default) means that cache size is not limited by this flag.");

DEFINE_NON_RUNTIME_uint32(
    pg_response_cache_size_percentage, 5,
    "Percentage of process' hard memory limit to use by the PgClientService response cache. "
    "Default value is 5, max is 100, min is 0 means that cache size is not limited by this flag.");


DEFINE_NON_RUNTIME_uint32(
    pg_response_cache_num_key_group_bucket, 512,
    "Number of buckets for key group values which are used as part of response cache key. "
    "Response cache can be disabled for particular key group, but actually it will be disabled for "
    "all key groups in same bucket");

DEFINE_test_flag(uint64, pg_response_cache_catalog_read_time_usec, 0,
                 "Value to substitute original catalog_read_time in cached responses");

namespace yb::tserver {
namespace {

void TEST_UpdateCatalogReadTime(
    PgPerformResponsePB* resp, const ReadHybridTime& new_catalog_read_time) {
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
  return !resp.response.has_status();
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
        value.response.has_catalog_read_time()) {
      TEST_UpdateCatalogReadTime(
          &value.response,
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
    VLOG(1) << "Fill complete for group " << group << " catalog version " << catalog_version
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

  [[nodiscard]] std::optional<const PgResponseCache::Response*> RegisterWaiter(
      const PgResponseCacheWaiterPtr& waiter) {
    std::lock_guard lock(mutex_);
    if (!response_) {
      if (!running_) {
        running_ = true;
        return std::nullopt;
      }
      waiters_.push_back(waiter);
      return nullptr;
    }
    return &*response_;
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
  Key(PgResponseCache::KeyGroup group, std::string&& key_value)
      : group(group), value(std::move(key_value)) {}

  friend bool operator==(const Key&, const Key&) = default;
};

inline size_t hash_value(const Key& key) {
  size_t value = 0;
  boost::hash_combine(value, key.group);
  boost::hash_range(value, key.value.begin(), key.value.end());
  return value;
}

struct Entry {
  Entry(PgResponseCache::KeyGroup group, std::string&& key_value)
      : key(group, std::move(key_value)) {}

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

class PgResponseCache::Impl : private GarbageCollector {
  [[nodiscard]] auto& GetKeyGroupBucket(KeyGroup group) REQUIRES(mutex_) {
    const auto sz = key_group_buckets_.size();
    DCHECK_EQ(sz, FLAGS_pg_response_cache_num_key_group_bucket);
    return key_group_buckets_[group % sz];
  }

  [[nodiscard]] std::shared_ptr<Data> GetEntryData(
      PgPerformOptionsPB::CachingInfoPB* cache_info, CoarseTimePoint deadline) EXCLUDES(mutex_) {
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
      PgPerformOptionsPB::CachingInfoPB* cache_info) EXCLUDES(mutex_) {
    const auto now = CoarseMonoClock::Now();
    std::lock_guard lock(mutex_);
    const auto group = cache_info->key_group();
    const auto& bucket = GetKeyGroupBucket(group);
    if (bucket.disabler.lock()) {
      return nullptr;
    }
    const auto i = entries_.find(Key(group, std::string(cache_info->key_value())));
    if (i == entries_.end()) {
      return nullptr;
    }
    const auto& data = i->value.data();
    return data && data->IsValid(now, bucket.version) ? data : nullptr;
  }

 public:
  Result<Setter> Get(
      PgPerformOptionsPB::CachingInfoPB* cache_info, CoarseTimePoint deadline,
      const PgResponseCacheWaiterPtr& waiter) EXCLUDES(mutex_) {
    if (cache_info->lookup_only()) {
      return LookupOnlyGet(cache_info, waiter);
    }
    auto data = GetEntryData(cache_info, deadline);
    queries_->Increment();
    if (!data) {
      return [](Response&& response) {};
    }
    auto response_opt = data->RegisterWaiter(waiter);
    // There are 3 outcomes here:
    // 1) !response_opt means that it is the first query to this data and it should be performed.
    // 2) *response_opt == nullptr means that query is running and waiter will be notified when
    //    response is ready.
    // 3) *response_opt != nullptr means that response is ready and we could send it.
    if (response_opt) {
      hits_->Increment();
      if (*response_opt) {
        waiter->Apply(**response_opt);
      }
      return Setter();
    }
    return [empty_data = std::move(data), observer = fill_observer_,
            group = cache_info->key_group(),
            version = cache_info->catalog_version(),
            completes_connection_prefetch =
                cache_info->completes_connection_prefetch()](Response&& response) {
      // A prefetch is paged. pggate keeps issuing reads while any operation still returns paging
      // state, and each round is cached under a key of its own, so an observer told about the
      // first round would learn of a version whose later rounds are not cached yet. The round
      // where nothing is left to page is the one after which this node holds all of them.
      // A failed round holds no rows and no paging state, so it must not be mistaken for the
      // last one. Computed before the move below, which empties the response.
      bool is_final_round = completes_connection_prefetch && IsOk(response);
      if (is_final_round) {
        for (const auto& op_response : response.response.responses()) {
          if (op_response.has_paging_state()) {
            is_final_round = false;
            break;
          }
        }
      }
      empty_data->Set(std::move(response), group, version);
      // After the entry is set, so an observer can rely on the entry being readable.
      if (observer && is_final_round) {
        observer(group, version);
      }
    };
  }

  // A lookup-only query answers whether a response is cached here and never fills the cache; its
  // caller reads from the master leader instead when nothing is. It must leave no entry behind on
  // a miss: an entry claimed for a fill that is not coming makes every later query on that key
  // wait out the RPC deadline.
  Result<Setter> LookupOnlyGet(
      PgPerformOptionsPB::CachingInfoPB* cache_info,
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
      const PgPerformOptionsPB::CachingInfoPB& cache_info, Counter** renew_metric) const {
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
    PgPerformOptionsPB::CachingInfoPB* cache_info,
    CoarseTimePoint deadline,
    const PgResponseCacheWaiterPtr& waiter) {
  return impl_->Get(cache_info, deadline, waiter);
}

PgResponseCache::Disabler PgResponseCache::Disable(KeyGroup key_group) {
  return impl_->Disable(key_group);
}

} // namespace yb::tserver
