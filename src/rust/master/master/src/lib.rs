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

//! The Rust master. The C++ `main` in src/yb/master_host/master_host_main.cc calls
//! [`register_flags`] before parsing gflags and [`run_master`] after.

mod ffi;
mod host_info;
mod web;

use std::sync::Arc;
use std::time::Duration;

use master_rpc_handlers::{HandlerOptions, MasterInfo, MasterServiceHandler};
use yb_cpp_host::rpc::{register_service, CppRpcClient};
use yb_flags::{yb_flag, FlagRef, FlagTag};
use yb_host_api::rpc::RpcClient;

yb_flag!(
    /// Worker threads of the Rust master's async runtime.
    RUST_MASTER_RUNTIME_THREADS: i32 = 0,
    "rust_master_runtime_threads",
    "Worker threads of the Rust master's async runtime. 0 means one per CPU core.",
    [FlagTag::Advanced]
);

static MASTER_RPC_TIMEOUT_MS: FlagRef<i32> = FlagRef::new("master_rpc_timeout_ms");
static LIST_RAFT_PEERS_CHECK_IS_LEADER: FlagRef<bool> =
    FlagRef::new("master_list_raft_peers_check_is_leader");

fn register_flags() {
    yb_flags::set_backend(Box::new(yb_cpp_host::flags::CppFlagBackend::default()));
    yb_flags::register(&[&RUST_MASTER_RUNTIME_THREADS]);
}

fn run_master() -> i32 {
    yb_cpp_host::logging::init();
    match run() {
        Ok(()) => 0,
        Err(e) => {
            tracing::error!("yb-master-rs failed: {e}");
            1
        }
    }
}

fn run() -> Result<(), String> {
    let mut runtime = tokio::runtime::Builder::new_multi_thread();
    if let Ok(threads @ 1..) = usize::try_from(RUST_MASTER_RUNTIME_THREADS.get()) {
        runtime.worker_threads(threads);
    }
    let runtime = runtime
        .thread_name("rust-master")
        .enable_all()
        .build()
        .map_err(|e| format!("creating the async runtime: {e}"))?;

    let mut host = ffi::bridge::create_master_host().map_err(|e| e.what().to_string())?;
    host.pin_mut().init().map_err(|e| e.what().to_string())?;

    let info: Arc<dyn MasterInfo> = Arc::new(host_info::HostInfo::new(host.info_handle()));
    let rpc: Arc<dyn RpcClient> = Arc::new(CppRpcClient::new(host.pin_mut().server_handle()));
    let options = HandlerOptions {
        master_rpc_timeout: || Duration::from_millis(MASTER_RPC_TIMEOUT_MS.get().max(0) as u64),
        list_raft_peers_check_is_leader: || LIST_RAFT_PEERS_CHECK_IS_LEADER.get(),
    };
    for service in yb_proto::SERVICES
        .iter()
        .filter(|s| s.proto_name.starts_with("yb.master."))
    {
        // Same queues and priorities as Master::RegisterServices.
        let (queue_limit, high_priority) = if service.proto_name == "yb.master.MasterYsqlLease" {
            (host.ysql_lease_service_queue_length(), true)
        } else {
            (host.master_service_queue_length(), false)
        };
        let handler = Arc::new(MasterServiceHandler::new(
            service,
            info.clone(),
            rpc.clone(),
            runtime.handle().clone(),
            options,
        ));
        register_service(
            host.pin_mut().server_handle(),
            service.wire_name,
            service.proto_name,
            service.methods.iter().map(|m| m.name.to_string()).collect(),
            queue_limit.max(1) as usize,
            high_priority,
            handler,
        )
        .map_err(|e| format!("registering {}: {e}", service.proto_name))?;
    }

    let web_listener = web::bind(&host)?;
    let web_addrs = web_listener.local_addrs();
    tracing::info!("Web server listening on {web_addrs:?}");
    host.pin_mut().set_web_server(
        &web_addrs.iter().map(|a| a.ip().to_string()).collect(),
        web_addrs[0].port(),
        Box::new(web::WebMasterList {
            info: info.clone(),
            rpc: rpc.clone(),
            runtime: runtime.handle().clone(),
            master_rpc_timeout: options.master_rpc_timeout,
        }),
    );

    host.pin_mut().start().map_err(|e| e.what().to_string())?;
    let loopback_port = host.loopback_web_port().map_err(|e| e.what().to_string())?;
    runtime.spawn(async move {
        if let Err(e) = web_listener.serve(axum::Router::new(), loopback_port).await {
            tracing::error!("The web server stopped: {e}");
        }
    });
    tracing::info!("Rust master started");
    host.wait_for_termination();
    tracing::info!("Rust master shutting down");
    host.pin_mut().shutdown();
    runtime.shutdown_timeout(Duration::from_secs(10));
    Ok(())
}
