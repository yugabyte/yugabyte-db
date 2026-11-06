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

//! The bridge to the simulator's C++ host (src/yb/master_host/tserver_sim_host.h), and the entry
//! points its `main()` calls.

use crate::{register_flags, run_tserver_sim};

#[cxx::bridge(namespace = "yb::master_host")]
pub(crate) mod bridge {
    extern "Rust" {
        /// Registers the simulator's flags with gflags. Called before flag parsing.
        #[namespace = "yb::tserver_sim_rs"]
        fn register_flags();

        /// Runs the simulator; returns the process exit code. Called after flag parsing.
        #[namespace = "yb::tserver_sim_rs"]
        fn run_tserver_sim() -> i32;
    }

    unsafe extern "C++" {
        include!("yb/master_host/tserver_sim_host.h");

        #[namespace = "yb::rust_bridge"]
        type RpcServerHandle = yb_cpp_host::rpc::RpcServerHandle;

        type TserverSimHost;

        #[cxx_name = "CreateTserverSimHost"]
        fn create_tserver_sim_host(rpc_bind_addresses: &str) -> UniquePtr<TserverSimHost>;
        #[cxx_name = "InitHost"]
        fn init(self: Pin<&mut TserverSimHost>) -> Result<()>;
        #[cxx_name = "ServerHandle"]
        fn server_handle(self: Pin<&mut TserverSimHost>) -> Pin<&mut RpcServerHandle>;
        #[cxx_name = "StartHost"]
        fn start(self: Pin<&mut TserverSimHost>) -> Result<()>;
        #[cxx_name = "ShutdownHost"]
        fn shutdown(self: Pin<&mut TserverSimHost>);
    }
}
