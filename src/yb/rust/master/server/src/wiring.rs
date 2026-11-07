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
use master_leadership::{Leadership, Phase};
use master_rpc_handlers::{LeaderServices, MasterInfo};
use master_sys_catalog::{ApplyEvents, SysCatalogStore};
use master_table_lifecycle::{
    LifecycleOptions, SchemaHost, TableAlterer, TableDeleter, TableLifecycle, TabletAssigner,
};
use master_task_runtime::{
    run_controller, Attempt, BlockingPool, BoxFuture, Controller, RetryPolicy, TermScope,
    TsRpcExecutor,
};
use master_ts_manager::{
    DeleteReplica, HeartbeatContributor, LocationStream, Peer, TsManager, TsManagerOptions,
};
use tokio::sync::watch;
use yb_flags::{yb_flag, FlagRef, FlagTag};
use yb_host_api::metrics::MetricRegistry;
use yb_host_api::rpc::RpcClient;
use yb_host_api::StatusCode;

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
        tombstone_evicted_replicas: || TOMBSTONE_EVICTED_REPLICAS.get(),
        use_pending_config_op_id: || USE_PENDING_CONFIG_OP_ID.get(),
        register_from_raft: || REGISTER_TS_FROM_RAFT.get(),
        metadata_min_time_left: || millis(HEARTBEAT_RPC_TIMEOUT_MS.get()) / 2,
        max_leader_lease_expired: || {
            Duration::from_secs(MAX_LEADER_LEASE_EXPIRED_SECS.get().into())
        },
        use_private_ip: || USE_PRIVATE_IP.get(),
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
        let tables = TableLifecycle::new(
            catalog.clone(),
            writer.clone(),
            leadership.clone(),
            ts_manager.clone(),
            schema,
            LifecycleOptions {
                replication_factor: || REPLICATION_FACTOR.get(),
                max_create_tablets_per_ts: || MAX_CREATE_TABLETS_PER_TS.get(),
                check_ts_count_for_create_table: || CHECK_TS_COUNT_FOR_CREATE_TABLE.get(),
                tablet_creation_timeout_ms: || TABLET_CREATION_TIMEOUT_MS.get() as i64,
                use_parent_table_id_field: || USE_PARENT_TABLE_ID_FIELD.get(),
                transaction_table_num_tablets: || TRANSACTION_TABLE_NUM_TABLETS.get(),
                transaction_table_num_tablets_per_tserver: || {
                    TRANSACTION_TABLE_NUM_TABLETS_PER_TSERVER.get()
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
        leadership.add_ready_hook(Box::new(move |term| {
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
                write_default_rows(&writer, &token, &*defaults, &DEFAULTS_OPTIONS).await?;
                ts_manager.on_leader_ready(&catalog.snapshot());
                Ok(())
            })
        }));
        runtime.spawn(leadership.clone().run());
        Core {
            catalog,
            leadership,
            leader,
            ts_rpc: TsRpcExecutor::new(ts_rpc_limit),
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
        let ts_rpc = self.ts_rpc.clone();
        let mut phases = self.leadership.subscribe();
        runtime.spawn(async move {
            let mut started: Option<TermScope> = None;
            loop {
                if let Some(scope) = leader.leadership.scope() {
                    let fresh = started
                        .as_ref()
                        .is_none_or(|s| s.is_cancelled() || s.term() != scope.term());
                    if fresh && !scope.is_cancelled() {
                        start_term(&scope, &leader, &info, &rpc, &ts_rpc);
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
        TabletAssigner::new(
            leader.tables.clone(),
            rpc.clone(),
            ts_rpc.clone(),
            retry_policy(false),
            || millis(MASTER_TS_RPC_TIMEOUT_MS.get()),
            || millis(BG_TASK_WAIT_MS.get().max(1)),
        ),
        leader.catalog.subscribe(),
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
