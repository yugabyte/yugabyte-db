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

#include "yb/tserver/tablet_memory_manager.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_cache.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/gutil/bits.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/sysinfo.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/memory_monitor.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/server_main_util.h"

#include "yb/util/background_task.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/status_log.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_bool(enable_log_cache_gc, true,
            "Set to true to enable log cache garbage collector.");

DEFINE_UNKNOWN_bool(log_cache_gc_evict_only_over_allocated, true,
            "If set to true, log cache garbage collection would evict only memory that was "
            "allocated over limit for log cache. Otherwise it will try to evict requested number "
            "of bytes.");
DEFINE_UNKNOWN_int64(global_memstore_size_percentage, 10,
             "Percentage of total available memory to use for the global memstore. "
             "Default is 10. See also memstore_size_mb and "
             "global_memstore_size_mb_max.");
DEFINE_UNKNOWN_int64(global_memstore_size_mb_max, 2048,
             "Global memstore size is determined as a percentage of the available "
             "memory. However, this flag limits it in absolute size. Value of 0 "
             "means no limit on the value obtained by the percentage. Default is 2048.");

// NOTE: The default here is for tools and tests; the actual defaults
// for the TServer and master processes are set in server_main_util.cc.
DEFINE_NON_RUNTIME_int32(tablet_overhead_size_percentage, 0,
    "Percentage of total available memory to use for tablet-related overheads. A value of 0 means "
    "no limit. Must be between 0 and 100 inclusive. Exception: "
    BOOST_PP_STRINGIZE(USE_RECOMMENDED_MEMORY_VALUE) " specifies to instead use a "
    "recommended value determined in part by the amount of RAM available.");

DEFINE_RUNTIME_bool(enable_block_based_table_cache_gc, false,
            "Set to true to enable block based table garbage collector.");

// NOTE: The default here is for tools and tests; the actual defaults
// for the TServer and master processes are set in server_main_util.cc.
DEFINE_NON_RUNTIME_int64(db_block_cache_size_bytes, DB_CACHE_SIZE_USE_PERCENTAGE,
    "Size of the shared RocksDB block cache (in bytes). "
    "A value of " BOOST_PP_STRINGIZE(DB_CACHE_SIZE_USE_PERCENTAGE)
    " specifies to instead use a percentage of this daemon's hard memory limit; "
    "see --db_block_cache_size_percentage for the percentage used. "
    "A value of " BOOST_PP_STRINGIZE(DB_CACHE_SIZE_CACHE_DISABLED) " disables the block cache.");

// NOTE: The default here is for tools and tests; the actual defaults
// for the TServer and master processes are set in server_main_util.cc.
DEFINE_NON_RUNTIME_int32(db_block_cache_size_percentage, DB_CACHE_SIZE_USE_DEFAULT,
    "Percentage of our hard memory limit to use for the shared RocksDB block cache, if "
    "--db_block_cache_size_bytes is " BOOST_PP_STRINGIZE(DB_CACHE_SIZE_USE_PERCENTAGE) ". "
    "The special value " BOOST_PP_STRINGIZE(USE_RECOMMENDED_MEMORY_VALUE)
    " means to instead use a recommended percentage determined in part by the amount of RAM "
    "available. "
    "The special value " BOOST_PP_STRINGIZE(DB_CACHE_SIZE_USE_DEFAULT)
    " means to use a older default that does not take the amount of RAM into account. "
    "The percentage used due to the special values may depend on whether this is a "
    "TServer or master daemon.");

DEFINE_RUNTIME_int32(db_block_cache_num_shard_bits, -1,
             "-1 indicates a dynamic scheme that evaluates to 4 if number of cores is less than "
             "or equal to 16, 5 for 17-32 cores, 6 for 33-64 cores and so on. If the value is "
             "overridden, that value would be used in favor of the dynamic scheme. "
             "The maximum permissible value is 19.");
TAG_FLAG(db_block_cache_num_shard_bits, advanced);

DEFINE_test_flag(bool, pretend_memory_exceeded_enforce_flush, false,
                  "Always pretend memory has been exceeded to enforce background flush.");

namespace yb::tserver {

using strings::Substitute;

namespace {

bool ValidateTabletOverheadSizePercentage(const char* flag_name, int value) {
  if (value >= 0 && value <= 100) {
    return true;
  }
  if (value == USE_RECOMMENDED_MEMORY_VALUE) {
    return true;
  }
  LOG(WARNING) << flag_name << " must be a percentage (0 to 100) or the special value "
               << USE_RECOMMENDED_MEMORY_VALUE << ", value " << value << " is invalid";
  return false;
}

DEFINE_validator(tablet_overhead_size_percentage, &ValidateTabletOverheadSizePercentage);

class FunctorGC : public GarbageCollector {
 public:
  explicit FunctorGC(std::function<void(size_t)> impl) : impl_(std::move(impl)) {}

