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

//! RPC on serialized messages, carried by the host's C++ Messenger.

use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use crate::Status;

/// An inbound RPC. Responding consumes it; dropping it unanswered makes the host answer with an
/// internal error, so a caller never waits until its deadline.
pub trait InboundCall: Send {
    /// Index of the called method within its service, in registration order.
    fn method_index(&self) -> usize;
    /// The serialized request.
    fn request(&self) -> &[u8];
    /// The caller's address, for logging.
    fn remote_address(&self) -> String;
    /// Time left until the caller's deadline; zero once it has passed.
    fn time_to_deadline(&self) -> Duration;
    /// Sends a serialized response.
    fn respond(self: Box<Self>, response: &[u8]);
    /// Fails the call at the RPC level. Application errors normally go in the response's own
    /// `error` field instead.
    fn respond_error(self: Box<Self>, status: Status);
}

/// Handles the inbound calls of one registered service. Called on the host's service-pool threads,
/// so it must not block: a handler that needs to wait hands the call to an async task.
pub trait RpcHandler: Send + Sync + 'static {
    /// Handles one call.
    fn handle(&self, call: Box<dyn InboundCall>);
}

/// The outcome of an outbound call: the serialized response, or why there is none.
pub type ResponseFuture = Pin<Box<dyn Future<Output = Result<Vec<u8>, Status>> + Send>>;

/// Makes outbound calls through the host's proxies.
pub trait RpcClient: Send + Sync {
    /// Calls `method` of the service named `service` on the wire at `host:port`.
    fn call(
        &self,
        host: &str,
        port: u16,
        service: &str,
        method: &str,
        request: &[u8],
        timeout: Duration,
    ) -> ResponseFuture;
}

/// What a [`FakeInboundCall`] was answered with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FakeResponse {
    /// `respond` was called with these bytes.
    Success(Vec<u8>),
    /// `respond_error` was called.
    Error(Status),
    /// The call was dropped unanswered.
    Dropped,
}

/// An [`InboundCall`] for tests. Its outcome lands in the shared slot returned by `new`.
pub struct FakeInboundCall {
    method_index: usize,
    request: Vec<u8>,
    outcome: Arc<Mutex<Option<FakeResponse>>>,
}

impl FakeInboundCall {
    /// Creates a call and the slot its outcome will be written to.
    pub fn new(
        method_index: usize,
        request: Vec<u8>,
    ) -> (Box<FakeInboundCall>, Arc<Mutex<Option<FakeResponse>>>) {
        let outcome = Arc::new(Mutex::new(None));
        let call = FakeInboundCall {
            method_index,
            request,
            outcome: outcome.clone(),
        };
        (Box::new(call), outcome)
    }

    fn set(&self, response: FakeResponse) {
        let mut slot = self.outcome.lock().unwrap();
        if slot.is_none() {
            *slot = Some(response);
        }
    }
}

impl InboundCall for FakeInboundCall {
    fn method_index(&self) -> usize {
        self.method_index
    }

    fn request(&self) -> &[u8] {
        &self.request
    }

    fn remote_address(&self) -> String {
        "127.0.0.1:0".into()
    }

    fn time_to_deadline(&self) -> Duration {
        Duration::from_secs(60)
    }

    fn respond(self: Box<Self>, response: &[u8]) {
        self.set(FakeResponse::Success(response.to_vec()));
    }

    fn respond_error(self: Box<Self>, status: Status) {
        self.set(FakeResponse::Error(status));
    }
}

impl Drop for FakeInboundCall {
    fn drop(&mut self) {
        self.set(FakeResponse::Dropped);
    }
}

type FakeRpcFn = dyn Fn(&str, u16, &str, &str, &[u8]) -> Result<Vec<u8>, Status> + Send + Sync;

/// An [`RpcClient`] for tests that answers every call with a function of its arguments.
pub struct FakeRpcClient {
    handler: Box<FakeRpcFn>,
}

impl FakeRpcClient {
    /// Creates a client answering calls with `handler(host, port, service, method, request)`.
    pub fn new(
        handler: impl Fn(&str, u16, &str, &str, &[u8]) -> Result<Vec<u8>, Status>
            + Send
            + Sync
            + 'static,
    ) -> FakeRpcClient {
        FakeRpcClient {
            handler: Box::new(handler),
        }
    }
}

impl RpcClient for FakeRpcClient {
    fn call(
        &self,
        host: &str,
        port: u16,
        service: &str,
        method: &str,
        request: &[u8],
        _timeout: Duration,
    ) -> ResponseFuture {
        let result = (self.handler)(host, port, service, method, request);
        Box::pin(async move { result })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::StatusCode;

    #[test]
    fn fake_call_records_first_outcome() {
        let (call, outcome) = FakeInboundCall::new(3, b"req".to_vec());
        assert_eq!(call.method_index(), 3);
        assert_eq!(call.request(), b"req");
        call.respond(b"resp");
        assert_eq!(
            *outcome.lock().unwrap(),
            Some(FakeResponse::Success(b"resp".to_vec()))
        );

        let (call, outcome) = FakeInboundCall::new(0, vec![]);
        drop(call);
        assert_eq!(*outcome.lock().unwrap(), Some(FakeResponse::Dropped));

        let (call, outcome) = FakeInboundCall::new(0, vec![]);
        call.respond_error(Status::new(StatusCode::TimedOut, "late"));
        assert!(matches!(
            *outcome.lock().unwrap(),
            Some(FakeResponse::Error(_))
        ));
    }
}
