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

//! Wires the core together: the materializer and writer on every master; on a ready leader, the
//! controllers and the location stream in the term's scope.

use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::{Duration, Instant};

use master_catalog_state::defaults::{
    set_universe_uuid_if_needed, write_default_rows, DefaultsHost, DefaultsOptions,
};
use master_catalog_state::{run_materializer, self_check, Catalog, CatalogWriter, WriterOptions};
use master_cluster_balancer::controller::{BalancerController, BalancerFlags, BalancerStatus};
use master_cluster_balancer::Options as BalancerOptions;
use master_leadership::{Leadership, Phase};
use master_rpc_handlers::{LeaderServices, MasterInfo};
use master_sys_catalog::{ApplyEvents, SysCatalogStore};
use master_table_lifecycle::TransactionTableController;
use master_table_lifecycle::{
    LifecycleOptions, SchemaHost, TableAlterer, TableDeleter, TableLifecycle, TabletAssigner,
};
use master_tablet_split_manager::{
    AutoSplitOptions, SplitController, SplitOptions, TabletSplitManager,
};
use master_task_runtime::{
    run_controller, Attempt, BlockingPool, BoxFuture, Controller, RetryPolicy, TermScope,
    TsRpcExecutor,
};
use master_ts_manager::{
    DeleteReplica, HeartbeatContributor, LocationStream, Peer, TsManager, TsManagerOptions,
};
use master_ysql_manager::initdb;
use master_ysql_manager::lease::{LeaseExpiryController, LeaseOptions, YsqlLeases};
use protobuf::Message;
use tokio::sync::watch;
use yb_flags::{yb_flag, FlagRef, FlagTag};
use yb_host_api::metrics::MetricRegistry;
use yb_host_api::rpc::RpcClient;
use yb_host_api::StatusCode;
use yb_proto::pb::master_rust_peer::{SetClusterMetricsRequestPB, SetLeaderReadyTermRequestPB};

yb_flag!(
    /// Rows per catalog Raft write.
    CATALOG_WRITER_MAX_BATCH_ROWS: i32 = 1000,
    "rust_master_catalog_writer_max_batch_rows",
    "Most sys catalog rows the Rust master's catalog writer puts in one Raft write.",
    [FlagTag::Advanced]
);

yb_flag!(
    /// Bytes per catalog Raft write.
    CATALOG_WRITER_MAX_BATCH_BYTES: i32 = 4 << 20,
    "rust_master_catalog_writer_max_batch_bytes",
    "Most bytes of sys catalog rows the Rust master's catalog writer puts in one Raft write.",
    [FlagTag::Advanced]
);

yb_flag!(
    /// Catalog Raft writes outstanding.
    CATALOG_WRITER_MAX_INFLIGHT: i32 = 4,
    "rust_master_catalog_writer_max_inflight",
    "Most catalog Raft writes the Rust master's catalog writer keeps outstanding.",
    [FlagTag::Advanced]
);

yb_flag!(
    /// Interval of the catalog self-check.
    CATALOG_SELF_CHECK_INTERVAL_MS: i32 = 60_000,
    "rust_master_catalog_self_check_interval_ms",
    "How often the Rust master compares its in-memory catalog with a scan of the sys catalog. \
     0 disables the check.",
    [FlagTag::Advanced]
);

yb_flag!(
    /// Interval of the location stream.
    LOCATION_PUSH_INTERVAL_MS: i32 = 500,
    "rust_master_location_push_interval_ms",
    "How often a Rust master leader pushes replica locations to Rust followers.",
    [FlagTag::Advanced]
);

/// The Rust-declared flags of this module.
pub(crate) const FLAGS: &[&dyn yb_flags::AnyFlagDef] = &[
    &CATALOG_WRITER_MAX_BATCH_ROWS,
    &CATALOG_WRITER_MAX_BATCH_BYTES,
    &CATALOG_WRITER_MAX_INFLIGHT,
    &CATALOG_SELF_CHECK_INTERVAL_MS,
    &LOCATION_PUSH_INTERVAL_MS,
];

static ALLOW_LEADERSHIP: FlagRef<bool> = FlagRef::new("TEST_rust_master_allow_leadership");
static UNRESPONSIVE_TIMEOUT_MS: FlagRef<i32> = FlagRef::new("tserver_unresponsive_timeout_ms");
static TABLET_REPORT_LIMIT: FlagRef<i32> = FlagRef::new("tablet_report_limit");
static HEARTBEAT_RPC_TIMEOUT_MS: FlagRef<i32> = FlagRef::new("heartbeat_rpc_timeout_ms");
static HEARTBEAT_SAFE_DEADLINE_RATIO: FlagRef<f64> = FlagRef::new("heartbeat_safe_deadline_ratio");
static PERSIST_TSERVER_REGISTRY: FlagRef<bool> = FlagRef::new("persist_tserver_registry");
static UNIVERSE_UUID_CHECK: FlagRef<bool> =
    FlagRef::new("master_enable_universe_uuid_heartbeat_check");
static GUARDED_LEASE_MS: FlagRef<u32> = FlagRef::new("xcluster_guarded_lease_duration_ms");
static MAX_CLOCK_SKEW_USEC: FlagRef<u64> = FlagRef::new("max_clock_skew_usec");
static REPLICATION_FACTOR: FlagRef<i32> = FlagRef::new("replication_factor");
static MAX_CREATE_TABLETS_PER_TS: FlagRef<i32> = FlagRef::new("max_create_tablets_per_ts");
static CHECK_TS_COUNT_FOR_CREATE_TABLE: FlagRef<bool> =
    FlagRef::new("catalog_manager_check_ts_count_for_create_table");
