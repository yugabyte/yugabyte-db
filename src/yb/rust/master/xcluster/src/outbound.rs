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

//! The source's outbound replication groups (`XClusterOutboundReplicationGroup`): one row per
//! group, with each namespace's tables and their streams. A namespace is CHECKPOINTING until its
//! task (see `checkpoint`) has checkpointed the streams of its initial tables, then READY.

use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;

use master_catalog_entity::Entity;
use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_xrepl::{stream_rows, strip_sequences_data_alias, StreamRow};
use protobuf::{EnumOrUnknown, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState,
    sys_xcluster_outbound_replication_group_entry_pb::{
        namespace_info_pb::{State as NsState, TableInfoPB},
        target_universe_info_pb::State as TargetState,
        NamespaceInfoPB, State as GroupState,
    },
    SysCDCStreamEntryPB, SysNamespaceEntryPB, SysTablesEntryPB,
    SysXClusterOutboundReplicationGroupEntryPB as GroupPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_replication::{
    get_xcluster_outbound_replication_group_info_response_pb::NamespaceInfoPB as InfoNamespacePB,
    get_xcluster_streams_response_pb::TableInfo as StreamsTableInfo,
    AddNamespaceToXClusterReplicationRequestPB, CreateXClusterReplicationRequestPB,
    DeleteCDCStreamRequestPB, DeleteCDCStreamResponsePB, DeleteXClusterWalAnchorStreamsRequestPB,
    GetXClusterOutboundReplicationGroupInfoRequestPB,
    GetXClusterOutboundReplicationGroupInfoResponsePB,
    GetXClusterOutboundReplicationGroupsRequestPB, GetXClusterOutboundReplicationGroupsResponsePB,
    GetXClusterStreamsRequestPB, GetXClusterStreamsResponsePB,
    IsAlterXClusterReplicationDoneRequestPB, IsAlterXClusterReplicationDoneResponsePB,
    IsCreateXClusterReplicationDoneRequestPB, IsCreateXClusterReplicationDoneResponsePB,
    IsXClusterBootstrapRequiredRequestPB, IsXClusterBootstrapRequiredResponsePB,
    RepairOutboundXClusterReplicationGroupAddTableRequestPB,
    RepairOutboundXClusterReplicationGroupRemoveTableRequestPB,
    XClusterAddNamespaceToOutboundReplicationGroupRequestPB,
    XClusterCreateOutboundReplicationGroupRequestPB,
    XClusterDeleteOutboundReplicationGroupRequestPB,
    XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB,
};
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};
use yb_proto::pb::wire_protocol::AppStatusPB;

use crate::{corrupt, eligible_tables, status, status_of, unknown, TableDesignator, XCluster};

const GROUP: SysRowEntryType = SysRowEntryType::XCLUSTER_OUTBOUND_REPLICATION_GROUP;

/// A stream a group mutation creates, with its row.
pub(crate) type NewStream = (String, SysCDCStreamEntryPB);

/// The group's row, unless it is missing or deleted.
pub(crate) fn group<'a>(
    snapshot: &'a CatalogSnapshot,
    id: &str,
) -> Result<(&'a Arc<Entity>, GroupPB), Status> {
    let not_found = || {
        status(
            StatusCode::NotFound,
            format!("xClusterOutboundReplicationGroup {id} not found"),
        )
    };
    let entity = snapshot.get(GROUP, id.as_bytes()).ok_or_else(not_found)?;
    let pb: GroupPB = entity.parse().map_err(corrupt)?;
    if pb.state() == GroupState::DELETED {
        return Err(not_found());
    }
    Ok((entity, pb))
}

/// Every group, by id.
pub(crate) fn groups(snapshot: &CatalogSnapshot) -> Vec<(String, GroupPB)> {
    snapshot
        .iter(GROUP)
        .filter_map(|e| {
            let pb: GroupPB = e.parse().ok()?;
            (pb.state() != GroupState::DELETED)
                .then(|| (String::from_utf8_lossy(&e.key.id).into_owned(), pb))
        })
        .collect()
}

pub(crate) fn namespace_info<'a>(
    pb: &'a GroupPB,
    group: &str,
    ns: &str,
) -> Result<&'a NamespaceInfoPB, Status> {
    pb.namespace_infos.get(ns).ok_or_else(|| {
        status(
            StatusCode::NotFound,
            format!("xClusterOutboundReplicationGroup {group}: Namespace {ns} not found"),
        )
    })
}

pub(crate) fn namespace_info_mut<'a>(
    pb: &'a mut GroupPB,
    group: &str,
    ns: &str,
) -> Result<&'a mut NamespaceInfoPB, Status> {
    pb.namespace_infos.get_mut(ns).ok_or_else(|| {
        status(
            StatusCode::NotFound,
            format!("xClusterOutboundReplicationGroup {group}: Namespace {ns} not found"),
        )
    })
}

/// `IsReady`: whether the namespace is READY; a FAILED one is its error.
fn is_ready(ns: &NamespaceInfoPB) -> Result<bool, Status> {
    if ns.state() == NsState::FAILED {
        let e: &AppStatusPB = &ns.error_status;
        return Err(Status::new(
            StatusCode::from_i32(protobuf::Enum::value(&e.code())),
            e.message().to_string(),
        ));
    }
    Ok(ns.state() == NsState::READY)
}

