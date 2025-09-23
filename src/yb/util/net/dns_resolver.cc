// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/net/dns_resolver.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "yb/util/logging.h"

#include "yb/util/metrics.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/flags.h"
#include "yb/util/shared_lock.h"

using namespace std::literals;

DEFINE_RUNTIME_int64(dns_cache_expiration_ms, 60000,
    "Time to store DNS resolution results in cache.");

DEFINE_RUNTIME_int64(dns_cache_failure_expiration_ms, 2000,
    "Time before DNS resolution retry in case of failure.");

METRIC_DEFINE_event_stats(
  server, dns_resolve_latency,
  "DNS Resolve latency",
  yb::MetricUnit::kMicroseconds,
  "Microseconds spent resolving DNS requests");

namespace yb {

namespace {


Result<IpAddress> PickResolvedAddress(
    const std::string& host, const boost::system::error_code& error,
    const ResolverResults& entries) {
  if (error) {
    return STATUS_FORMAT(NetworkError, "Resolve failed $0: $1", host, error.message());
  }
  std::vector<IpAddress> addresses;
  for (const auto& entry : entries) {
    addresses.push_back(entry.endpoint().address());
    VLOG(3) << "Resolved address " << entry.endpoint().address().to_string()
            << " for host " << host;
  }
  FilterAddresses(FLAGS_net_address_filter, &addresses);
  if (addresses.empty()) {
    return STATUS_FORMAT(NetworkError, "No endpoints resolved for: $0", host);
  }
  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
  if (addresses.size() > 1) {
    LOG(WARNING) << "Peer address '" << host << "' "
                 << "resolves to " << yb::ToString(addresses) << " different addresses. Using "
                 << yb::ToString(addresses.front());
  }

  VLOG(3) << "Returned address " << addresses[0].to_string() << " for host "
          << host;
  return addresses.front();
}

} // namespace

class DnsResolver::Impl {
 public:
  Impl(IoService* io_service, const scoped_refptr<MetricEntity>& metric_entity)
      : io_service_(*io_service), resolver_(*io_service) {
    if (metric_entity) {
      metric_ = METRIC_dns_resolve_latency.Instantiate(metric_entity);
    }
  }

  std::shared_future<Result<IpAddress>> ResolveFuture(const std::string& host) {
    return ObtainEntry(host)->DoResolve(
        host, /* callback= */ nullptr, &io_service_, &resolver_, metric_);
  }

  void AsyncResolve(const std::string& host, const AsyncResolveCallback& callback) {
    ObtainEntry(host)->DoResolve(host, &callback, &io_service_, &resolver_, metric_);
  }

 private:
  using Resolver = boost::asio::ip::basic_resolver<boost::asio::ip::tcp>;

  struct CacheEntry {
    std::mutex mutex;
    CoarseTimePoint expiration GUARDED_BY(mutex) = CoarseTimePoint::min();
    std::shared_future<Result<IpAddress>> future GUARDED_BY(mutex);
    std::vector<AsyncResolveCallback> waiters GUARDED_BY(mutex);
    bool has_resolved_address GUARDED_BY(mutex) = false;

    void SetResult(
        const Result<IpAddress>& result,
        std::promise<Result<IpAddress>>* promise) EXCLUDES(mutex) {
      try {
        promise->set_value(result);
      } catch (std::future_error& error) {
        return;
      }

      decltype(waiters) to_notify;
      {
        std::lock_guard lock(mutex);
        uint64_t expiration_ms;
        if (result.ok()) {
          // If address was already resolved we did not fetch future from promise on resolution
          // start so do it now to update existing resolved address.
          if (has_resolved_address) {
            future = promise->get_future().share();
          } else {
            has_resolved_address = true;
          }
          expiration_ms = FLAGS_dns_cache_expiration_ms;
        } else {
          expiration_ms = FLAGS_dns_cache_failure_expiration_ms;
        }
        expiration = CoarseMonoClock::now() + expiration_ms * 1ms;
        waiters.swap(to_notify);
      }
      for (const auto& waiter : to_notify) {
        waiter(result);
      }
    }