static TABLET_CREATION_TIMEOUT_MS: FlagRef<i32> = FlagRef::new("tablet_creation_timeout_ms");
static USE_PARENT_TABLE_ID_FIELD: FlagRef<bool> = FlagRef::new("use_parent_table_id_field");
static TRANSACTION_TABLE_NUM_TABLETS: FlagRef<i32> = FlagRef::new("transaction_table_num_tablets");
static TRANSACTION_TABLE_NUM_TABLETS_PER_TSERVER: FlagRef<i32> =
    FlagRef::new("transaction_table_num_tablets_per_tserver");
static ENABLE_DELETE_TRUNCATE_XCLUSTER_REPLICATED_TABLE: FlagRef<bool> =
    FlagRef::new("enable_delete_truncate_xcluster_replicated_table");
static BLACKLIST_PROGRESS_INITIAL_DELAY_SECS: FlagRef<i32> =
    FlagRef::new("blacklist_progress_initial_delay_secs");
static DELAY_LEADER_BLACKLIST_COMPLETION: FlagRef<bool> =
    FlagRef::new("delay_leader_blacklist_completion_percent_until_tservers_heartbeat");
static SEND_LEADER_BLACKLISTED_TSERVERS: FlagRef<bool> =
    FlagRef::new("send_leader_blacklisted_tservers_on_heartbeat");
static ENABLE_MULTI_WAY_SPLIT: FlagRef<bool> = FlagRef::new("TEST_enable_multi_way_tablet_split");
static ENABLE_LOAD_BALANCING: FlagRef<bool> = FlagRef::new("enable_load_balancing");
static LB_INITIAL_DELAY_SECS: FlagRef<i32> = FlagRef::new("load_balancer_initial_delay_secs");
static LB_NUM_IDLE_RUNS: FlagRef<i32> = FlagRef::new("load_balancer_num_idle_runs");
static MIN_LEADER_STEPDOWN_RETRY_INTERVAL_MS: FlagRef<i32> =
    FlagRef::new("min_leader_stepdown_retry_interval_ms");
static LEADER_BALANCE_THRESHOLD: FlagRef<i32> = FlagRef::new("leader_balance_threshold");
static LEADER_BALANCE_UNRESPONSIVE_TIMEOUT_MS: FlagRef<i32> =
    FlagRef::new("leader_balance_unresponsive_timeout_ms");
static LB_MAX_RBS: FlagRef<i32> =
    FlagRef::new("load_balancer_max_concurrent_tablet_remote_bootstraps");
static LB_MAX_RBS_PER_TABLE: FlagRef<i32> =
    FlagRef::new("load_balancer_max_concurrent_tablet_remote_bootstraps_per_table");
static LB_MAX_INBOUND_RBS: FlagRef<i32> =
    FlagRef::new("load_balancer_max_inbound_remote_bootstraps_per_tserver");
static LB_MIN_INBOUND_RBS: FlagRef<i32> =
    FlagRef::new("load_balancer_min_inbound_remote_bootstraps_per_tserver");
static RBS_RATE_LIMIT: FlagRef<i64> = FlagRef::new("remote_bootstrap_rate_limit_bytes_per_sec");
static AUTO_SPLIT: FlagRef<bool> = FlagRef::new("enable_automatic_tablet_splitting");
static ENABLE_YSQL: FlagRef<bool> = FlagRef::new("enable_ysql");
static PAUSE_BG_LOOP_START: FlagRef<bool> =
    FlagRef::new("TEST_pause_catalog_manager_bg_loop_start");
static PAUSE_BG_LOOP_END: FlagRef<bool> = FlagRef::new("TEST_pause_catalog_manager_bg_loop_end");
static TXN_TABLE_WAIT_MIN_TS_COUNT: FlagRef<i32> = FlagRef::new("txn_table_wait_min_ts_count");
static USE_CREATE_TABLE_LEADER_HINT: FlagRef<bool> = FlagRef::new("use_create_table_leader_hint");
static YSQL_LEASE_TTL_MS: FlagRef<u64> = FlagRef::new("master_ysql_operation_lease_ttl_ms");
static YSQL_LEASE_CLIENT_BUFFER_MS: FlagRef<u64> =
    FlagRef::new("ysql_operation_lease_ttl_client_buffer_ms");
static OBJECT_LOCK_CLEANUP_INTERVAL_MS: FlagRef<u64> =
    FlagRef::new("object_lock_cleanup_interval_ms");
static SPLIT_CANDIDATES_INTERVAL_MS: FlagRef<i32> =
    FlagRef::new("process_split_tablet_candidates_interval_msec");
static OUTSTANDING_SPLIT_LIMIT: FlagRef<u64> = FlagRef::new("outstanding_tablet_split_limit");
static OUTSTANDING_SPLIT_LIMIT_PER_TS: FlagRef<i64> =
    FlagRef::new("outstanding_tablet_split_limit_per_tserver");
static SPLIT_LIMIT_PER_TABLE: FlagRef<u64> = FlagRef::new("tablet_split_limit_per_table");
static LOW_PHASE_SHARDS: FlagRef<i64> = FlagRef::new("tablet_split_low_phase_shard_count_per_node");
static HIGH_PHASE_SHARDS: FlagRef<i64> =
    FlagRef::new("tablet_split_high_phase_shard_count_per_node");
static LOW_PHASE_SIZE: FlagRef<i64> = FlagRef::new("tablet_split_low_phase_size_threshold_bytes");
static HIGH_PHASE_SIZE: FlagRef<i64> = FlagRef::new("tablet_split_high_phase_size_threshold_bytes");
static FORCE_SPLIT_SIZE: FlagRef<i64> = FlagRef::new("tablet_force_split_threshold_bytes");
static SORT_SPLIT_CANDIDATES: FlagRef<bool> =
    FlagRef::new("sort_automatic_tablet_splitting_candidates");
