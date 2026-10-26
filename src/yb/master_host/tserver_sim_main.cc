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

// main() of master-tserver-sim, which loads a master with fake tservers; see
// src/rust/master/docs/testing.md.

#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "master/tools/tserver-sim-main/src/ffi.rs.h"

int main(int argc, char** argv) {
  // Flags declared in Rust must be registered before gflags parses the command line.
  yb::tserver_sim_rs::register_flags();
  yb::ParseCommandLineFlags(&argc, &argv, /* remove_flags= */ true);
  yb::InitGoogleLoggingSafe(argv[0]);
  return yb::tserver_sim_rs::run_tserver_sim();
}
