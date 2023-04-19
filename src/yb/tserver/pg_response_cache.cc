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

#include <future>
#include <mutex>
#include <utility>

#include <boost/multi_index/member.hpp>

#include "yb/client/yb_op.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/sidecars.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/lru_cache.h"
#include "yb/util/metrics.h"
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

DEFINE_NON_RUNTIME_uint64(
    pg_response_cache_capacity, 1024, "PgClientService response cache capacity.");

DEFINE_test_flag(uint64, pg_response_cache_catalog_read_time_usec, 0,
                 "Value to substitute original catalog_read_time in cached responses");

namespace yb {
namespace tserver {

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

class Data {
 public:
  explicit Data(const CoarseTimePoint& creation_time, const CoarseTimePoint& readiness_deadline)
      : creation_time_(creation_time),
        readiness_deadline_(readiness_deadline),
        future_(promise_.get_future()) {}

  Result<const PgResponseCache::Response&> Get(const CoarseTimePoint& deadline) const {
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
    promise_.set_value(std::move(value));
  }

  [[nodiscard]] bool IsValid(const CoarseTimePoint& now) const {
    return IsReady(future_) ? future_.get().is_ok_status : now < readiness_deadline_;
  }

  [[nodiscard]] const CoarseTimePoint& creation_time() const {
    return creation_time_;
  }

 private:
  const CoarseTimePoint creation_time_;
  const CoarseTimePoint readiness_deadline_;
  std::promise<PgResponseCache::Response> promise_;
  std::shared_future<PgResponseCache::Response> future_;
};

struct Entry {
  explicit Entry(std::string&& key_)
      : key(std::move(key_)) {}

  std::string key;
  std::shared_ptr<Data> data;
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

} // namespace

class PgResponseCache::Impl {
  [[nodiscard]] auto DoGetEntry(
      PgPerformOptionsPB::CachingInfoPB* cache_info, const CoarseTimePoint& deadline) {
    auto now = CoarseMonoClock::Now();
    std::lock_guard lock(mutex_);
    const auto& data = entries_.emplace(std::move(*cache_info->mutable_key()))->data;
    bool loading_required = false;
    if (!data ||
        !data->IsValid(now) ||
        (cache_info->has_lifetime_threshold_ms() &&
         RenewRequired(*data, now, cache_info->lifetime_threshold_ms().value()))) {
      const_cast<std::shared_ptr<Data>&>(data) = std::make_shared<Data>(now, deadline);
      loading_required = true;
    }
    return std::make_pair(data, loading_required);
  }

 public:
  explicit Impl(MetricEntity* metric_entity)
      : entries_(FLAGS_pg_response_cache_capacity),
        queries_(METRIC_pg_response_cache_queries.Instantiate(metric_entity)),
        hits_(METRIC_pg_response_cache_hits.Instantiate(metric_entity)),
        renew_soft_(METRIC_pg_response_cache_renew_soft.Instantiate(metric_entity)),
        renew_hard_(METRIC_pg_response_cache_renew_hard.Instantiate(metric_entity)) {
  }

  [[nodiscard]] bool RenewRequired(
      const Data& data, const CoarseTimePoint& now, uint32_t lifetime_threshold_ms) {
      if (lifetime_threshold_ms == 0) {
        IncrementCounter(renew_hard_);
        return true;
      }

      if (data.creation_time() < now - std::chrono::milliseconds(lifetime_threshold_ms)) {
        IncrementCounter(renew_soft_);
        return true;
      }
      return false;
  }

  Result<PgResponseCache::Setter> Get(
      PgPerformOptionsPB::CachingInfoPB* cache_info, PgPerformResponsePB* response,
      rpc::Sidecars* sidecars, CoarseTimePoint deadline) {
    auto [data, loading_required] = DoGetEntry(cache_info, deadline);
    IncrementCounter(queries_);
    if (!loading_required) {
      IncrementCounter(hits_);
      FillResponse(response, sidecars, VERIFY_RESULT_REF(data->Get(deadline)));
      return PgResponseCache::Setter();
    }
    return [empty_data = std::move(data)](Response&& response) {
      empty_data->Set(std::move(response));
    };
  }

 private:
  std::mutex mutex_;
  LRUCache<
      Entry,
      boost::multi_index::member<Entry, std::string, &Entry::key>
  > entries_ GUARDED_BY(mutex_);
  scoped_refptr<Counter> queries_;
  scoped_refptr<Counter> hits_;
  scoped_refptr<Counter> renew_soft_;
  scoped_refptr<Counter> renew_hard_;
};

PgResponseCache::PgResponseCache(MetricEntity* metric_entity)
    : impl_(new Impl(metric_entity)) {
}

PgResponseCache::~PgResponseCache() = default;

Result<PgResponseCache::Setter> PgResponseCache::Get(
    PgPerformOptionsPB::CachingInfoPB* cache_info,
    PgPerformResponsePB* response, rpc::Sidecars* sidecars,
    CoarseTimePoint deadline) {
  return impl_->Get(cache_info, response, sidecars, deadline);
}

} // namespace tserver
} // namespace yb