fn table_info(initial: bool) -> TableInfoPB {
    let mut t = TableInfoPB::new();
    t.set_is_checkpointing(true);
    t.set_is_part_of_initial_bootstrap(initial);
    t
}

impl XCluster {
    /// Applies `f` to the group's row and writes it with the streams `f` creates, rebuilding on
    /// conflict. `f` says whether it changed the row.
    pub(crate) async fn update_group<T>(
        &self,
        token: &LeaderToken,
        id: &str,
        mut f: impl FnMut(
            &CatalogSnapshot,
            &mut GroupPB,
            &mut Vec<NewStream>,
        ) -> Result<(bool, T), Status>,
    ) -> Result<T, Status> {
        self.writer
            .mutate(token, |snapshot| {
                let (entity, mut pb) = group(snapshot, id)?;
                let mut streams = Vec::new();
                let (changed, out) = f(snapshot, &mut pb, &mut streams)?;
                let mut m = Mutation::new();
                if changed {
                    m = m.read(entity).upsert_message(GROUP, id.as_bytes(), &pb)?;
                }
                for (stream_id, stream) in &streams {
                    m = m
                        .read_absent(SysRowEntryType::CDC_STREAM, stream_id.as_bytes())
                        .upsert_message(
                            SysRowEntryType::CDC_STREAM,
                            stream_id.as_bytes(),
                            stream,
                        )?;
                }
                Ok((m, out))
            })
            .await
            .map(|(out, _)| out)
    }

    /// `CreateStreamsInternal`'s row of a bootstrapping (INITIATED) transactional stream.
    pub(crate) fn new_stream(
        &self,
        snapshot: &CatalogSnapshot,
        table_id: &str,
        automatic_ddl_mode: bool,
        allow_hidden_table: bool,
        is_wal_anchor: bool,
        taken: &[NewStream],
    ) -> Result<NewStream, Status> {
        let stripped = strip_sequences_data_alias(table_id);
        let exists = snapshot
            .get(SysRowEntryType::TABLE, stripped.as_bytes())
            .is_some_and(|t| {
                if allow_hidden_table {
                    crate::is_running(t)
                } else {
                    master_table_lifecycle::lookup::is_visible_to_client(t)
                }
            });
        if !exists {
            return Err(status(
                StatusCode::NotFound,
                format!("Table {stripped} does not exist"),
            ));
        }
        let mut pb = SysCDCStreamEntryPB::new();
        pb.table_id.push(table_id.to_string());
        pb.set_transactional(true);
        pb.options = master_xrepl::xcluster_stream_options();
        pb.state = Some(EnumOrUnknown::new(StreamState::INITIATED));
        if automatic_ddl_mode && (self.options.target_applied_filter)() {
            pb.set_xcluster_use_target_applied_filter(true);
        }
        if is_wal_anchor {
            pb.set_xcluster_is_wal_anchor(true);
        }
        let mut rows = stream_rows(snapshot);
        for (id, _) in taken {
            rows.push(StreamRow::placeholder(id));
        }
        Ok((master_xrepl::new_stream_id(&rows), pb))
    }

