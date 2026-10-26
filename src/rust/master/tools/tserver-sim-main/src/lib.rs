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

//! The static library of `master-tserver-sim`: runs [`master_tserver_sim`] on the C++ RPC
//! bridge. The C++ `main` in src/yb/master_host/tserver_sim_main.cc calls [`register_flags`]
//! before parsing gflags and [`run_tserver_sim`] after.

mod ffi;

use std::net::Ipv4Addr;
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use master_tserver_sim::{SimCluster, SimService, SimTserver, WorkloadConfig};
use yb_cpp_host::rpc::{register_service, CppRpcClient};
use yb_flags::yb_flag;

yb_flag!(
    /// Fake tservers to run.
    NUM_TSERVERS: i32 = 10,
    "tserver_sim_num_tservers",
    "Fake tservers to run, each on its own address.",
    []
);
yb_flag!(
    /// Address of the first fake tserver; the others follow it.
    FIRST_IP: String = "127.1.0.1".to_string(),
    "tserver_sim_first_ip",
    "IPv4 address of the first fake tserver; tserver i listens on this address plus i.",
    []
);
yb_flag!(
    /// RPC port of every fake tserver.
    RPC_PORT: i32 = 9100,
    "tserver_sim_rpc_port",
    "RPC port every fake tserver listens on.",
    []
);
yb_flag!(
    /// The masters.
    MASTER_ADDRESSES: String = "127.0.0.1:7100".to_string(),
    "tserver_sim_master_addresses",
    "Comma-separated host:port RPC addresses of the masters.",
    []
);
yb_flag!(
    /// Heartbeat interval.
    HEARTBEAT_INTERVAL_MS: i32 = 1000,
    "tserver_sim_heartbeat_interval_ms",
    "Time between a fake tserver's heartbeats, as heartbeat_interval_ms on a real one.",
    []
);
yb_flag!(
    /// Steady-state measurement window.
    STEADY_STATE_SEC: i32 = 10,
    "tserver_sim_steady_state_sec",
    "How long to measure heartbeats after registration, before any DDL.",
    []
);
yb_flag!(
    /// Tables to create.
    NUM_TABLES: i32 = 10,
    "tserver_sim_num_tables",
    "YCQL tables to create, one after another.",
    []
);
yb_flag!(
    /// Tablets per table.
    TABLETS_PER_TABLE: i32 = 8,
    "tserver_sim_tablets_per_table",
    "Tablets per table.",
    []
);
yb_flag!(
    /// RPC timeout.
    RPC_TIMEOUT_MS: i32 = 10000,
    "tserver_sim_rpc_timeout_ms",
    "Timeout of each RPC the simulator sends.",
    []
);
yb_flag!(
    /// Where the report goes.
    OUTPUT_FILE: String = String::new(),
    "tserver_sim_output_file",
    "File to write the JSON report to; standard output when empty.",
    []
);

fn register_flags() {
    yb_flags::set_backend(Box::new(yb_cpp_host::flags::CppFlagBackend::default()));
    yb_flags::register(&[
        &NUM_TSERVERS,
        &FIRST_IP,
        &RPC_PORT,
        &MASTER_ADDRESSES,
        &HEARTBEAT_INTERVAL_MS,
        &STEADY_STATE_SEC,
        &NUM_TABLES,
        &TABLETS_PER_TABLE,
        &RPC_TIMEOUT_MS,
        &OUTPUT_FILE,
    ]);
}

fn run_tserver_sim() -> i32 {
    yb_cpp_host::logging::init();
    match run() {
        Ok(()) => 0,
        Err(e) => {
            tracing::error!("master-tserver-sim failed: {e}");
            1
        }
    }
}

fn millis(flag_value: i32) -> Duration {
    Duration::from_millis(flag_value.max(0) as u64)
}

fn master_addresses() -> Result<Vec<(String, u16)>, String> {
    MASTER_ADDRESSES
        .get()
        .split(',')
        .map(|hp| {
            let (host, port) = hp
                .trim()
                .rsplit_once(':')
                .ok_or_else(|| format!("{hp} is not host:port"))?;
            let port = port.parse().map_err(|e| format!("{hp}: {e}"))?;
            Ok((host.to_string(), port))
        })
        .collect()
}

fn run() -> Result<(), String> {
    let num_tservers = NUM_TSERVERS.get().max(1) as u32;
    let first: Ipv4Addr = FIRST_IP
        .get()
        .parse()
        .map_err(|e| format!("tserver_sim_first_ip: {e}"))?;
    let port = u16::try_from(RPC_PORT.get()).map_err(|e| format!("tserver_sim_rpc_port: {e}"))?;
    let ips: Vec<String> = (0..num_tservers)
        .map(|i| Ipv4Addr::from(u32::from(first) + i).to_string())
        .collect();
    let config = WorkloadConfig {
        master_addresses: master_addresses()?,
        heartbeat_interval: millis(HEARTBEAT_INTERVAL_MS.get()),
        rpc_timeout: millis(RPC_TIMEOUT_MS.get()),
        steady_state: Duration::from_secs(STEADY_STATE_SEC.get().max(0) as u64),
        num_tables: NUM_TABLES.get().max(0) as usize,
        tablets_per_table: TABLETS_PER_TABLE.get().max(1),
        keyspace_prefix: "tserver_sim".to_string(),
    };

    let runtime = tokio::runtime::Builder::new_multi_thread()
        .thread_name("tserver-sim")
        .enable_all()
        .build()
        .map_err(|e| format!("creating the async runtime: {e}"))?;

    let bind_addresses = ips
        .iter()
        .map(|ip| format!("{ip}:{port}"))
        .collect::<Vec<_>>()
        .join(",");
    let mut host = ffi::bridge::create_tserver_sim_host(&bind_addresses);
    host.pin_mut().init().map_err(|e| e.what().to_string())?;

    let instance_seqno = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as i64)
        .unwrap_or_default();
    let cluster = Arc::new(SimCluster::new(
        ips.iter()
            .enumerate()
            .map(|(i, ip)| SimTserver::new(i, ip.clone(), port, instance_seqno))
            .collect(),
    ));
    for name in [
        "yb.tserver.TabletServerAdminService",
        "yb.consensus.ConsensusService",
    ] {
        let service = yb_proto::SERVICES
            .iter()
            .find(|s| s.proto_name == name)
            .ok_or_else(|| format!("{name} is not generated"))?;
        register_service(
            host.pin_mut().server_handle(),
            service.wire_name,
            service.proto_name,
            service.methods.iter().map(|m| m.name.to_string()).collect(),
            1000,
            false,
            Arc::new(SimService::new(service, cluster.clone())),
        )
        .map_err(|e| format!("registering {name}: {e}"))?;
    }
    host.pin_mut().start().map_err(|e| e.what().to_string())?;
    tracing::info!("{num_tservers} fake tservers listening from {first}:{port}");

    let rpc = Arc::new(CppRpcClient::new(host.pin_mut().server_handle()));
    let result = runtime.block_on(master_tserver_sim::run(cluster, rpc, config));
    host.pin_mut().shutdown();
    runtime.shutdown_timeout(Duration::from_secs(10));
    let json = result.map_err(|e| e.to_string())?.to_json();
    let output = OUTPUT_FILE.get();
    if output.is_empty() {
        print!("{json}");
    } else {
        std::fs::write(&output, json).map_err(|e| format!("writing {output}: {e}"))?;
    }
    Ok(())
}
