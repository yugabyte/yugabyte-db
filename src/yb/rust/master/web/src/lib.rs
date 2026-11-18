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

//! The master's leader pages (`MasterPathHandlers`), served by a ready leader.
//!
//! Each page is a pure function of a [`View`]: one catalog snapshot, the tserver registry and the
//! observed replica locations, taken together when the request arrives. On a master that is not a
//! ready leader every route answers [`yb_web::PassToHost`], and the host's C++ web server fetches
//! the page from the leader, as a C++ follower does.
//!
//! The JSON pages keep the C++ leader's bytes (see [`json`]) because YBA and scripts parse them.
//! The HTML pages are the web UI (`ui/`), which reads the JSON.

pub mod json;

mod classify;
mod config;
mod replication;
mod table;
mod tables;
mod tservers;
mod ui;
mod ui_json;

#[cfg(test)]
mod tests;

use std::collections::BTreeMap;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::{Duration, Instant};

use axum::body::Body;
use axum::extract::Request;
use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::Router;
use master_catalog_state::CatalogSnapshot;
use master_rpc_handlers::LeaderServices;
use master_table_lifecycle::SchemaHost;
use master_tablet_locations::TabletLocations;
use master_ts_manager::TsDescriptor;
use protobuf::Message;
use yb_host_api::rpc::RpcClient;
use yb_host_api::Status;
use yb_proto::pb::master_replication::{
    GetReplicationStatusRequestPB, GetReplicationStatusResponsePB,
};
use yb_proto::pb::tserver_admin::{GetActiveRbsInfoRequestPB, GetActiveRbsInfoResponsePB};

pub use ui::UiAssets;

/// Flags the pages read, when they read them.
#[derive(Clone, Copy)]
pub struct WebOptions {
    /// `enable_ysql`.
    pub enable_ysql: fn() -> bool,
    /// `IsYsqlLeaseEnabled`: `enable_object_locking_for_table_locks` or
    /// `enable_ysql_operation_lease`.
    pub ysql_lease_enabled: fn() -> bool,
    /// `replication_factor`, for a cluster config without a live replica count.
    pub replication_factor: fn() -> i32,
    /// `leaderless_tablet_alert_delay_secs`.
    pub leaderless_alert_delay: fn() -> Duration,
    /// `ysql_tablespace_info_refresh_secs`.
    pub tablespace_refresh_secs: fn() -> i32,
    /// `tablet_replicas_per_core_limit`; 0 for none.
    pub tablet_replicas_per_core_limit: fn() -> u32,
    /// `tablet_replicas_per_gib_limit`; 0 for none.
    pub tablet_replicas_per_gib_limit: fn() -> u32,
    /// `enforce_tablet_replica_limits`.
    pub enforce_tablet_replica_limits: fn() -> bool,
    /// `hide_dead_node_threshold_mins`.
    pub hide_dead_node_threshold_mins: fn() -> i32,
    /// `GetProtocol`: links to tserver pages use https (`webserver_certificate_file` set and
    /// `master_webserver_require_https`).
    pub https_links: fn() -> bool,
    /// `TEST_sleep_before_reporting_lb_ui_ms`: between reading the balancer's tasks and showing
    /// their states.
    pub sleep_before_reporting_lb_ui: fn() -> Duration,
}

/// A tserver's YSQL lease deadline in the current term.
pub type LeaseDeadline = Box<dyn Fn(&[u8]) -> Instant + Send + Sync>;

/// `GetReplicationStatus`; false on an error.
pub type ReplicationStatus = Box<
    dyn Fn(&GetReplicationStatusRequestPB, &mut GetReplicationStatusResponsePB) -> bool
        + Send
        + Sync,
>;

/// When a tserver's leases were last recorded in this term, if it is live.
pub type LeaseReportTime = Box<dyn Fn(&[u8]) -> Option<Instant> + Send + Sync>;

