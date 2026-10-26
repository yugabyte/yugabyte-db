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

// main() of yb-master-rs, the Rust master. The process host is C++ and the master logic is the
// Rust static library built from src/rust/master/master; see src/rust/master/docs/cpp-host.md.

#include <boost/algorithm/string/predicate.hpp>

#include "yb/master/master_flag_defaults.h"
#include "yb/master/master_options.h"

#include "yb/tserver/server_main_util.h"

#include "yb/util/logging.h"
#include "yb/util/main_util.h"
#include "yb/util/path_util.h"

#include "master/master/src/ffi.rs.h"

#if YB_LTO_ENABLED
#include "yb/tserver/tablet_server_main_impl.h"
#endif

namespace yb::master_host {

static int MasterHostMain(int argc, char** argv) {
  master::SetMasterFlagDefaults();
  // Flags declared in Rust must be registered before gflags parses the command line.
  master_rs::register_flags();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(MasterTServerParseFlagsAndInit(
      master::MasterOptions::kServerType, /*is_master=*/true, &argc, &argv));
  return master_rs::run_master();
}

}  // namespace yb::master_host

int main(int argc, char** argv) {
  // Same dispatch as master_main.cc: in LTO builds one executable serves as both servers.
  const auto executable_basename = yb::BaseName(argv[0]);
  if (boost::starts_with(executable_basename, "yb-tserver") ||
      boost::starts_with(executable_basename, "tserver")) {
#if YB_LTO_ENABLED
    return yb::tserver::TabletServerMain(argc, argv);
#else
    LOG(FATAL) << "yb-master-rs executable can only function as yb-tserver in LTO mode. "
               << "The basename of argv[0] is: " << executable_basename;
#endif
  }
  return yb::master_host::MasterHostMain(argc, argv);
}