static SPLIT_MIN_SIZE_RATIO: FlagRef<f64> = FlagRef::new("tablet_split_min_size_ratio");
static PREVENT_TTL_SPLIT_SECS: FlagRef<u64> =
    FlagRef::new("prevent_split_for_ttl_tables_for_seconds");
static PREVENT_SMALL_RANGE_SPLIT_SECS: FlagRef<u64> =
    FlagRef::new("prevent_split_for_small_key_range_tablets_for_seconds");
static LB_MAX_OVER_REPLICATED: FlagRef<i32> =
    FlagRef::new("load_balancer_max_over_replicated_tablets");
static LB_MAX_ADDS: FlagRef<i32> = FlagRef::new("load_balancer_max_concurrent_adds");
static LB_MAX_REMOVALS: FlagRef<i32> = FlagRef::new("load_balancer_max_concurrent_removals");
static LB_MAX_MOVES: FlagRef<i32> = FlagRef::new("load_balancer_max_concurrent_moves");
static LB_MAX_MOVES_PER_TABLE: FlagRef<i32> =
    FlagRef::new("load_balancer_max_concurrent_moves_per_table");
static ALLOW_LEADER_BALANCING_DEAD_NODE: FlagRef<bool> =
    FlagRef::new("allow_leader_balancing_dead_node");
static LB_MIN_FREE_DISK_PCT: FlagRef<u32> = FlagRef::new("load_balancer_min_free_disk_space_pct");
static TXN_TABLES_USE_PREFERRED_ZONES: FlagRef<bool> =
    FlagRef::new("transaction_tables_use_preferred_zones");
static ENABLE_GLOBAL_LOAD_BALANCING: FlagRef<bool> = FlagRef::new("enable_global_load_balancing");
static LB_DRIVE_AWARE: FlagRef<bool> = FlagRef::new("load_balancer_drive_aware");
static LB_IGNORE_CLOUD_INFO_SIMILARITY: FlagRef<bool> =
    FlagRef::new("load_balancer_ignore_cloud_info_similarity");
static STEPDOWN_TO_PREFERRED_LEADER_ON_REMOVE: FlagRef<bool> =
    FlagRef::new("cluster_balancer_stepdown_to_preferred_leader_on_remove");
static LB_UNDER_REPLICATED_ONLY: FlagRef<bool> =
    FlagRef::new("TEST_load_balancer_handle_under_replicated_tablets_only");
static BG_TASK_WAIT_MS: FlagRef<i32> = FlagRef::new("catalog_manager_bg_task_wait_ms");
static MASTER_TS_RPC_TIMEOUT_MS: FlagRef<i32> = FlagRef::new("master_ts_rpc_timeout_ms");
static RETRY_MAX_DELAY_MS: FlagRef<i32> = FlagRef::new("retrying_ts_rpc_max_delay_ms");
static RETRY_LIMIT: FlagRef<i32> = FlagRef::new("unresponsive_ts_rpc_retry_limit");
static RETRY_TIMEOUT_MS: FlagRef<i32> = FlagRef::new("unresponsive_ts_rpc_timeout_ms");
static TOMBSTONE_EVICTED_REPLICAS: FlagRef<bool> =
    FlagRef::new("master_tombstone_evicted_tablet_replicas");
static USE_PENDING_CONFIG_OP_ID: FlagRef<bool> =
    FlagRef::new("use_tablet_report_pending_config_op_id");
static REGISTER_TS_FROM_RAFT: FlagRef<bool> = FlagRef::new("enable_register_ts_from_raft");
static MAX_CONCURRENT_DELETE_REPLICA_RPCS_PER_TS: FlagRef<i32> =
    FlagRef::new("max_concurrent_delete_replica_rpcs_per_ts");
static MAX_LEADER_LEASE_EXPIRED_SECS: FlagRef<u32> =
    FlagRef::new("maximum_tablet_leader_lease_expired_secs");

static CLUSTER_UUID: FlagRef<String> = FlagRef::new("cluster_uuid");
static USE_PRIVATE_IP: FlagRef<String> = FlagRef::new("use_private_ip");
static ALLOW_YCQL_ROLE_DROP: FlagRef<bool> = FlagRef::new("ycql_allow_cassandra_drop");

const DEFAULTS_OPTIONS: DefaultsOptions = DefaultsOptions {
    cluster_uuid: || CLUSTER_UUID.get(),
    universe_uuid_check: || UNIVERSE_UUID_CHECK.get(),
    allow_ycql_role_drop: || ALLOW_YCQL_ROLE_DROP.get(),
};

fn millis(value: i32) -> Duration {
    Duration::from_millis(value.max(0) as u64)
}

