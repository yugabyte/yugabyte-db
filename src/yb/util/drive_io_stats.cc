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

#include "yb/util/drive_io_stats.h"

#include "yb/gutil/bind.h"

#include "yb/util/metrics.h"
#include "yb/util/shared_lock.h"

// Note the units below: the *_time counters are cumulative microseconds, so the useful queries
// are rates over a scrape interval. Effective write bandwidth for a drive is
// rate(drive_bytes_written), and average cost per sync is
// rate(drive_sync_time) / rate(drive_sync_count), with rate(drive_bytes_written) as the context
// that makes that number mean something.
//
// rate(drive_sync_time) / rate(drive_bytes_written) is the one worth putting on a dashboard: time
// in the device per byte delivered. It rises when a device degrades and stays flat when a device
// with headroom simply gets more traffic, which is what separates those two causes of a latency
// increase. It blurs near saturation, where queueing delay grows faster than the offered rate, so
// read it against sibling drives or against the same drive's own history rather than as an
// absolute.

METRIC_DEFINE_gauge_uint64(drive, drive_bytes_written, "Drive Bytes Written",
    yb::MetricUnit::kBytes,
    "Bytes handed to write()/writev() for files on this drive, since the server started. "
    "Covers the Raft WAL, RocksDB SSTs for both the regular and intents databases, RocksDB "
    "bookkeeping files, tablet metadata, and remote bootstrap and snapshot writes. This is the "
    "volume side of the picture: fsync latency is not interpretable without it. Note that under "
    "O_DIRECT (durable_wal_write=true) writes are block-aligned, so a partially filled trailing "
    "block is re-written on each sync and is counted each time. That is genuinely what reached "
    "the device, but it means for a small-append workload this counter exceeds the payload "
    "written and the derived throughput is device throughput rather than payload throughput.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(drive, drive_write_time, "Drive Write Time",
    yb::MetricUnit::kMicroseconds,
    "Cumulative time spent inside write()/writev() for files on this drive. Normally small, "
    "because buffered writes land in the page cache. Under O_DIRECT (durable_wal_write=true) "
    "there is no page cache in between and no fsync afterwards, so for those drives this "
    "counter, not drive_sync_time, is where the device cost appears.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(drive, drive_sync_count, "Drive Sync Count",
    yb::MetricUnit::kOperations,
    "Number of fsync()/fdatasync() calls issued for files on this drive since the server "
    "started.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(drive, drive_sync_time, "Drive Sync Time",
    yb::MetricUnit::kMicroseconds,
    "Cumulative time blocked in fsync()/fdatasync() for files on this drive. Divide by "
    "drive_sync_count for average cost per sync, and read it beside drive_bytes_written to tell "
    "a slow device apart from a large amount of data.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(drive, drive_range_sync_count, "Drive Range Sync Count",
    yb::MetricUnit::kOperations,
    "Number of sync_file_range() writeback calls issued for files on this drive since the "
    "server started. RocksDB paces SST writeback this way rather than by fsyncing.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(drive, drive_range_sync_time, "Drive Range Sync Time",
    yb::MetricUnit::kMicroseconds,
    "Cumulative time spent in sync_file_range() writeback for files on this drive. Counted "
    "separately from drive_sync_time because RocksDB drains SST dirty data continuously and "
    "leaves the tail of the file unsynced, so the closing fsync on an SST can be cheap even on "
    "a badly degraded device. Without this counter, SST write cost is invisible.",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(drive, drive_bytes_unsynced, "Drive Bytes Unsynced",
    yb::MetricUnit::kBytes,
    "Approximate bytes written to this drive but not yet fsynced. This is an upper bound, not a "
    "measurement: it only decreases when YugabyteDB itself fsyncs a file or closes one. It "
    "therefore takes no account of writeback the kernel did on its own, nor of writeback we "
    "initiate ourselves - Flush() and RangeSync() push data to the device via sync_file_range and "
    "do not decrement it - so on a RocksDB-heavy drive this tracks bytes written since our last "
    "fsync or close rather than bytes dirty in the page cache, and will read high on a perfectly "
    "healthy node. O_DIRECT writes never contribute, since nothing is left pending. Useful for "
    "spotting a drive whose backlog grows without bound relative to its peers, not as an absolute "
    "figure.");

METRIC_DEFINE_event_stats(drive, drive_sync_latency, "Drive Sync Latency",
    yb::MetricUnit::kMicroseconds,
    "Latency of individual fsync()/fdatasync() calls for files on this drive. Unlike the "
    "table-level log_sync_latency, this can be attributed to a device.");

namespace yb {

namespace {

// Subtracts without wrapping past zero. The unsynced-bytes gauge is an estimate maintained from
// two directions, so it can legitimately be asked to go negative.
void SubtractSaturating(std::atomic<uint64_t>& counter, uint64_t delta) {
  auto current = counter.load(std::memory_order_relaxed);
  while (current > 0) {
    const auto next = delta >= current ? 0 : current - delta;
    if (counter.compare_exchange_weak(
            current, next, std::memory_order_relaxed, std::memory_order_relaxed)) {
      return;
    }
  }
}

uint64_t Micros(MonoDelta elapsed) {
  const auto micros = elapsed.ToMicroseconds();
  return micros > 0 ? static_cast<uint64_t>(micros) : 0;
}

// True when `root` is a path-boundary-aligned prefix of `path`, so that "/mnt/d1" owns
// "/mnt/d1/yb-data/..." but not "/mnt/d10/yb-data/...".
bool IsPathPrefix(const std::string& root, const std::string& path) {
  if (path.size() < root.size() || path.compare(0, root.size(), root) != 0) {
    return false;
  }
  if (path.size() == root.size()) {
    return true;
  }
  // A root recorded with a trailing slash already ends at a boundary.
  return root.back() == '/' || path[root.size()] == '/';
}

} // namespace

void DriveIoStats::RecordBufferedWrite(uint64_t bytes, MonoDelta elapsed) {
  bytes_written_.fetch_add(bytes, std::memory_order_relaxed);
  write_micros_.fetch_add(Micros(elapsed), std::memory_order_relaxed);
  bytes_unsynced_.fetch_add(bytes, std::memory_order_relaxed);
}

void DriveIoStats::RecordDirectWrite(uint64_t bytes, MonoDelta elapsed) {
  bytes_written_.fetch_add(bytes, std::memory_order_relaxed);
  write_micros_.fetch_add(Micros(elapsed), std::memory_order_relaxed);
}

void DriveIoStats::RecordSync(uint64_t bytes_synced, MonoDelta elapsed) {
  const auto micros = Micros(elapsed);
  sync_count_.fetch_add(1, std::memory_order_relaxed);
  sync_micros_.fetch_add(micros, std::memory_order_relaxed);
  SubtractSaturating(bytes_unsynced_, bytes_synced);
  // Acquire, unlike the counters: this pointer is dereferenced, so it has to be ordered against
  // the construction of what it points at. See the member declaration.
  auto* sync_latency = sync_latency_.load(std::memory_order_acquire);
  if (sync_latency != nullptr) {
    // Micros() already clamped to non-negative, and no fsync latency comes near INT64_MAX.
    sync_latency->Increment(static_cast<int64_t>(micros));
  }
}

void DriveIoStats::RecordRangeSync(MonoDelta elapsed) {
  range_sync_count_.fetch_add(1, std::memory_order_relaxed);
  range_sync_micros_.fetch_add(Micros(elapsed), std::memory_order_relaxed);
}

void DriveIoStats::ReleaseUnsyncedBytes(uint64_t bytes) {
  SubtractSaturating(bytes_unsynced_, bytes);
}

DriveIoStatsRegistry& DriveIoStatsRegistry::Instance() {
  static DriveIoStatsRegistry instance;
  return instance;
}

DriveIoStats& DriveIoStatsRegistry::Register(
    const std::string& root, const EventStatsPtr& sync_latency) {
  std::lock_guard lock(mutex_);
  auto it = roots_.find(root);
  if (it == roots_.end()) {
    it = roots_.emplace(root, std::make_unique<DriveIoStats>()).first;
  }
  if (sync_latency) {
    // This load needs no ordering: mutex_ is held and the registry is the only writer.
    auto* previous = it->second->sync_latency_.load(std::memory_order_relaxed);
    if (previous != sync_latency.get()) {
      // Retained forever: a writer may already hold the raw pointer being replaced. Retain only
      // on an actual change, or a mini-cluster would accumulate one refptr per root per server
      // restart.
      retained_sync_stats_.push_back(sync_latency);
      // Release: publishes a fully constructed EventStats to the acquire load in RecordSync(),
      // which does not take mutex_.
      it->second->sync_latency_.store(sync_latency.get(), std::memory_order_release);
      LOG_IF(INFO, previous != nullptr)
          << "Drive " << root << " sync-latency stats re-pointed at a newer metric entity; "
          << "the previous one stops receiving samples.";
    }
  }
  return *it->second;
}

std::map<std::string, std::unique_ptr<DriveIoStats>>::const_iterator
DriveIoStatsRegistry::FindUnlocked(const std::string& path) const {
  // Roots are few (one per --fs_data_dirs / --fs_wal_dirs entry) and this runs once per file
  // open, so a linear scan for the longest match is cheaper than being clever.
  auto best = roots_.end();
  for (auto it = roots_.begin(); it != roots_.end(); ++it) {
    if (IsPathPrefix(it->first, path) &&
        (best == roots_.end() || it->first.size() > best->first.size())) {
      best = it;
    }
  }
  return best;
}

DriveIoStats* DriveIoStatsRegistry::Find(const std::string& path) const {
  SharedLock lock(mutex_);
  auto it = FindUnlocked(path);
  return it == roots_.end() ? nullptr : it->second.get();
}

std::string DriveIoStatsRegistry::FindRoot(const std::string& path) const {
  SharedLock lock(mutex_);
  auto it = FindUnlocked(path);
  return it == roots_.end() ? std::string() : it->first;
}

void RegisterDriveIoMetrics(const scoped_refptr<MetricEntity>& entity, const std::string& root) {
  auto& stats = DriveIoStatsRegistry::Instance().Register(
      root, METRIC_drive_sync_latency.Instantiate(entity));

  // Unretained + NeverRetire because the counters live in the process-global registry and so
  // does the drive.
  entity->NeverRetire(METRIC_drive_bytes_written.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::bytes_written, Unretained(&stats))));
  entity->NeverRetire(METRIC_drive_write_time.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::write_micros, Unretained(&stats))));
  entity->NeverRetire(METRIC_drive_sync_count.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::sync_count, Unretained(&stats))));
  entity->NeverRetire(METRIC_drive_sync_time.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::sync_micros, Unretained(&stats))));
  entity->NeverRetire(METRIC_drive_range_sync_count.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::range_sync_count, Unretained(&stats))));
  entity->NeverRetire(METRIC_drive_range_sync_time.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::range_sync_micros, Unretained(&stats))));
  entity->NeverRetire(METRIC_drive_bytes_unsynced.InstantiateFunctionGauge(
      entity, Bind(&DriveIoStats::bytes_unsynced, Unretained(&stats))));
}

} // namespace yb
