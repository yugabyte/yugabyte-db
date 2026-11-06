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

//! The bytes-only RPC bridge (C++ side: src/yb/rust_bridge/rpc_bridge.h).

use crate::rpc::{complete_outbound, handle_call, OutboundCompletion, RustRpcService};

/// Bridge declarations; use [`crate::rpc`] rather than calling these directly.
#[cxx::bridge(namespace = "yb::rust_bridge")]
pub mod ffi {
    extern "Rust" {
        type RustRpcService;
        type OutboundCompletion;

        /// Called on a service-pool thread for every call to a Rust service.
        fn handle_call(service: &RustRpcService, call: UniquePtr<InboundCallHandle>);

        /// Called once per outbound call, on a reactor thread. `status_code` is 0 on success.
        fn complete_outbound(
            completion: Box<OutboundCompletion>,
            status_code: i32,
            message: &str,
            response: &[u8],
        );
    }

    unsafe extern "C++" {
        include!("yb/rust_bridge/rpc_bridge.h");

        /// The host's RPC server, implemented by each process that hosts Rust services.
        type RpcServerHandle;
        /// An inbound call, answered exactly once.
        type InboundCallHandle;
        /// Outbound calls through the host's proxy cache.
        type RpcClient;

        /// Registers a Rust service; see `crate::rpc::register_service`.
        #[cxx_name = "RegisterRustService"]
        fn register_rust_service(
            server: Pin<&mut RpcServerHandle>,
            wire_name: &str,
            metric_name: &str,
            methods: &Vec<String>,
            queue_limit: usize,
            high_priority: bool,
            service: Box<RustRpcService>,
        ) -> Result<()>;

        /// Creates an outbound client on the host's proxy cache.
        #[cxx_name = "MakeRpcClient"]
        fn make_rpc_client(server: Pin<&mut RpcServerHandle>) -> UniquePtr<RpcClient>;

        /// Index of the called method within its service.
        #[cxx_name = "MethodIndex"]
        fn method_index(self: &InboundCallHandle) -> usize;
        /// The serialized request.
        #[cxx_name = "Request"]
        fn request(self: &InboundCallHandle) -> &[u8];
        /// The caller's address.
        #[cxx_name = "RemoteAddress"]
        fn remote_address(self: &InboundCallHandle) -> String;
        /// Milliseconds until the caller's deadline; negative once it has passed.
        #[cxx_name = "MillisToDeadline"]
        fn millis_to_deadline(self: &InboundCallHandle) -> i64;
        /// Sends a serialized response.
        #[cxx_name = "RespondSuccess"]
        fn respond_success(self: Pin<&mut InboundCallHandle>, response: &[u8]);
        /// Fails the call with a `yb::Status` code and message.
        #[cxx_name = "RespondError"]
        fn respond_error(self: Pin<&mut InboundCallHandle>, status_code: i32, message: &str);

        /// Starts an outbound call; `completion` receives the result.
        #[cxx_name = "Call"]
        #[allow(clippy::too_many_arguments)]
        fn call(
            self: &RpcClient,
            host: &str,
            port: u16,
            service: &str,
            method: &str,
            request: &[u8],
            timeout_ms: i64,
            completion: Box<OutboundCompletion>,
        );
    }
}

// SAFETY: InboundCall::RespondSuccess/RespondFailure may be called from any thread, and the handle
// is used by one thread at a time (it is moved, never shared).
unsafe impl Send for ffi::InboundCallHandle {}
// SAFETY: RpcClient::Call only reads immutable state and the thread-safe proxy cache.
unsafe impl Send for ffi::RpcClient {}
// SAFETY: as above.
unsafe impl Sync for ffi::RpcClient {}
