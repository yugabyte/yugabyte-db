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

#include "yb/tserver/server_main_util.h"

#include <algorithm>
#include <iostream>
#include <boost/algorithm/string/trim.hpp>
#include "yb/util/string_case.h"

#if YB_ABSL_ENABLED
#include "absl/debugging/symbolize.h"
#endif

#include "yb/common/init.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_queue.h"
#include "yb/consensus/log_util.h"

#include "yb/server/clockbound_clock.h"
#include "yb/server/skewed_clock.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/pg_util.h"
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
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_auto_analyze_service);
DECLARE_bool(ysql_enable_table_mutation_counter);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_string(ysql_yb_enable_cbo);

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

// Return the lower case value of a GUC set in a CSV string (FLAGS_ysql_pg_conf_csv),
// accounting for the following
// 1. GUC keys and values are case insensitive
// 2. A GUC can be repeated multiple times, the final value is what matters
//    (consistent with WritePostgresConfig)
std::optional<std::string> FindGUCValue(
    const std::string& guc_key, const std::vector<std::string>& user_configs) {
  std::optional<std::string> result;
  for (const auto& config : user_configs) {
    const auto lower_case_config = yb::ToLowerCase(config);
    if (lower_case_config.find(guc_key) == std::string::npos) {
      continue;
    }
    const auto pos = lower_case_config.find('=');
    if (pos != std::string::npos) {
      std::string config_key = boost::trim_copy(lower_case_config.substr(0, pos));
      std::string config_value = boost::trim_copy(lower_case_config.substr(pos + 1));
      if (config_key == guc_key) {
        result = config_value;
      }
    }
  }
  return result;
}

// Auto analyze flag can have different defaults based on other flags like
// older (deprecated) auto analyze flags or the cbo flags. In any case, an explict
// user set flag value should be respected as-is.
void AdjustAutoAnalyzeFlagIfNeeded() {
  // If the auto analyze flag is explictly set, do not change it.
  if (!google::GetCommandLineFlagInfoOrDie("ysql_enable_auto_analyze").is_default) {
    LOG(INFO) << "Flag: ysql_enable_auto_analyze has been set to " << FLAGS_ysql_enable_auto_analyze
              << " by explicit config in command line";
    return;
  }

  // When we change ysql_yb_enable_cbo to default on, we need to update the logic here, so adding
  // a DCHECK to catch this situation.
  DCHECK(
      !(google::GetCommandLineFlagInfoOrDie("ysql_yb_enable_cbo").is_default &&
        FLAGS_ysql_yb_enable_cbo == "on"));

  // If ysql_yb_enable_cbo was explicitly set by user, then ysql_enable_auto_analyze flag is set to
  // true whenever it is on. The reasoning is that CBO in legacy mode
  // can have more unpredictable behavior when ANALYZE is run, so we don't want to run ANALYZE
  // automatically.
  bool cbo_flag_on = false;
  std::string cbo_reason = "";
  const std::vector<std::string> cbo_on_values = {"on", "true", "1", "yes"};
  if (!google::GetCommandLineFlagInfoOrDie("ysql_yb_enable_cbo").is_default) {
    if (std::find(
            cbo_on_values.begin(), cbo_on_values.end(),
            yb::ToLowerCase(FLAGS_ysql_yb_enable_cbo)) != cbo_on_values.end()) {
      cbo_flag_on = true;
      cbo_reason = "ysql_yb_enable_cbo flag was explicitly set by user to '" +
                   FLAGS_ysql_yb_enable_cbo + "'";
    }
  } else {
    // If ysql_pg_conf_csv contains yb_enable_cbo=on, then ysql_enable_auto_analyze is set to true.
    // Note that ysql_pg_conf_csv has lower precedence than user-set ysql_yb_enable_cbo.
    std::vector<std::string> user_configs;
    std::optional<std::string> cbo_guc_value;
    if (!FLAGS_ysql_pg_conf_csv.empty() &&
        ReadCSVValues(FLAGS_ysql_pg_conf_csv, &user_configs).ok() &&
        (cbo_guc_value = FindGUCValue("yb_enable_cbo", user_configs)).has_value() &&
        std::find(cbo_on_values.begin(), cbo_on_values.end(), cbo_guc_value.value()) !=
            cbo_on_values.end()) {
      cbo_flag_on = true;
      cbo_reason = "yb_enable_cbo was set to " + cbo_guc_value.value() + " in ysql_pg_conf_csv";
    }
  }

  if (cbo_flag_on) {
    google::SetCommandLineOptionWithMode(
      "ysql_enable_auto_analyze", "true", google::SET_FLAG_IF_DEFAULT);
    LOG(INFO) << "Flag: ysql_yb_enable_auto_analyze was auto-set to "
              << FLAGS_ysql_enable_auto_analyze << " because " << cbo_reason;
    return;
  }

  // If ysql_enable_auto_analyze_service and ysql_enable_table_mutation_counter are set
  // explicitly, use their values to set value for ysql_enable_auto_analyze.
  // ysql_enable_auto_analyze is true only if both ysql_enable_auto_analyze_service and
  // ysql_enable_table_mutation_counter are true.
  const bool old_value_of_new_flag = FLAGS_ysql_enable_auto_analyze;
  const bool auto_analyze_enabled_using_old_flags =
      FLAGS_ysql_enable_auto_analyze_service && FLAGS_ysql_enable_table_mutation_counter;
  if (auto_analyze_enabled_using_old_flags) {
    google::SetCommandLineOptionWithMode("ysql_enable_auto_analyze", "true",
                                         google::SET_FLAG_IF_DEFAULT);

    LOG(INFO) << "Flag: ysql_enable_auto_analyze was auto-set to " << FLAGS_ysql_enable_auto_analyze
              << " from its default value of " << old_value_of_new_flag
              << "because it was enabled by the following deprecated flags"
              << " - ysql_enable_auto_analyze_service and ysql_enable_table_mutation_counter.";
    return;
  }

  LOG(INFO) << "Not changing ysql_enable_auto_analyze from its default value "
            << FLAGS_ysql_enable_auto_analyze;
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
  server::RegisterClockboundClockProvider();

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

  if (!is_master)
    AdjustAutoAnalyzeFlagIfNeeded();

  MemTracker::ConfigureTCMalloc();
  MemTracker::PrintTCMallocConfigs();

  return Status::OK();
}

}  // namespace yb
