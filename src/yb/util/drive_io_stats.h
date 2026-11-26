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

#pragma once

// Per-drive write-IO accounting, exported on the 'drive' metric entity.
//
// Reading order: DriveIoStats for the counters and what each one obliges its caller to do,
// DriveIoStatsRegistry for how a path is attributed to a drive, RegisterDriveIoMetrics for the
// export.
//
// The single choke point this relies on: yb::PosixWritableFile is the one writable-file
// implementation behind both the Raft WAL and everything RocksDB writes through the YugabyteDB
// Env, so instrumenting it covers all of them without any of them knowing that drives exist.
// Direct and buffered writes reach the device differently and owe different bookkeeping, which is
// why they are separate entry points here rather than one RecordWrite().
//
// Layering constraint worth knowing before moving anything: yb_fs links yb_util, so util must not
// depend on fs. That is why the registry and every metric prototype live here while FsManager
// keeps ownership of the 'drive' metric entity prototype and passes an already-instantiated entity
// in. Metric prototypes only stringize the entity name, so defining them here against the 'drive'
// entity creates no link edge; only Instantiate needs METRIC_ENTITY_drive.
//
// The thread-local IOStatsContext was considered and not reused: it has the right fields, but it
// is thread-local, perf-level-gated, RocksDB-scoped and never exported as a server metric, so
// widening it is more code than a separate always-on per-drive accumulator.

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yb/util/locks.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"

namespace yb {

// Per-drive write-IO accounting, aggregated over every writable file that lives under a
// registered drive root.
//
// Why volume is counted and not just latency: an fsync duration with no byte count beside it is
// not interpretable. 500 ms of fsync means a broken device if 64 KB went through it and means
// nothing at all if 64 MB did. Before these counters existed a support bundle carried the duration
// and not the volume, so those two cases could not be told apart after the fact. Reading
// drive_bytes_written next to drive_sync_time is what tells them apart.
//
// Why the unit is the drive and not the tablet or the file: fsync latency is not a property of
// the file being synced, it is set by the shared block device. The queue that fsync waits on is
// fed concurrently by every other tablet's WAL, by memtable flushes and compactions writing
// SSTs, and by anything else on the same mount. Pairing one file's bytes with one fsync's
// duration therefore has a contaminated denominator and can be wrong by orders of magnitude in
// either direction. Aggregating per drive over a scrape interval averages over exactly that
// contention, which is what makes "is this device an order of magnitude slower than its peers"
// answerable.
//
// Everything here is approximate on purpose: counters read in one scrape are not consistent with
// each other, and no individual operation is ever paired with any other. The question being
// answered is order-of-magnitude device throughput, not accounting.
class DriveIoStats {
 public:
  // A buffered write of `bytes` bytes taking `elapsed`. The bytes are now dirty in the page
  // cache and some later fsync will pay for them, so they also count toward the unsynced gauge -
  // which makes this call impose a bookkeeping obligation on the caller: it must eventually
  // report those bytes to RecordSync() or ReleaseUnsyncedBytes().
  void RecordBufferedWrite(uint64_t bytes, MonoDelta elapsed);

  // An O_DIRECT write of `bytes` bytes taking `elapsed`. Deliberately a separate entry point
  // from the buffered case: there is no page cache in between and no fsync follows, so the bytes
  // are already on the device, nothing is left pending, and no unsynced bookkeeping is owed.
  // Under O_DIRECT this call also carries the whole device cost, which is why write time is
  // tracked at all.
  void RecordDirectWrite(uint64_t bytes, MonoDelta elapsed);

  // fsync()/fdatasync() taking `elapsed`. `bytes_synced` is how much the calling file had
  // written but not yet synced; it is used only to walk the approximate unsynced gauge back
  // down, and is deliberately not divided by `elapsed` (see the class comment).
  void RecordSync(uint64_t bytes_synced, MonoDelta elapsed);

  // sync_file_range() writeback, counted apart from RecordSync because this is where SST write
  // cost actually lands. RocksDB paces its own writeback with sync_file_range and deliberately
  // leaves the tail of the file unsynced, so the closing fsync on an SST can be cheap even on a
  // badly degraded device. A metric watching only fsync duration reports SST writes as nearly
  // free.
  void RecordRangeSync(MonoDelta elapsed);

  // Drops `bytes` from the unsynced gauge without counting a sync. Used when a file goes away
  // still holding dirty bytes: closing without syncing is normal (WritableFileOptions::
  // sync_on_close defaults to false, and RocksDB's writer does not sync on close either), and
  // without this the gauge would only ever climb.
  void ReleaseUnsyncedBytes(uint64_t bytes);

