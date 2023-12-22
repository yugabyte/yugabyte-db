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

#include "yb/util/init.h"
#include "yb/util/flags.h"
#include "yb/util/status.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/mem_tracker.h"

#include "yb/common/wire_protocol.h"

#include "yb/server/skewed_clock.h"

#include "yb/consensus/log_util.h"
#include "yb/consensus/consensus_queue.h"

DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);

namespace yb {

Status MasterTServerParseFlagsAndInit(const std::string& server_type, int* argc, char*** argv) {
  debug::EnableTraceEvents();

  // Do not sync GLOG to disk for INFO, WARNING.
  // ERRORs, and FATALs will still cause a sync to disk.
  FLAGS_logbuflevel = google::GLOG_WARNING;

  // Only write FATALs by default to stderr.
  FLAGS_stderrthreshold = google::FATAL;

  server::SkewedClock::Register();

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

  MemTracker::ConfigureTCMalloc();
  MemTracker::PrintTCMallocConfigs();

  return Status::OK();
}

}  // namespace yb
