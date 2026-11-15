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

//! The target's inbound replication groups (`XClusterTargetManager`): a group is its
//! `UNIVERSE_REPLICATION` row and its producer entry in the cluster config's consumer registry,
//! which tservers poll the source by. Setups run as in-memory tasks whose outcome
//! `IsSetupUniverseReplicationDone` reports once; replication status comes from tserver
//! heartbeats and is kept in memory, as in C++.

use std::collections::{BTreeMap, HashMap};
use std::sync::Arc;

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use protobuf::{EnumOrUnknown, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_universe_replication_entry_pb::State as UniverseState, SysClusterConfigEntryPB,
    SysUniverseReplicationEntryPB,
};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::xcluster_namespace_info_pb::XClusterRole;
use yb_proto::pb::common_types::{ReplicationErrorPb, XClusterReplicationType};
use yb_proto::pb::master_heartbeat::XClusterConsumerReplicationStatusPB;
use yb_proto::pb::master_replication::{
    get_universe_replication_info_response_pb::{DbScopedInfoPB, TableInfoPB},
    DeleteCDCStreamRequestPB, DeleteCDCStreamResponsePB, DeleteUniverseReplicationRequestPB,
    DeleteUniverseReplicationResponsePB, GetReplicationStatusRequestPB,
    GetReplicationStatusResponsePB, GetUniverseReplicationInfoRequestPB,
    GetUniverseReplicationInfoResponsePB, GetUniverseReplicationRequestPB,
    GetUniverseReplicationResponsePB, GetUniverseReplicationsRequestPB,
    GetUniverseReplicationsResponsePB, IsSetupUniverseReplicationDoneRequestPB,
    IsSetupUniverseReplicationDoneResponsePB, ReplicationStatusErrorPB, ReplicationStatusPB,
    SetUniverseReplicationEnabledRequestPB, SetupUniverseReplicationRequestPB,
    SetupUniverseReplicationResponsePB,
};
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};

use crate::remote::{call_leader, MASTER_SERVICE};
use crate::setup::{
    cluster_config, includes_consumer_namespace, universe, universes, SetupData, ALTER_SUFFIX,
    PGSQL,
};
use crate::{corrupt, status, unknown, XCluster};

/// A setup task's outcome; None while it runs.
#[derive(Default)]
pub(crate) struct SetupTask {
    pub done: Option<Result<(), Status>>,
    pub abort: Option<tokio::task::AbortHandle>,
}

/// What a tserver last reported of one producer tablet (`StoreReplicationStatus`).
#[derive(Clone, Debug)]
pub(crate) struct TabletStatus {
    pub term: i64,
    pub error: ReplicationErrorPb,
    pub detail: String,
}

/// Each consumer table with its stream and its producer tablets' status.
type GroupStatuses = BTreeMap<String, (String, BTreeMap<String, TabletStatus>)>;

/// (group, consumer table, producer tablet).
pub(crate) type StatusKey = (String, String, String);

fn missing(req: &impl std::fmt::Debug) -> MasterErrorPB {
    unknown(status(
        StatusCode::InvalidArgument,
        format!("Missing required fields in request: {req:?}"),
    ))
}

