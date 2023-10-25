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

#include <iostream>

#include "yb/util/logging.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/log_util.h"
#include "yb/consensus/consensus_queue.h"

#include "yb/gutil/sysinfo.h"

#include "yb/master/master_call_home.h"
#include "yb/master/master.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/server/total_mem_watcher.h"

#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/main_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/thread.h"
#include "yb/util/ulimit_util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/path_util.h"
#include "yb/common/termination_monitor.h"

#include "yb/tserver/server_main_util.h"

#if YB_LTO_ENABLED
#include "yb/tserver/tablet_server_main_impl.h"
#endif

using std::string;

DECLARE_bool(callhome_enabled);
DECLARE_bool(evict_failed_followers);
DECLARE_double(default_memory_limit_to_ram_ratio);
DECLARE_int32(logbuflevel);
DECLARE_int32(webserver_port);
DECLARE_string(rpc_bind_addresses);
DECLARE_bool(durable_wal_write);
DECLARE_int32(stderrthreshold);

DECLARE_string(metric_node_name);
DECLARE_int32(remote_bootstrap_max_chunk_size);
DECLARE_bool(use_docdb_aware_bloom_filter);
DECLARE_int32(follower_unavailable_considered_failed_sec);
DECLARE_int32(log_min_seconds_to_retain);

using namespace std::literals;

namespace yb {
namespace master {

static int MasterMain(int argc, char** argv) {
#ifndef NDEBUG
  HybridTime::TEST_SetPrettyToString(true);
#endif

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0", kMasterDefaultPort);
  FLAGS_webserver_port = kMasterDefaultWebPort;
  // Hotfix for https://github.com/yugabyte/yugabyte-db/issues/8731.
  // Before enabling bloom filters for the master tablet we need to check whether master code use
  // bloom filters properly at all, including if filter key is set correctly during scans.
  FLAGS_use_docdb_aware_bloom_filter = false;

  string host_name;
  if (GetHostname(&host_name).ok()) {
    FLAGS_metric_node_name = strings::Substitute("$0:$1", host_name, kMasterDefaultWebPort);
  } else {
    LOG(INFO) << "Failed to get master's host name, keeping default metric_node_name";
  }

  FLAGS_default_memory_limit_to_ram_ratio = 0.10;
  // For masters we always want to fsync the WAL files (except in testing).
  FLAGS_durable_wal_write = true;
  // Master has a lot less memory and relatively less data. So by default, let's keep the
  // RBS chunk size small.
  FLAGS_remote_bootstrap_max_chunk_size = 1_MB;

  // A multi-node Master leader should not evict failed Master followers
  // because there is no-one to assign replacement servers in order to maintain
  // the desired replication factor. (It's not turtles all the way down!)
  FLAGS_evict_failed_followers = false;

  FLAGS_follower_unavailable_considered_failed_sec = 2 * MonoTime::kSecondsPerHour;
  FLAGS_log_min_seconds_to_retain = 2 * MonoTime::kSecondsPerHour;

  LOG_AND_RETURN_FROM_MAIN_NOT_OK(
      MasterTServerParseFlagsAndInit(MasterOptions::kServerType, &argc, &argv));

  auto termination_monitor = TerminationMonitor::Create();

  auto opts_result = MasterOptions::CreateMasterOptions();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(opts_result);
  Master server(*opts_result);

  SetDefaultInitialSysCatalogSnapshotFlags();

  // ==============================================================================================
  // End of setting master flags
  // ==============================================================================================

  LOG(INFO) << "Initializing master server...";
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server.Init());

  LOG(INFO) << "Starting Master server...";
  UlimitUtil::InitUlimits();
  LOG(INFO) << "ulimit cur(max)..." << UlimitUtil::GetUlimitInfo();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server.Start());

  LOG(INFO) << "Master server successfully started.";

  std::unique_ptr<MasterCallHome> call_home;
  call_home = std::make_unique<MasterCallHome>(&server);
  call_home->ScheduleCallHome();

  auto total_mem_watcher = server::TotalMemWatcher::Create();
  scoped_refptr<Thread> total_mem_watcher_thread;
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(Thread::Create(
      "total_mem_watcher", "loop",
      [&total_mem_watcher, &termination_monitor]() {
        total_mem_watcher->MemoryMonitoringLoop(
            [&termination_monitor]() { termination_monitor->Terminate(); });
      },
      &total_mem_watcher_thread));

  termination_monitor->WaitForTermination();

  total_mem_watcher->Shutdown();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(ThreadJoiner(total_mem_watcher_thread.get()).Join());

  if (call_home) {
    call_home->Shutdown();
  }

  server.Shutdown();

  return EXIT_SUCCESS;
}

} // namespace master
} // namespace yb

int main(int argc, char** argv) {
  const auto executable_basename = yb::BaseName(argv[0]);
  if (boost::starts_with(executable_basename, "yb-tserver") ||
      boost::starts_with(executable_basename, "tserver")) {
#if YB_LTO_ENABLED
    return yb::tserver::TabletServerMain(argc, argv);
#else
    LOG(FATAL) << "yb-master executable can only function as yb-tserver in LTO mode. "
               << "The basename of argv[0] is: " << executable_basename;
#endif
    }
    return yb::master::MasterMain(argc, argv);
}