fn ts_manager_options() -> TsManagerOptions {
    TsManagerOptions {
        unresponsive_timeout: || millis(UNRESPONSIVE_TIMEOUT_MS.get()),
        tablet_report_limit: || TABLET_REPORT_LIMIT.get(),
        report_safety_margin: || {
            let ratio = HEARTBEAT_SAFE_DEADLINE_RATIO.get();
            let ratio = if ratio.is_finite() {
                ratio.clamp(0.0, 1.0)
            } else {
                0.0
            };
            millis(HEARTBEAT_RPC_TIMEOUT_MS.get()).mul_f64(ratio)
        },
        persist_registry: || PERSIST_TSERVER_REGISTRY.get(),
        universe_uuid_check: || UNIVERSE_UUID_CHECK.get(),
        guarded_lease_expiry: || {
            Duration::from_millis(GUARDED_LEASE_MS.get().into())
                + Duration::from_micros(MAX_CLOCK_SKEW_USEC.get().saturating_mul(2))
        },
        guarded_lease_duration: || Duration::from_millis(GUARDED_LEASE_MS.get().into()),
        tombstone_evicted_replicas: || TOMBSTONE_EVICTED_REPLICAS.get(),
        use_pending_config_op_id: || USE_PENDING_CONFIG_OP_ID.get(),
        register_from_raft: || REGISTER_TS_FROM_RAFT.get(),
        metadata_min_time_left: || millis(HEARTBEAT_RPC_TIMEOUT_MS.get()) / 2,
        max_leader_lease_expired: || {
            Duration::from_secs(MAX_LEADER_LEASE_EXPIRED_SECS.get().into())
        },
        use_private_ip: || USE_PRIVATE_IP.get(),
        send_leader_blacklisted_tservers: || SEND_LEADER_BLACKLISTED_TSERVERS.get(),
    }
}

/// Calls in flight per tserver, by RPC kind (K2). Kinds without a C++ cap are not limited.
fn ts_rpc_limit(kind: &str) -> usize {
    match kind {
        // A negative value is no limit, as the C++ throttler's conversion to uint64 makes it; 0
        // would never send there, and is one call here.
        "DeleteTablet" => match MAX_CONCURRENT_DELETE_REPLICA_RPCS_PER_TS.get() {
            limit if limit < 0 => usize::MAX,
            limit => limit.max(1) as usize,
        },
        _ => usize::MAX,
    }
}

/// The policy of `RetryingTSRpcTask`s that are limited in attempts; `DeleteReplica` tasks are not
/// (`RetryLimitTaskType`).
fn retry_policy(limit_attempts: bool) -> RetryPolicy {
    RetryPolicy {
        max_delay: millis(RETRY_MAX_DELAY_MS.get()),
        max_attempts: if limit_attempts {
            RETRY_LIMIT.get().max(1) as u32
        } else {
            u32::MAX
        },
        deadline: millis(RETRY_TIMEOUT_MS.get()),
        ..Default::default()
    }
}

/// `ClusterLoadBalancer::ReportMetrics` and `UpdatePerRunMetrics`: the host keeps them in the
/// "cluster" metric entity, as C++ does.
fn report_cluster_metrics(
    host: &dyn SchemaHost,
    enabled: bool,
    metrics: Option<&master_cluster_balancer::RunMetrics>,
) {
    let mut req = SetClusterMetricsRequestPB::new();
    req.set_is_load_balancing_enabled(enabled);
    if let Some(m) = metrics {
        req.set_tablets_in_wrong_placement(m.tablets_in_wrong_placement);
        req.set_blacklisted_leaders(m.blacklisted_leaders);
        req.set_total_table_load_difference(m.total_table_load_difference);
        req.set_estimated_data_to_balance_bytes(m.estimated_data_to_balance_bytes);
    }
    let result = req
        .write_to_bytes()
        .map_err(|e| yb_host_api::Status::new(StatusCode::InternalError, e.to_string()))
        .and_then(|bytes| host.call("SetClusterMetrics", &bytes));
    if let Err(status) = result {
        tracing::warn!("Failed to report the cluster balancer's metrics: {status}");
    }
}

/// Tells the host the term in which this leader is ready, -1 when it is not, as C++ keeps
/// `leader_ready_term_`: the host serves YSQL's catalog reads and writes only in that term. A
/// lapsed lease keeps the term; the host's Raft check refuses such calls anyway.
async fn report_ready_term(leadership: Arc<Leadership>, host: Arc<dyn SchemaHost>) {
    let mut phases = leadership.subscribe();
    loop {
        let term = match *phases.borrow_and_update() {
            Phase::Ready { term, .. } | Phase::LeaseLapsed { term, .. } => term,
            _ => -1,
        };
        let mut req = SetLeaderReadyTermRequestPB::new();
        req.set_term(term);
        match req.write_to_bytes() {
            Ok(bytes) => {
                if let Err(status) = host.call("SetLeaderReadyTerm", &bytes) {
                    tracing::warn!("Failed to report the ready term {term} to the host: {status}");
                }
            }
            Err(e) => tracing::warn!("Failed to encode the ready term: {e}"),
        }
        if phases.changed().await.is_err() {
            return;
        }
    }
}

/// What the core takes from the C++ host.
pub(crate) struct HostParts {
    /// The server's metric registry.
    pub(crate) metrics: Arc<dyn MetricRegistry>,
    /// What only the host can build of the default rows.
    pub(crate) defaults: Arc<dyn DefaultsHost>,
    /// Heartbeat response fields the host owns.
    pub(crate) contributors: Vec<Arc<dyn HeartbeatContributor>>,
    /// The C++ schema steps of table DDL.
    pub(crate) schema: Arc<dyn SchemaHost>,
}

/// The core's components, built before the host starts so the apply tap has a sink.
pub(crate) struct Core {
    pub(crate) catalog: Arc<Catalog>,
    pub(crate) leadership: Arc<Leadership>,
    pub(crate) leader: Arc<LeaderServices>,
    /// Paces master-to-tserver RPCs across terms (K2).
    ts_rpc: Arc<TsRpcExecutor>,
    /// The host's C++ steps the controllers call.
    host: Arc<dyn SchemaHost>,
}