impl XCluster {
    /// `SetupUniverseReplication`: validates, then sets up in the background.
    pub async fn setup_universe_replication(
        self: &Arc<Self>,
        token: &LeaderToken,
        req: &SetupUniverseReplicationRequestPB,
        resp: &mut SetupUniverseReplicationResponsePB,
        local_masters: Vec<HostPortPB>,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("SetupUniverseReplication: {:?}", req);
        let snapshot = self.catalog.snapshot();
        // The universe uuid is set even when the group is already present.
        let config = cluster_config(&snapshot).map_err(unknown)?;
        if !config.universe_uuid().is_empty() {
            resp.set_universe_uuid(config.universe_uuid().to_string());
        }
        if req.replication_group_id().is_empty()
            || req.producer_master_addresses.is_empty()
            || req.producer_table_ids.is_empty()
        {
            return Err(missing(req));
        }
        let mut data = SetupData {
            group_id: req.replication_group_id().to_string(),
            source_masters: req.producer_master_addresses.clone(),
            transactional: req.transactional(),
            automatic_ddl_mode: req.automatic_ddl_mode(),
            source_table_ids: req.producer_table_ids.clone(),
            ..Default::default()
        };
        for id in &req.producer_bootstrap_ids {
            data.stream_ids
                .push(master_xrepl::parse_stream_id(id.as_bytes())?);
        }
        for ns in &req.producer_namespaces {
            if ns.id().is_empty() {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    "Invalid Namespace Id",
                )));
            }
            if ns.name().is_empty() {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    "Invalid Namespace name",
                )));
            }
            if ns.database_type() != yb_proto::pb::common_types::YQLDatabase::YQL_DATABASE_PGSQL {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    "Invalid Namespace database_type",
                )));
            }
            data.source_namespace_ids
                .push(String::from_utf8_lossy(ns.id()).into_owned());
            let target =
                master_table_lifecycle::lookup::find_namespace_by_name(&snapshot, ns.name(), PGSQL)
                    .ok_or_else(|| {
                        master_ts_manager::master_error_coded(
                            yb_proto::pb::master_types::master_error_pb::Code::NAMESPACE_NOT_FOUND,
                            StatusCode::NotFound,
                            &format!("Keyspace name not found: {}", ns.name()),
                        )
                    })?;
            data.target_namespace_ids
                .push(String::from_utf8_lossy(&target.key.id).into_owned());
        }
        self.start_setup(token, data, &local_masters)
            .map_err(unknown)
    }

    /// Registers and starts a setup task (`XClusterTargetManager::SetupUniverseReplication`).
    pub(crate) fn start_setup(
        self: &Arc<Self>,
        token: &LeaderToken,
        data: SetupData,
        local_masters: &[HostPortPB],
    ) -> Result<(), Status> {
        self.validate_setup(&data, local_masters)?;
        let id = data.group_id.clone();
        let task = {
            let mut setups = self.setups.lock().unwrap();
            if setups.contains_key(&id) {
                return Err(if data.is_alter() {
                    status(
                        StatusCode::TryAgain,
                        format!(
                            "Alter replication group already running for xCluster \
                             ReplicationGroup {id}"
                        ),
                    )
                } else {
                    status(
                        StatusCode::AlreadyPresent,
                        format!("Setup already running for xCluster ReplicationGroup {id}"),
                    )
                });
            }
            let task = Arc::new(std::sync::Mutex::new(SetupTask::default()));
            setups.insert(id.clone(), task.clone());
            task
        };
        let (this, token, record) = (self.clone(), *token, task.clone());
        let handle = tokio::spawn(async move {
            let result = this.run_setup(&token, &data).await;
            if let Err(s) = &result {
                tracing::warn!(
                    "xCluster InboundReplicationGroup [{}]: Failed: {s}",
                    data.group_id
                );
            }
            let mut t = record.lock().unwrap();
            if t.done.is_none() {
                t.done = Some(result);
            }
        });
        task.lock().unwrap().abort = Some(handle.abort_handle());
        Ok(())
    }

    /// `IsSetupUniverseReplicationDone`: the task's outcome, reported once; then, for a new
    /// group, that it is ACTIVE, healthy and (DB-scoped) has a safe time.
    pub async fn is_setup_universe_replication_done(
        &self,
        req: &IsSetupUniverseReplicationDoneRequestPB,
        resp: &mut IsSetupUniverseReplicationDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(missing(req));
        }
        let outcome = self.setup_done(id, false).map_err(unknown)?;
        match outcome {
            None => resp.set_done(false),
            Some(result) => {
                resp.set_done(true);
                resp.replication_error = MessageField::some(crate::app_status(&result));
            }
        }
        Ok(())
    }

    /// None while not done.
    pub(crate) fn setup_done(
        &self,
        id: &str,
        skip_health_check: bool,
    ) -> Result<Option<Result<(), Status>>, Status> {
        let task = self.setups.lock().unwrap().get(id).cloned();
        if let Some(task) = task {
            let done = task.lock().unwrap().done.clone();
            match done {
                None => return Ok(None),
                Some(result) => {
                    self.setups.lock().unwrap().remove(id);
                    if result.is_err() {
                        return Ok(Some(result));
                    }
                }
            }
        }
        if skip_health_check {
            return Ok(Some(Ok(())));
        }
        let alter = id.ends_with(ALTER_SUFFIX);
        let snapshot = self.catalog.snapshot();
        let Some((_, pb)) = universe(&snapshot, id) else {
            return Ok(Some(if alter {
                Ok(())
            } else {
                Err(status(
                    StatusCode::NotFound,
                    "Could not find xCluster replication group",
                ))
            }));
        };
        if matches!(
            pb.state(),
            UniverseState::DELETED_ERROR | UniverseState::FAILED
        ) {
            return Ok(Some(Err(status(
                StatusCode::InternalError,
                "unknown error",
            ))));
        }
        if alter || pb.state() != UniverseState::ACTIVE {
            return Ok(None);
        }
        if !(self.options.skip_health_check)() && self.has_replication_errors(&snapshot, id)? {
            return Ok(None);
        }
        // A DB-scoped group needs a safe time for each of its namespaces.
        let safe_times = crate::safe_time::safe_time_map(&snapshot);
        for i in &pb.db_scoped_info.namespace_infos {
            let ready = safe_times
                .get(i.consumer_namespace_id())
                .is_some_and(|t| crate::safe_time::is_valid(*t));
            if !ready {
                return Ok(None);
            }
        }
        tracing::info!("xCluster replication group {id} is ready");
        Ok(Some(Ok(())))
    }

    /// `GetUniverseReplication`.
    pub fn get_universe_replication(
        &self,
        req: &GetUniverseReplicationRequestPB,
        resp: &mut GetUniverseReplicationResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let pb = snapshot
            .get(
                SysRowEntryType::UNIVERSE_REPLICATION,
                req.replication_group_id().as_bytes(),
            )
            .and_then(|e| e.parse::<SysUniverseReplicationEntryPB>().ok())
            .ok_or_else(|| {
                master_ts_manager::master_error_coded(
                    yb_proto::pb::master_types::master_error_pb::Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    &format!(
                        "Could not find xCluster replication group {}",
                        req.replication_group_id()
                    ),
                )
            })?;
        resp.entry = MessageField::some(pb);
        Ok(())
    }

    /// `GetUniverseReplications`.
    pub fn get_universe_replications(
        &self,
        req: &GetUniverseReplicationsRequestPB,
        resp: &mut GetUniverseReplicationsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        for pb in universes(&self.catalog.snapshot()) {
            if req.namespace_id().is_empty() || includes_consumer_namespace(&pb, req.namespace_id())
            {
                resp.replication_group_ids
                    .push(pb.replication_group_id().to_string());
            }
        }
        Ok(())
    }

    /// `GetUniverseReplicationInfo`.
    pub fn get_universe_replication_info(
        &self,
        req: &GetUniverseReplicationInfoRequestPB,
        resp: &mut GetUniverseReplicationInfoResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(missing(req));
        }
        let snapshot = self.catalog.snapshot();
        let (_, pb) = universe(&snapshot, id).ok_or_else(|| {
            unknown(status(
                StatusCode::NotFound,
                format!("Replication group {id} not found"),
            ))
        })?;
        let config = cluster_config(&snapshot).map_err(unknown)?;
        let db_scoped = !pb.db_scoped_info.namespace_infos.is_empty();
        resp.replication_type = Some(EnumOrUnknown::new(if db_scoped {
            XClusterReplicationType::XCLUSTER_YSQL_DB_SCOPED
        } else if pb.transactional() {
            XClusterReplicationType::XCLUSTER_YSQL_TRANSACTIONAL
        } else {
            XClusterReplicationType::XCLUSTER_NON_TRANSACTIONAL
        }));
        let producer = config.consumer_registry.producer_map.get(id);
        if let Some(p) = producer {
            // `PBListAsString`: each address's ShortDebugString.
            resp.set_DEPRECATED_source_master_addresses(
                p.master_addrs
                    .iter()
                    .map(|h| format!("host: \"{}\" port: {}", h.host(), h.port()))
                    .collect::<Vec<_>>()
                    .join(","),
            );
            if !(self.options.return_legacy_universe_replication_info)() {
                resp.source_master_addrs = p.master_addrs.clone();
            }
        }
        if !(self.options.return_legacy_universe_replication_info)() {
            resp.set_automatic_ddl_mode(pb.db_scoped_info.automatic_ddl_mode());
        }
        for source in &pb.tables {
            let Some(stream_id) = pb.table_streams.get(source) else {
                continue;
            };
            let target = producer
                .and_then(|p| p.stream_map.get(stream_id))
                .map(|s| s.consumer_table_id.clone())
                .unwrap_or_default();
            let mut t = TableInfoPB::new();
            t.set_target_table_id(target);
            t.set_source_table_id(source.clone());
            t.set_stream_id(stream_id.clone());
            resp.table_infos.push(t);
        }
        for i in &pb.db_scoped_info.namespace_infos {
            let mut d = DbScopedInfoPB::new();
            d.set_target_namespace_id(i.consumer_namespace_id().to_string());
            d.set_source_namespace_id(i.producer_namespace_id().to_string());
            resp.db_scoped_infos.push(d);
        }
        Ok(())
    }

    /// `SetUniverseReplicationEnabled`: the producer entry's `disable_stream`; a pause waits for
    /// every tablet to report it paused.
    pub async fn set_universe_replication_enabled(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &SetUniverseReplicationEnabledRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("SetUniverseReplicationEnabled: {:?}", req);
        let id = req.replication_group_id();
        if id.is_empty() || !req.has_is_enabled() {
            return Err(missing(req));
        }
        let enabled = req.is_enabled();
        self.update_cluster_config(token, |config| {
            let entry = config
                .consumer_registry
                .mut_or_insert_default()
                .producer_map
                .get_mut(id)
                .ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Replication group {id} not found in Consumer Registry"),
                    )
                })?;
            if entry.disable_stream != enabled {
                return Ok(false);
            }
            entry.disable_stream = !enabled;
            Ok(true)
        })
        .await
        .map_err(unknown)?;
        if enabled {
            return Ok(());
        }
        loop {
            let snapshot = self.catalog.snapshot();
            if self.all_paused(&snapshot, id) {
                return Ok(());
            }
            if Instant::now() >= deadline {
                return Err(unknown(status(
                    StatusCode::TimedOut,
                    format!("Wait for replication group {id} to pause"),
                )));
            }
            tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        }
    }

    /// Applies `f` to the cluster config and writes it with its version bumped when `f` changed
    /// it.
    pub(crate) async fn update_cluster_config(
        &self,
        token: &LeaderToken,
        f: impl Fn(&mut SysClusterConfigEntryPB) -> Result<bool, Status>,
    ) -> Result<(), Status> {
        self.writer
            .mutate(token, |snapshot| {
                let entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut pb: SysClusterConfigEntryPB = entity.parse().map_err(corrupt)?;
                if !f(&mut pb)? {
                    return Ok((Mutation::new(), ()));
                }
                pb.set_version(pb.version() + 1);
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::CLUSTER_CONFIG,
                        &entity.key.id,
                        &pb,
                    )?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// `DeleteUniverseReplication`: a running setup is cancelled; otherwise the group leaves the
    /// consumer registry, its streams on the source are deleted (not for DB-scoped groups, whose
    /// outbound group owns them), and its row is deleted.
    pub async fn delete_universe_replication(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteUniverseReplicationRequestPB,
        resp: &mut DeleteUniverseReplicationResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("DeleteUniverseReplication: {:?}", req);
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(missing(req));
        }
        self.validate_universe_uuid(req.universe_uuid())
            .map_err(unknown)?;
        let running = self.setups.lock().unwrap().remove(id);
        if let Some(task) = running {
            let mut t = task.lock().unwrap();
            match &t.done {
                None => {
                    if let Some(h) = &t.abort {
                        h.abort();
                    }
                    t.done = Some(Err(status(StatusCode::Aborted, "Cancelled")));
                    return Ok(());
                }
                Some(Err(_)) => return Ok(()),
                Some(Ok(())) => {}
            }
        }
        let snapshot = self.catalog.snapshot();
        let (_, pb) = universe(&snapshot, id).ok_or_else(|| {
            unknown(status(
                StatusCode::NotFound,
                format!("Universe replication {id} does not exist"),
            ))
        })?;
        if !req.producer_namespace_oids.is_empty() {
            let hooks = self.hooks.get().ok_or_else(|| {
                unknown(status(StatusCode::ServiceUnavailable, "No OID hooks yet"))
            })?;
            for (source_ns, oid) in &req.producer_namespace_oids {
                let target = pb
                    .db_scoped_info
                    .namespace_infos
                    .iter()
                    .find(|i| i.producer_namespace_id() == source_ns)
                    .map(|i| i.consumer_namespace_id().to_string())
                    .ok_or_else(|| {
                        unknown(status(
                            StatusCode::InvalidArgument,
                            format!(
                                "DeleteUniverseReplication called with a namespace ID {source_ns} \
                                 that is not present in the replication group ({id})"
                            ),
                        ))
                    })?;
                hooks
                    .reserve_oids_above(target.into_bytes(), *oid)
                    .await
                    .map_err(unknown)?;
            }
            self.invalidate_tserver_oid_caches(token)
                .await
                .map_err(unknown)?;
        }
        self.delete_replication_group(
            token,
            deadline,
            &pb,
            req.ignore_errors(),
            req.skip_producer_stream_deletion(),
            resp,
        )
        .await
        .map_err(unknown)
    }

    /// `master::DeleteUniverseReplication`.
    pub(crate) async fn delete_replication_group(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        pb: &SysUniverseReplicationEntryPB,
        ignore_errors: bool,
        mut skip_producer_stream_deletion: bool,
        resp: &mut DeleteUniverseReplicationResponsePB,
    ) -> Result<(), Status> {
        let id = pb.replication_group_id().to_string();
        let set_state = |state: UniverseState| {
            let id = id.clone();
            move |snapshot: &master_catalog_state::CatalogSnapshot| {
                let Some((entity, mut pb)) = universe(snapshot, &id) else {
                    return Ok((Mutation::new(), ()));
                };
                pb.state = Some(EnumOrUnknown::new(state));
                Ok((
                    Mutation::new().read(&entity).upsert_message(
                        SysRowEntryType::UNIVERSE_REPLICATION,
                        id.as_bytes(),
                        &pb,
                    )?,
                    (),
                ))
            }
        };
        self.writer
            .mutate(token, set_state(UniverseState::DELETING))
            .await?;
        let db_scoped = !pb.db_scoped_info.namespace_infos.is_empty();
        if db_scoped {
            skip_producer_stream_deletion = true;
        }
        tracing::info!("Deleting subscribers for producer {id}");
        self.update_cluster_config(token, |config| {
            Ok(config
                .consumer_registry
                .mut_or_insert_default()
                .producer_map
                .remove(&id)
                .is_some())
        })
        .await?;
        if !pb.table_streams.is_empty() && !skip_producer_stream_deletion {
            let mut req = DeleteCDCStreamRequestPB::new();
            let mut table_of = HashMap::new();
            for (table, stream) in &pb.table_streams {
                req.stream_id.push(stream.clone().into_bytes());
                table_of.insert(stream.clone(), table.clone());
            }
            req.set_force_delete(true);
            req.set_ignore_errors(true);
            let result: Result<DeleteCDCStreamResponsePB, Status> = call_leader(
                &*self.remote,
                &id,
                &pb.producer_master_addresses,
                MASTER_SERVICE,
                "DeleteCDCStream",
                &req,
                deadline,
            )
            .await;
            let mut outcome = result.and_then(crate::remote::check);
            let mut ignore_missing = false;
            if let Ok(r) = &outcome {
                if !r.not_found_stream_ids.is_empty() {
                    let missing: Vec<String> = r
                        .not_found_stream_ids
                        .iter()
                        .map(|s| {
                            let s = String::from_utf8_lossy(s).into_owned();
                            let table = table_of.get(&s).cloned().unwrap_or_default();
                            format!("{s} (table_id: {table})")
                        })
                        .collect();
                    ignore_missing = true;
                    outcome = Err(status(
                        StatusCode::NotFound,
                        format!("Could not find the following streams: {missing:?}."),
                    ));
                }
            }
            if let Err(s) = outcome {
                if ignore_errors || ignore_missing {
                    resp.warnings.push(crate::app_status(&Err(s)));
                } else {
                    return Err(status(
                        s.code,
                        format!("{}\nUse 'ignore-errors' to ignore this error.", s.message),
                    ));
                }
            }
        }
        if pb.db_scoped_info.automatic_ddl_mode() {
            for i in &pb.db_scoped_info.namespace_infos {
                self.handle_extension_on_drop_replication(token, i.consumer_namespace_id())
                    .await?;
            }
        }
        self.writer
            .mutate(token, |snapshot| {
                let Some(entity) =
                    snapshot.get(SysRowEntryType::UNIVERSE_REPLICATION, id.as_bytes())
                else {
                    return Ok((Mutation::new(), ()));
                };
                Ok((
                    Mutation::new()
                        .read(entity)
                        .delete(SysRowEntryType::UNIVERSE_REPLICATION, id.as_bytes()),
                    (),
                ))
            })
            .await?;
        self.statuses
            .lock()
            .unwrap()
            .retain(|(g, _, _), _| g != &id);
        tracing::info!("Processed delete universe replication of {id}");
        Ok(())
    }

    /// `HandleExtensionOnDropReplication`: a switchover makes the namespace a source; otherwise
    /// the extension is dropped.
    pub(crate) async fn handle_extension_on_drop_replication(
        &self,
        token: &LeaderToken,
        ns: &str,
    ) -> Result<(), Status> {
        let switchover = crate::outbound::groups(&self.catalog.snapshot())
            .iter()
            .any(|(_, g)| g.automatic_ddl_mode() && g.namespace_infos.contains_key(ns));
        if switchover {
            tracing::info!(
                "Switchover detected for namespace {ns}, switching yb_xcluster_ddl_replication \
                 extension role to Source."
            );
            let name = XCluster::namespace_name(&self.catalog.snapshot(), ns)?;
            self.execute_pgsql(
                &name,
                &[format!(
                    "CREATE EXTENSION IF NOT EXISTS {}",
                    crate::DDL_QUEUE_PG_SCHEMA_NAME
                )],
            )
            .await?;
            return self
                .set_role(token, ns, XClusterRole::AUTOMATIC_SOURCE)
                .await;
        }
        self.drop_target_ddl_extension(token, ns).await
    }

    /// `DropDDLReplicationExtensionIfExists` on the target.
    async fn drop_target_ddl_extension(&self, token: &LeaderToken, ns: &str) -> Result<(), Status> {
        let name = XCluster::namespace_name(&self.catalog.snapshot(), ns)?;
        self.set_role(token, ns, XClusterRole::NOT_AUTOMATIC_MODE)
            .await?;
        self.execute_pgsql(
            &name,
            &[format!(
                "DROP EXTENSION IF EXISTS {}",
                crate::DDL_QUEUE_PG_SCHEMA_NAME
            )],
        )
        .await
    }

    /// `StoreReplicationStatus`: a tserver's poller states, the highest consumer term winning.
    pub(crate) fn store_replication_status(&self, reports: &[XClusterConsumerReplicationStatusPB]) {
        if reports.is_empty() {
            return;
        }
        let mut statuses = self.statuses.lock().unwrap();
        for report in reports {
            let group = String::from_utf8_lossy(report.replication_group_id()).into_owned();
            for table in &report.table_status {
                let consumer = String::from_utf8_lossy(table.consumer_table_id()).into_owned();
                for t in &table.stream_tablet_status {
                    let key = (
                        group.clone(),
                        consumer.clone(),
                        String::from_utf8_lossy(t.producer_tablet_id()).into_owned(),
                    );
                    let newer = statuses
                        .get(&key)
                        .is_none_or(|s| s.term <= t.consumer_term());
                    if newer {
                        statuses.insert(
                            key,
                            TabletStatus {
                                term: t.consumer_term(),
                                error: t.error(),
                                detail: t.error_detail().to_string(),
                            },
                        );
                    }
                }
            }
        }
    }

    /// Each consumer table of the group's registry entry with each of its producer tablets'
    /// status; a tablet no tserver reported is UNINITIALIZED (`SyncReplicationStatusMap`).
    fn group_statuses(
        &self,
        config: &SysClusterConfigEntryPB,
        group: &str,
    ) -> Option<GroupStatuses> {
        let entry = config.consumer_registry.producer_map.get(group)?;
        let statuses = self.statuses.lock().unwrap();
        let mut out = BTreeMap::new();
        for (stream_id, stream) in &entry.stream_map {
            let mut tablets = BTreeMap::new();
            for list in stream.consumer_producer_tablet_map.values() {
                for tablet in &list.tablets {
                    let status = statuses
                        .get(&(
                            group.to_string(),
                            stream.consumer_table_id.clone(),
                            tablet.clone(),
                        ))
                        .cloned()
                        .unwrap_or(TabletStatus {
                            term: 0,
                            error: ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED,
                            detail: String::new(),
                        });
                    tablets.insert(tablet.clone(), status);
                }
            }
            if !tablets.is_empty() {
                out.insert(
                    stream.consumer_table_id.clone(),
                    (stream_id.clone(), tablets),
                );
            }
        }
        Some(out)
    }

    /// `HasReplicationGroupErrors`.
    pub(crate) fn has_replication_errors(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        group: &str,
    ) -> Result<bool, Status> {
        let config = cluster_config(snapshot)?;
        let tables = self.group_statuses(&config, group).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("Could not find replication group {group}"),
            )
        })?;
        Ok(tables.values().any(|(_, tablets)| {
            tablets
                .values()
                .any(|s| s.error != ReplicationErrorPb::REPLICATION_OK)
        }))
    }

    /// `IsReplicationGroupFullyPaused`.
    fn all_paused(&self, snapshot: &master_catalog_state::CatalogSnapshot, group: &str) -> bool {
        let Ok(config) = cluster_config(snapshot) else {
            return false;
        };
        self.group_statuses(&config, group).is_some_and(|tables| {
            tables.values().all(|(_, tablets)| {
                tablets
                    .values()
                    .all(|s| s.error == ReplicationErrorPb::REPLICATION_PAUSED)
            })
        })
    }

    /// `GetReplicationStatus`: each table's errors, with the producer tablets reporting them.
    pub fn get_replication_status(
        &self,
        req: &GetReplicationStatusRequestPB,
        resp: &mut GetReplicationStatusResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let config = cluster_config(&snapshot).map_err(unknown)?;
        let groups: Vec<String> = if req.replication_group_id().is_empty() {
            config
                .consumer_registry
                .producer_map
                .keys()
                .cloned()
                .collect()
        } else {
            vec![req.replication_group_id().to_string()]
        };
        for group in groups {
            let tables = self.group_statuses(&config, &group).ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!("Could not find replication group {group}"),
                ))
            })?;
            for (table, (stream_id, tablets)) in tables {
                let mut by_error: BTreeMap<i32, (Vec<String>, String)> = BTreeMap::new();
                for (tablet, s) in &tablets {
                    let e = by_error.entry(protobuf::Enum::value(&s.error)).or_default();
                    e.0.push(tablet.clone());
                    if e.1.is_empty() && !s.detail.is_empty() {
                        e.1 = s.detail.clone();
                    }
                }
                let mut out = ReplicationStatusPB::new();
                out.set_table_id(table.into_bytes());
                out.set_stream_id(stream_id.into_bytes());
                for (error, (tablets, detail)) in by_error {
                    let error = <ReplicationErrorPb as protobuf::Enum>::from_i32(error)
                        .unwrap_or(ReplicationErrorPb::REPLICATION_UNKNOWN_ERROR);
                    if error == ReplicationErrorPb::REPLICATION_OK {
                        continue;
                    }
                    let mut e = ReplicationStatusErrorPB::new();
                    e.error = Some(EnumOrUnknown::new(error));
                    if error == ReplicationErrorPb::REPLICATION_PAUSED {
                        e.set_error_detail("Replication paused".to_string());
                    } else {
                        let shown: Vec<&str> =
                            tablets.iter().take(20).map(String::as_str).collect();
                        let mut text = format!("Producer Tablet IDs: {}", shown.join(","));
                        if tablets.len() > 20 {
                            text.push_str(&format!(", ... ({} more)", tablets.len() - 20));
                        }
                        if !detail.is_empty() {
                            text.push_str(&format!(". {detail}"));
                        }
                        e.set_error_detail(text);
                    }
                    out.errors.push(e);
                }
                resp.statuses.push(out);
            }
        }
        Ok(())
    }
}
