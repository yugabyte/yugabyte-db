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

//! [`yb_host_api`] RPC traits implemented on the C++ Messenger.

use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use cxx::UniquePtr;
use tokio::sync::oneshot;
use yb_host_api::rpc::{InboundCall, ResponseFuture, RpcClient, RpcHandler};
use yb_host_api::{Status, StatusCode};

use crate::ffi::rpc::ffi;

pub use crate::ffi::rpc::ffi::RpcServerHandle;

/// The service object C++ calls into for every inbound call of one registered service.
pub struct RustRpcService {
    handler: Arc<dyn RpcHandler>,
}

pub(crate) fn handle_call(service: &RustRpcService, call: UniquePtr<ffi::InboundCallHandle>) {
    service.handler.handle(Box::new(CppInboundCall(call)));
}

struct CppInboundCall(UniquePtr<ffi::InboundCallHandle>);

impl CppInboundCall {
    fn handle(&mut self) -> Pin<&mut ffi::InboundCallHandle> {
        self.0.pin_mut()
    }
}

impl InboundCall for CppInboundCall {
    fn method_index(&self) -> usize {
        self.0.method_index()
    }

    fn request(&self) -> &[u8] {
        self.0.request()
    }

    fn remote_address(&self) -> String {
        self.0.remote_address()
    }

    fn time_to_deadline(&self) -> Duration {
        Duration::from_millis(self.0.millis_to_deadline().max(0) as u64)
    }

    fn respond(mut self: Box<Self>, response: &[u8]) {
        self.handle().respond_success(response);
    }

    fn respond_error(mut self: Box<Self>, status: Status) {
        self.handle()
            .respond_error(status.code as i32, &status.message);
    }
}

/// Registers a service whose calls go to `handler`. `methods` gives the method names in the order
/// `InboundCall::method_index` refers to. Must be called before the host starts its RPC server.
pub fn register_service(
    server: Pin<&mut ffi::RpcServerHandle>,
    wire_name: &str,
    metric_name: &str,
    methods: Vec<String>,
    queue_limit: usize,
    high_priority: bool,
    handler: Arc<dyn RpcHandler>,
) -> Result<(), Status> {
    ffi::register_rust_service(
        server,
        wire_name,
        metric_name,
        &methods,
        queue_limit,
        high_priority,
        Box::new(RustRpcService { handler }),
    )
    .map_err(|e| Status::new(StatusCode::RuntimeError, e.what()))
}

/// Carries an outbound call's result back from the C++ completion callback.
pub struct OutboundCompletion {
    sender: oneshot::Sender<Result<Vec<u8>, Status>>,
}

// The Box is the bridge's ownership transfer from C++, so clippy's boxed_local does not apply.
#[allow(clippy::boxed_local)]
pub(crate) fn complete_outbound(
    completion: Box<OutboundCompletion>,
    status_code: i32,
    message: &str,
    response: &[u8],
) {
    let result = if status_code == 0 {
        Ok(response.to_vec())
    } else {
        Err(Status::new(StatusCode::from_i32(status_code), message))
    };
    // The receiver is gone if the caller stopped waiting.
    let _ = completion.sender.send(result);
}

/// [`RpcClient`] through the host's proxy cache.
pub struct CppRpcClient(UniquePtr<ffi::RpcClient>);

impl CppRpcClient {
    /// Creates a client that uses `server`'s proxy cache.
    pub fn new(server: Pin<&mut ffi::RpcServerHandle>) -> CppRpcClient {
        CppRpcClient(ffi::make_rpc_client(server))
    }
}

impl RpcClient for CppRpcClient {
    fn call(
        &self,
        host: &str,
        port: u16,
        service: &str,
        method: &str,
        request: &[u8],
        timeout: Duration,
    ) -> ResponseFuture {
        let (sender, receiver) = oneshot::channel();
        self.0.call(
            host,
            port,
            service,
            method,
            request,
            timeout.as_millis().min(i64::MAX as u128) as i64,
            Box::new(OutboundCompletion { sender }),
        );
        Box::pin(async move {
            receiver.await.unwrap_or_else(|_| {
                Err(Status::new(
                    StatusCode::Aborted,
                    "outbound call dropped without completing",
                ))
            })
        })
    }
}
