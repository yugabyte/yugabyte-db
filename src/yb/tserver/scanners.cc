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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/tserver/scanners.h"

#include <mutex>

#include <boost/bind.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <gflags/gflags.h>
#include "yb/common/iterator.h"
#include "yb/common/scan_spec.h"
#include "yb/gutil/hash/string_hash.h"
#include "yb/gutil/map-util.h"
#include "yb/tserver/scanner_metrics.h"
#include "yb/util/flag_tags.h"
#include "yb/util/thread.h"
#include "yb/util/metrics.h"

DEFINE_int32(scanner_ttl_ms, 60000,
             "Number of milliseconds of inactivity allowed for a scanner"
             "before it may be expired");
TAG_FLAG(scanner_ttl_ms, advanced);
DEFINE_int32(scanner_gc_check_interval_us, 5 * 1000L *1000L, // 5 seconds
             "Number of microseconds in the interval at which we remove expired scanners");
TAG_FLAG(scanner_ttl_ms, hidden);

// TODO: would be better to scope this at a tablet level instead of
// server level.
METRIC_DEFINE_gauge_size(server, active_scanners,
                         "Active Scanners",
                         yb::MetricUnit::kScanners,
                         "Number of scanners that are currently active");

namespace yb {

using tablet::TabletPeer;

namespace tserver {

ScannerManager::ScannerManager(const scoped_refptr<MetricEntity>& metric_entity)
  : shutdown_(false) {
  if (metric_entity) {
    metrics_.reset(new ScannerMetrics(metric_entity));
    METRIC_active_scanners.InstantiateFunctionGauge(
        metric_entity, Bind(&ScannerManager::CountActiveScanners,
                               Unretained(this)))
      ->AutoDetach(&metric_detacher_);
  }
  for (size_t i = 0; i < kNumScannerMapStripes; i++) {
    scanner_maps_.push_back(new ScannerMapStripe());
  }
}

ScannerManager::~ScannerManager() {
  {
    boost::lock_guard<boost::mutex> l(shutdown_lock_);
    shutdown_ = true;
    shutdown_cv_.notify_all();
  }
  if (removal_thread_.get() != nullptr) {
    CHECK_OK(ThreadJoiner(removal_thread_.get()).Join());
  }
  STLDeleteElements(&scanner_maps_);
}

Status ScannerManager::StartRemovalThread() {
  RETURN_NOT_OK(Thread::Create("scanners", "removal_thread",
                               &ScannerManager::RunRemovalThread, this,
                               &removal_thread_));
  return Status::OK();
}

void ScannerManager::RunRemovalThread() {
  while (true) {
    // Loop until we are shutdown.
    {
      boost::unique_lock<boost::mutex> l(shutdown_lock_);
      if (shutdown_) {
        return;
      }
      boost::system_time wtime = boost::get_system_time() +
          boost::posix_time::microseconds(FLAGS_scanner_gc_check_interval_us);
      shutdown_cv_.timed_wait(l, wtime);
    }
    RemoveExpiredScanners();
  }
}

ScannerManager::ScannerMapStripe& ScannerManager::GetStripeByScannerId(const string& scanner_id) {
  size_t slot = HashStringThoroughly(scanner_id.data(), scanner_id.size()) % kNumScannerMapStripes;
  return *scanner_maps_[slot];
}

void ScannerManager::NewScanner(const scoped_refptr<TabletPeer>& tablet_peer,
                                const std::string& requestor_string,
                                SharedScanner* scanner) {
  // Keep trying to generate a unique ID until we get one.
  bool success = false;
  while (!success) {
    // TODO(security): are these UUIDs predictable? If so, we should
    // probably generate random numbers instead, since we can safely
    // just retry until we avoid a collission.
    string id = oid_generator_.Next();
    scanner->reset(new Scanner(id, tablet_peer, requestor_string, metrics_.get()));

    ScannerMapStripe& stripe = GetStripeByScannerId(id);
    std::lock_guard<boost::shared_mutex> l(stripe.lock_);
    success = InsertIfNotPresent(&stripe.scanners_by_id_, id, *scanner);
  }
}

bool ScannerManager::LookupScanner(const string& scanner_id, SharedScanner* scanner) {
  ScannerMapStripe& stripe = GetStripeByScannerId(scanner_id);
  boost::shared_lock<boost::shared_mutex> l(stripe.lock_);
  return FindCopy(stripe.scanners_by_id_, scanner_id, scanner);
}

bool ScannerManager::UnregisterScanner(const string& scanner_id) {
  ScannerMapStripe& stripe = GetStripeByScannerId(scanner_id);
  std::lock_guard<boost::shared_mutex> l(stripe.lock_);
  return stripe.scanners_by_id_.erase(scanner_id) > 0;
}

size_t ScannerManager::CountActiveScanners() const {
  size_t total = 0;
  for (const ScannerMapStripe* e : scanner_maps_) {
    boost::shared_lock<boost::shared_mutex> l(e->lock_);
    total += e->scanners_by_id_.size();
  }
  return total;
}

void ScannerManager::ListScanners(std::vector<SharedScanner>* scanners) {
  for (const ScannerMapStripe* stripe : scanner_maps_) {
    boost::shared_lock<boost::shared_mutex> l(stripe->lock_);
    for (const ScannerMapEntry& se : stripe->scanners_by_id_) {
      scanners->push_back(se.second);
    }
  }
}

void ScannerManager::RemoveExpiredScanners() {
  auto scanner_ttl = std::chrono::milliseconds(FLAGS_scanner_ttl_ms);

  for (ScannerMapStripe* stripe : scanner_maps_) {
    std::lock_guard<boost::shared_mutex> l(stripe->lock_);
    for (auto it = stripe->scanners_by_id_.begin(); it != stripe->scanners_by_id_.end();) {
      SharedScanner& scanner = it->second;
      auto time_live = scanner->TimeSinceLastAccess(CoarseMonoClock::Now());
      if (time_live >= scanner_ttl) {
        // TODO: once we have a metric for the number of scanners expired, make this a
        // VLOG(1).
        LOG(INFO) << "Expiring scanner id: " << it->first << ", of tablet " << scanner->tablet_id()
                  << ", after " << ToMicroseconds(time_live)
                  << " us of inactivity, which is > TTL ("
                  << ToMicroseconds(scanner_ttl) << " us).";
        it = stripe->scanners_by_id_.erase(it);
        if (metrics_) {
          metrics_->scanners_expired->Increment();
        }
      } else {
        ++it;
      }
    }
  }
}

Scanner::Scanner(string id, const scoped_refptr<TabletPeer>& tablet_peer,
                 string requestor_string, ScannerMetrics* metrics)
    : id_(std::move(id)),
      tablet_peer_(tablet_peer),
      requestor_string_(std::move(requestor_string)),
      call_seq_id_(0),
      start_time_(CoarseMonoClock::Now()),
      metrics_(metrics),
      arena_(1024, 1024 * 1024) {
  UpdateAccessTime();
}

Scanner::~Scanner() {
  if (metrics_) {
    metrics_->SubmitScannerDuration(start_time_);
  }
}

void Scanner::UpdateAccessTime() {
  auto now = CoarseMonoClock::Now();
  std::lock_guard<simple_spinlock> l(lock_);
  last_access_time_ = now;
}

void Scanner::Init(gscoped_ptr<RowwiseIterator> iter,
                   gscoped_ptr<ScanSpec> spec) {
  std::lock_guard<simple_spinlock> l(lock_);
  CHECK(!iter_) << "Already initialized";
  iter_.reset(iter.release());
  spec_.reset(spec.release());
}

const ScanSpec& Scanner::spec() const {
  return *spec_;
}

void Scanner::GetIteratorStats(vector<IteratorStats>* stats) const {
  iter_->GetIteratorStats(stats);
}


} // namespace tserver
} // namespace yb