/// A host method by name, with the serialized request; the serialized response.
pub type HostCall = Box<dyn Fn(&'static str, &[u8]) -> Result<Vec<u8>, Status> + Send + Sync>;

/// The cluster balancer as the pages show it.
#[derive(Default)]
pub struct BalancerView {
    /// `IsLoadBalancerEnabled`: the flag and `ChangeLoadBalancerState`.
    pub enabled: bool,
    /// Every recent run was idle.
    pub idle: bool,
    /// The last recorded run's activity.
    pub activities: Vec<master_cluster_balancer::Activity>,
    /// The tasks that run sent, whose states change as they end.
    pub tasks: Vec<Arc<master_cluster_balancer::controller::BalancerTask>>,
}

/// What one request reads: a consistent catalog snapshot and the tserver registry, at `now`.
pub struct View {
    /// The leader's catalog.
    pub snapshot: Arc<CatalogSnapshot>,
    /// Every tserver the leader knows, by uuid.
    pub tservers: Vec<TsDescriptor>,
    /// Observed replica locations.
    pub locations: Arc<TabletLocations>,
    /// When the request was taken.
    pub now: Instant,
    /// The tserver's YSQL lease deadline in this term.
    pub lease_deadline: LeaseDeadline,
    /// `GetReplicationStatus` on this universe as an xCluster target; false on an error.
    pub replication_status: ReplicationStatus,
    /// When leaderless time starts to count: the term's start.
    pub observable_from: Instant,
    /// `TsManager::live_lease_report_time`.
    pub last_lease_report: LeaseReportTime,
    /// The YSQL tablespaces; None in tests without YSQL.
    pub tablespaces: Option<Arc<master_table_lifecycle::Tablespaces>>,
    /// A call to the host (`MasterHost::HandleOnHost`), for what only C++ renders.
    pub host_call: HostCall,
    /// The cluster balancer.
    pub balancer: BalancerView,
    /// The leader's running tasks by name.
    pub running_tasks: BTreeMap<&'static str, usize>,
    /// The tserver RPC tasks in flight, by table.
    pub table_tasks: Arc<master_task_runtime::TableTasks>,
    /// `FetchRbsInfo`: each answering tserver's remote bootstraps, by its uuid; fetched only for
    /// the pages that show them.
    pub remote_bootstraps: Vec<(Vec<u8>, GetActiveRbsInfoResponsePB)>,
    /// The flags.
    pub options: WebOptions,
}

/// A request's query arguments in order (squeasel's `parsed_args`).
#[derive(Clone, Debug, Default)]
pub struct Args(Vec<(String, String)>);

impl Args {
    /// Parses `a=1&b&c=x%20y`; a key without `=` has an empty value.
    pub fn parse(query: Option<&str>) -> Args {
        Args(
            query
                .unwrap_or_default()
                .split('&')
                .filter(|kv| !kv.is_empty())
                .map(|kv| {
                    let (k, v) = kv.split_once('=').unwrap_or((kv, ""));
                    (url_decode(k), url_decode(v))
                })
                .collect(),
        )
    }

    /// The first value of `key`. `parsed_args` is a map, so the first occurrence wins there too.
    pub fn get(&self, key: &str) -> Option<&str> {
        self.0
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.as_str())
    }
}

fn url_decode(s: &str) -> String {
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < bytes.len() => {
                let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).unwrap_or("-");
                match u8::from_str_radix(hex, 16) {
                    Ok(b) => {
                        out.push(b);
                        i += 2;
                    }
                    Err(_) => out.push(b'%'),
                }
            }
            b => out.push(b),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// A page: the body for a view and the request's arguments.
pub type Render = fn(&View, &Args) -> Vec<u8>;

/// The JSON leader pages, by path.
pub const LEADER_JSON: &[(&str, Render)] = &[
    ("/api/v1/tablet-servers", tservers::tablet_servers),
    ("/api/v1/health-check", tservers::health_check),
    (
        "/api/v1/tablet-replication",
        replication::tablet_replication,
    ),
    (
        "/api/v1/tablet-under-replication",
        replication::tablet_under_replication,
    ),
    ("/dump-entities", tables::dump_entities),
    ("/api/v1/namespaces", tables::namespaces),
    ("/api/v1/table", table::table),
    ("/api/v1/tables", tables::tables),
    ("/api/v1/cluster-config", config::cluster_config_json),
    ("/api/v1/xcluster", config::xcluster_json),
    ("/api/v1/stateful-services", config::stateful_services_json),
    ("/api/v1/overview", ui_json::overview),
    ("/api/v1/load-distribution", ui_json::load_distribution),
    ("/api/v1/tasks", ui_json::tasks),
    (
        "/api/v1/tablet-server-clocks",
        ui_json::tablet_server_clocks,
    ),
];

/// What the routes need.
pub struct MasterWeb {
    /// The leader's components.
    pub leader: Arc<LeaderServices>,
    /// The flags.
    pub options: WebOptions,
    /// The built web UI, if this binary has one.
    pub ui: Option<UiAssets>,
    /// The host's C++ helpers.
    pub host: Arc<dyn SchemaHost>,
    /// The leader's running tasks.
    pub tasks: Arc<master_task_runtime::TaskRegistry>,
    /// Calls to tservers; None in tests.
    pub rpc: Option<Arc<dyn RpcClient>>,
}

