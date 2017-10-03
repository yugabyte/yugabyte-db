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

#include "kudu/tablet/tablet_peer_mm_ops.h"

#include <algorithm>
#include <map>
#include <string>

#include <gflags/gflags.h>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/maintenance_manager.h"
#include "kudu/tablet/tablet_metrics.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"

DEFINE_int32(flush_threshold_mb, 64,
             "Size at which MemRowSet flushes are triggered. "
             "A MRS can still flush below this threshold if it if hasn't flushed in a while");
TAG_FLAG(flush_threshold_mb, experimental);

METRIC_DEFINE_gauge_uint32(tablet, log_gc_running,
                           "Log GCs Running",
                           kudu::MetricUnit::kOperations,
                           "Number of log GC operations currently running.");
METRIC_DEFINE_histogram(tablet, log_gc_duration,
                        "Log GC Duration",
                        kudu::MetricUnit::kMilliseconds,
                        "Time spent garbage collecting the logs.", 60000LU, 1);

namespace kudu {
namespace tablet {

using std::map;
using strings::Substitute;

// How long we wait before considering a time-based flush.
const double kFlushDueToTimeMs = 2 * 60 * 1000;
// Upper bound for how long it takes to reach "full perf improvement" in time-based flushing.
const double kFlushUpperBoundMs = 60 * 60 * 1000;

//
// FlushOpPerfImprovementPolicy.
//

void FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(MaintenanceOpStats* stats,
                                                              double elapsed_ms) {
  if (stats->ram_anchored() > FLAGS_flush_threshold_mb * 1024 * 1024) {
    // If we're over the user-specified flush threshold, then consider the perf
    // improvement to be 1 for every extra MB.  This produces perf_improvement results
    // which are much higher than any compaction would produce, and means that, when
    // there is an MRS over threshold, a flush will almost always be selected instead of
    // a compaction.  That's not necessarily a good thing, but in the absence of better
    // heuristics, it will do for now.
    double extra_mb =
        static_cast<double>(FLAGS_flush_threshold_mb - (stats->ram_anchored()) / (1024 * 1024));
    stats->set_perf_improvement(extra_mb);
  } else if (elapsed_ms > kFlushDueToTimeMs) {
    // Even if we aren't over the threshold, consider flushing if we haven't flushed
    // in a long time. But, don't give it a large perf_improvement score. We should
    // only do this if we really don't have much else to do, and if we've already waited a bit.
    // The following will give an improvement that's between 0.0 and 1.0, gradually growing
    // as 'elapsed_ms' approaches 'kFlushUpperBoundMs'.
    double perf = elapsed_ms / kFlushUpperBoundMs;
    if (perf > 1.0) {
      perf = 1.0;
    }
    stats->set_perf_improvement(perf);
  }
}

//
// FlushMRSOp.
//

void FlushMRSOp::UpdateStats(MaintenanceOpStats* stats) {
  boost::lock_guard<simple_spinlock> l(lock_);

  map<int64_t, int64_t> max_idx_to_segment_size;
  if (tablet_peer_->tablet()->MemRowSetEmpty() ||
      !tablet_peer_->GetMaxIndexesToSegmentSizeMap(&max_idx_to_segment_size).ok()) {
    return;
  }

  {
    boost::unique_lock<Semaphore> lock(tablet_peer_->tablet()->rowsets_flush_sem_,
                                       boost::defer_lock);
    stats->set_runnable(lock.try_lock());
  }

  stats->set_ram_anchored(tablet_peer_->tablet()->MemRowSetSize());
  stats->set_logs_retained_bytes(
      tablet_peer_->tablet()->MemRowSetLogRetentionSize(max_idx_to_segment_size));

  // TODO: use workload statistics here to find out how "hot" the tablet has
  // been in the last 5 minutes.
  FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(
      stats,
      time_since_flush_.elapsed().wall_millis());
}

bool FlushMRSOp::Prepare() {
  // Try to acquire the rowsets_flush_sem_.  If we can't, the Prepare step
  // fails.  This also implies that only one instance of FlushMRSOp can be
  // running at once.
  return tablet_peer_->tablet()->rowsets_flush_sem_.try_lock();
}

void FlushMRSOp::Perform() {
  CHECK(!tablet_peer_->tablet()->rowsets_flush_sem_.try_lock());

  tablet_peer_->tablet()->FlushUnlocked();

  {
    boost::lock_guard<simple_spinlock> l(lock_);
    time_since_flush_.start();
  }
  tablet_peer_->tablet()->rowsets_flush_sem_.unlock();
}

scoped_refptr<Histogram> FlushMRSOp::DurationHistogram() const {
  return tablet_peer_->tablet()->metrics()->flush_mrs_duration;
}

scoped_refptr<AtomicGauge<uint32_t> > FlushMRSOp::RunningGauge() const {
  return tablet_peer_->tablet()->metrics()->flush_mrs_running;
}

//
// FlushDeltaMemStoresOp.
//

void FlushDeltaMemStoresOp::UpdateStats(MaintenanceOpStats* stats) {
  boost::lock_guard<simple_spinlock> l(lock_);
  int64_t dms_size;
  int64_t retention_size;
  map<int64_t, int64_t> max_idx_to_segment_size;
  if (tablet_peer_->tablet()->DeltaMemRowSetEmpty() ||
      !tablet_peer_->GetMaxIndexesToSegmentSizeMap(&max_idx_to_segment_size).ok()) {
    return;
  }
  tablet_peer_->tablet()->GetInfoForBestDMSToFlush(max_idx_to_segment_size,
                                                   &dms_size, &retention_size);

  stats->set_ram_anchored(dms_size);
  stats->set_runnable(true);
  stats->set_logs_retained_bytes(retention_size);

  FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(
      stats,
      time_since_flush_.elapsed().wall_millis());
}

void FlushDeltaMemStoresOp::Perform() {
  map<int64_t, int64_t> max_idx_to_segment_size;
  if (!tablet_peer_->GetMaxIndexesToSegmentSizeMap(&max_idx_to_segment_size).ok()) {
    LOG(WARNING) << "Won't flush deltas since tablet shutting down: " << tablet_peer_->tablet_id();
    return;
  }
  WARN_NOT_OK(tablet_peer_->tablet()->FlushDMSWithHighestRetention(max_idx_to_segment_size),
                  Substitute("Failed to flush DMS on $0",
                             tablet_peer_->tablet()->tablet_id()));
  {
    boost::lock_guard<simple_spinlock> l(lock_);
    time_since_flush_.start();
  }
}

scoped_refptr<Histogram> FlushDeltaMemStoresOp::DurationHistogram() const {
  return tablet_peer_->tablet()->metrics()->flush_dms_duration;
}

scoped_refptr<AtomicGauge<uint32_t> > FlushDeltaMemStoresOp::RunningGauge() const {
  return tablet_peer_->tablet()->metrics()->flush_dms_running;
}

//
// LogGCOp.
//

LogGCOp::LogGCOp(TabletPeer* tablet_peer)
    : MaintenanceOp(StringPrintf("LogGCOp(%s)", tablet_peer->tablet()->tablet_id().c_str()),
                    MaintenanceOp::LOW_IO_USAGE),
      tablet_peer_(tablet_peer),
      log_gc_duration_(METRIC_log_gc_duration.Instantiate(
                           tablet_peer->tablet()->GetMetricEntity())),
      log_gc_running_(METRIC_log_gc_running.Instantiate(
                          tablet_peer->tablet()->GetMetricEntity(), 0)),
      sem_(1) {}

void LogGCOp::UpdateStats(MaintenanceOpStats* stats) {
  int64_t retention_size;

  if (!tablet_peer_->GetGCableDataSize(&retention_size).ok()) {
    return;
  }

  stats->set_logs_retained_bytes(retention_size);
  stats->set_runnable(sem_.GetValue() == 1);
}

bool LogGCOp::Prepare() {
  return sem_.try_lock();
}

void LogGCOp::Perform() {
  CHECK(!sem_.try_lock());

  tablet_peer_->RunLogGC();

  sem_.unlock();
}

scoped_refptr<Histogram> LogGCOp::DurationHistogram() const {
  return log_gc_duration_;
}

scoped_refptr<AtomicGauge<uint32_t> > LogGCOp::RunningGauge() const {
  return log_gc_running_;
}

}  // namespace tablet
}  // namespace kudu