  // Relaxed like every other access to these counters: see the note on the members below.
  uint64_t bytes_written() const { return bytes_written_.load(std::memory_order_relaxed); }
  uint64_t write_micros() const { return write_micros_.load(std::memory_order_relaxed); }
  uint64_t sync_count() const { return sync_count_.load(std::memory_order_relaxed); }
  uint64_t sync_micros() const { return sync_micros_.load(std::memory_order_relaxed); }
  uint64_t range_sync_count() const { return range_sync_count_.load(std::memory_order_relaxed); }
  uint64_t range_sync_micros() const { return range_sync_micros_.load(std::memory_order_relaxed); }
  uint64_t bytes_unsynced() const { return bytes_unsynced_.load(std::memory_order_relaxed); }

 private:
  friend class DriveIoStatsRegistry;

  // Optional fsync-latency stats (EventStats: sum and count, not a full histogram), owned by the
  // registry and never destroyed, so a raw pointer is safe. Atomic because the registry may
  // re-point it (a second MetricRegistry in the same process, i.e. a mini-cluster server restart)
  // while writers are running.
  //
  // This is the one member here that is not accessed relaxed. It publishes an object rather than
  // a value: a reader dereferences what it loads, so the store has to be release and the load
  // acquire, or the EventStats a reader reaches through the pointer is not guaranteed to be
  // constructed yet.
  std::atomic<EventStats*> sync_latency_{nullptr};

  // Every counter below is read and written with memory_order_relaxed, which is what the rest of
  // our metrics do (AtomicInt behind AtomicGauge defaults to kMemOrderNoBarrier, and the
  // std::atomic counters behind yb::Thread's FunctionGauges pass relaxed explicitly). Nothing here
  // orders anything else: no counter is consistent with another within a scrape by design, and a
  // reader only ever wants a recent value. Ordering them would buy a guarantee nobody uses, at the
  // price of a barrier per operation on the WAL and SST write paths.
  //
  // Contention is bounded rather than absent: roughly 50 ns per update on one thread and 275 ns
  // with 16 threads sharing a drive, against fsyncs three orders of magnitude larger.
  std::atomic<uint64_t> bytes_written_{0};
  std::atomic<uint64_t> write_micros_{0};
  std::atomic<uint64_t> sync_count_{0};
  std::atomic<uint64_t> sync_micros_{0};
  std::atomic<uint64_t> range_sync_count_{0};
  std::atomic<uint64_t> range_sync_micros_{0};
  std::atomic<uint64_t> bytes_unsynced_{0};
};

// Process-global map from drive root to its counters.
//
// Roots are registered once at FsManager startup and never removed, so the returned pointers are
// stable for the whole lifetime of the process. Attribution is by longest matching path prefix,
// which is what makes a single instrumentation point in PosixWritableFile cover the WAL, both
// RocksDB databases, tablet metadata, remote bootstrap and snapshot writes without any of them
// knowing about drives.
class DriveIoStatsRegistry {
 public:
  static DriveIoStatsRegistry& Instance();

  // Registers `root` and returns its counters, or the existing entry if already registered.
  //
  // A non-null `sync_latency` becomes the stats the drive reports into, replacing any earlier
  // one; the earlier one is retained rather than freed, because writers may still hold it.
  // Replacement happens when a second MetricRegistry appears for the same root in one process,
  // e.g. a mini-cluster restarting a server.
  DriveIoStats& Register(const std::string& root, const EventStatsPtr& sync_latency);

  // Counters for the drive that owns `path`, or null when `path` is under no registered root.
  DriveIoStats* Find(const std::string& path) const;

  // The drive root that owns `path`, or an empty string. For log lines that want to name the
  // drive.
  std::string FindRoot(const std::string& path) const;

 private:
  // Returns an iterator to the longest registered root that is a path-boundary-aligned prefix
  // of `path`, or end().
  std::map<std::string, std::unique_ptr<DriveIoStats>>::const_iterator FindUnlocked(
      const std::string& path) const REQUIRES_SHARED(mutex_);

  mutable rw_spinlock mutex_;
  std::map<std::string, std::unique_ptr<DriveIoStats>> roots_ GUARDED_BY(mutex_);

  // Keeps every EventStats ever handed to Register() alive, including replaced ones, since a
  // writer may be holding the raw pointer.
  std::vector<EventStatsPtr> retained_sync_stats_ GUARDED_BY(mutex_);
};

// Instantiates the per-drive IO metrics on `entity` and wires them to the counters for `root`,
// registering `root` if needed. Calling it again for the same root and the same entity is a
// no-op; calling it with a different entity re-points the drive's sync stats at the new entity
// (see DriveIoStatsRegistry::Register).
//
// Lives here rather than in FsManager so that every drive metric prototype sits next to the
// code that feeds it; the caller only has to own the `drive` metric entity.
void RegisterDriveIoMetrics(const scoped_refptr<MetricEntity>& entity, const std::string& root);

} // namespace yb