  void CollectGarbage(size_t required) override {
    impl_(required);
  }

 private:
  std::function<void(size_t)> impl_;
};

class LRUCacheGC : public GarbageCollector {
 public:
  explicit LRUCacheGC(std::shared_ptr<rocksdb::Cache> cache) : cache_(std::move(cache)) {}

  void CollectGarbage(size_t required) override {
    if (!FLAGS_enable_block_based_table_cache_gc) {
      return;
    }

    auto evicted = cache_->Evict(required);
    LOG(INFO) << "Evicted from table cache: " << HumanReadableNumBytes::ToString(evicted)
              << ", new usage: " << HumanReadableNumBytes::ToString(cache_->GetUsage())
              << ", required: " << HumanReadableNumBytes::ToString(required);
  }

 private:
  std::shared_ptr<rocksdb::Cache> cache_;
};

// Evaluates the target block cache size based on the db_block_cache_size_percentage and
// db_block_cache_size_bytes flags, as well as the passed default_block_cache_size_percentage.
int64_t GetTargetBlockCacheSize(const int32_t default_block_cache_size_percentage) {
  int32_t target_block_cache_size_percentage =
      (FLAGS_db_block_cache_size_percentage == DB_CACHE_SIZE_USE_DEFAULT) ?
      default_block_cache_size_percentage : FLAGS_db_block_cache_size_percentage;

  // If we aren't assigning block cache sized based on percentage, then the size is determined by
  // db_block_cache_size_bytes.
  int64_t target_block_cache_size_bytes = FLAGS_db_block_cache_size_bytes;
  // Auto-compute size of block cache based on percentage of memory available if asked to.
  if (target_block_cache_size_bytes == DB_CACHE_SIZE_USE_PERCENTAGE) {
    // Check some bounds.
    CHECK(target_block_cache_size_percentage > 0 && target_block_cache_size_percentage <= 100)
        << Substitute(
               "tablet_block_cache_size_percentage must be between 0 and 100. Current value: "
               "$0",
               target_block_cache_size_percentage);

    const int64_t total_ram_avail = MemTracker::GetRootTracker()->limit();
    target_block_cache_size_bytes = total_ram_avail * target_block_cache_size_percentage / 100;
  }
  return target_block_cache_size_bytes;
}

size_t GetLogCacheSize(tablet::TabletPeer* peer) {
  auto consensus_result = peer->GetRaftConsensus();
  if (!consensus_result) {
    return 0;
  }
  return consensus_result.get()->LogCacheSize();
}

}  // namespace

TabletMemoryManager::TabletMemoryManager(
    tablet::TabletOptions* options,
    const std::shared_ptr<MemTracker>& mem_tracker,
    const int32_t default_block_cache_size_percentage,
    const scoped_refptr<MetricEntity>& metrics,
    const std::function<std::vector<tablet::TabletPeerPtr>()>& peers_fn) {
  server_mem_tracker_ = mem_tracker;
  peers_fn_ = peers_fn;
  // TODO after #20876 and #20879, change the -1 below back into ComputeTabletOverheadLimit().
  // See #20667 for why removing this limit temporarily was necessary.
  tablets_overhead_mem_tracker_ = MemTracker::FindOrCreateTracker(
      /*no limit*/ -1, "Tablets_overhead", server_mem_tracker_);

  InitBlockCache(metrics, default_block_cache_size_percentage, options);
  InitLogCacheGC();
  // Assign background_task_ if necessary.
  ConfigureBackgroundTask(options);
}

Status TabletMemoryManager::Init() {
  if (background_task_) {
    RETURN_NOT_OK(background_task_->Init());
  }
  return Status::OK();
}

void TabletMemoryManager::Shutdown() {
  if (background_task_) {
    background_task_->Shutdown();
  }
}

std::shared_ptr<MemTracker> TabletMemoryManager::block_based_table_mem_tracker() {
  return block_based_table_mem_tracker_;
}

std::shared_ptr<MemTracker> TabletMemoryManager::tablets_overhead_mem_tracker() {
  return tablets_overhead_mem_tracker_;
}

std::shared_ptr<MemTracker> TabletMemoryManager::FindOrCreateOverheadMemTrackerForTablet(
    const TabletId& id) {
  return MemTracker::FindOrCreateTracker(
      Format("tablet-$0", id), /* metric_name */ "PerTablet", tablets_overhead_mem_tracker(),
      AddToParent::kTrue, CreateMetrics::kFalse);
}

void TabletMemoryManager::InitBlockCache(
    const scoped_refptr<MetricEntity>& metrics,
    const int32_t default_block_cache_size_percentage,
    tablet::TabletOptions* options) {
  int64_t block_cache_size_bytes = GetTargetBlockCacheSize(default_block_cache_size_percentage);

  block_based_table_mem_tracker_ = MemTracker::FindOrCreateTracker(
      block_cache_size_bytes,
      "BlockBasedTable",
      server_mem_tracker_);

  if (block_cache_size_bytes != DB_CACHE_SIZE_CACHE_DISABLED) {
    options->block_cache = rocksdb::NewLRUCache(block_cache_size_bytes,
                                                GetDbBlockCacheNumShardBits());
    options->block_cache->SetMetrics(metrics);
    block_based_table_gc_ = std::make_shared<LRUCacheGC>(options->block_cache);
    block_based_table_mem_tracker_->AddGarbageCollector(block_based_table_gc_);
  }
}

void TabletMemoryManager::InitLogCacheGC() {
  auto log_cache_mem_tracker = consensus::LogCache::GetServerMemTracker(server_mem_tracker_);
  log_cache_gc_ = std::make_shared<FunctorGC>(
      std::bind(&TabletMemoryManager::LogCacheGC, this, log_cache_mem_tracker.get(), _1));
  log_cache_mem_tracker->AddGarbageCollector(log_cache_gc_);
}

void TabletMemoryManager::ConfigureBackgroundTask(tablet::TabletOptions* options) {
  // Calculate memstore_size_bytes based on total RAM available and global percentage.
  CHECK(FLAGS_global_memstore_size_percentage > 0 && FLAGS_global_memstore_size_percentage <= 100)
    << Substitute(
        "Flag FLAGS_global_memstore_size_percentage must be between 0 and 100. Current value: "
        "$0",
        FLAGS_global_memstore_size_percentage);
  int64_t total_ram_avail = MemTracker::GetRootTracker()->limit();
  size_t memstore_size_bytes = total_ram_avail * FLAGS_global_memstore_size_percentage / 100;

  if (FLAGS_global_memstore_size_mb_max != 0) {
    memstore_size_bytes = std::min(memstore_size_bytes,
                                   static_cast<size_t>(FLAGS_global_memstore_size_mb_max << 20));
  }

  // Add memory monitor and background thread for flushing.
  // TODO(zhaoalex): replace task with Poller
  background_task_.reset(new BackgroundTask(
    std::function<void()>([this]() { FlushTabletIfLimitExceeded(); }),
    "tablet manager",
    "flush scheduler bgtask"));
  options->memory_monitor = std::make_shared<rocksdb::MemoryMonitor>(
      memstore_size_bytes,
      std::function<void()>([this](){
                              YB_WARN_NOT_OK(background_task_->Wake(), "Wakeup error"); }));

  // Must assign memory_monitor_ after configuring the background task.
  memory_monitor_ = options->memory_monitor;
}

void TabletMemoryManager::LogCacheGC(MemTracker* log_cache_mem_tracker, size_t bytes_to_evict) {
  if (!FLAGS_enable_log_cache_gc) {
    return;
  }

  if (FLAGS_log_cache_gc_evict_only_over_allocated) {
    if (!log_cache_mem_tracker->has_limit()) {
      return;
    }
    auto limit = log_cache_mem_tracker->limit();
    auto consumption = log_cache_mem_tracker->consumption();
    if (consumption <= limit) {
      return;
    }
    bytes_to_evict = std::min<size_t>(bytes_to_evict, consumption - limit);
  }

  auto peers = peers_fn_();
  // Sort by inverse log size.
  std::sort(peers.begin(), peers.end(), [](const auto& lhs, const auto& rhs) {
    return GetLogCacheSize(lhs.get()) > GetLogCacheSize(rhs.get());
  });

  size_t total_evicted = 0;
  for (const auto& peer : peers) {
    auto consensus_result = peer->GetRaftConsensus();
    if (!consensus_result) {
      VLOG_WITH_FUNC(3) << "Skipping Peer " << peer->permanent_uuid() << consensus_result.status();
      continue;
    }
    if (GetLogCacheSize(peer.get()) <= 0) {
      continue;
    }
    size_t evicted = consensus_result.get()->EvictLogCache(bytes_to_evict - total_evicted);
    total_evicted += evicted;
    if (total_evicted >= bytes_to_evict) {
      break;
    }
  }


  LOG(INFO) << "Evicted from log cache: " << HumanReadableNumBytes::ToString(total_evicted)
            << ", required: " << HumanReadableNumBytes::ToString(bytes_to_evict);
}

void TabletMemoryManager::FlushTabletIfLimitExceeded() {
  int iteration = 0;
  while (memory_monitor_->Exceeded() ||
         (iteration++ == 0 && FLAGS_TEST_pretend_memory_exceeded_enforce_flush)) {
    YB_LOG_EVERY_N_SECS(INFO, 5) << Format("Memstore global limit of $0 bytes reached, looking for "
                                           "tablet to flush", memory_monitor_->limit());
    auto flush_tick = rocksdb::FlushTick();
    tablet::TabletPeerPtr peer_to_flush = TabletToFlush();
    if (peer_to_flush) {
      auto tablet_to_flush = peer_to_flush->shared_tablet();
      // TODO(bojanserafimov): If peer_to_flush flushes now because of other reasons,
      // we will schedule a second flush, which will unnecessarily stall writes for a short time.
      // This will not happen often, but should be fixed.
      if (tablet_to_flush) {
        LOG(INFO)
            << LogPrefix(peer_to_flush)
            << "Flushing tablet with oldest memstore write at "
            << tablet_to_flush->OldestMutableMemtableWriteHybridTime();
        WARN_NOT_OK(
            tablet_to_flush->Flush(
                tablet::FlushMode::kAsync, tablet::FlushFlags::kAllDbs, flush_tick),
            Substitute("Flush failed on $0", peer_to_flush->tablet_id()));
        WARN_NOT_OK(peer_to_flush->log()->AsyncAllocateSegmentAndRollover(),
            Format("Roll log failed on $0", peer_to_flush->tablet_id()));
        for (auto listener : TEST_listeners) {
          listener->StartedFlush(peer_to_flush->tablet_id());
        }
      }
    }
  }
}

// Return the tablet with the oldest write in memstore, or nullptr if all tablet memstores are
// empty or about to flush.
tablet::TabletPeerPtr TabletMemoryManager::TabletToFlush() {
  HybridTime oldest_write_in_memstores = HybridTime::kMax;
  tablet::TabletPeerPtr tablet_to_flush;
  for (const tablet::TabletPeerPtr& peer : peers_fn_()) {
    const auto tablet = peer->shared_tablet();
    if (tablet) {
      const auto ht = tablet->OldestMutableMemtableWriteHybridTime();
      if (ht.ok()) {
        if (*ht < oldest_write_in_memstores) {
          oldest_write_in_memstores = *ht;
          tablet_to_flush = peer;
        }
      } else {
        YB_LOG_EVERY_N_SECS(WARNING, 5) << Format(
            "Failed to get oldest mutable memtable write ht for tablet $0: $1",
            tablet->tablet_id(), ht.status());
      }
    }
  }
  return tablet_to_flush;
}

std::string TabletMemoryManager::LogPrefix(const tablet::TabletPeerPtr& peer) const {
  return Substitute("T $0 P $1 : ",
      peer->tablet_id(),
      peer->permanent_uuid());
}

int64 ComputeTabletOverheadLimit() {
  if (0 == FLAGS_tablet_overhead_size_percentage) {
    return -1;
  }
  return MemTracker::GetRootTracker()->limit() * FLAGS_tablet_overhead_size_percentage / 100;
}

int32_t GetDbBlockCacheNumShardBits() {
  auto num_cache_shard_bits = FLAGS_db_block_cache_num_shard_bits;
  if (num_cache_shard_bits < 0) {
    const auto num_cores = base::NumCPUs();
    if (num_cores <= 16) {
      return rocksdb::kSharedLRUCacheDefaultNumShardBits;
    }
    num_cache_shard_bits = Bits::Log2Ceiling(num_cores);
  }
  if (num_cache_shard_bits > rocksdb::kSharedLRUCacheMaxNumShardBits) {
    LOG(INFO) << Format(
        "The value of db_block_cache_num_shard_bits is higher than the "
        "maximum permissible value of $0. The value used will be $0.",
        rocksdb::kSharedLRUCacheMaxNumShardBits);
    return rocksdb::kSharedLRUCacheMaxNumShardBits;
  }
  return num_cache_shard_bits;
}

}  // namespace yb::tserver
