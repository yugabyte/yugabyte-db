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

//! The bridge to the master's C++ host (src/yb/master_host/master_host.h), and the entry points
//! its `main()` calls.

use crate::web::WebMasterList;
use crate::{register_flags, run_master};

#[cxx::bridge(namespace = "yb::master_host")]
pub(crate) mod bridge {
    extern "Rust" {
        /// Registers the master's Rust-declared flags with gflags. Called before flag parsing.
        #[namespace = "yb::master_rs"]
        fn register_flags();

        /// Runs the master; returns the process exit code. Called after flag parsing.
        #[namespace = "yb::master_rs"]
        fn run_master() -> i32;

        /// The master list the host's web pages show.
        type WebMasterList;
        /// A serialized `ListMastersResponsePB`. Blocks; called on web server threads.
        #[cxx_name = "ListMasters"]
        fn list_masters(self: &WebMasterList) -> Vec<u8>;
    }

    unsafe extern "C++" {
        include!("yb/master_host/master_host.h");

        #[namespace = "yb::rust_bridge"]
        type RpcServerHandle = yb_cpp_host::rpc::RpcServerHandle;

        type MasterHost;
        type MasterInfoHandle;

        #[cxx_name = "CreateMasterHost"]
        fn create_master_host() -> Result<UniquePtr<MasterHost>>;
        #[cxx_name = "InitHost"]
        fn init(self: Pin<&mut MasterHost>) -> Result<()>;
        #[cxx_name = "ServerHandle"]
        fn server_handle(self: Pin<&mut MasterHost>) -> Pin<&mut RpcServerHandle>;
        #[cxx_name = "StartHost"]
        fn start(self: Pin<&mut MasterHost>) -> Result<()>;
        #[cxx_name = "WaitForTermination"]
        fn wait_for_termination(self: &MasterHost);
        #[cxx_name = "ShutdownHost"]
        fn shutdown(self: Pin<&mut MasterHost>);
        #[cxx_name = "InfoHandle"]
        fn info_handle(self: Pin<&mut MasterHost>) -> UniquePtr<MasterInfoHandle>;
        #[cxx_name = "MasterServiceQueueLength"]
        fn master_service_queue_length(self: &MasterHost) -> i32;
        #[cxx_name = "YsqlLeaseServiceQueueLength"]
        fn ysql_lease_service_queue_length(self: &MasterHost) -> i32;
        #[cxx_name = "PublicWebInterface"]
        fn public_web_interface(self: &MasterHost) -> String;
        #[cxx_name = "PublicWebPort"]
        fn public_web_port(self: &MasterHost) -> u16;
        #[cxx_name = "WebCertificateFile"]
        fn web_certificate_file(self: &MasterHost) -> String;
        #[cxx_name = "WebPrivateKeyFile"]
        fn web_private_key_file(self: &MasterHost) -> String;
        #[cxx_name = "WebPrivateKeyPassword"]
        fn web_private_key_password(self: &MasterHost) -> Vec<u8>;
        #[cxx_name = "WebSslCiphers"]
        fn web_ssl_ciphers(self: &MasterHost) -> String;
        #[cxx_name = "SetWebServer"]
        fn set_web_server(
            self: Pin<&mut MasterHost>,
            bound_ips: &Vec<String>,
            bound_port: u16,
            master_list: Box<WebMasterList>,
        );
        #[cxx_name = "LoopbackWebPort"]
        fn loopback_web_port(self: &MasterHost) -> Result<u16>;

        #[cxx_name = "InstancePB"]
        fn instance_pb(self: &MasterInfoHandle) -> Vec<u8>;
        #[cxx_name = "RegistrationPB"]
        fn registration_pb(self: &MasterInfoHandle) -> Result<Vec<u8>>;
        #[cxx_name = "CommittedConsensusStatePB"]
        fn committed_consensus_state_pb(self: &MasterInfoHandle) -> Result<Vec<u8>>;
        #[cxx_name = "RaftRole"]
        fn raft_role(self: &MasterInfoHandle) -> i32;
        #[cxx_name = "IsSysCatalogInitialized"]
        fn is_sys_catalog_initialized(self: &MasterInfoHandle) -> bool;
        #[cxx_name = "IsInShellMode"]
        fn is_in_shell_mode(self: &MasterInfoHandle) -> bool;
        #[cxx_name = "FlushSysCatalogResponse"]
        fn flush_sys_catalog_response(self: &MasterInfoHandle) -> Vec<u8>;
        #[cxx_name = "CompactSysCatalogResponse"]
        fn compact_sys_catalog_response(self: &MasterInfoHandle) -> Vec<u8>;
        #[cxx_name = "HandleOnHost"]
        fn handle_on_host(self: &MasterInfoHandle, method: &str, request: &[u8])
            -> Result<Vec<u8>>;
    }
}

// SAFETY: MasterInfoHandle methods only touch host state the host guards itself.
unsafe impl Send for bridge::MasterInfoHandle {}
// SAFETY: as above.
unsafe impl Sync for bridge::MasterInfoHandle {}