impl Core {
    /// Builds the core and starts the materializer and the leadership loop on `runtime`.
    pub(crate) fn start(
        runtime: &tokio::runtime::Handle,
        store: Arc<dyn SysCatalogStore>,
        events: ApplyEvents,
        catalog: Arc<Catalog>,
        leadership: Arc<Leadership>,
        host: HostParts,
    ) -> Core {
        let HostParts {
            metrics,
            defaults,
            contributors,
            schema,
        } = host;
        let _guard = runtime.enter();
        runtime.spawn(run_materializer(catalog.clone(), store.clone(), events));
        let writer = CatalogWriter::start(
            catalog.clone(),
            store.clone(),
            leadership.clone(),
            WriterOptions {
                max_batch_rows: || CATALOG_WRITER_MAX_BATCH_ROWS.get().max(1) as usize,
                max_batch_bytes: || CATALOG_WRITER_MAX_BATCH_BYTES.get().max(1) as usize,
                max_inflight: || CATALOG_WRITER_MAX_INFLIGHT.get().max(1) as usize,
                metrics,
            },
        );
        let ts_manager = TsManager::new(Arc::default(), ts_manager_options());
        let ts_rpc = TsRpcExecutor::new(ts_rpc_limit);
        let splits = TabletSplitManager::new(
            catalog.clone(),
            writer.clone(),
            leadership.clone(),
            ts_manager.clone(),
            schema.clone(),
            ts_rpc.clone(),
            // `AsyncGetTabletSplitKey` and `AsyncSplitTablet` are limited in attempts.
            retry_policy(true),
            SplitOptions {
                use_parent_table_id_field: || USE_PARENT_TABLE_ID_FIELD.get(),
                enable_multi_way_split: || ENABLE_MULTI_WAY_SPLIT.get(),
                rpc_timeout: || millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
                interval: || millis(BG_TASK_WAIT_MS.get().max(1)),
                auto: AutoSplitOptions {
                    enabled: || AUTO_SPLIT.get(),
                    interval: || millis(SPLIT_CANDIDATES_INTERVAL_MS.get().max(0)),
                    outstanding_limit: || OUTSTANDING_SPLIT_LIMIT.get(),
                    outstanding_limit_per_tserver: || OUTSTANDING_SPLIT_LIMIT_PER_TS.get(),
                    split_limit_per_table: || SPLIT_LIMIT_PER_TABLE.get(),
                    low_phase_shard_count_per_node: || LOW_PHASE_SHARDS.get(),
                    high_phase_shard_count_per_node: || HIGH_PHASE_SHARDS.get(),
                    low_phase_size_threshold_bytes: || LOW_PHASE_SIZE.get(),
                    high_phase_size_threshold_bytes: || HIGH_PHASE_SIZE.get(),
                    force_split_threshold_bytes: || FORCE_SPLIT_SIZE.get(),
                    sort_candidates: || SORT_SPLIT_CANDIDATES.get(),
                    min_size_ratio: || SPLIT_MIN_SIZE_RATIO.get(),
                    replication_factor: || REPLICATION_FACTOR.get(),
                    max_create_tablets_per_ts: || MAX_CREATE_TABLETS_PER_TS.get(),
                    prevent_split_for_ttl_tables: || {
                        Duration::from_secs(PREVENT_TTL_SPLIT_SECS.get())
                    },
                    prevent_split_for_small_key_range_tablets: || {
                        Duration::from_secs(PREVENT_SMALL_RANGE_SPLIT_SECS.get())
                    },
                },
            },
        );
        let tables = TableLifecycle::new(
            catalog.clone(),
            writer.clone(),
            leadership.clone(),
            ts_manager.clone(),
            schema.clone(),
            LifecycleOptions {
                replication_factor: || REPLICATION_FACTOR.get(),
                max_create_tablets_per_ts: || MAX_CREATE_TABLETS_PER_TS.get(),
                check_ts_count_for_create_table: || CHECK_TS_COUNT_FOR_CREATE_TABLE.get(),
                enable_ysql: || ENABLE_YSQL.get(),
                txn_table_wait_min_ts_count: || TXN_TABLE_WAIT_MIN_TS_COUNT.get(),
                use_create_table_leader_hint: || USE_CREATE_TABLE_LEADER_HINT.get(),
                tablet_creation_timeout_ms: || TABLET_CREATION_TIMEOUT_MS.get() as i64,
                use_parent_table_id_field: || USE_PARENT_TABLE_ID_FIELD.get(),
                transaction_table_num_tablets: || TRANSACTION_TABLE_NUM_TABLETS.get(),
                transaction_table_num_tablets_per_tserver: || {
                    TRANSACTION_TABLE_NUM_TABLETS_PER_TSERVER.get()
                },
                enable_delete_truncate_xcluster_replicated_table: || {
                    ENABLE_DELETE_TRUNCATE_XCLUSTER_REPLICATED_TABLE.get()
                },
            },
        );
        let leases = YsqlLeases::new(
            writer.clone(),
            leadership.clone(),
            ts_manager.clone(),
            LeaseOptions {
                ttl: || Duration::from_millis(YSQL_LEASE_TTL_MS.get()),
                client_buffer: || Duration::from_millis(YSQL_LEASE_CLIENT_BUFFER_MS.get()),
                cleanup_interval: || {
                    Duration::from_millis(OBJECT_LOCK_CLEANUP_INTERVAL_MS.get().max(1))
                },
            },
        );
        let leader = Arc::new(LeaderServices {
            catalog: catalog.clone(),
            writer: writer.clone(),
            leadership: leadership.clone(),
            tables,
            ts_manager: ts_manager.clone(),
            contributors,
            follower_stream: Default::default(),
            load_balancer_enabled: AtomicBool::new(true),
            replication_factor: || REPLICATION_FACTOR.get(),
            balancer: Arc::new(BalancerStatus::new(LB_NUM_IDLE_RUNS.get().max(1) as usize)),
            load_balancing_flag: || ENABLE_LOAD_BALANCING.get(),
            transaction_tables_use_preferred_zones: || TXN_TABLES_USE_PREFERRED_ZONES.get(),
            splits,
            leases,
            blacklist_progress_initial_delay: || {
                Duration::from_secs(BLACKLIST_PROGRESS_INITIAL_DELAY_SECS.get().max(0) as u64)
            },
            delay_leader_blacklist_completion: || DELAY_LEADER_BLACKLIST_COMPLETION.get(),
        });

        // Ready only once the catalog reflects everything committed before the election: the
        // term's NoOp is committed (leadership checks LEADER_AND_READY), and the barrier makes
        // the materializer catch up to the tap.
        // Then the default rows a catalog must have (K10): all of them in a new universe. They are
        // written before Ready, as C++ writes them before setting leader_ready_term_.
        let hook_catalog = catalog.clone();
        let hook_ts_manager = ts_manager.clone();
        let hook_writer = writer.clone();
        let hook_leadership = Arc::downgrade(&leadership);
        let hook_schema = schema.clone();
        leadership.add_ready_hook(Box::new(move |term| {
            let schema = hook_schema.clone();
            let catalog = hook_catalog.clone();
            let ts_manager = hook_ts_manager.clone();
            let writer = hook_writer.clone();
            let leadership = hook_leadership.clone();
            let defaults = defaults.clone();
            Box::pin(async move {
                catalog.barrier().await;
                if !catalog.snapshot().loaded {
                    return Err(master_catalog_state::not_loaded_status());
                }
                let leadership = leadership.upgrade().ok_or_else(|| {
                    yb_host_api::Status::new(StatusCode::Aborted, "shutting down")
                })?;
                let token = leadership.catch_up_token(term)?;
                // A new YSQL universe's catalog starts as the initial sys catalog snapshot.
                let restored =
                    initdb::restore_initial_snapshot_if_needed(&catalog, schema, term).await?;
                write_default_rows(&writer, &token, &*defaults, &DEFAULTS_OPTIONS, restored)
                    .await?;
                ts_manager.on_leader_ready(&catalog.snapshot());
                Ok(())
            })
        }));
        runtime.spawn(leadership.clone().run());
        runtime.spawn(report_ready_term(leadership.clone(), schema.clone()));
        Core {
            catalog,
            leadership,
            leader,
            ts_rpc,
            host: schema,
        }
    }