    /// `CreateNamespaceInfo`: the namespace's eligible tables, each to be checkpointed.
    fn create_namespace_info(
        &self,
        snapshot: &CatalogSnapshot,
        namespace_id: &str,
        automatic_ddl_mode: bool,
    ) -> Result<NamespaceInfoPB, Status> {
        let tables = eligible_tables(snapshot, namespace_id, automatic_ddl_mode)?;
        if !automatic_ddl_mode && tables.is_empty() {
            return Err(status(
                StatusCode::InvalidArgument,
                "Database should have at least one table in order to be part of xCluster \
                 replication",
            ));
        }
        let ns: SysNamespaceEntryPB = snapshot
            .get(SysRowEntryType::NAMESPACE, namespace_id.as_bytes())
            .ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!("Keyspace identifier not found: {namespace_id}"),
                )
            })?
            .parse()
            .map_err(corrupt)?;
        if ns.database_type() != YQLDatabase::YQL_DATABASE_PGSQL {
            return Err(status(
                StatusCode::InvalidArgument,
                "Only YSQL databases are supported in xCluster DB Scoped replication",
            ));
        }
        if ns.colocated()
            && !tables.iter().any(|t| {
                master_table_lifecycle::lookup::is_colocation_parent_table_id(t.id.as_bytes())
            })
        {
            return Err(status(
                StatusCode::InvalidArgument,
                "Colocated database should have at least one colocated table in order to be part \
                 of xCluster replication",
            ));
        }
        let mut info = NamespaceInfoPB::new();
        info.state = Some(EnumOrUnknown::new(NsState::CHECKPOINTING));
        for t in tables {
            info.table_infos.insert(t.id, table_info(true));
        }
        Ok(info)
    }

    /// `XClusterCreateOutboundReplicationGroup`.
    pub async fn create_outbound_group(
        self: &Arc<Self>,
        token: &LeaderToken,
        req: &XClusterCreateOutboundReplicationGroupRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterCreateOutboundReplicationGroup: {:?}", req);
        if !(self.options.api_v2)() {
            return Err(unknown(status(
                StatusCode::IllegalState,
                "xCluster API v2 is not enabled.",
            )));
        }
        let id = req.replication_group_id();
        if id.is_empty() || req.namespace_ids.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let automatic = req.automatic_ddl_mode() || (self.options.force_automatic_ddl_mode)();
        if automatic && !(self.options.ddl_replication)() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Automatic DDL replication (xcluster_enable_ddl_replication) is not enabled.",
            )));
        }
        self.writer
            .mutate(token, |snapshot| {
                if snapshot.get(GROUP, id.as_bytes()).is_some() {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!("xClusterOutboundReplicationGroup {id} already exists"),
                    ));
                }
                let mut pb = GroupPB::new();
                pb.set_automatic_ddl_mode(automatic);
                for ns in &req.namespace_ids {
                    if pb.namespace_infos.contains_key(ns) {
                        continue;
                    }
                    let info = self.create_namespace_info(snapshot, ns, automatic)?;
                    pb.namespace_infos.insert(ns.clone(), info);
                }
                Ok((
                    Mutation::new()
                        .read_absent(GROUP, id.as_bytes())
                        .upsert_message(GROUP, id.as_bytes(), &pb)?,
                    (),
                ))
            })
            .await
            .map_err(unknown)?;
        self.start_checkpoint_tasks(token);
        Ok(())
    }

    /// `XClusterAddNamespaceToOutboundReplicationGroup`.
    pub async fn add_namespace_to_outbound_group(
        self: &Arc<Self>,
        token: &LeaderToken,
        req: &XClusterAddNamespaceToOutboundReplicationGroupRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterAddNamespaceToOutboundReplicationGroup: {:?}", req);
        let (id, ns) = (req.replication_group_id(), req.namespace_id());
        if id.is_empty() || ns.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let added = self
            .update_group(token, id, |snapshot, pb, _| {
                if pb.namespace_infos.contains_key(ns) {
                    tracing::info!("Skip adding Namespace {ns} since it already exists in {id}");
                    return Ok((false, false));
                }
                // Removing and re-adding a namespace while an older task runs could make that
                // task mark streams of the new one.
                self.verify_no_tasks(id)?;
                let info = self.create_namespace_info(snapshot, ns, pb.automatic_ddl_mode())?;
                pb.namespace_infos.insert(ns.to_string(), info);
                Ok((true, true))
            })
            .await
            .map_err(unknown)?;
        if added {
            self.start_checkpoint_tasks(token);
        }
        Ok(())
    }

    /// `DeleteNamespaceStreams`: the streams of the namespace's tables are deleted (forced).
    async fn delete_namespace_streams(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        info: &NamespaceInfoPB,
    ) -> Result<(), Status> {
        let mut req = DeleteCDCStreamRequestPB::new();
        for t in info.table_infos.values() {
            if !t.stream_id().is_empty() {
                req.stream_id.push(t.stream_id().to_vec());
            }
            if !t.wal_anchor_stream_id().is_empty() {
                req.stream_id.push(t.wal_anchor_stream_id().to_vec());
            }
        }
        self.delete_streams(token, deadline, req).await
    }

    async fn delete_streams(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        mut req: DeleteCDCStreamRequestPB,
    ) -> Result<(), Status> {
        if req.stream_id.is_empty() {
            return Ok(());
        }
        req.set_force_delete(true);
        req.set_ignore_errors(true);
        let mut resp = DeleteCDCStreamResponsePB::new();
        self.xrepl
            .delete_cdc_stream(token, deadline, &req, &mut resp)
            .await
            .map_err(|e| status_of(&e))
    }

    /// `XClusterRemoveNamespaceFromOutboundReplicationGroup`: with target addresses, the target's
    /// inbound group drops the namespace first.
    pub async fn remove_namespace_from_outbound_group(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!(
            "XClusterRemoveNamespaceFromOutboundReplicationGroup: {:?}",
            req
        );
        let (id, ns) = (req.replication_group_id(), req.namespace_id());
        if id.is_empty() || ns.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let pb = group(&self.catalog.snapshot(), id).map_err(unknown)?.1;
        if !req.target_master_addresses.is_empty() {
            let target = pb.target_universe_info.as_ref().ok_or_else(|| {
                unknown(status(
                    StatusCode::IllegalState,
                    "Target universe info not found. xCluster replication must already be setup \
                     in order to remove namespace from the target.",
                ))
            })?;
            self.remove_namespace_from_target(
                id,
                &req.target_master_addresses,
                ns,
                target.universe_uuid(),
                deadline,
            )
            .await
            .map_err(unknown)?;
        }
        if let Some(info) = pb.namespace_infos.get(ns) {
            tracing::info!("Deleting streams for namespace {ns} of {id}");
            self.delete_namespace_streams(token, deadline, info)
                .await
                .map_err(unknown)?;
        }
        if pb.automatic_ddl_mode() {
            self.drop_ddl_replication_extension(token, ns, Some(id))
                .await
                .map_err(unknown)?;
        }
        self.update_group(token, id, |_, pb, _| {
            Ok((pb.namespace_infos.remove(ns).is_some(), ()))
        })
        .await
        .map_err(unknown)
    }

    /// `XClusterDeleteOutboundReplicationGroup`: the target's inbound group is deleted first when
    /// its masters are given; the streams go, then the row.
    pub async fn delete_outbound_group(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &XClusterDeleteOutboundReplicationGroupRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterDeleteOutboundReplicationGroup: {:?}", req);
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let pb = group(&self.catalog.snapshot(), id).map_err(unknown)?.1;
        self.abort_tasks(id);
        if !req.target_master_addresses.is_empty() {
            let target = pb.target_universe_info.as_ref().ok_or_else(|| {
                unknown(status(
                    StatusCode::IllegalState,
                    "Target universe info not found. xCluster replication must already be setup \
                     in order to remove namespace from the target.",
                ))
            })?;
            // A switchover leaves the target's normal OIDs behind ours: it bumps them above the
            // highest we used (D42692).
            let mut oids = BTreeMap::new();
            if pb.automatic_ddl_mode() {
                for ns in pb.namespace_infos.keys() {
                    if self.is_automatic_mode_switchover(ns) {
                        let oid = self.next_normal_oid(token, ns).await.map_err(unknown)?;
                        tracing::info!(
                            "xCluster automatic mode switchover of namespace ID {ns} detected; \
                             need to bump target OID above {oid}"
                        );
                        oids.insert(ns.clone(), oid);
                    }
                }
            }
            tracing::info!("Deleting replication group {id} from target");
            self.delete_universe_replication_on_target(
                id,
                &req.target_master_addresses,
                target.universe_uuid(),
                oids,
                deadline,
            )
            .await
            .map_err(unknown)?;
        }
        for (ns, info) in &pb.namespace_infos {
            self.delete_namespace_streams(token, deadline, info)
                .await
                .map_err(unknown)?;
            if pb.automatic_ddl_mode() {
                self.drop_ddl_replication_extension(token, ns, Some(id))
                    .await
                    .map_err(unknown)?;
            }
        }
        self.writer
            .mutate(token, |snapshot| {
                let Some(entity) = snapshot.get(GROUP, id.as_bytes()) else {
                    return Ok((Mutation::new(), ()));
                };
                Ok((
                    Mutation::new().read(entity).delete(GROUP, id.as_bytes()),
                    (),
                ))
            })
            .await
            .map(|_| ())
            .map_err(|s| {
                unknown(status(
                    s.code,
                    format!(
                        "updating xClusterOutboundReplicationGroup in sys-catalog: {}",
                        s.message
                    ),
                ))
            })
    }

    /// `IsXClusterBootstrapRequired`: `not_ready` until the namespace is READY.
    pub fn is_xcluster_bootstrap_required(
        &self,
        req: &IsXClusterBootstrapRequiredRequestPB,
        resp: &mut IsXClusterBootstrapRequiredResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let (id, ns) = (req.replication_group_id(), req.namespace_id());
        if id.is_empty() || ns.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id).map_err(unknown)?.1;
        let info = namespace_info(&pb, id, ns).map_err(unknown)?;
        if !is_ready(info).map_err(unknown)? {
            resp.set_not_ready(true);
            return Ok(());
        }
        resp.set_initial_bootstrap_required(info.initial_bootstrap_required());
        Ok(())
    }

    /// The ids of streams of `table_ids` not being deleted (`GetAliveStreamIds`).
    fn alive_streams(snapshot: &CatalogSnapshot, table_ids: &[String]) -> HashSet<String> {
        stream_rows(snapshot)
            .into_iter()
            .filter(|r| {
                r.pb.namespace_id().is_empty()
                    && !r.is_deleting()
                    && r.pb.table_id.iter().any(|t| table_ids.contains(t))
            })
            .map(|r| r.id)
            .collect()
    }

    /// `GetXClusterStreams`: the namespace's streams, by table names, by table ids, or all.
    pub async fn get_xcluster_streams(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &GetXClusterStreamsRequestPB,
        resp: &mut GetXClusterStreamsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let (id, ns) = (req.replication_group_id(), req.namespace_id());
        if id.is_empty() || ns.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        if !req.table_infos.is_empty() && !req.source_table_ids.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Only one of table_infos or table_ids should be set",
            )));
        }
        let table_ids: Vec<String> = req
            .source_table_ids
            .iter()
            .map(|t| String::from_utf8_lossy(t).into_owned())
            .collect();
        if !table_ids.is_empty() && req.create_stream_if_missing() {
            self.create_and_checkpoint_anchored_streams(token, deadline, id, ns, &table_ids)
                .await
                .map_err(unknown)?;
        }
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id).map_err(unknown)?.1;
        let info = namespace_info(&pb, id, ns).map_err(unknown)?;
        if !is_ready(info).map_err(unknown)? {
            resp.set_not_ready(true);
            return Ok(());
        }
        let not_ready = |table_id: &str| {
            tracing::debug!(
                "xCluster stream for Table {table_id} in Namespace {ns} is not ready yet."
            );
        };
        let mut out = Vec::new();
        if !table_ids.is_empty() {
            let alive = Self::alive_streams(&snapshot, &table_ids);
            for table_id in &table_ids {
                let t = info.table_infos.get(table_id).ok_or_else(|| {
                    unknown(status(
                        StatusCode::NotFound,
                        format!("Table {table_id} not found in Namespace {ns}: {id}"),
                    ))
                })?;
                if t.stream_id().is_empty() || t.is_checkpointing() {
                    not_ready(table_id);
                    resp.set_not_ready(true);
                    return Ok(());
                }
                let stream_id = String::from_utf8_lossy(t.stream_id()).into_owned();
                if !alive.contains(&stream_id) {
                    tracing::debug!(
                        "xCluster stream {stream_id} of Table {table_id} in Namespace {ns} is \
                         being deleted."
                    );
                    resp.set_not_ready(true);
                    return Ok(());
                }
                out.push((table_id.clone(), stream_id, String::new(), String::new()));
            }
        } else {
            let all = eligible_tables(&snapshot, ns, pb.automatic_ddl_mode()).map_err(unknown)?;
            let chosen: Vec<TableDesignator> = if req.table_infos.is_empty() {
                all
            } else {
                let mut by_name: BTreeMap<(String, String), TableDesignator> = BTreeMap::new();
                for t in all {
                    let key = (t.name.clone(), t.pgschema_name.clone());
                    if let Some(other) = by_name.get(&key) {
                        return Err(unknown(status(
                            StatusCode::AlreadyPresent,
                            format!(
                                "xClusterOutboundReplicationGroup {id}: Multiple table ids found \
                                 for table {}.{}. Table ids: {}, {}",
                                t.pgschema_name, t.name, other.id, t.id
                            ),
                        )));
                    }
                    by_name.insert(key, t);
                }
                let mut chosen = Vec::new();
                for t in &req.table_infos {
                    let key = (t.table_name().to_string(), t.pg_schema_name().to_string());
                    let d = by_name.get(&key).ok_or_else(|| {
                        unknown(status(
                            StatusCode::NotFound,
                            format!(
                                "Table {}.{} not found in namespace {ns}",
                                t.pg_schema_name(),
                                t.table_name()
                            ),
                        ))
                    })?;
                    chosen.push(d.clone());
                }
                chosen
            };
            for d in chosen {
                let Some(t) = info.table_infos.get(&d.id) else {
                    // Removed with the repair APIs: an error only when asked for by name.
                    if !req.table_infos.is_empty() {
                        return Err(unknown(status(
                            StatusCode::NotFound,
                            format!("Table {} not found in {id}", d.id),
                        )));
                    }
                    continue;
                };
                if t.stream_id().is_empty() || t.is_checkpointing() {
                    not_ready(&d.id);
                    resp.set_not_ready(true);
                    return Ok(());
                }
                out.push((
                    d.id.clone(),
                    String::from_utf8_lossy(t.stream_id()).into_owned(),
                    d.name,
                    d.pgschema_name,
                ));
            }
        }
        resp.set_initial_bootstrap_required(info.initial_bootstrap_required());
        for (table_id, stream_id, name, schema) in out {
            let mut t = StreamsTableInfo::new();
            t.set_table_id(table_id.into_bytes());
            t.set_xrepl_stream_id(stream_id.into_bytes());
            t.set_table_name(name);
            t.set_pg_schema_name(schema);
            resp.table_infos.push(t);
        }
        Ok(())
    }

    /// `GetXClusterOutboundReplicationGroups`.
    pub fn get_outbound_groups(
        &self,
        req: &GetXClusterOutboundReplicationGroupsRequestPB,
        resp: &mut GetXClusterOutboundReplicationGroupsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        for (id, pb) in groups(&self.catalog.snapshot()) {
            if req.namespace_id().is_empty() || pb.namespace_infos.contains_key(req.namespace_id())
            {
                resp.replication_group_ids.push(id);
            }
        }
        Ok(())
    }

    /// `GetXClusterOutboundReplicationGroupInfo`: the streams of the READY namespaces.
    pub fn get_outbound_group_info(
        &self,
        req: &GetXClusterOutboundReplicationGroupInfoRequestPB,
        resp: &mut GetXClusterOutboundReplicationGroupInfoResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id).map_err(unknown)?.1;
        for (ns, info) in &pb.namespace_infos {
            if !is_ready(info).map_err(unknown)? {
                continue;
            }
            let tables =
                eligible_tables(&snapshot, ns, pb.automatic_ddl_mode()).map_err(unknown)?;
            let mut out = InfoNamespacePB::new();
            out.set_namespace_id(ns.clone());
            let mut complete = true;
            for d in tables {
                let Some(t) = info.table_infos.get(&d.id) else {
                    continue;
                };
                if t.stream_id().is_empty() || t.is_checkpointing() {
                    complete = false;
                    break;
                }
                out.table_streams
                    .insert(d.id, String::from_utf8_lossy(t.stream_id()).into_owned());
            }
            if complete {
                resp.namespace_infos.push(out);
            }
        }
        resp.set_automatic_ddl_mode(pb.automatic_ddl_mode());
        Ok(())
    }

    /// `RepairOutboundXClusterReplicationGroupAddTable`: an existing xCluster stream of the table
    /// joins the group as checkpointed.
    pub async fn repair_add_table(
        &self,
        token: &LeaderToken,
        req: &RepairOutboundXClusterReplicationGroupAddTableRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("RepairOutboundXClusterReplicationGroupAddTable: {:?}", req);
        let id = req.replication_group_id();
        let table_id = String::from_utf8_lossy(req.table_id()).into_owned();
        let stream_id = master_xrepl::parse_stream_id(req.stream_id())?;
        let snapshot = self.catalog.snapshot();
        let stripped = strip_sequences_data_alias(&table_id);
        let table = snapshot
            .get(SysRowEntryType::TABLE, stripped.as_bytes())
            .filter(|t| master_table_lifecycle::lookup::is_visible_to_client(t))
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!("Table {stripped} not found"),
                ))
            })?;
        let namespace_id = if stripped != table_id {
            table_id[stripped.len() + ".sequences_data_for.".len()..].to_string()
        } else {
            String::from_utf8_lossy(&table.table().expect("table").namespace_id).into_owned()
        };
        let row = stream_rows(&snapshot)
            .into_iter()
            .find(|r| r.id == stream_id && !r.is_deleting())
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!("Could not find CDC stream: {stream_id}"),
                ))
            })?;
        if !row.pb.namespace_id().is_empty() || row.pb.table_id.len() != 1 {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Stream {stream_id} is not valid for use in xCluster"),
            )));
        }
        if row.pb.table_id[0] != table_id {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Stream {stream_id} belongs to a different table"),
            )));
        }
        self.update_group(token, id, |_, pb, _| {
            let info = namespace_info_mut(pb, id, &namespace_id)?;
            if info.table_infos.contains_key(&table_id) {
                return Err(status(
                    StatusCode::AlreadyPresent,
                    format!("Table {table_id} already exists in {id}"),
                ));
            }
            let mut t = TableInfoPB::new();
            t.set_stream_id(stream_id.clone().into_bytes());
            t.set_is_checkpointing(false);
            t.set_is_part_of_initial_bootstrap(false);
            info.table_infos.insert(table_id.clone(), t);
            Ok((true, ()))
        })
        .await
        .map_err(unknown)
    }

    /// `RepairOutboundXClusterReplicationGroupRemoveTable`: the table leaves the group; its WAL
    /// anchor stream is deleted, its stream is not.
    pub async fn repair_remove_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &RepairOutboundXClusterReplicationGroupRemoveTableRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!(
            "RepairOutboundXClusterReplicationGroupRemoveTable: {:?}",
            req
        );
        let id = req.replication_group_id();
        let table_id = String::from_utf8_lossy(req.table_id()).into_owned();
        let anchor = self
            .update_group(token, id, |_, pb, _| {
                for info in pb.namespace_infos.values_mut() {
                    if let Some(t) = info.table_infos.remove(&table_id) {
                        return Ok((true, t.wal_anchor_stream_id().to_vec()));
                    }
                }
                Err(status(
                    StatusCode::NotFound,
                    format!("Table {table_id} not found in {id}"),
                ))
            })
            .await
            .map_err(unknown)?;
        if !anchor.is_empty() {
            tracing::info!(
                "Removing WAL anchor stream {} of table {table_id}",
                String::from_utf8_lossy(&anchor)
            );
            let mut req = DeleteCDCStreamRequestPB::new();
            req.stream_id.push(anchor);
            self.delete_streams(token, deadline, req)
                .await
                .map_err(unknown)?;
        }
        Ok(())
    }

    /// `DeleteXClusterWalAnchorStreams`: the target committed the tables' creation.
    pub async fn delete_wal_anchor_streams(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteXClusterWalAnchorStreamsRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id).map_err(unknown)?.1;
        let mut del = DeleteCDCStreamRequestPB::new();
        for table_id in &req.source_table_ids {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                continue;
            };
            let ns =
                String::from_utf8_lossy(&table.table().expect("table").namespace_id).into_owned();
            let anchor = pb
                .namespace_infos
                .get(&ns)
                .and_then(|i| i.table_infos.get(&*String::from_utf8_lossy(table_id)))
                .map(|t| t.wal_anchor_stream_id().to_vec())
                .unwrap_or_default();
            if !anchor.is_empty() {
                del.stream_id.push(anchor);
            }
        }
        if del.stream_id.is_empty() {
            return Ok(());
        }
        tracing::info!(
            "Deleting {} xCluster WAL_ANCHOR stream(s) for committed source tables in \
             replication group {id}",
            del.stream_id.len()
        );
        self.delete_streams(token, deadline, del)
            .await
            .map_err(unknown)
    }

    /// The initial tables of the group's READY namespaces, each with its stream: what the target
    /// sets up from (only those were backed up and restored on it).
    fn initial_tables(
        pb: &GroupPB,
        namespaces: &[&str],
    ) -> Result<(Vec<String>, Vec<String>), Status> {
        let (mut tables, mut streams) = (Vec::new(), Vec::new());
        for ns in namespaces {
            let info = &pb.namespace_infos[*ns];
            if info.state() != NsState::READY {
                return Err(status(
                    StatusCode::TryAgain,
                    format!("Namespace {ns} is not yet ready to start replicating"),
                ));
            }
            for (table_id, t) in &info.table_infos {
                if !t.is_part_of_initial_bootstrap() {
                    continue;
                }
                if t.is_checkpointing() {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!("Table {table_id} is not yet ready"),
                    ));
                }
                tables.push(table_id.clone());
                streams.push(String::from_utf8_lossy(t.stream_id()).into_owned());
            }
        }
        Ok((tables, streams))
    }

    pub(crate) fn namespace_name(snapshot: &CatalogSnapshot, ns: &str) -> Result<String, Status> {
        let pb: SysNamespaceEntryPB = snapshot
            .get(SysRowEntryType::NAMESPACE, ns.as_bytes())
            .ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!("Keyspace identifier not found: {ns}"),
                )
            })?
            .parse()
            .map_err(corrupt)?;
        Ok(String::from_utf8_lossy(pb.name()).into_owned())
    }

    /// `CreateXClusterReplication`: the target sets up its inbound group from our streams.
    pub async fn create_xcluster_replication(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateXClusterReplicationRequestPB,
        source_masters: Vec<yb_proto::pb::common_net::HostPortPB>,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("CreateXClusterReplication: {:?}", req);
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        if req.target_master_addresses.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Missing Target Master addresses",
            )));
        }
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id).map_err(unknown)?.1;
        if pb.target_universe_info.is_some() {
            return Ok(());
        }
        let namespaces: Vec<&str> = pb.namespace_infos.keys().map(String::as_str).collect();
        let (tables, streams) = Self::initial_tables(&pb, &namespaces).map_err(unknown)?;
        let names = namespaces
            .iter()
            .map(|ns| Self::namespace_name(&snapshot, ns))
            .collect::<Result<Vec<_>, _>>()
            .map_err(unknown)?;
        let target_uuid = self
            .setup_db_scoped_replication_on_target(
                id,
                &req.target_master_addresses,
                source_masters,
                &names,
                &namespaces,
                &tables,
                &streams,
                pb.automatic_ddl_mode(),
                deadline,
            )
            .await
            .map_err(unknown)?;
        self.update_group(token, id, |_, pb, _| {
            if pb.target_universe_info.is_some() {
                return Ok((false, ()));
            }
            let target = pb.target_universe_info.mut_or_insert_default();
            target.set_universe_uuid(target_uuid.clone().into_bytes());
            target.state = Some(EnumOrUnknown::new(TargetState::CREATING_REPLICATION_GROUP));
            Ok((true, ()))
        })
        .await
        .map_err(unknown)
    }

    /// `IsCreateXClusterReplicationDone`: the target's setup outcome becomes the group's target
    /// state; a failure clears it so the setup can be retried.
    pub async fn is_create_xcluster_replication_done(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &IsCreateXClusterReplicationDoneRequestPB,
        resp: &mut IsCreateXClusterReplicationDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        if req.target_master_addresses.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Missing Target Master addresses",
            )));
        }
        let pb = group(&self.catalog.snapshot(), id).map_err(unknown)?.1;
        let target = pb.target_universe_info.as_ref().ok_or_else(|| {
            unknown(status(
                StatusCode::IllegalState,
                "Target universe info not found",
            ))
        })?;
        let outcome: Option<Result<(), Status>> = match target.state() {
            TargetState::REPLICATING => Some(Ok(())),
            TargetState::FAILED => Some(Err(if target.error_status.is_some() {
                let e = &target.error_status;
                Status::new(
                    StatusCode::from_i32(protobuf::Enum::value(&e.code())),
                    e.message().to_string(),
                )
            } else {
                status(
                    StatusCode::IllegalState,
                    format!(
                        "Failed to create replication group on target cluster: {}",
                        String::from_utf8_lossy(target.universe_uuid())
                    ),
                )
            })),
            TargetState::CREATING_REPLICATION_GROUP => self
                .is_setup_done_on_target(id, &req.target_master_addresses, id, deadline)
                .await
                .map_err(unknown)?,
        };
        let Some(result) = outcome else {
            resp.set_done(false);
            return Ok(());
        };
        if target.state() == TargetState::CREATING_REPLICATION_GROUP {
            let failed = result.is_err();
            if let Err(s) = &result {
                tracing::warn!(
                    "xClusterOutboundReplicationGroup {id}: Failed to create replication group on \
                     target cluster: {s}"
                );
            }
            self.update_group(token, id, |_, pb, _| {
                if failed {
                    pb.target_universe_info = MessageField::none();
                } else {
                    pb.target_universe_info.mut_or_insert_default().state =
                        Some(EnumOrUnknown::new(TargetState::REPLICATING));
                }
                Ok((true, ()))
            })
            .await
            .map_err(unknown)?;
        }
        resp.set_done(true);
        resp.replication_error = MessageField::some(crate::app_status(&result));
        Ok(())
    }

    /// `AddNamespaceToXClusterReplication`: the target adds the READY namespace to its group.
    pub async fn add_namespace_to_xcluster_replication(
        &self,
        deadline: Instant,
        req: &AddNamespaceToXClusterReplicationRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("AddNamespaceToXClusterReplication: {:?}", req);
        let (id, ns) = (req.replication_group_id(), req.namespace_id());
        if id.is_empty() || ns.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        if req.target_master_addresses.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Missing Target Master addresses",
            )));
        }
        let snapshot = self.catalog.snapshot();
        let pb = group(&snapshot, id).map_err(unknown)?.1;
        let target = pb.target_universe_info.as_ref().ok_or_else(|| {
            unknown(status(
                StatusCode::IllegalState,
                "Target universe info not found. xCluster replication must already be setup in \
                 order to add a new namespace to the target.",
            ))
        })?;
        if !pb.namespace_infos.contains_key(ns) {
            return Err(unknown(status(
                StatusCode::NotFound,
                format!("Namespace {ns} not found in replication group {id}"),
            )));
        }
        let (tables, streams) = Self::initial_tables(&pb, &[ns]).map_err(unknown)?;
        let name = Self::namespace_name(&snapshot, ns).map_err(unknown)?;
        self.add_namespace_on_target(
            id,
            &req.target_master_addresses,
            target.universe_uuid(),
            &name,
            ns,
            &tables,
            &streams,
            deadline,
        )
        .await
        .map_err(unknown)
    }

    /// `IsAlterXClusterReplicationDone`: the target's alter group (`<id>.ALTER`) is set up.
    pub async fn is_alter_xcluster_replication_done(
        &self,
        deadline: Instant,
        req: &IsAlterXClusterReplicationDoneRequestPB,
        resp: &mut IsAlterXClusterReplicationDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        if req.target_master_addresses.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Missing Target Master addresses",
            )));
        }
        let alter_id = format!("{id}.ALTER");
        let outcome = self
            .is_setup_done_on_target(id, &req.target_master_addresses, &alter_id, deadline)
            .await
            .map_err(unknown)?;
        match outcome {
            None => resp.set_done(false),
            Some(result) => {
                resp.set_done(true);
                resp.replication_error = MessageField::some(crate::app_status(&result));
            }
        }
        Ok(())
    }

    /// Whether a namespace in automatic mode is both a source and a target, as during a
    /// switchover.
    pub(crate) fn is_automatic_mode_switchover(&self, ns: &str) -> bool {
        let snapshot = self.catalog.snapshot();
        groups(&snapshot)
            .iter()
            .any(|(_, pb)| pb.automatic_ddl_mode() && pb.namespace_infos.contains_key(ns))
            && self.is_automatic_mode_target(&snapshot, ns)
    }

    /// `IsNamespaceInAutomaticModeTarget`: the namespace is the target of an inbound group in
    /// automatic mode.
    /// Whether the namespace is the target of an automatic-mode inbound group.
    pub fn is_automatic_target(&self, namespace_id: &[u8]) -> bool {
        let ns = String::from_utf8_lossy(namespace_id);
        self.is_automatic_mode_target(&self.catalog.snapshot(), &ns)
    }

    pub(crate) fn is_automatic_mode_target(&self, snapshot: &CatalogSnapshot, ns: &str) -> bool {
        use yb_proto::pb::catalog_entity_info::SysUniverseReplicationEntryPB;
        snapshot
            .iter(SysRowEntryType::UNIVERSE_REPLICATION)
            .filter_map(|e| e.parse::<SysUniverseReplicationEntryPB>().ok())
            .any(|pb| {
                pb.db_scoped_info.automatic_ddl_mode()
                    && pb
                        .db_scoped_info
                        .namespace_infos
                        .iter()
                        .any(|i| i.consumer_namespace_id() == ns)
            })
    }

    /// `VerifyNoTasksInProgress`.
    pub(crate) fn verify_no_tasks(&self, id: &str) -> Result<(), Status> {
        let running: Vec<String> = self
            .checkpoints
            .lock()
            .unwrap()
            .iter()
            .filter(|((g, _), (_, h))| g == id && !h.is_finished())
            .map(|((_, ns), _)| format!("XClusterCheckpointNamespaceTask [{id}] {ns}"))
            .collect();
        if !running.is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                format!("xClusterOutboundReplicationGroup {id} has in progress tasks: {running:?}"),
            ));
        }
        Ok(())
    }

    /// `CloseAndWaitForAllTasksToAbort`.
    fn abort_tasks(&self, id: &str) {
        self.checkpoints.lock().unwrap().retain(|(g, _), (_, h)| {
            if g == id {
                h.abort();
                false
            } else {
                true
            }
        });
    }

    /// Tables of the namespaces of groups: which groups a new table joins.
    pub(crate) fn groups_with_namespace(
        snapshot: &CatalogSnapshot,
        ns: &[u8],
    ) -> Vec<(String, GroupPB)> {
        groups(snapshot)
            .into_iter()
            .filter(|(_, pb)| {
                pb.namespace_infos
                    .contains_key(&*String::from_utf8_lossy(ns))
            })
            .collect()
    }
}

