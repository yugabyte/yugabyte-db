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

//! Checkpointing the streams of an outbound group.
//!
//! A CHECKPOINTING namespace has a task per term (`XClusterCheckpointNamespaceTask`): in
//! automatic mode it creates `sequences_data`, advances the OID counters and creates the DDL
//! replication extension; then it creates the initial tables' streams and has a tserver bootstrap
//! them at the end of their WAL, a batch at a time, until the namespace is READY. A table created
//! later gets its stream, checkpointed at op 0, before it runs (`AddTableToXClusterSourceTask`).

use std::collections::BTreeSet;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use master_leadership::LeaderToken;
use master_xrepl::strip_sequences_data_alias;
use protobuf::{EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::sys_xcluster_outbound_replication_group_entry_pb::{
    namespace_info_pb::{State as NsState, TableInfoPB},
    NamespaceInfoPB,
};
use yb_proto::pb::cdc_service::{BootstrapProducerRequestPB, BootstrapProducerResponsePB};
use yb_proto::pb::common_types::xcluster_namespace_info_pb::XClusterRole;
use yb_proto::pb::master_rust_peer::{
    ClosestLiveTserverRequestPB, ClosestLiveTserverResponsePB, CreateSequencesDataTableRequestPB,
    CreateSequencesDataTableResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tserver_service::{AdminExecutePgsqlRequestPB, AdminExecutePgsqlResponsePB};

use crate::outbound::{group, table_pb};
use crate::{
    corrupt, eligible_tables, is_ddl_queue_table, is_eligible, is_replicated_ddls_table,
    sequences_data_alias, status, XCluster, DDL_QUEUE_PG_SCHEMA_NAME,
};

static TASK_IDS: AtomicU64 = AtomicU64::new(0);

/// `IsRetryableError` of the checkpoint task.
fn retryable(s: &Status) -> bool {
    matches!(
        s.code,
        StatusCode::TryAgain
            | StatusCode::ServiceUnavailable
            | StatusCode::NetworkError
            | StatusCode::LeaderNotReadyToServe
            | StatusCode::LeaderHasNoLease
    )
}

impl XCluster {
    /// Starts a checkpoint task for each CHECKPOINTING namespace without one this term
    /// (`StartPostLoadTasks` after a leader change).
    pub(crate) fn start_checkpoint_tasks(self: &Arc<Self>, token: &LeaderToken) {
        let snapshot = self.catalog.snapshot();
        let mut tasks = self.checkpoints.lock().unwrap();
        tasks.retain(|_, (term, h)| *term == token.term() && !h.is_finished());
        for (id, pb) in crate::outbound::groups(&snapshot) {
            for (ns, info) in &pb.namespace_infos {
                let key = (id.clone(), ns.clone());
                if info.state() != NsState::CHECKPOINTING || tasks.contains_key(&key) {
                    continue;
                }
                let this = self.clone();
                let (task_id, token) = (TASK_IDS.fetch_add(1, Ordering::Relaxed), *token);
                let (g, n) = key.clone();
                let handle = tokio::spawn(async move {
                    let result = this.checkpoint_namespace(&token, &g, &n).await;
                    if let Err(s) = result {
                        if this.leadership.check(&token).is_ok() {
                            this.mark_namespace_failed(&token, &g, &n, &s).await;
                        }
                    }
                    let _ = task_id;
                });
                tasks.insert(key, (token.term(), handle.abort_handle()));
            }
        }
    }

    /// The steps of `XClusterCheckpointNamespaceTask`.
    async fn checkpoint_namespace(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
    ) -> Result<(), Status> {
        let automatic = group(&self.catalog.snapshot(), id)?.1.automatic_ddl_mode();
        if automatic {
            let _: CreateSequencesDataTableResponsePB = self
                .host_call(
                    "CreateSequencesDataTable",
                    CreateSequencesDataTableRequestPB::new(),
                )
                .await?;
            let alias = sequences_data_alias(ns);
            self.update_group(token, id, |_, pb, _| {
                let info = crate::outbound::namespace_info_mut(pb, id, ns)?;
                if info.table_infos.contains_key(&alias) {
                    return Ok((false, ()));
                }
                let mut t = TableInfoPB::new();
                t.set_is_checkpointing(true);
                t.set_is_part_of_initial_bootstrap(true);
                info.table_infos.insert(alias.clone(), t);
                Ok((true, ()))
            })
            .await?;
            self.advance_oid_counters(token, ns).await?;
            self.setup_ddl_replication_extension(token, ns)
                .await
                .map_err(|s| {
                    status(
                        s.code,
                        format!(
                            "Failed to setup xCluster DDL replication extension: {}",
                            s.message
                        ),
                    )
                })?;
            self.set_ddl_queue_table_initial(token, id, ns).await?;
        }
        self.create_initial_streams(token, id, ns).await?;
        let mut delay = Duration::from_millis(100);
        loop {
            self.leadership.check(token)?;
            let result = self.checkpoint_initial_streams(id, ns).await;
            while (self.options.block_checkpoint_namespace_task)() {
                tokio::time::sleep(Duration::from_millis(100)).await;
            }
            let result = match result {
                Err(s) if retryable(&s) => {
                    tracing::info!(
                        "Failed to checkpoint streams with retryable error: {s}. Scheduling retry"
                    );
                    tokio::time::sleep(delay).await;
                    delay = Duration::from_secs_f64((delay.as_secs_f64() * 1.1).min(5.0));
                    continue;
                }
                other => other?,
            };
            if self
                .mark_initial_checkpointed(token, id, ns, result)
                .await?
            {
                return Ok(());
            }
        }
    }

    /// `CreateStreamsForInitialBootstrap`: a stream for each initial table without one, written
    /// with the group in one write.
    async fn create_initial_streams(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
    ) -> Result<(), Status> {
        self.xrepl
            .sync_point("XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap")
            .await;
        self.xrepl
            .create_cdc_state_table_if_not_found(
                &*self.tables,
                token,
                Instant::now() + (self.options.rpc_timeout)(),
            )
            .await
            .map_err(|e| crate::status_of(&e))?;
        let count = self
            .update_group(token, id, |snapshot, pb, streams| {
                let automatic = pb.automatic_ddl_mode();
                let info = crate::outbound::namespace_info_mut(pb, id, ns)?;
                if info.state() != NsState::CHECKPOINTING {
                    return Err(status(
                        StatusCode::IllegalState,
                        "Namespace in unexpected state",
                    ));
                }
                for (table_id, t) in info.table_infos.iter_mut() {
                    if !t.is_part_of_initial_bootstrap() || !t.stream_id().is_empty() {
                        continue;
                    }
                    if !t.is_checkpointing() {
                        return Err(status(
                            StatusCode::IllegalState,
                            format!(
                                "Table {table_id} does not have stream but is marked as completed \
                                 checkpoint"
                            ),
                        ));
                    }
                    let stream =
                        self.new_stream(snapshot, table_id, automatic, false, false, streams)?;
                    t.set_stream_id(stream.0.clone().into_bytes());
                    streams.push(stream);
                }
                let n = streams.len();
                Ok((n > 0, n))
            })
            .await?;
        if count > 0 {
            tracing::info!(
                "xClusterOutboundReplicationGroup {id}: Created xcluster streams for {count} \
                 table(s) in namespace {ns}"
            );
        }
        Ok(())
    }

    /// `CheckpointStreamsForInitialBootstrap`: the next batch of initial tables still
    /// checkpointing, bootstrapped at the end of their WAL; an empty batch means all are done.
    /// Returns the tables and whether a bootstrap is required.
    async fn checkpoint_initial_streams(
        &self,
        id: &str,
        ns: &str,
    ) -> Result<(Vec<String>, bool), Status> {
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id)?.1;
        let info = crate::outbound::namespace_info(&pb, id, ns)?;
        if info.state() != NsState::CHECKPOINTING {
            return Err(status(
                StatusCode::IllegalState,
                "Namespace in unexpected state",
            ));
        }
        // Automatic mode always needs a bootstrap: OIDs such as enum labels must match, which no
        // check of the tables' data shows.
        let check = !pb.automatic_ddl_mode() && !info.initial_bootstrap_required();
        let limit = (self.options.max_streams_to_checkpoint)() as usize;
        let mut batch = Vec::new();
        for (table_id, t) in &info.table_infos {
            if !(t.is_part_of_initial_bootstrap() && t.is_checkpointing()) {
                continue;
            }
            if t.stream_id().is_empty() {
                return Err(status(
                    StatusCode::IllegalState,
                    format!("Stream id not found for table {table_id}"),
                ));
            }
            if batch.len() >= limit {
                break;
            }
            batch.push((
                table_id.clone(),
                String::from_utf8_lossy(t.stream_id()).into_owned(),
            ));
        }
        if batch.is_empty() {
            return Ok((Vec::new(), false));
        }
        tracing::info!(
            "xClusterOutboundReplicationGroup {id}: Checkpointing xcluster streams for {} \
             table(s) in namespace {ns}",
            batch.len()
        );
        let required = self.bootstrap_producer(&batch, check).await?;
        Ok((
            batch.into_iter().map(|(t, _)| t).collect(),
            pb.automatic_ddl_mode() || required,
        ))
    }

    /// `MarkBootstrapTablesAsCheckpointed`: true once the namespace is READY, which the call
    /// after the last batch makes it, after checking its tables did not change meanwhile.
    async fn mark_initial_checkpointed(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
        (tables, required): (Vec<String>, bool),
    ) -> Result<bool, Status> {
        self.update_group(token, id, |snapshot, pb, _| {
            let automatic = pb.automatic_ddl_mode();
            let info = crate::outbound::namespace_info_mut(pb, id, ns)?;
            if info.state() != NsState::CHECKPOINTING {
                return Err(status(
                    StatusCode::IllegalState,
                    "Namespace in unexpected state",
                ));
            }
            if tables.is_empty() {
                let now: BTreeSet<String> = eligible_tables(snapshot, ns, automatic)?
                    .into_iter()
                    .map(|t| t.id)
                    .collect();
                let initial: BTreeSet<String> = info
                    .table_infos
                    .iter()
                    .filter(|(_, t)| t.is_part_of_initial_bootstrap())
                    .map(|(k, _)| k.clone())
                    .collect();
                let diff: Vec<&String> = now.symmetric_difference(&initial).collect();
                if !diff.is_empty() {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!(
                            "List of tables changed during xCluster checkpoint of replication \
                             group xClusterOutboundReplicationGroup {id}: {diff:?}"
                        ),
                    ));
                }
                tracing::info!(
                    "xClusterOutboundReplicationGroup {id}: Marking namespace {ns} as READY"
                );
                info.state = Some(EnumOrUnknown::new(NsState::READY));
                return Ok((true, true));
            }
            if required {
                info.set_initial_bootstrap_required(true);
            }
            for table_id in &tables {
                let t = info.table_infos.get_mut(table_id).ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!(
                            "xClusterOutboundReplicationGroup {id}: Table {table_id} not found"
                        ),
                    )
                })?;
                if !(t.is_part_of_initial_bootstrap() && t.is_checkpointing()) {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!("Table {table_id} is not checkpointing"),
                    ));
                }
                t.set_is_checkpointing(false);
            }
            Ok((true, false))
        })
        .await
    }

    /// `MarkCheckpointNamespaceAsFailed`: FAILED is terminal; removing the namespace or deleting
    /// the group cleans it up.
    async fn mark_namespace_failed(&self, token: &LeaderToken, id: &str, ns: &str, s: &Status) {
        tracing::warn!(
            "xClusterOutboundReplicationGroup {id}: Failed to checkpoint namespace {ns}: {s}"
        );
        let result = self
            .update_group(token, id, |_, pb, _| {
                let Some(info) = pb.namespace_infos.get_mut(ns) else {
                    return Ok((false, ()));
                };
                if info.state() != NsState::CHECKPOINTING {
                    return Ok((false, ()));
                }
                info.state = Some(EnumOrUnknown::new(NsState::FAILED));
                info.error_status = MessageField::some(crate::app_status(&Err(status(
                    StatusCode::InternalError,
                    format!("Failed to checkpoint namespace {ns}: {s}"),
                ))));
                Ok((true, ()))
            })
            .await;
        if let Err(e) = result {
            tracing::warn!("Marking namespace {ns} of {id} FAILED: {e}");
        }
    }

    /// `SetDDLQueueTableIsPartOfInitialBootstrap`: the extension's `ddl_queue` table joined the
    /// group when it was created; the target restores it with the initial tables.
    async fn set_ddl_queue_table_initial(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let queue = snapshot
            .iter(SysRowEntryType::TABLE)
            .find(|t| {
                t.table().is_some_and(|h| h.namespace_id == ns.as_bytes())
                    && master_table_lifecycle::lookup::is_visible_to_client(t)
                    && t.parse().is_ok_and(|pb| is_ddl_queue_table(&pb))
            })
            .map(|t| String::from_utf8_lossy(&t.key.id).into_owned())
            .ok_or_else(|| {
                status(
                    StatusCode::IllegalState,
                    format!(
                        "xCluster DDL queue table not found in namespace {ns} of \
                         xClusterOutboundReplicationGroup {id}"
                    ),
                )
            })?;
        self.update_group(token, id, |_, pb, _| {
            let info = crate::outbound::namespace_info_mut(pb, id, ns)?;
            let t = info.table_infos.get_mut(&queue).ok_or_else(|| {
                status(
                    StatusCode::IllegalState,
                    format!(
                        "Table {queue} not found in namespace info of replication group \
                         xClusterOutboundReplicationGroup {id}"
                    ),
                )
            })?;
            if t.is_part_of_initial_bootstrap() {
                return Ok((false, ()));
            }
            t.set_is_part_of_initial_bootstrap(true);
            Ok((true, ()))
        })
        .await
    }

    /// `CheckpointStreamsToEndOfWAL`: a tserver leading the first table's first tablet
    /// bootstraps the streams (`BootstrapProducer` with their ids), writing their `cdc_state`
    /// rows at each tablet's latest op id.
    pub(crate) async fn bootstrap_producer(
        &self,
        table_streams: &[(String, String)],
        check_if_bootstrap_required: bool,
    ) -> Result<bool, Status> {
        let snapshot = self.catalog.snapshot();
        let mut req = BootstrapProducerRequestPB::new();
        req.set_check_if_bootstrap_required(check_if_bootstrap_required);
        let mut leader = None;
        for (table_id, stream_id) in table_streams {
            let stripped = strip_sequences_data_alias(table_id);
            let visible = snapshot
                .get(SysRowEntryType::TABLE, stripped.as_bytes())
                .is_some_and(|t| master_table_lifecycle::lookup::is_visible_to_client(t));
            if !visible {
                return Err(status(
                    StatusCode::NotFound,
                    format!("Table does not exist: {table_id}"),
                ));
            }
            req.table_ids.push(stripped.to_string());
            req.xrepl_stream_ids.push(stream_id.clone().into_bytes());
            if leader.is_none() {
                let tablet = snapshot
                    .hosting_tablets_of(stripped.as_bytes())
                    .first()
                    .map(|t| t.key.id.to_vec())
                    .ok_or_else(|| status(StatusCode::NotFound, "No tablets for the table"))?;
                // After a master failover the leader may not be known yet.
                let uuid = self
                    .ts_manager
                    .locations
                    .get(&tablet)
                    .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
                    .ok_or_else(|| status(StatusCode::TryAgain, "Tablet leader not found"))?;
                leader = Some(uuid);
            }
        }
        let uuid = leader.ok_or_else(|| {
            status(
                StatusCode::IllegalState,
                "No valid tserver found to bootstrap from",
            )
        })?;
        let resp: BootstrapProducerResponsePB = self
            .ts_call(&uuid, "yb.cdc.CDCService", "BootstrapProducer", &req)
            .await?;
        if let Some(e) = resp.error.as_ref() {
            return Err(Status::new(
                StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                e.status.message().to_string(),
            ));
        }
        if !resp.cdc_bootstrap_ids.is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                "No new streams should have been created",
            ));
        }
        Ok(resp.bootstrap_required())
    }

    /// One call to a tserver with the master's RPC timeout.
    pub(crate) async fn ts_call<Req: Message, Resp: Message>(
        &self,
        uuid: &[u8],
        service: &str,
        method: &str,
        req: &Req,
    ) -> Result<Resp, Status> {
        let (host, port) = self.ts_manager.rpc_address(uuid).ok_or_else(|| {
            status(
                StatusCode::ServiceUnavailable,
                format!("No address for tserver {}", String::from_utf8_lossy(uuid)),
            )
        })?;
        let bytes = req
            .write_to_bytes()
            .map_err(|e| status(StatusCode::InternalError, e.to_string()))?;
        let out = self
            .rpc()?
            .call(
                &host,
                port,
                service,
                method,
                &bytes,
                (self.options.rpc_timeout)(),
            )
            .await?;
        Resp::parse_from_bytes(&out).map_err(corrupt)
    }

    /// `ExecutePgsqlStatements` on the closest live tserver.
    pub(crate) async fn execute_pgsql(
        &self,
        database: &str,
        statements: &[String],
    ) -> Result<(), Status> {
        let mut req = ClosestLiveTserverRequestPB::new();
        for ts in self.ts_manager.live() {
            req.live_tservers.push(ts.info());
        }
        let closest: ClosestLiveTserverResponsePB =
            self.host_call("ClosestLiveTserver", req).await?;
        let mut req = AdminExecutePgsqlRequestPB::new();
        req.set_database_name(database.to_string());
        req.pgsql_statements = statements.to_vec();
        req.set_yb_internal_conn_kind("xcluster_setup".to_string());
        let (host, port) = self
            .ts_manager
            .rpc_address(closest.permanent_uuid())
            .ok_or_else(|| status(StatusCode::ServiceUnavailable, "No live tserver"))?;
        let bytes = req
            .write_to_bytes()
            .map_err(|e| status(StatusCode::InternalError, e.to_string()))?;
        let out = self
            .rpc()?
            .call(
                &host,
                port,
                "yb.tserver.TabletServerService",
                "AdminExecutePgsql",
                &bytes,
                (self.options.ysql_statement_timeout)(),
            )
            .await?;
        let resp = AdminExecutePgsqlResponsePB::parse_from_bytes(&out).map_err(corrupt)?;
        if let Some(e) = resp.error.as_ref() {
            return Err(status(
                StatusCode::IllegalState,
                format!("Error executing pgsql statements: {}", e.status.message()),
            ));
        }
        Ok(())
    }

    /// `SetupDDLReplicationExtension` as a source: during a switchover the namespace stays a
    /// target until the reverse group is dropped.
    async fn setup_ddl_replication_extension(
        &self,
        token: &LeaderToken,
        ns: &str,
    ) -> Result<(), Status> {
        if self.is_automatic_mode_target(&self.catalog.snapshot(), ns) {
            tracing::info!(
                "Switchover detected for namespace {ns}, keeping extension replication role as a \
                 Target."
            );
            return Ok(());
        }
        let name = XCluster::namespace_name(&self.catalog.snapshot(), ns)?;
        tracing::info!(
            "Creating if exists {DDL_QUEUE_PG_SCHEMA_NAME} extension for namespace {ns} ({name})"
        );
        self.execute_pgsql(
            &name,
            &[format!(
                "CREATE EXTENSION IF NOT EXISTS {DDL_QUEUE_PG_SCHEMA_NAME}"
            )],
        )
        .await?;
        self.set_role(token, ns, XClusterRole::AUTOMATIC_SOURCE)
            .await
    }

    /// `DropDDLReplicationExtensionIfExists`, unless another automatic group (other than
    /// `dropping`) has the namespace or a switchover keeps it a target.
    pub(crate) async fn drop_ddl_replication_extension(
        &self,
        token: &LeaderToken,
        ns: &str,
        dropping: Option<&str>,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        for (id, pb) in crate::outbound::groups(&snapshot) {
            if Some(id.as_str()) != dropping
                && pb.automatic_ddl_mode()
                && pb.namespace_infos.contains_key(ns)
            {
                tracing::info!(
                    "Skipping drop of DDL replication extension for namespace {ns} as it also \
                     belongs to replication group {id}"
                );
                return Ok(());
            }
        }
        if self.is_automatic_mode_target(&snapshot, ns) {
            tracing::info!(
                "Switchover detected for namespace {ns}, not dropping yb_xcluster_ddl_replication \
                 extension."
            );
            return Ok(());
        }
        let name = XCluster::namespace_name(&self.catalog.snapshot(), ns)?;
        tracing::info!(
            "Dropping if exists {DDL_QUEUE_PG_SCHEMA_NAME} extension for namespace {ns} ({name})"
        );
        self.set_role(token, ns, XClusterRole::NOT_AUTOMATIC_MODE)
            .await?;
        self.execute_pgsql(
            &name,
            &[format!(
                "DROP EXTENSION IF EXISTS {DDL_QUEUE_PG_SCHEMA_NAME}"
            )],
        )
        .await
    }

    /// `AdvanceOidCounters` and `InvalidateTserverOidCaches`: the source's OID counters move above
    /// every OID in use, so the ones the target preserves cannot collide.
    async fn advance_oid_counters(&self, token: &LeaderToken, ns: &str) -> Result<(), Status> {
        let hooks = self
            .hooks
            .get()
            .ok_or_else(|| status(StatusCode::ServiceUnavailable, "No OID hooks yet"))?;
        hooks.advance_oid_counters(ns.as_bytes().to_vec()).await?;
        self.invalidate_tserver_oid_caches(token).await
    }

    /// `InvalidateTserverOidCaches`.
    pub(crate) async fn invalidate_tserver_oid_caches(
        &self,
        token: &LeaderToken,
    ) -> Result<(), Status> {
        use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;
        self.writer
            .mutate(token, |snapshot| {
                let config = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut pb: SysClusterConfigEntryPB = config.parse().map_err(corrupt)?;
                pb.set_oid_cache_invalidations_count(pb.oid_cache_invalidations_count() + 1);
                pb.set_version(pb.version() + 1);
                Ok((
                    master_catalog_state::Mutation::new()
                        .read(config)
                        .upsert_message(SysRowEntryType::CLUSTER_CONFIG, &config.key.id, &pb)?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// An OID above every normal-space OID the namespace used: one newly reserved.
    pub(crate) async fn next_normal_oid(
        &self,
        token: &LeaderToken,
        ns: &str,
    ) -> Result<u32, Status> {
        let hooks = self
            .hooks
            .get()
            .ok_or_else(|| status(StatusCode::ServiceUnavailable, "No OID hooks yet"))?;
        let _ = token;
        hooks.reserve_normal_oid(ns.as_bytes().to_vec()).await
    }

    /// `AddTableToXClusterSourceTask` for each group with the new table's namespace, before the
    /// table runs: a stream (and in automatic mode a WAL anchor stream), checkpointed at op 0.
    pub(crate) async fn add_new_table(&self, table_id: &[u8]) -> Result<(), Status> {
        let token = self.leadership.token()?;
        let snapshot = self.catalog.snapshot();
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Ok(());
        };
        let Some(hot) = table.table() else {
            return Ok(());
        };
        let pb = table_pb(&snapshot, table_id).ok_or_else(|| corrupt("table row"))?;
        let table_id = String::from_utf8_lossy(table_id).into_owned();
        for (id, group_pb) in XCluster::groups_with_namespace(&snapshot, &hot.namespace_id) {
            if !is_eligible(&snapshot, table, &pb, group_pb.automatic_ddl_mode()) {
                continue;
            }
            let ns = String::from_utf8_lossy(&hot.namespace_id).into_owned();
            // Internal DDL tables are connected at setup, so they need no WAL anchor.
            let anchor = !is_ddl_queue_table(&pb) && !is_replicated_ddls_table(&pb);
            self.create_stream_for_new_table(&token, &id, &ns, &table_id, anchor)
                .await?;
            self.checkpoint_new_table(&token, &id, &ns, &table_id)
                .await?;
        }
        Ok(())
    }

    /// `CreateStreamForNewTable`.
    async fn create_stream_for_new_table(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
        table_id: &str,
        needs_wal_anchor: bool,
    ) -> Result<(), Status> {
        let wal_anchors = (self.options.wal_anchor_streams)();
        self.update_group(token, id, |snapshot, pb, streams| {
            let automatic = pb.automatic_ddl_mode();
            let Some(info) = pb.namespace_infos.get_mut(ns) else {
                return Ok((false, ()));
            };
            if info.state() == NsState::FAILED {
                return Ok((false, ()));
            }
            if info
                .table_infos
                .get(table_id)
                .is_some_and(|t| !t.stream_id().is_empty())
            {
                return Ok((false, ()));
            }
            let mut t = TableInfoPB::new();
            t.set_is_checkpointing(true);
            t.set_is_part_of_initial_bootstrap(false);
            let stream = self.new_stream(snapshot, table_id, automatic, false, false, streams)?;
            t.set_stream_id(stream.0.clone().into_bytes());
            streams.push(stream);
            if needs_wal_anchor && automatic && wal_anchors {
                let anchor =
                    self.new_stream(snapshot, table_id, automatic, false, true, streams)?;
                tracing::info!(
                    "Created xCluster WAL anchor stream {} for new table {table_id}",
                    anchor.0
                );
                t.set_wal_anchor_stream_id(anchor.0.clone().into_bytes());
                streams.push(anchor);
            }
            info.table_infos.insert(table_id.to_string(), t);
            Ok((true, ()))
        })
        .await
    }

    /// `CheckpointNewTable` and `MarkNewTablesAsCheckpointed`.
    async fn checkpoint_new_table(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
        table_id: &str,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let Ok((_, pb)) = group(&snapshot, id) else {
            return Ok(());
        };
        let Some(info) = pb.namespace_infos.get(ns) else {
            return Ok(());
        };
        if info.state() == NsState::FAILED {
            return Ok(());
        }
        let t = info.table_infos.get(table_id).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("xClusterOutboundReplicationGroup {id}: Table {table_id} not found"),
            )
        })?;
        if !t.is_checkpointing() {
            return Ok(());
        }
        let mut streams = vec![(
            table_id.to_string(),
            String::from_utf8_lossy(t.stream_id()).into_owned(),
        )];
        if !t.wal_anchor_stream_id().is_empty() {
            streams.push((
                table_id.to_string(),
                String::from_utf8_lossy(t.wal_anchor_stream_id()).into_owned(),
            ));
        }
        self.xrepl.checkpoint_streams_to_op0(&streams).await?;
        self.mark_new_table_checkpointed(token, id, ns, table_id)
            .await?;
        tracing::info!(
            "Table {table_id} successfully checkpointed for xCluster universe replication with \
             Stream: {}",
            streams[0].1
        );
        Ok(())
    }

    async fn mark_new_table_checkpointed(
        &self,
        token: &LeaderToken,
        id: &str,
        ns: &str,
        table_id: &str,
    ) -> Result<(), Status> {
        self.update_group(token, id, |_, pb, _| {
            let Some(t) = pb
                .namespace_infos
                .get_mut(ns)
                .and_then(|i| i.table_infos.get_mut(table_id))
            else {
                return Ok((false, ()));
            };
            if !t.is_checkpointing() {
                return Ok((false, ()));
            }
            t.set_is_checkpointing(false);
            Ok((true, ()))
        })
        .await
    }

    /// `CreateAndCheckpointStreamsForAnchoredTables`: a table that still has a WAL anchor gets a
    /// new stream when its stream is gone or being deleted (the target rolled back the table's
    /// creation and retries it), checkpointed at op 0.
    pub(crate) async fn create_and_checkpoint_anchored_streams(
        &self,
        token: &LeaderToken,
        _deadline: Instant,
        id: &str,
        ns: &str,
        table_ids: &[String],
    ) -> Result<(), Status> {
        let alive = {
            let snapshot = self.catalog.snapshot();
            master_xrepl::stream_rows(&snapshot)
                .into_iter()
                .filter(|r| {
                    r.pb.namespace_id().is_empty()
                        && !r.is_deleting()
                        && r.pb.table_id.iter().any(|t| table_ids.contains(t))
                })
                .map(|r| r.id)
                .collect::<std::collections::HashSet<_>>()
        };
        let to_checkpoint = self
            .update_group(token, id, |snapshot, pb, streams| {
                let automatic = pb.automatic_ddl_mode();
                let Some(info) = pb.namespace_infos.get_mut(ns) else {
                    return Ok((false, Vec::new()));
                };
                if !is_ready_ns(info)? {
                    return Ok((false, Vec::new()));
                }
                let mut out = Vec::new();
                for table_id in table_ids {
                    let Some(t) = info.table_infos.get_mut(table_id) else {
                        continue;
                    };
                    if t.wal_anchor_stream_id().is_empty() {
                        continue;
                    }
                    if !t.stream_id().is_empty() {
                        let stream_id = String::from_utf8_lossy(t.stream_id()).into_owned();
                        if alive.contains(&stream_id) {
                            if t.is_checkpointing() {
                                out.push(table_id.clone());
                            }
                            continue;
                        }
                        tracing::info!(
                            "Replacing xCluster stream {stream_id} of table {table_id} as it is \
                             being deleted"
                        );
                    }
                    let stream =
                        self.new_stream(snapshot, table_id, automatic, true, false, streams)?;
                    tracing::info!(
                        "Creating xCluster stream {} for table {table_id} from WAL_ANCHOR \
                         stream {}",
                        stream.0,
                        String::from_utf8_lossy(t.wal_anchor_stream_id())
                    );
                    t.set_stream_id(stream.0.clone().into_bytes());
                    t.set_is_checkpointing(true);
                    streams.push(stream);
                    out.push(table_id.clone());
                }
                Ok((!streams.is_empty(), out))
            })
            .await?;
        for table_id in to_checkpoint {
            if (self.options.fail_table_stream_checkpoint)() {
                return Err(status(
                    StatusCode::TryAgain,
                    "Failing table stream checkpoint for testing",
                ));
            }
            // A failure makes the target retry the DDL, and the retry checkpoints again.
            if let Err(s) = self.checkpoint_new_table(token, id, ns, &table_id).await {
                tracing::warn!("Failed to checkpoint xCluster stream of table {table_id}: {s}");
                return Err(s);
            }
        }
        Ok(())
    }
}

fn is_ready_ns(info: &NamespaceInfoPB) -> Result<bool, Status> {
    if info.state() == NsState::FAILED {
        let e = &info.error_status;
        return Err(Status::new(
            StatusCode::from_i32(protobuf::Enum::value(&e.code())),
            e.message().to_string(),
        ));
    }
    Ok(info.state() == NsState::READY)
}