    /// Starts what runs on every master (the self-check) and what runs in each leader term.
    pub(crate) fn run(
        &self,
        runtime: &tokio::runtime::Handle,
        store: Arc<dyn SysCatalogStore>,
        info: Arc<dyn MasterInfo>,
        rpc: Arc<dyn RpcClient>,
    ) {
        match info.registration() {
            Ok(registration) => self
                .leader
                .ts_manager
                .set_local_cloud_info(registration.cloud_info.clone().unwrap_or_default()),
            Err(status) => tracing::warn!("This master's registration is unknown: {status}"),
        }
        let blocking = BlockingPool::new("catalog-check", 1, 2);
        let catalog = self.catalog.clone();
        runtime.spawn(async move {
            loop {
                let interval = CATALOG_SELF_CHECK_INTERVAL_MS.get();
                tokio::time::sleep(millis(interval.max(1000))).await;
                if interval <= 0 {
                    continue;
                }
                let (catalog, store) = (catalog.clone(), store.clone());
                match blocking.run(move || self_check(&catalog, &*store)).await {
                    Ok(Err(status)) | Err(status) => {
                        tracing::warn!("Catalog self-check could not run: {status}")
                    }
                    Ok(Ok(_)) => {}
                }
            }
        });

        // A reload while leading leaves Ready as soon as it is requested, and comes back with a
        // new scope and pitr count once the reloaded catalog is published: nothing decided on the
        // stale catalog can be written after it. A reload that started and finished between two
        // wakeups is still seen, as a new generation.
        let leadership = self.leadership.clone();
        let catalog = self.catalog.clone();
        let mut changes = self.catalog.subscribe();
        let (mut requested, mut done) = self.catalog.subscribe_reloads();
        runtime.spawn(async move {
            let mut generation = catalog.snapshot().generation;
            loop {
                let reloading = catalog.is_reloading();
                let current = catalog.snapshot().generation;
                let leading = matches!(
                    leadership.phase(),
                    Phase::Ready { .. } | Phase::LeaseLapsed { .. }
                );
                if leading && (reloading || current != generation) {
                    leadership.begin_reload();
                }
                if !reloading {
                    if matches!(leadership.phase(), Phase::Reloading { .. }) {
                        leadership.end_reload();
                    }
                    generation = current;
                }
                tokio::select! {
                    r = requested.changed() => if r.is_err() { return },
                    r = done.changed() => if r.is_err() { return },
                    r = changes.changed() => if r.is_err() { return },
                }
            }
        });

        let leader = self.leader.clone();
        leader.splits.set_rpc(rpc.clone());
        let ts_rpc = self.ts_rpc.clone();
        let host = self.host.clone();
        let mut phases = self.leadership.subscribe();
        runtime.spawn(async move {
            let mut started: Option<TermScope> = None;
            loop {
                if let Some(scope) = leader.leadership.scope() {
                    let fresh = started
                        .as_ref()
                        .is_none_or(|s| s.is_cancelled() || s.term() != scope.term());
                    if fresh && !scope.is_cancelled() {
                        start_term(&scope, &leader, &info, &rpc, &ts_rpc, &host);
                        started = Some(scope);
                    }
                }
                if phases.changed().await.is_err() {
                    return;
                }
            }
        });
    }
}