    std::shared_future<Result<IpAddress>> DoResolve(
        const std::string& host, const AsyncResolveCallback* callback, IoService* io_service,
        Resolver* resolver, const scoped_refptr<EventStats>& metric) {
      std::shared_ptr<std::promise<Result<IpAddress>>> promise;
      std::shared_future<Result<IpAddress>> result;
      {
        std::lock_guard lock(mutex);
        promise = StartResolve(host);
        result = future;
        if (callback && expiration == CoarseTimePoint::max() && !has_resolved_address) {
          // Resolve is in progress by a different caller.
          waiters.push_back(*callback);
          callback = nullptr;
        }
      }

      if (callback) {
        (*callback)(result.get());
      }

      if (promise) {
        static const std::string kService = "";
        resolver->async_resolve(
            Resolver::query(host, kService),
            [this, host, promise, metric, start_time = CoarseMonoClock::Now()](
                const boost::system::error_code& error,
                const Resolver::results_type& entries) mutable {
          // Unfortunately there is no safe way to set promise value from 2 different threads, w/o
          // catching exception in case of concurrency.
          const auto now = CoarseMonoClock::Now();
          if (metric) {
            metric->Increment(ToMicroseconds(now - start_time));
          }
          if (start_time + 250ms < now) {
            YB_LOG_EVERY_N_SECS(WARNING, 5)
                << "Long time to resolse DNS for " << host << ": " << MonoDelta(now - start_time)
                << THROTTLE_MSG;
          }
          SetResult(PickResolvedAddress(host, error, entries), promise.get());
        });

        if (io_service->stopped()) {
          SetResult(STATUS(Aborted, "Messenger already stopped"), promise.get());
        }
      }

      return result;
    }

    std::shared_ptr<std::promise<Result<IpAddress>>> StartResolve(
        const std::string& host) REQUIRES(mutex) {
      if (expiration >= CoarseMonoClock::now()) {
        return nullptr;
      }

      auto promise = std::make_shared<std::promise<Result<IpAddress>>>();
      if (!has_resolved_address) {
        future = promise->get_future().share();
      }

      auto address = TryFastResolve(host);
      if (address) {
        expiration = CoarseTimePoint::max() - 1ms;
        promise->set_value(*address);
        return nullptr;
      } else {
        expiration = CoarseTimePoint::max();
      }

      return promise;
    }
  };

  CacheEntry* ObtainEntry(const std::string& host) {
    {
      SharedLock lock(mutex_);
      auto it = cache_.find(host);
      if (it != cache_.end()) {
        return &it->second;
      }
    }

    std::lock_guard lock(mutex_);
    return &cache_[host];
  }

  IoService& io_service_;
  Resolver resolver_;
  scoped_refptr<EventStats> metric_;
  std::shared_timed_mutex mutex_;
  std::unordered_map<std::string, CacheEntry> cache_;
};

DnsResolver::DnsResolver(IoService* io_service, const scoped_refptr<MetricEntity>& metric_entity)
    : impl_(new Impl(io_service, metric_entity)) {
}

DnsResolver::~DnsResolver() {
}

namespace {

thread_local EventStats* active_metric_ = nullptr;

} // anonymous namespace

ScopedDnsTracker::ScopedDnsTracker(const scoped_refptr<EventStats>& metric)
    : old_metric_(active_metric()), metric_(metric) {
  active_metric_ = metric.get();
}

ScopedDnsTracker::~ScopedDnsTracker() {
  DCHECK_EQ(metric_.get(), active_metric());
  active_metric_ = old_metric_;
}

EventStats* ScopedDnsTracker::active_metric() {
  return active_metric_;
}

std::shared_future<Result<IpAddress>> DnsResolver::ResolveFuture(const std::string& host) {
  return impl_->ResolveFuture(host);
}

void DnsResolver::AsyncResolve(const std::string& host, const AsyncResolveCallback& callback) {
  impl_->AsyncResolve(host, callback);
}

Result<IpAddress> DnsResolver::Resolve(const std::string& host) {
  return ResolveFuture(host).get();
}

} // namespace yb
