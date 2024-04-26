// Copyright (c) Yugabyte, Inc.
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

#include "yb/tserver/server_main_util.h"

#include <iostream>

#if YB_ABSL_ENABLED
#include "absl/debugging/symbolize.h"
#endif

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_queue.h"
#include "yb/consensus/log_util.h"

#include "yb/server/skewed_clock.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"

DEFINE_NON_RUNTIME_bool(
    use_memory_defaults_optimized_for_ysql, false,
    "If true, the recommended defaults for the memory usage settings take into account the amount "
    "of RAM and cores available and are optimized for using YSQL.  "
    "If false, the recommended defaults will be the old defaults, which are more suitable "
    "for YCQL but do not take into account the amount of RAM and cores available.");

DECLARE_double(default_memory_limit_to_ram_ratio);
DECLARE_int32(db_block_cache_size_percentage);
DECLARE_int32(tablet_overhead_size_percentage);
DECLARE_int64(db_block_cache_size_bytes);
DECLARE_int64(memory_limit_hard_bytes);

namespace yb {

namespace {

struct RecommendedMemoryValues {
  double default_memory_limit_to_ram_ratio;
  int32_t db_block_cache_size_percentage;
  int32_t tablet_overhead_size_percentage;
};

RecommendedMemoryValues GetLegacyMemoryValues(bool is_master) {
  RecommendedMemoryValues result;
  // TServer legacy defaults:
  result.default_memory_limit_to_ram_ratio = 0.85;
  // As of 2/2024, DB_CACHE_SIZE_USE_PERCENTAGE specifies:
  //   kDefaultMasterBlockCacheSizePercentage  = 25 on master
  //   kDefaultTserverBlockCacheSizePercentage = 50 on TServer
  result.db_block_cache_size_percentage = DB_CACHE_SIZE_USE_DEFAULT;
  result.tablet_overhead_size_percentage = 0;

  if (is_master) {
    result.default_memory_limit_to_ram_ratio = 0.10;
  }
  return result;
}

// Substitute the recommended value for the flag's value if the flag is set to
// USE_RECOMMENDED_MEMORY_VALUE.
#define SET_MEMORY_FLAG_IF_NEEDED(flag_name, recommended_value_expression)                       \
  do {                                                                                           \
    auto actual_recommendation = (recommended_value_expression);                                 \
    if (FLAGS_##flag_name == USE_RECOMMENDED_MEMORY_VALUE) {                                     \
      LOG(INFO) << "Setting flag " #flag_name " to recommended value " << actual_recommendation; \
      CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(flag_name, actual_recommendation));                  \
    } else {                                                                                     \
      LOG(INFO) << "Flag " #flag_name " has value " << FLAGS_##flag_name                         \
                << " (recommended value is " << actual_recommendation << ")";                    \
    }                                                                                            \
  } while (false)

void AdjustMemoryLimits(const RecommendedMemoryValues& values) {
  SET_MEMORY_FLAG_IF_NEEDED(db_block_cache_size_percentage, values.db_block_cache_size_percentage);
  SET_MEMORY_FLAG_IF_NEEDED(
      default_memory_limit_to_ram_ratio, values.default_memory_limit_to_ram_ratio);
  SET_MEMORY_FLAG_IF_NEEDED(
      tablet_overhead_size_percentage, values.tablet_overhead_size_percentage);
}

void AdjustMemoryLimitsIfNeeded(bool is_master) {
  int64_t signed_total_ram;
  CHECK_OK(Env::Default()->GetTotalRAMBytes(&signed_total_ram));
  size_t total_ram = signed_total_ram;
  LOG(INFO) << StringPrintf(
      "Total available RAM is %.6f GiB",
      (static_cast<float>(total_ram) / (1024.0 * 1024.0 * 1024.0)));

  auto values = GetLegacyMemoryValues(is_master);

  if (FLAGS_use_memory_defaults_optimized_for_ysql) {
    values.tablet_overhead_size_percentage = is_master ? 0 : 10;
    values.db_block_cache_size_percentage = is_master ? 25 : 32;
    if (total_ram <= 4_GB) {
      values.default_memory_limit_to_ram_ratio = is_master ? 0.20 : 0.45;
    } else if (total_ram <= 8_GB) {
      values.default_memory_limit_to_ram_ratio = is_master ? 0.15 : 0.48;
    } else if (total_ram <= 16_GB) {
      values.default_memory_limit_to_ram_ratio = is_master ? 0.10 : 0.57;
    } else {
      values.default_memory_limit_to_ram_ratio = is_master ? 0.10 : 0.60;
    }
  }

  AdjustMemoryLimits(values);
}

}  // anonymous namespace

Status MasterTServerParseFlagsAndInit(
    const std::string& server_type, bool is_master, int* argc, char*** argv) {
  debug::EnableTraceEvents();

  // Do not sync GLOG to disk for INFO, WARNING.
  // ERRORs, and FATALs will still cause a sync to disk.
  FLAGS_logbuflevel = google::GLOG_WARNING;

  // Only write FATALs by default to stderr.
  FLAGS_stderrthreshold = google::FATAL;

  server::SkewedClock::Register();

  // These are the actual defaults for these gflags for master and TServer.
  FLAGS_memory_limit_hard_bytes = 0;
  FLAGS_default_memory_limit_to_ram_ratio = USE_RECOMMENDED_MEMORY_VALUE;
  FLAGS_db_block_cache_size_bytes = DB_CACHE_SIZE_USE_PERCENTAGE;
  FLAGS_db_block_cache_size_percentage = USE_RECOMMENDED_MEMORY_VALUE;
  FLAGS_tablet_overhead_size_percentage = USE_RECOMMENDED_MEMORY_VALUE;

  ParseCommandLineFlags(argc, argv, /* remove_flag= */ true);
  if (*argc != 1) {
    std::cerr << "usage: " << (*argv)[0] << std::endl;
    return STATUS(InvalidArgument, "Error parsing command-line flags");
  }

#if YB_ABSL_ENABLED
  // Must be called before installing a failure signal handler (in InitYB).
  absl::InitializeSymbolizer((*argv)[0]);
#endif

  RETURN_NOT_OK(log::ModifyDurableWriteFlagIfNotODirect());

  RETURN_NOT_OK(InitYB(server_type, (*argv)[0]));

  RETURN_NOT_OK(GetPrivateIpMode());

  LOG(INFO) << "NumCPUs determined to be: " << base::NumCPUs();

  DLOG(INFO) << "Process id: " << getpid();

  AdjustMemoryLimitsIfNeeded(is_master);

  MemTracker::ConfigureTCMalloc();
  MemTracker::PrintTCMallocConfigs();

  return Status::OK();
}

}  // namespace yb