/// The leader's background work for one term.
fn start_term(
    scope: &TermScope,
    leader: &Arc<LeaderServices>,
    info: &Arc<dyn MasterInfo>,
    rpc: &Arc<dyn RpcClient>,
    ts_rpc: &Arc<TsRpcExecutor>,
    host: &Arc<dyn SchemaHost>,
) {
    tracing::info!("Starting leader controllers for term {}", scope.term());
    // These run on their interval only: nothing triggers them.
    run_controller(
        scope,
        Arc::new(LivenessController {
            leader: leader.clone(),
        }),
        watch::channel(0).1,
    );
    run_controller(
        scope,
        Arc::new(UniverseUuidController {
            leader: leader.clone(),
        }),
        watch::channel(0).1,
    );

    run_controller(
        scope,
        SplitController::new(leader.splits.clone()),
        leader.catalog.subscribe(),
    );
    run_controller(
        scope,
        LeaseExpiryController::new(leader.leases.clone()),
        watch::channel(0).1,
    );
    run_controller(
        scope,
        TransactionTableController::new(leader.tables.clone()),
        leader.ts_manager.subscribe(),
    );
    let enabled_leader = leader.clone();
    run_controller(
        scope,
        BalancerController::new(
            leader.catalog.clone(),
            leader.leadership.clone(),
            leader.ts_manager.clone(),
            rpc.clone(),
            ts_rpc.clone(),
            // `AsyncAddServerTask` and `AsyncRemoveServerTask` are limited in attempts.
            retry_policy(true),
            BalancerFlags {
                enabled: Arc::new(move || {
                    ENABLE_LOAD_BALANCING.get()
                        && enabled_leader
                            .load_balancer_enabled
                            .load(std::sync::atomic::Ordering::Relaxed)
                }),
                initial_delay: || Duration::from_secs(LB_INITIAL_DELAY_SECS.get().max(0) as u64),
                interval: || millis(BG_TASK_WAIT_MS.get().max(1)),
                rpc_timeout: || millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
                min_leader_stepdown_retry_interval: || {
                    millis(MIN_LEADER_STEPDOWN_RETRY_INTERVAL_MS.get())
                },
                num_idle_runs: || LB_NUM_IDLE_RUNS.get().max(1) as usize,
                options: balancer_options,
                pause_before_run: || PAUSE_BG_LOOP_START.get(),
                pause_after_run: || PAUSE_BG_LOOP_END.get(),
                report: {
                    let host = host.clone();
                    Arc::new(move |enabled, metrics| {
                        report_cluster_metrics(&*host, enabled, metrics)
                    })
                },
            },
            leader.balancer.clone(),
        ),
        watch::channel(0).1,
    );
    let assigner = TabletAssigner::new(
        leader.tables.clone(),
        rpc.clone(),
        ts_rpc.clone(),
        retry_policy(false),
        || millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
        || millis(BG_TASK_WAIT_MS.get().max(1)),
    );
    run_controller(scope, assigner.clone(), leader.catalog.subscribe());
    run_controller(
        scope,
        assigner.elections(),
        leader.ts_manager.locations.subscribe(),
    );
    run_controller(
        scope,
        TableAlterer::new(
            leader.tables.clone(),
            rpc.clone(),
            ts_rpc.clone(),
            retry_policy(true),
            || millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
            || millis(BG_TASK_WAIT_MS.get().max(1)),
        ),
        leader.catalog.subscribe(),
    );
    run_controller(
        scope,
        TableDeleter::new(leader.tables.clone(), || {
            millis(BG_TASK_WAIT_MS.get().max(1))
        }),
        leader.catalog.subscribe(),
    );

    let deletes_leader = leader.clone();
    let deletes_rpc = rpc.clone();
    let deletes_executor = ts_rpc.clone();
    let deletes_scope = scope.clone();
    scope.spawn("replica-deletes", async move {
        loop {
            for delete in deletes_leader.ts_manager.deletes.drain() {
                let leader = deletes_leader.clone();
                let rpc = deletes_rpc.clone();
                let executor = deletes_executor.clone();
                // Lets a later report queue it again. Built before spawning: a task cancelled
                // before its first poll, or not spawned because the term ended, drops it too.
                let sent = SentGuard {
                    leader: leader.clone(),
                    delete: delete.clone(),
                };
                deletes_scope.entity().spawn("delete-replica", async move {
                    let _sent = sent;
                    let ts_uuid = delete.ts_uuid.clone();
                    let result = executor
                        .run(&ts_uuid, "DeleteTablet", &retry_policy(false), |_| {
                            let current = leader.leadership.check(&delete.token);
                            let address = leader.ts_manager.rpc_address(&delete.ts_uuid);
                            let (delete, rpc) = (delete.clone(), rpc.clone());
                            async move {
                                // Decided under another term or before a restore: the next
                                // report decides again.
                                current.map_err(Attempt::Fail)?;
                                let Some((host, port)) = address else {
                                    return Err(Attempt::Fail(yb_host_api::Status::new(
                                        StatusCode::NotFound,
                                        "tserver has no address",
                                    )));
                                };
                                delete
                                    .send(
                                        &*rpc,
                                        &host,
                                        port,
                                        millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
                                    )
                                    .await
                            }
                        })
                        .await;
                    if let Err(status) = result {
                        tracing::warn!("Deleting a replica failed: {status}");
                    }
                });
            }
            tokio::select! {
                _ = deletes_leader.ts_manager.deletes.wait() => {}
                _ = tokio::time::sleep(Duration::from_secs(1)) => {}
            }
        }
    });

    let self_uuid = info.instance().permanent_uuid().to_vec();
    let peers_info = info.clone();
    let peers_uuid = self_uuid.clone();
    let peers = Arc::new(move || {
        peers_info
            .committed_consensus_state()
            .map(|state| {
                state
                    .config
                    .peers
                    .iter()
                    .filter(|p| p.permanent_uuid() != peers_uuid)
                    .filter_map(|p| {
                        let hp = p
                            .last_known_private_addr
                            .first()
                            .or(p.last_known_broadcast_addr.first())?;
                        Some(Peer {
                            uuid: p.permanent_uuid().to_vec(),
                            host: hp.host().to_string(),
                            port: hp.port() as u16,
                        })
                    })
                    .collect()
            })
            .unwrap_or_default()
    });
    LocationStream::new(
        leader.ts_manager.clone(),
        rpc.clone(),
        self_uuid,
        scope.term(),
    )
    .run(
        scope,
        peers,
        || millis(LOCATION_PUSH_INTERVAL_MS.get()),
        || millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
    );
}

