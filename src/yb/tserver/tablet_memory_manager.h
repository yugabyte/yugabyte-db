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

#pragma once

#include <memory>

#include <boost/optional.hpp>

#include "yb/tablet/tablet_options.h"

#include "yb/util/background_task.h"
#include "yb/util/mem_tracker.h"

namespace yb {
namespace tserver {

class TabletMemoryManagerListenerIf {
 public:
  virtual ~TabletMemoryManagerListenerIf() {}
  virtual void StartedFlush(const TabletId& tablet_id) {}
};

// TabletMemoryManager keeps track of memory management for a tablet, including:
// - Block cache initialization and tracking
// - Log cache garbage collection
// - Memory flushing once the global memstore limit is reached
class TabletMemoryManager {
 public:
  // 'default_block_cache_size_percentage' indicates what percentage of tablet memory will be
  // allocated to the block cache by default, assuming it isn't overridden via flags
  // (combination of 'db_block_cache_size_bytes' and 'db_block_cache_size_percentage' flags).
  //
  // 'peers_fn' is a function to return an up-to-date list of the tablet's peers (unsorted).
  TabletMemoryManager(
      tablet::TabletOptions* options,
      const std::shared_ptr<MemTracker>& mem_tracker,
      const int32_t default_block_cache_size_percentage,
      const scoped_refptr<MetricEntity>& metrics,
      const std::function<std::vector<tablet::TabletPeerPtr>()>& peers_fn);

  ~TabletMemoryManager() = default;

  // Init and Shutdown start/stop the background memstore management task.
  Status Init();
  void Shutdown();

  // The MemTracker associated with the block cache.
  std::shared_ptr<MemTracker> block_based_table_mem_tracker();

  std::shared_ptr<MemTracker> tablets_overhead_mem_tracker();

  std::shared_ptr<MemTracker> FindOrCreateOverheadMemTrackerForTablet(const TabletId& id);

  // Flushing function for the memstore.
  void FlushTabletIfLimitExceeded();

  std::vector<std::shared_ptr<TabletMemoryManagerListenerIf>> TEST_listeners;

 private:
  // Initializes the block cache and associated mem tracker and garbage collector.
  // Will use the percentage of memory specified unless modified with the
  // db_block_cache_size_bytes and db_block_cache_size_percentage flags.
  void InitBlockCache(
      const scoped_refptr<MetricEntity>& metrics,
      const int32_t default_block_cache_size_percentage,
      tablet::TabletOptions* options);

  // Initializes the log cache garbage collector.
  void InitLogCacheGC();

  // Initializes the background thread that periodically wakes up to flush memory over the
  // shared memstore limit.
  void ConfigureBackgroundTask(tablet::TabletOptions* options);

  // Log cache garbage collection function bound to the memory tracker.
  void LogCacheGC(MemTracker* log_cache_mem_tracker, size_t bytes_to_evict);

  // Determines which tablet has the oldest mutable memtable write time.  May return a null ptr
  // if no tablet meets the criteria.  Uses peers_fn_ to determine the full list of peers to check.
  tablet::TabletPeerPtr TabletToFlush();

  // Function to return a log prefix with the tablet's tablet_id and permanent_uuid.
  std::string LogPrefix(const tablet::TabletPeerPtr& peer) const;

  // Function that returns all current peers of the server associated with this memory manager.
  std::function<std::vector<tablet::TabletPeerPtr>()> peers_fn_;

  std::shared_ptr<MemTracker> server_mem_tracker_;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker_;
  std::shared_ptr<MemTracker> tablets_overhead_mem_tracker_;

  std::shared_ptr<GarbageCollector> block_based_table_gc_;
  std::shared_ptr<GarbageCollector> log_cache_gc_;

  std::unique_ptr<BackgroundTask> background_task_;

  std::shared_ptr<rocksdb::MemoryMonitor> memory_monitor_;
};

int64 ComputeTabletOverheadLimit();

// Evaluates the number of bits used to shard the block cache depending on the number of cores.
int32_t GetDbBlockCacheNumShardBits();

}  // namespace tserver
}  // namespace yb
