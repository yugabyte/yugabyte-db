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
    "Percentage of total available memory to use by the PgClientService response cache. "
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

class Data {
 public:
  Data(uint64_t version,
       std::weak_ptr<MetricUpdater> metric_updater,
       CoarseTimePoint creation_time,
       CoarseTimePoint readiness_deadline)
      : metric_updater_(std::move(metric_updater)),
        version_(version),
        creation_time_(creation_time),
        readiness_deadline_(readiness_deadline),
        future_(promise_.get_future()) {}

  Result<const PgResponseCache::Response&> Get(CoarseTimePoint deadline) const {
    const auto& actual_deadline = std::min(deadline, readiness_deadline_);
    if (future_.wait_until(actual_deadline) != std::future_status::timeout) {
      return future_.get();
    }
    return STATUS(TimedOut, "Timeout on getting response from the cache");
  }

  void Set(PgResponseCache::Response&& value) {
    if (PREDICT_FALSE(FLAGS_TEST_pg_response_cache_catalog_read_time_usec > 0) &&
        value.response.has_catalog_read_time()) {
      TEST_UpdateCatalogReadTime(
          &value.response,
          ReadHybridTime::SingleTime(HybridTime::FromMicros(
              FLAGS_TEST_pg_response_cache_catalog_read_time_usec)));
    }
    size_t sz = 0;
    if (IsOk(value)) {
      for (const auto& data : value.rows_data) {
        sz += data.size();
      }
    }
    promise_.set_value(std::move(value));
    if (sz) {
      auto updater = metric_updater_.lock();
      if (updater) {
        updater->Consume(sz);
      }
    }
  }

  [[nodiscard]] bool IsValid(CoarseTimePoint now, uint64_t version) const {
    return version == version_ &&
           (IsReady(future_) ? IsOk(future_.get()) : now < readiness_deadline_);
  }

  [[nodiscard]] CoarseTimePoint creation_time() const {
    return creation_time_;
  }

 private:
  std::weak_ptr<MetricUpdater> metric_updater_;
  const uint64_t version_;
  const CoarseTimePoint creation_time_;
  const CoarseTimePoint readiness_deadline_;
  std::promise<PgResponseCache::Response> promise_;
  std::shared_future<PgResponseCache::Response> future_;

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

void FillResponse(PgPerformResponsePB* response,
                  rpc::Sidecars* sidecars,
                  const PgResponseCache::Response& value) {
  *response = value.response;
  auto rows_data_it = value.rows_data.begin();
  for (auto& op : *response->mutable_responses()) {
    if (op.has_rows_data_sidecar()) {
      sidecars->Start().Append(rows_data_it->AsSlice());
      op.set_rows_data_sidecar(narrow_cast<int>(sidecars->Complete()));
    } else {
      DCHECK(!*rows_data_it);
    }
    ++rows_data_it;
  }
}

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

  [[nodiscard]] auto GetEntryData(
      PgPerformOptionsPB::CachingInfoPB* cache_info, CoarseTimePoint deadline) EXCLUDES(mutex_) {
    auto now = CoarseMonoClock::Now();
    Counter* renew_metric = nullptr;
    auto metric_updater = ScopeExit([&renew_metric] {
      if (renew_metric) {
        renew_metric->Increment();
      }
    });
    std::lock_guard lock(mutex_);
    std::shared_ptr<Data> data;
    auto loading_required = false;
    const auto group = cache_info->key_group();
    const auto& bucket = GetKeyGroupBucket(group);
    if (!bucket.disabler.lock()) {
      auto actual_version = bucket.version;
      const auto& value = entries_.emplace(
          group, std::move(*cache_info->mutable_key_value()))->value;
      data = value.data();
      if (!data ||
          !data->IsValid(now, actual_version) ||
          IsRenewRequired(data->creation_time(), now, *cache_info, &renew_metric)) {
        VLOG(5) << "(Re)Building element group=" << group << " actual_version=" << actual_version;
        const_cast<Value&>(value) = Value(actual_version, &metric_context_, now, deadline);
        loading_required = true;
        data = value.data();
      }
    } else {
      loading_required = true;
    }
    return std::make_pair(std::move(data), loading_required);
  }

 public:
  Result<Setter> Get(
      PgPerformOptionsPB::CachingInfoPB* cache_info, PgPerformResponsePB* response,
      rpc::Sidecars* sidecars, CoarseTimePoint deadline) EXCLUDES(mutex_) {
    auto [data, loading_required] = GetEntryData(cache_info, deadline);
    queries_->Increment();
    if (!loading_required) {
      hits_->Increment();
      FillResponse(response, sidecars, VERIFY_RESULT_REF(data->Get(deadline)));
      return Setter();
    }
    if (!data) {
      return [](Response&& response) {};
    }
    return [empty_data = std::move(data)](Response&& response) {
      empty_data->Set(std::move(response));
    };
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
      const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity) {
    struct PrivateConstructorAccessor : public Impl {
      PrivateConstructorAccessor(
          const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity)
          : Impl(parent_mem_tracker, metric_entity) {}
    };
    auto result = std::make_shared<PrivateConstructorAccessor>(parent_mem_tracker, metric_entity);
    result->metric_context_.mem_tracker().AddGarbageCollector(
        std::shared_ptr<GarbageCollector>(result, result.get()));
    return result;
  }

 private:
  Impl(const std::shared_ptr<MemTracker>& parent_mem_tracker, MetricEntity* metric_entity)
      : queries_(METRIC_pg_response_cache_queries.Instantiate(metric_entity)),
        hits_(METRIC_pg_response_cache_hits.Instantiate(metric_entity)),
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
        key_group_buckets_(FLAGS_pg_response_cache_num_key_group_bucket)
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

    const auto threshold = cache_info.has_lifetime_threshold_ms();
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

  DISALLOW_COPY_AND_ASSIGN(Impl);
};

PgResponseCache::PgResponseCache(
    const std::shared_ptr<MemTracker>& parent_mem_tracker,
    MetricEntity* metric_entity)
    : impl_(Impl::Make(parent_mem_tracker, metric_entity)) {
}

PgResponseCache::~PgResponseCache() = default;

Result<PgResponseCache::Setter> PgResponseCache::Get(
    PgPerformOptionsPB::CachingInfoPB* cache_info,
    PgPerformResponsePB* response, rpc::Sidecars* sidecars,
    CoarseTimePoint deadline) {
  return impl_->Get(cache_info, response, sidecars, deadline);
}

PgResponseCache::Disabler PgResponseCache::Disable(KeyGroup key_group) {
  return impl_->Disable(key_group);
}

} // namespace yb::tserver