/// The table's row.
pub(crate) fn table_pb(snapshot: &CatalogSnapshot, id: &[u8]) -> Option<SysTablesEntryPB> {
    snapshot.get(SysRowEntryType::TABLE, id)?.parse().ok()
}

impl XCluster {
    /// `XClusterEnsureSequenceUpdatesAreInWal`: each namespace's sequences get a fresh update in
    /// the WAL, so the target's bootstrap sees their latest values.
    pub async fn ensure_sequence_updates_are_in_wal(
        &self,
        req: &yb_proto::pb::master_replication::XClusterEnsureSequenceUpdatesAreInWalRequestPB,
    ) -> Result<(), MasterErrorPB> {
        use yb_proto::pb::master_rust_peer::{
            EnsureSequenceUpdatesAreInWalRequestPB, EnsureSequenceUpdatesAreInWalResponsePB,
        };
        if req.replication_group_id().is_empty() || req.namespace_ids.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        if (self.options.fail_ensure_sequence_updates)() {
            return Err(unknown(status(
                StatusCode::RuntimeError,
                "EnsureSequenceUpdatesAreInWal call failed due to test flag \
                 simulate_EnsureSequenceUpdatesAreInWal_failure",
            )));
        }
        let mut host_req = EnsureSequenceUpdatesAreInWalRequestPB::new();
        for ns in &req.namespace_ids {
            let oid = master_table_lifecycle::pg_database_oid(ns.as_bytes()).ok_or_else(|| {
                unknown(status(
                    StatusCode::InvalidArgument,
                    format!("Invalid PostgreSQL namespace id: {ns}"),
                ))
            })?;
            host_req.database_oids.push(oid);
        }
        let _: EnsureSequenceUpdatesAreInWalResponsePB = self
            .host_call("EnsureSequenceUpdatesAreInWal", host_req)
            .await
            .map_err(unknown)?;
        Ok(())
    }
}
