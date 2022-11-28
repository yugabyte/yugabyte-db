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
#include <mutex>
#include <future>
#include <utility>

#include <boost/multi_index/member.hpp>

#include "yb/client/yb_op.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/pg_client.pb.h"

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
                      "PgClientService Response Cache QUeries",
                      yb::MetricUnit::kCacheQueries,
                      "Total number of queries to PgClientService response cache");
DEFINE_NON_RUNTIME_uint64(
    pg_response_cache_capacity, 1024, "PgClientService response cache capacity.");

namespace yb {
namespace tserver {

namespace {

YB_DEFINE_ENUM(DataState, (kInitializing)(kInitialized)(kFailed));

class Data {
 public:
  explicit Data(const CoarseTimePoint& deadline)
      : state_(DataState::kInitializing),
        deadline_(deadline),
        future_(promise_.get_future()) {}

  const PgResponseCache::Response& Get() const {
    return future_.get();
  }

  void Set(PgResponseCache::Response&& value, IsFailure is_failure) {
    auto expected = DataState::kInitializing;
    auto exchanged = state_.compare_exchange_strong(
        expected,
        is_failure ? DataState::kFailed : DataState::kInitialized,
        std::memory_order_acq_rel);
    if (!exchanged) {
      LOG(DFATAL) << "Unexpected state " << expected;
      return;
    }
    promise_.set_value(std::move(value));
  }

  bool IsValid() const {
    const auto state = state_.load(std::memory_order_acquire);
    switch (state) {
      case DataState::kInitializing: return CoarseMonoClock::Now() < deadline_;
      case DataState::kInitialized: return true;
      case DataState::kFailed: return false;
    }
    FATAL_INVALID_ENUM_VALUE(DataState, state);
  }

 private:
  std::atomic<DataState> state_;
  const CoarseTimePoint deadline_;
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
                  rpc::RpcContext* context,
                  const PgResponseCache::Response& value) {
  *response = value.response;
  auto rows_data_it = value.rows_data.begin();
  for (auto& op : *response->mutable_responses()) {
    if (op.has_rows_data_sidecar()) {
      context->StartRpcSidecar().Append(rows_data_it->AsSlice());
      op.set_rows_data_sidecar(narrow_cast<int>(context->CompleteRpcSidecar()));
    }
    ++rows_data_it;
  }
  context->RespondSuccess();
}

} // namespace

class PgResponseCache::Impl {
  auto DoGetEntry(std::string&& key, const CoarseTimePoint& deadline) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = *entries_.emplace(std::move(key));
    bool loading_required = false;
    if (!entry.data || !entry.data->IsValid()) {
      const_cast<Entry&>(entry).data = std::make_shared<Data>(deadline);
      loading_required = true;
    }
    return std::make_pair(entry.data, loading_required);
  }

 public:
  explicit Impl(MetricEntity* metric_entity)
      : entries_(FLAGS_pg_response_cache_capacity),
        queries_(METRIC_pg_response_cache_queries.Instantiate(metric_entity)),
        hits_(METRIC_pg_response_cache_hits.Instantiate(metric_entity)) {
  }

  PgResponseCache::Setter Get(
      std::string&& cache_key, PgPerformResponsePB* response, rpc::RpcContext* context) {
    auto[data, loading_required] = DoGetEntry(std::move(cache_key), context->GetClientDeadline());
    IncrementCounter(queries_);
    if (!loading_required) {
      IncrementCounter(hits_);
      FillResponse(response, context, data->Get());
      return PgResponseCache::Setter();
    }
    return [empty_data = std::move(data)](Response&& response, IsFailure is_failure) {
      empty_data->Set(std::move(response), is_failure);
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
};

PgResponseCache::PgResponseCache(MetricEntity* metric_entity)
    : impl_(new Impl(metric_entity)) {
}

PgResponseCache::~PgResponseCache() = default;

PgResponseCache::Setter PgResponseCache::Get(
    std::string&& cache_key, PgPerformResponsePB* response, rpc::RpcContext* context) {
  return impl_->Get(std::move(cache_key), response, context);
}

} // namespace tserver
} // namespace yb
