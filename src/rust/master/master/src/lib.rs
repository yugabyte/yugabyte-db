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

//! The Rust master. The C++ `main` in src/yb/master_host/master_host_main.cc calls into this
//! library through the bridge in `ffi.rs` after it has parsed gflags.

mod ffi;

/// Entry point called from C++ `main` once gflags and glog are initialized. Returns the process
/// exit code.
fn run_master() -> i32 {
    yb_cpp_host::logging::init();
    tracing::error!("yb-master-rs has no master services yet");
    1
}