impl MasterWeb {
    /// `FetchRbsInfo`: `GetActiveRbsInfo` from every live tserver, keeping the answers that
    /// arrive within 5 seconds.
    async fn fetch_remote_bootstraps(
        &self,
        view: &View,
    ) -> Vec<(Vec<u8>, GetActiveRbsInfoResponsePB)> {
        const TIMEOUT: Duration = Duration::from_secs(5);
        let Some(rpc) = &self.rpc else {
            return Vec::new();
        };
        let Ok(request) = GetActiveRbsInfoRequestPB::new().write_to_bytes() else {
            return Vec::new();
        };
        let mut calls = tokio::task::JoinSet::new();
        for desc in view.tservers.iter().filter(|d| d.is_live()) {
            let Some((host, port)) = self.leader.ts_manager.rpc_address(&desc.uuid) else {
                continue;
            };
            let call = rpc.call(
                &host,
                port,
                "yb.tserver.TabletServerAdminService",
                "GetActiveRbsInfo",
                &request,
                TIMEOUT,
            );
            let uuid = desc.uuid.to_vec();
            calls.spawn(async move { (uuid, call.await) });
        }
        let mut responses = Vec::new();
        let _ = tokio::time::timeout(TIMEOUT, async {
            while let Some(joined) = calls.join_next().await {
                let Ok((uuid, result)) = joined else {
                    continue;
                };
                if let Ok(response) = result.and_then(|bytes| {
                    GetActiveRbsInfoResponsePB::parse_from_bytes(&bytes).map_err(|e| {
                        Status::new(yb_host_api::StatusCode::Corruption, e.to_string())
                    })
                }) {
                    responses.push((uuid, response));
                }
            }
        })
        .await;
        responses.sort_by(|a, b| a.0.cmp(&b.0));
        responses
    }

    /// The view for one request, if this master is a ready leader.
    fn view(&self) -> Option<View> {
        let (token, snapshot) = self.leader.ready().ok()?;
        let leases = self.leader.leases.clone();
        let xcluster = self.leader.xcluster.clone();
        let ts_manager = self.leader.ts_manager.clone();
        let host = self.host.clone();
        let term = token.term();
        let now = Instant::now();
        // As C++, which counts a tablet's leaderless time from when the term loaded it.
        let observable_from = now
            - self
                .leader
                .balancer
                .time_since_elected()
                .unwrap_or_default();
        Some(View {
            snapshot,
            tservers: self.leader.ts_manager.all(),
            locations: self.leader.ts_manager.locations.clone(),
            now,
            lease_deadline: Box::new(move |uuid| leases.deadline_in_term(term, uuid).into_std()),
            replication_status: Box::new(move |req, resp| {
                xcluster.get_replication_status(req, resp).is_ok()
            }),
            observable_from,
            last_lease_report: Box::new(move |uuid| ts_manager.live_lease_report_time(uuid)),
            tablespaces: Some(self.leader.tables.tablespaces()),
            host_call: Box::new(move |method, request| host.call(method, request)),
            balancer: BalancerView {
                enabled: (self.leader.load_balancing_flag)()
                    && self.leader.load_balancer_enabled.load(Ordering::Acquire),
                idle: self.leader.balancer.is_idle(),
                activities: self.leader.balancer.latest().into_iter().collect(),
                tasks: self.leader.balancer.latest_tasks(),
            },
            running_tasks: self.tasks.running(),
            table_tasks: self.leader.tables.table_tasks(),
            remote_bootstraps: Vec::new(),
            options: self.options,
        })
    }
}

/// The routes for every leader page; anything else falls through to the host.
pub fn routes(web: Arc<MasterWeb>) -> Router {
    let mut router = Router::new();
    for &(path, render) in LEADER_JSON {
        let web = web.clone();
        router = router.route(
            path,
            get(move |request: Request| serve_json(web.clone(), path, render, request)),
        );
    }
    ui::routes(router, web)
}

async fn serve_json(
    web: Arc<MasterWeb>,
    path: &'static str,
    render: Render,
    request: Request,
) -> Response {
    let Some(mut view) = web.view() else {
        return pass_to_host();
    };
    if path == "/api/v1/load-distribution" {
        view.remote_bootstraps = web.fetch_remote_bootstraps(&view).await;
    }
    let args = Args::parse(request.uri().query());
    // A page walks the whole catalog; keep it off the async workers.
    match tokio::task::spawn_blocking(move || render(&view, &args)).await {
        Ok(body) => text_response(body),
        Err(e) => (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()).into_response(),
    }
}

/// Lets the host's C++ web server answer: on a master that is not a ready leader, it fetches the
/// page from the leader.
pub fn pass_to_host() -> Response {
    let mut response = StatusCode::NOT_FOUND.into_response();
    response.extensions_mut().insert(yb_web::PassToHost);
    response
}

/// The C++ web server's answer for an unstyled page: always 200, as `text/plain`.
fn text_response(body: Vec<u8>) -> Response {
    let mut response = Response::new(Body::from(body));
    let headers = response.headers_mut();
    headers.insert(header::CONTENT_TYPE, HeaderValue::from_static("text/plain"));
    headers.insert(
        header::X_CONTENT_TYPE_OPTIONS,
        HeaderValue::from_static("nosniff"),
    );
    headers.insert(
        header::ACCESS_CONTROL_ALLOW_ORIGIN,
        HeaderValue::from_static("*"),
    );
    response
}