/// Marks a drained delete as no longer being sent when dropped.
struct SentGuard {
    leader: Arc<LeaderServices>,
    delete: DeleteReplica,
}

impl Drop for SentGuard {
    fn drop(&mut self) {
        self.leader.ts_manager.deletes.sent(&self.delete);
    }
}

/// Marks tservers that stopped heartbeating unresponsive and persists the change.
struct LivenessController {
    leader: Arc<LeaderServices>,
}

impl Controller for LivenessController {
    fn name(&self) -> &'static str {
        "tserver-liveness"
    }

    fn interval(&self) -> Duration {
        Duration::from_secs(1)
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let ts_manager = &self.leader.ts_manager;
            let changed = ts_manager.mark_unresponsive(Instant::now());
            let Ok(token) = self.leader.leadership.token() else {
                ts_manager.defer_persist(&changed);
                return;
            };
            let result = match ts_manager
                .persist(&self.leader.writer, &token, &changed)
                .await
            {
                Ok(()) => {
                    ts_manager
                        .persist_unpersisted(&self.leader.writer, &token)
                        .await
                }
                Err(status) => Err(status),
            };
            if let Err(status) = result {
                tracing::warn!("Persisting tserver registrations failed, will retry: {status}");
            }
        })
    }
}

/// Gives an existing universe a uuid once the heartbeat check is on: C++ runs
/// `SetUniverseUuidIfNeeded` from its background task, since the AutoFlag is promoted mid-term.
struct UniverseUuidController {
    leader: Arc<LeaderServices>,
}

impl Controller for UniverseUuidController {
    fn name(&self) -> &'static str {
        "universe-uuid"
    }

    fn interval(&self) -> Duration {
        Duration::from_secs(1)
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let Ok(token) = self.leader.leadership.token() else {
                return;
            };
            let result =
                set_universe_uuid_if_needed(&self.leader.writer, &token, &DEFAULTS_OPTIONS).await;
            if let Err(status) = result {
                tracing::warn!("Setting the universe uuid failed, will retry: {status}");
            }
        })
    }
}

/// `TEST_rust_master_allow_leadership`.
pub(crate) fn leadership_allowed() -> bool {
    ALLOW_LEADERSHIP.get()
}

/// The balancer's flags, read for each run as C++ builds `Options` for each run.
fn balancer_options() -> BalancerOptions {
    BalancerOptions {
        max_tablet_remote_bootstraps: LB_MAX_RBS.get(),
        max_tablet_remote_bootstraps_per_table: LB_MAX_RBS_PER_TABLE.get(),
        max_inbound_remote_bootstraps_per_ts: LB_MAX_INBOUND_RBS.get(),
        min_inbound_remote_bootstraps_per_ts: LB_MIN_INBOUND_RBS.get(),
        max_inbound_bytes_per_ts: (RBS_RATE_LIMIT.get().max(0) as u64)
            * BG_TASK_WAIT_MS.get().max(0) as u64
            / 1000,
        max_over_replicated_tablets_per_table: LB_MAX_OVER_REPLICATED.get(),
        max_concurrent_removals: LB_MAX_REMOVALS.get(),
        max_concurrent_adds: LB_MAX_ADDS.get(),
        max_concurrent_leader_moves: LB_MAX_MOVES.get(),
        max_concurrent_leader_moves_per_table: LB_MAX_MOVES_PER_TABLE.get(),
        leader_balance_threshold: LEADER_BALANCE_THRESHOLD.get(),
        leader_balance_unresponsive_timeout: millis(LEADER_BALANCE_UNRESPONSIVE_TIMEOUT_MS.get()),
        allow_leader_balancing_dead_node: ALLOW_LEADER_BALANCING_DEAD_NODE.get(),
        min_free_disk_space_pct: LB_MIN_FREE_DISK_PCT.get(),
        replication_factor: REPLICATION_FACTOR.get(),
        transaction_tables_use_preferred_zones: TXN_TABLES_USE_PREFERRED_ZONES.get(),
        enable_global_load_balancing: ENABLE_GLOBAL_LOAD_BALANCING.get(),
        drive_aware: LB_DRIVE_AWARE.get(),
        ignore_cloud_info_similarity: LB_IGNORE_CLOUD_INFO_SIMILARITY.get(),
        stepdown_to_preferred_leader_on_remove: STEPDOWN_TO_PREFERRED_LEADER_ON_REMOVE.get(),
        handle_under_replicated_tablets_only: LB_UNDER_REPLICATED_ONLY.get(),
        ..BalancerOptions::default()
    }
}
