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
mod store;
mod web;
mod wiring;

use std::sync::{Arc, Once};
use std::time::Duration;

use cxx::UniquePtr;
use ffi::bridge::MasterHost;
use master_catalog_state::Catalog;
use master_leadership::Leadership;
use master_rpc_handlers::{HandlerOptions, MasterInfo, MasterServiceHandler};
use master_sys_catalog::SysCatalogStore;
use yb_cpp_host::metrics::CppMetricRegistry;
use yb_cpp_host::rpc::{register_service, CppRpcClient};
use yb_flags::{yb_flag, FlagRef, FlagTag};
use yb_host_api::metrics::MetricRegistry;
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

/// Registers the Rust-declared flags with gflags. The binary calls it before flag parsing; an
/// embedded master (MiniCluster) calls it on its first start, after parsing, where the flags then
/// keep their defaults. Several masters in one process share the flags.
fn register_flags() {
    static REGISTER: Once = Once::new();
    REGISTER.call_once(|| {
        yb_flags::set_backend(Box::new(yb_cpp_host::flags::CppFlagBackend::default()));
        yb_flags::register(&[&RUST_MASTER_RUNTIME_THREADS]);
        yb_flags::register(wiring::FLAGS);
    });
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
    let host = ffi::bridge::create_master_host().map_err(|e| e.what().to_string())?;
    let master = start(host)?;
    master.host.wait_for_termination();
    master.stop();
    Ok(())
}

/// A started master, which `stop` shuts down. Each owns its runtime and host, so several can run
/// in one process.
struct RunningMaster {
    host: UniquePtr<MasterHost>,
    runtime: tokio::runtime::Runtime,
    /// `MasterYsqlLease` calls run here, never behind heartbeats or DDL (K3).
    lease_runtime: tokio::runtime::Runtime,
    gate: Arc<store::HostGate>,
}

impl RunningMaster {
    fn stop(mut self) {
        tracing::info!("Rust master shutting down");
        self.host.pin_mut().shutdown();
        self.runtime.shutdown_timeout(Duration::from_secs(10));
        self.lease_runtime.shutdown_timeout(Duration::from_secs(10));
        self.gate.close();
    }
}

/// A Rust master embedded in a C++ process (`src/yb/master_host/embedded_master.h`), for
/// MiniCluster tests (D12).
pub struct EmbeddedRustMaster(RunningMaster);

/// Starts a master on a host the embedding process created with its own options.
fn start_embedded(host: UniquePtr<MasterHost>) -> Result<Box<EmbeddedRustMaster>, String> {
    register_flags();
    yb_cpp_host::logging::init();
    start(host).map(|master| Box::new(EmbeddedRustMaster(master)))
}

/// Shuts an embedded master down.
// The C++ side owns a `rust::Box`, which cxx hands back as a `Box`.
#[allow(clippy::boxed_local)]
fn stop_embedded(master: Box<EmbeddedRustMaster>) {
    master.0.stop();
}

fn start(mut host: UniquePtr<MasterHost>) -> Result<RunningMaster, String> {
    let mut runtime = tokio::runtime::Builder::new_multi_thread();
    if let Ok(threads @ 1..) = usize::try_from(RUST_MASTER_RUNTIME_THREADS.get()) {
        runtime.worker_threads(threads);
    }
    let runtime = runtime
        .thread_name("rust-master")
        .enable_all()
        .build()
        .map_err(|e| format!("creating the async runtime: {e}"))?;
    let lease_runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .thread_name("rust-master-ysql-lease")
        .enable_all()
        .build()
        .map_err(|e| format!("creating the YSQL lease runtime: {e}"))?;

    host.pin_mut().init().map_err(|e| e.what().to_string())?;

    let info: Arc<dyn MasterInfo> =
        Arc::new(host_info::HostInfo::new(host.pin_mut().info_handle()));
    let rpc: Arc<dyn RpcClient> = Arc::new(CppRpcClient::new(host.pin_mut().server_handle()));
    let metrics: Arc<dyn MetricRegistry> = CppMetricRegistry::new(&host.pin_mut().server_handle());

    // The core, before the host opens the sys catalog: the tap needs its sink from the start.
    let (apply_sender, apply_events) = master_sys_catalog::apply_channel();
    let registry = Arc::new(master_task_runtime::TaskRegistry::default());
    let gate = store::HostGate::open();
    let leadership = Leadership::new(
        Arc::new(store::HostLeaderState(
            host.pin_mut().info_handle(),
            gate.clone(),
        )),
        wiring::leadership_allowed,
        registry,
    );
    host.pin_mut().set_apply_sink(Box::new(store::ApplySink {
        sender: apply_sender.clone(),
        leadership: leadership.clone(),
    }));
    let sys_catalog: Arc<dyn SysCatalogStore> =
        Arc::new(store::HostStore(host.pin_mut().info_handle(), gate.clone()));
    let core = wiring::Core::start(
        runtime.handle(),
        sys_catalog.clone(),
        apply_events,
        Catalog::new(apply_sender),
        leadership,
        wiring::HostParts {
            metrics,
            defaults: Arc::new(store::HostDefaults(
                host.pin_mut().info_handle(),
                gate.clone(),
            )),
            contributors: vec![Arc::new(store::HostHeartbeatParts(
                host.pin_mut().info_handle(),
                gate.clone(),
            ))],
            schema: Arc::new(store::HostSchema(
                host.pin_mut().info_handle(),
                gate.clone(),
            )),
            local_locks: Arc::new(store::HostLocalLocks(
                host.pin_mut().info_handle(),
                gate.clone(),
            )),
        },
    );
    host.pin_mut()
        .set_virtual_tables(Box::new(store::VirtualTables {
            leader: core.leader.clone(),
            info: info.clone(),
        }));
    let options = HandlerOptions {
        master_rpc_timeout: || Duration::from_millis(MASTER_RPC_TIMEOUT_MS.get().max(0) as u64),
        list_raft_peers_check_is_leader: || LIST_RAFT_PEERS_CHECK_IS_LEADER.get(),
    };
    for service in yb_proto::SERVICES
        .iter()
        .filter(|s| s.proto_name.starts_with("yb.master."))
    {
        // Same queues and priorities as Master::RegisterServices.
        let lease_service = service.proto_name == "yb.master.MasterYsqlLease";
        let (queue_limit, high_priority) = if lease_service {
            (host.ysql_lease_service_queue_length(), true)
        } else {
            (host.master_service_queue_length(), false)
        };
        let handler = Arc::new(MasterServiceHandler::new(
            service,
            info.clone(),
            rpc.clone(),
            if lease_service {
                lease_runtime.handle().clone()
            } else {
                runtime.handle().clone()
            },
            options,
            Some(core.leader.clone()),
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
    core.run(runtime.handle(), sys_catalog, info.clone(), rpc.clone());
    let loopback_port = host.loopback_web_port().map_err(|e| e.what().to_string())?;
    runtime.spawn(async move {
        if let Err(e) = web_listener.serve(axum::Router::new(), loopback_port).await {
            tracing::error!("The web server stopped: {e}");
        }
    });
    tracing::info!("Rust master started");
    Ok(RunningMaster {
        host,
        runtime,
        lease_runtime,
        gate,
    })
}
