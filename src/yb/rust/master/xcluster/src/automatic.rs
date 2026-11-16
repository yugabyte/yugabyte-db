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

//! Schemas in automatic DDL mode on the target. A poller that meets a source schema version the
//! stream's mapping lacks asks `HandleNewSchemaForAutomaticXClusterTarget`: a task finds the
//! target schema version every tablet can apply it with (`GetCompatibleSchemaVersion`), inserting
//! the source's packed schema into the tablets when none can, and then maps the two versions. A
//! colocated table that does not exist yet keeps the packings in its group's row until it is
//! created (`InsertHistoricalColocatedSchemaPacking`).

use std::collections::HashSet;
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use protobuf::MessageField;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{SysTablesEntryPB, SysUniverseReplicationEntryPB};
use yb_proto::pb::common::SchemaPB;
use yb_proto::pb::master_replication::{
    HandleNewSchemaForAutomaticXClusterTargetRequestPB,
    InsertHistoricalColocatedSchemaPackingRequestPB,
    InsertHistoricalColocatedSchemaPackingResponsePB, InsertPackedSchemaForXClusterTargetRequestPB,
};
use yb_proto::pb::master_rust_peer::{
    XClusterHistoricalSchemaPackingRequestPB, XClusterHistoricalSchemaPackingResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::operations::ChangeMetadataRequestPB;
use yb_proto::pb::tserver::{
    GetCompatibleSchemaVersionRequestPB, GetCompatibleSchemaVersionResponsePB,
};
use yb_proto::pb::tserver_admin::ChangeMetadataResponsePB;

use crate::consumer::SCHEMA_VERSION_TOO_OLD;
use crate::setup::universe;
use crate::{corrupt, status, unknown, XCluster};

/// `kColocationIdNotSet`.
const NOT_COLOCATED: u32 = 0;
const POLL: Duration = Duration::from_millis(200);

/// `GetTablegroupIdFromParentTableId`.
fn tablegroup_of(parent: &str) -> &str {
    &parent[..parent.len().min(32)]
}

/// The colocated table with `colocation_id` among those the parent's tablet hosts.
fn colocated_table(snapshot: &CatalogSnapshot, parent: &str, colocation_id: u32) -> Option<String> {
    let tablet = snapshot
        .hosting_tablets_of(parent.as_bytes())
        .first()
        .cloned()?;
    snapshot
        .hosted_table_ids(tablet)
        .into_iter()
        .filter(|t| t.as_slice() != parent.as_bytes())
        .find(|t| {
            snapshot
                .get(SysRowEntryType::TABLE, t)
                .filter(|e| master_table_lifecycle::lookup::is_visible_to_client(e))
                .and_then(|e| e.parse::<SysTablesEntryPB>().ok())
                .is_some_and(|pb| pb.schema.colocated_table_id.colocation_id() == colocation_id)
        })
        .map(|t| String::from_utf8_lossy(&t).into_owned())
}

impl XCluster {
    /// `InsertPackedSchemaForXClusterTarget`: the table's version goes up by two; each tablet
    /// inserts the packed schema at version + 1 and keeps its current schema at version + 2.
    /// A request for another current version changes nothing (or fails, when asked to).
    async fn insert_packed_schema(
        &self,
        token: &LeaderToken,
        table_id: &str,
        packed: &SchemaPB,
        current_version: u32,
        error_on_incorrect_version: bool,
    ) -> Result<(), Status> {
        let bumped = self
            .writer
            .mutate(token, |snapshot| {
                let table = snapshot
                    .get(SysRowEntryType::TABLE, table_id.as_bytes())
                    .ok_or_else(|| {
                        status(StatusCode::NotFound, format!("Table {table_id} not found"))
                    })?;
                if !crate::is_running(table) {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!("Table {table_id} is not running"),
                    ));
                }
                let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                if pb.version() != current_version {
                    if error_on_incorrect_version {
                        return Err(status(
                            StatusCode::InvalidArgument,
                            format!(
                                "Table {table_id} has schema version {} but request has version \
                                 {current_version}.",
                                pb.version()
                            ),
                        ));
                    }
                    return Ok((Mutation::new(), None));
                }
                pb.set_version(pb.version() + 2);
                Ok((
                    Mutation::new().read(table).upsert_message(
                        SysRowEntryType::TABLE,
                        table_id.as_bytes(),
                        &pb,
                    )?,
                    Some(pb),
                ))
            })
            .await?
            .0;
        let Some(pb) = bumped else {
            return Ok(());
        };
        let snapshot = self.catalog.snapshot();
        let tablets: Vec<Vec<u8>> = snapshot
            .hosting_tablets_of(table_id.as_bytes())
            .iter()
            .map(|t| t.key.id.to_vec())
            .collect();
        let mut result = Ok(());
        for tablet in tablets {
            let mut req = ChangeMetadataRequestPB::new();
            req.set_schema_version(pb.version());
            req.set_tablet_id(tablet.clone());
            req.set_alter_table_id(table_id.as_bytes().to_vec());
            if pb.has_wal_retention_secs() {
                req.set_wal_retention_secs(pb.wal_retention_secs());
            }
            req.set_new_table_name(String::from_utf8_lossy(pb.name()).into_owned());
            req.indexes = pb.indexes.clone();
            req.set_first_provisional_column_id(pb.next_column_id());
            req.set_insert_packed_schema(true);
            req.schema = MessageField::some(packed.clone());
            if let Err(s) = self.alter_tablet_leader(&tablet, req).await {
                result = Err(s);
                break;
            }
        }
        tracing::info!("Inserted packed schema for xCluster target table {table_id}: {result:?}");
        result
    }

    /// One `AlterSchema` to the tablet's leader, retried until it succeeds or a minute passes.
    async fn alter_tablet_leader(
        &self,
        tablet: &[u8],
        mut req: ChangeMetadataRequestPB,
    ) -> Result<(), Status> {
        let deadline = Instant::now() + Duration::from_secs(60);
        loop {
            let leader = self
                .ts_manager
                .locations
                .get(tablet)
                .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()));
            let outcome = match leader {
                None => Err(status(
                    StatusCode::ServiceUnavailable,
                    "No leader for the tablet",
                )),
                Some(leader) => {
                    req.set_dest_uuid(leader.clone());
                    if let Ok(now) = self.xrepl.host_now() {
                        req.set_propagated_hybrid_time(now);
                    }
                    self.ts_call::<_, ChangeMetadataResponsePB>(
                        &leader,
                        "yb.tserver.TabletServerAdminService",
                        "AlterSchema",
                        &req,
                    )
                    .await
                    .and_then(|r| match r.error.as_ref() {
                        Some(e) => Err(Status::new(
                            StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                            e.status.message().to_string(),
                        )),
                        None => Ok(()),
                    })
                }
            };
            match outcome {
                Ok(()) => return Ok(()),
                Err(s) if Instant::now() >= deadline => return Err(s),
                Err(s) => {
                    tracing::debug!(
                        "AlterSchema of {} failed, retrying: {s}",
                        String::from_utf8_lossy(tablet)
                    );
                    tokio::time::sleep(POLL).await;
                }
            }
        }
    }

    /// `InsertPackedSchemaForXClusterTarget`.
    pub async fn insert_packed_schema_rpc(
        &self,
        token: &LeaderToken,
        req: &InsertPackedSchemaForXClusterTargetRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("InsertPackedSchemaForXClusterTarget: {:?}", req);
        crate::required(&[("table_id", req.table_id().is_empty())])?;
        let mut table_id = String::from_utf8_lossy(req.table_id()).into_owned();
        if master_table_lifecycle::lookup::is_colocation_parent_table_id(table_id.as_bytes()) {
            if !req.has_colocation_id() {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    format!("Missing colocation id for given colocated table {table_id}"),
                )));
            }
            if req.colocation_id() == NOT_COLOCATED {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    "Invalid colocation id",
                )));
            }
            table_id = colocated_table(&self.catalog.snapshot(), &table_id, req.colocation_id())
                .ok_or_else(|| {
                    unknown(status(
                        StatusCode::NotFound,
                        format!(
                            "Table with colocation id {} not found in tablegroup {}",
                            req.colocation_id(),
                            tablegroup_of(&table_id)
                        ),
                    ))
                })?;
        }
        self.insert_packed_schema(
            token,
            &table_id,
            &req.packed_schema,
            req.current_consumer_schema_version(),
            false,
        )
        .await
        .map_err(unknown)
    }

    /// `HandleNewSchemaForAutomaticXClusterTarget`: a known source version needs nothing (the
    /// poller gets the mapping by heartbeat); otherwise one task per table maps it.
    pub async fn handle_new_schema(
        &self,
        token: &LeaderToken,
        req: &HandleNewSchemaForAutomaticXClusterTargetRequestPB,
    ) -> Result<(), MasterErrorPB> {
        crate::required(&[
            (
                "replication_group_id",
                req.replication_group_id().is_empty(),
            ),
            ("stream_id", req.stream_id().is_empty()),
            ("table_id", req.table_id().is_empty()),
            (
                "producer_schema_version",
                !req.has_producer_schema_version(),
            ),
            ("schema", req.schema.is_none()),
        ])?;
        let group = req.replication_group_id().to_string();
        let stream_id = master_xrepl::parse_stream_id(req.stream_id().as_bytes())?;
        let mut table_id = String::from_utf8_lossy(req.table_id()).into_owned();
        let parent =
            master_table_lifecycle::lookup::is_colocation_parent_table_id(table_id.as_bytes());
        let colocation_id = if parent {
            if !req.schema.colocated_table_id.has_colocation_id() {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    format!("Missing colocation id for given colocated table {table_id}"),
                )));
            }
            req.schema.colocated_table_id.colocation_id()
        } else {
            NOT_COLOCATED
        };
        let snapshot = self.catalog.snapshot();
        let config = crate::setup::cluster_config(&snapshot).map_err(unknown)?;
        let stream = config
            .consumer_registry
            .producer_map
            .get(&group)
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!("Replication group {group} not found"),
                ))
            })?
            .stream_map
            .get(&stream_id)
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!("Stream {stream_id} not found in replication group {group}"),
                ))
            })?;
        let mapping = if colocation_id == NOT_COLOCATED {
            stream.schema_versions.as_ref()
        } else {
            stream.colocated_schema_versions.get(&colocation_id)
        };
        let producer_version = req.producer_schema_version();
        let known = mapping.is_some_and(|m| {
            m.current_producer_schema_version == producer_version
                || m.old_producer_schema_versions.contains(&producer_version)
        });
        if known {
            return Ok(());
        }
        if parent {
            match colocated_table(&snapshot, &table_id, colocation_id) {
                Some(t) => table_id = t,
                None => {
                    return self
                        .new_schema_for_upcoming_colocated_table(
                            token,
                            &group,
                            &stream_id,
                            &table_id,
                            colocation_id,
                            producer_version,
                            &req.schema,
                        )
                        .await
                        .map_err(unknown);
                }
            }
        }
        if !self
            .new_schema_tasks
            .lock()
            .unwrap()
            .insert(table_id.clone())
        {
            // The pollers retry until the mapping has the version.
            return Ok(());
        }
        let Some(this) = self.me.upgrade() else {
            return Ok(());
        };
        let (token, schema) = (*token, (*req.schema).clone());
        tracing::info!(
            "Scheduled HandleNewSchemaForAutomaticXClusterTargetTask for table {table_id}, \
             producer_schema_version {producer_version}, replication_group_id {group}, stream_id \
             {stream_id}"
        );
        tokio::spawn(async move {
            let result = this
                .map_new_schema(
                    &token,
                    &group,
                    &stream_id,
                    &table_id,
                    producer_version,
                    colocation_id,
                    &schema,
                )
                .await;
            if let Err(s) = result {
                tracing::warn!(
                    "HandleNewSchemaForAutomaticXClusterTarget[{group}:{stream_id}:{table_id}] \
                     failed: {s}"
                );
            }
            this.new_schema_tasks.lock().unwrap().remove(&table_id);
        });
        Ok(())
    }

    /// `HandleNewSchemaForAutomaticXClusterTargetTask`: the version every tablet can apply the
    /// source schema with, inserting the packed schema while there is none.
    #[allow(clippy::too_many_arguments)]
    async fn map_new_schema(
        &self,
        token: &LeaderToken,
        group: &str,
        stream_id: &str,
        table_id: &str,
        producer_version: u32,
        colocation_id: u32,
        schema: &SchemaPB,
    ) -> Result<(), Status> {
        loop {
            self.leadership.check(token)?;
            let snapshot = self.catalog.snapshot();
            let tablets: Vec<Vec<u8>> = snapshot
                .hosting_tablets_of(table_id.as_bytes())
                .iter()
                .map(|t| t.key.id.to_vec())
                .collect();
            if tablets.is_empty() {
                return Err(status(
                    StatusCode::IllegalState,
                    format!("No tablets found for table {table_id}"),
                ));
            }
            let mut versions = HashSet::new();
            let mut failed = false;
            for tablet in &tablets {
                match self.compatible_schema_version(tablet, schema).await {
                    Ok(v) => {
                        versions.insert(v);
                    }
                    Err(s) => {
                        tracing::debug!(
                            "Failed to get compatible schema version for tablet {}: {s}",
                            String::from_utf8_lossy(tablet)
                        );
                        failed = true;
                        break;
                    }
                }
            }
            if !failed && versions.len() == 1 {
                let version = *versions.iter().next().expect("one");
                match self
                    .update_schema_versions(
                        token,
                        group,
                        stream_id,
                        producer_version,
                        version,
                        colocation_id,
                        true,
                    )
                    .await
                {
                    Ok(_) => return Ok(()),
                    // The version may be garbage collected meanwhile: insert the schema again.
                    Err(s) if s.message.starts_with(SCHEMA_VERSION_TOO_OLD) => {
                        tracing::info!(
                            "Consumer schema version is older than the minimum consumer schema \
                             version in the mapping. Reinserting schema. {s}"
                        );
                    }
                    Err(s) => return Err(s),
                }
            }
            let current = crate::outbound::table_pb(&snapshot, table_id.as_bytes())
                .map(|pb| pb.version())
                .ok_or_else(|| {
                    status(StatusCode::NotFound, format!("Table {table_id} not found"))
                })?;
            self.insert_packed_schema(token, table_id, schema, current, true)
                .await?;
            tokio::time::sleep(POLL).await;
        }
    }

    /// `GetCompatibleSchemaVersion` of one tablet's leader.
    async fn compatible_schema_version(
        &self,
        tablet: &[u8],
        schema: &SchemaPB,
    ) -> Result<u32, Status> {
        let leader = self
            .ts_manager
            .locations
            .get(tablet)
            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
            .ok_or_else(|| status(StatusCode::ServiceUnavailable, "No leader for the tablet"))?;
        let mut req = GetCompatibleSchemaVersionRequestPB::new();
        req.set_tablet_id(tablet.to_vec());
        req.schema = MessageField::some(schema.clone());
        let resp: GetCompatibleSchemaVersionResponsePB = self
            .ts_call(
                &leader,
                "yb.tserver.TabletServerService",
                "GetCompatibleSchemaVersion",
                &req,
            )
            .await?;
        if let Some(e) = resp.error.as_ref() {
            return Err(Status::new(
                StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                e.status.message().to_string(),
            ));
        }
        if !resp.has_compatible_schema_version() {
            return Err(status(
                StatusCode::IllegalState,
                "Missing compatible_schema_version in response",
            ));
        }
        Ok(resp.compatible_schema_version())
    }

    /// `HandleNewTableSchemaForUpcomingColocatedTable`.
    #[allow(clippy::too_many_arguments)]
    async fn new_schema_for_upcoming_colocated_table(
        &self,
        token: &LeaderToken,
        group: &str,
        stream_id: &str,
        parent: &str,
        colocation_id: u32,
        producer_version: u32,
        schema: &SchemaPB,
    ) -> Result<(), Status> {
        tracing::info!(
            "Inserting historical schema packing for table {parent}, colocation_id {colocation_id}"
        );
        let version = self
            .add_historical_packing(token, group, parent, colocation_id, schema)
            .await?;
        self.update_schema_versions(
            token,
            group,
            stream_id,
            producer_version,
            version,
            colocation_id,
            false,
        )
        .await
        .map(|_| ())
    }

    /// `AddHistoricalPackedSchemaForColocatedTable`: the version of an equivalent packing the
    /// group's row already has for the colocation id, else the schema's packing added as the
    /// next one.
    async fn add_historical_packing(
        &self,
        token: &LeaderToken,
        group: &str,
        parent: &str,
        colocation_id: u32,
        schema: &SchemaPB,
    ) -> Result<u32, Status> {
        let snapshot = self.catalog.snapshot();
        let ns = snapshot
            .get(SysRowEntryType::TABLE, parent.as_bytes())
            .and_then(|t| {
                t.table()
                    .map(|h| String::from_utf8_lossy(&h.namespace_id).into_owned())
            })
            .ok_or_else(|| status(StatusCode::NotFound, format!("Table {parent} not found")))?;
        let (_, pb) = universe(&snapshot, group)
            .ok_or_else(|| status(StatusCode::NotFound, format!("Universe not found: {group}")))?;
        if pb.db_scoped_info.namespace_infos.is_empty() || !pb.db_scoped_info.automatic_ddl_mode() {
            return Err(status(
                StatusCode::IllegalState,
                "Replication group is not in automatic DDL mode",
            ));
        }
        let existing = pb
            .db_scoped_info
            .target_namespace_infos
            .get(&ns)
            .and_then(|i| i.colocated_historical_schema_packings.get(&colocation_id))
            .map(|h| h.old_schema_packings.clone())
            .unwrap_or_default();
        let mut req = XClusterHistoricalSchemaPackingRequestPB::new();
        req.old_schema_packings = existing;
        req.schema = MessageField::some(schema.clone());
        let resp: XClusterHistoricalSchemaPackingResponsePB = self
            .host_call("XClusterHistoricalSchemaPacking", req)
            .await?;
        if resp.has_existing_version() {
            return Ok(resp.existing_version());
        }
        let packing = resp.new_packing.unwrap_or_default();
        let version = packing.schema_version();
        self.writer
            .mutate(token, |snapshot| {
                let (entity, mut pb) = universe(snapshot, group).ok_or_else(|| {
                    status(StatusCode::NotFound, format!("Universe not found: {group}"))
                })?;
                let packings = &mut pb
                    .db_scoped_info
                    .mut_or_insert_default()
                    .target_namespace_infos
                    .entry(ns.clone())
                    .or_default()
                    .colocated_historical_schema_packings
                    .entry(colocation_id)
                    .or_default()
                    .old_schema_packings;
                if packings.len() as u32 != version {
                    return Err(status(
                        StatusCode::TryAgain,
                        "The historical packings changed meanwhile",
                    ));
                }
                packings.push(packing.clone());
                Ok((
                    Mutation::new().read(&entity).upsert_message(
                        SysRowEntryType::UNIVERSE_REPLICATION,
                        group.as_bytes(),
                        &pb,
                    )?,
                    (),
                ))
            })
            .await?;
        tracing::info!(
            "Added historical packed schema for colocation_id: {colocation_id}, parent_table_id: \
             {parent}, schema_version: {version}"
        );
        Ok(version)
    }

    /// `InsertHistoricalColocatedSchemaPacking`: only for a colocated table not created yet.
    pub async fn insert_historical_colocated_schema_packing(
        &self,
        token: &LeaderToken,
        req: &InsertHistoricalColocatedSchemaPackingRequestPB,
        resp: &mut InsertHistoricalColocatedSchemaPackingResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("InsertHistoricalColocatedSchemaPacking: {:?}", req);
        crate::required(&[
            (
                "replication_group_id",
                req.replication_group_id().is_empty(),
            ),
            (
                "target_parent_table_id",
                req.target_parent_table_id().is_empty(),
            ),
            ("colocation_id", !req.has_colocation_id()),
            ("source_schema_version", !req.has_source_schema_version()),
            ("schema", req.schema.is_none()),
        ])?;
        let parent = req.target_parent_table_id();
        if !master_table_lifecycle::lookup::is_colocation_parent_table_id(parent.as_bytes()) {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Parent table id is not a colocation parent id: {parent}"),
            )));
        }
        if colocated_table(&self.catalog.snapshot(), parent, req.colocation_id()).is_some() {
            return Err(master_ts_manager::master_error_coded(
                Code::UNKNOWN_ERROR,
                StatusCode::AlreadyPresent,
                "Table is already created, not adding historical schema",
            ));
        }
        let version = self
            .add_historical_packing(
                token,
                req.replication_group_id(),
                parent,
                req.colocation_id(),
                &req.schema,
            )
            .await
            .map_err(unknown)?;
        resp.set_last_compatible_consumer_schema_version(version);
        Ok(())
    }
}

impl XCluster {
    /// `ValidateCreateTableRequest` and `ProcessCreateTableReq`.
    pub(crate) fn process_create_table(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_ddl::CreateTableRequestPB,
        table: &mut SysTablesEntryPB,
    ) -> Result<(), Status> {
        let refused = if !req.old_rewrite_table_id().is_empty() {
            Some((
                req.old_rewrite_table_id().to_vec(),
                "Cannot rewrite a table that is a part of non-automatic mode XCluster replication.",
            ))
        } else if !req.indexed_table_id().is_empty()
            && req.skip_index_backfill()
            && !req.index_info.vector_idx_options.is_some()
        {
            Some((
                req.indexed_table_id().to_vec(),
                "Cannot create nonconcurrent index on a table that is a part of non-automatic \
                 mode XCluster replication.",
            ))
        } else {
            None
        };
        if let Some((table_id, message)) = refused {
            let ns = snapshot
                .get(SysRowEntryType::TABLE, &table_id)
                .and_then(|t| {
                    t.table()
                        .map(|h| String::from_utf8_lossy(&h.namespace_id).into_owned())
                })
                .unwrap_or_default();
            if self.is_table_replicated(snapshot, &table_id)
                && !self.is_namespace_in_automatic_mode(snapshot, &ns)
            {
                return Err(status(StatusCode::NotSupported, message));
            }
        }
        let info = &req.xcluster_table_info;
        if info.xcluster_source_table_id().is_empty() {
            return Ok(());
        }
        let out = table.xcluster_table_info.mut_or_insert_default();
        out.set_xcluster_source_table_id(info.xcluster_source_table_id().to_vec());
        if info.xcluster_backfill_hybrid_time() != 0 {
            out.set_xcluster_backfill_hybrid_time(info.xcluster_backfill_hybrid_time());
        }
        let colocation_id = table.schema.colocated_table_id.colocation_id();
        if colocation_id == NOT_COLOCATED || req.index_info.vector_idx_options.is_some() {
            return Ok(());
        }
        if master_table_lifecycle::lookup::is_colocation_parent_table_id(req.table_id()) {
            return Err(status(
                StatusCode::InvalidArgument,
                format!(
                    "Received unexpected parent colocation table id: {}",
                    String::from_utf8_lossy(req.table_id())
                ),
            ));
        }
        let ns = String::from_utf8_lossy(table.namespace_id()).into_owned();
        let universe = crate::setup::universes(snapshot)
            .into_iter()
            .find(|u| crate::setup::includes_consumer_namespace(u, &ns))
            .ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!(
                        "XCluster replication group not found for table {}, colocation_id \
                         {colocation_id}, namespace_id {ns}",
                        String::from_utf8_lossy(req.table_id())
                    ),
                )
            })?;
        // `UpdateColocatedTableWithHistoricalSchemaPackings`.
        let packings = universe
            .db_scoped_info
            .target_namespace_infos
            .get(&ns)
            .ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!("Colocation ID map not found for namespace {ns}"),
                )
            })?
            .colocated_historical_schema_packings
            .get(&colocation_id)
            .ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!(
                        "Historical packed schemas not found for colocation ID {colocation_id}"
                    ),
                )
            })?
            .old_schema_packings
            .clone();
        // With one packing the existing schema serves.
        if packings.len() <= 1 {
            return Ok(());
        }
        let last = packings.last().expect("not empty").schema_version();
        table
            .xcluster_table_info
            .mut_or_insert_default()
            .xcluster_colocated_old_schema_packings = packings;
        table.set_version(last + 1);
        Ok(())
    }

    /// `ClearXClusterFieldsAfterYsqlDDL`: the table's `xcluster_table_info` goes (an index keeps
    /// its backfill time until its backfill succeeds), and for a colocated table so do its
    /// historical packings in the inbound group (`CleanupColocatedTableHistoricalSchemaPackings`).
    /// C++ also marks the table's WAL anchor stream for deletion here; WAL anchors on a target
    /// are not handled yet.
    pub(crate) fn clear_after_ddl(
        &self,
        snapshot: &CatalogSnapshot,
        table_id: &[u8],
        table: &mut SysTablesEntryPB,
        mut mutation: Mutation,
    ) -> Result<Mutation, Status> {
        if table.xcluster_table_info.is_none() {
            return Ok(mutation);
        }
        let secondary = table.colocated()
            && !master_table_lifecycle::lookup::is_colocation_parent_table_id(table_id);
        if secondary {
            let ns = String::from_utf8_lossy(table.namespace_id()).into_owned();
            let colocation_id = table.schema.colocated_table_id.colocation_id();
            for entity in snapshot.iter(SysRowEntryType::UNIVERSE_REPLICATION) {
                let Ok(mut pb) = entity.parse::<SysUniverseReplicationEntryPB>() else {
                    continue;
                };
                if !crate::setup::includes_consumer_namespace(&pb, &ns) {
                    continue;
                }
                let erased = pb
                    .db_scoped_info
                    .mut_or_insert_default()
                    .target_namespace_infos
                    .get_mut(&ns)
                    .and_then(|i| {
                        i.colocated_historical_schema_packings
                            .remove(&colocation_id)
                    })
                    .is_some();
                if erased {
                    mutation = mutation.read(entity).upsert_message(
                        SysRowEntryType::UNIVERSE_REPLICATION,
                        &entity.key.id,
                        &pb,
                    )?;
                }
                // Only one automatic-mode group has the namespace.
                break;
            }
        }
        let info = table.xcluster_table_info.mut_or_insert_default();
        if !table.index_info.indexed_table_id().is_empty()
            && info.has_xcluster_backfill_hybrid_time()
        {
            info.clear_xcluster_source_table_id();
            info.xcluster_colocated_old_schema_packings.clear();
        } else {
            table.xcluster_table_info = protobuf::MessageField::none();
        }
        Ok(mutation)
    }

    /// `ProcessCleanupTablesPeriodically`: in each automatic-mode database, rows of `ddl_queue`
    /// (on a source) and `replicated_ddls` older than `xcluster_ddl_tables_retention_secs` are
    /// deleted, best effort and in the background.
    pub(crate) fn clean_up_ddl_tables(&self) {
        let snapshot = self.catalog.snapshot();
        let Some(this) = self.me.upgrade() else {
            return;
        };
        let outbound = crate::outbound::groups(&snapshot);
        for ns in snapshot.iter(SysRowEntryType::NAMESPACE) {
            let running = ns
                .parse::<yb_proto::pb::catalog_entity_info::SysNamespaceEntryPB>()
                .is_ok_and(|n| {
                    n.state()
                        == yb_proto::pb::catalog_entity_info::sys_namespace_entry_pb::State::RUNNING
                });
            if !running {
                continue;
            }
            let id = String::from_utf8_lossy(&ns.key.id).into_owned();
            let is_source = outbound
                .iter()
                .any(|(_, g)| g.automatic_ddl_mode() && g.namespace_infos.contains_key(&id));
            let is_target = self.is_automatic_mode_target(&snapshot, &id);
            if !(is_source || is_target) {
                continue;
            }
            let Ok(name) = XCluster::namespace_name(&snapshot, &id) else {
                continue;
            };
            // C++ prints the flags as 0 or 1; tests wait for this line.
            tracing::info!(
                "Attempting to clean up DDL replication tables for namespace {name}; is_source: \
                 {} is_target: {}",
                u8::from(is_source),
                u8::from(is_target)
            );
            let retention = (self.options.ddl_tables_retention)();
            let too_old = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .saturating_sub(retention)
                .as_micros();
            let mut statements = Vec::new();
            if is_source {
                // Fails if the database became a target since: the mode may switch first.
                statements.push(format!(
                    "DELETE FROM yb_xcluster_ddl_replication.ddl_queue WHERE ddl_end_time < \
                     {too_old};"
                ));
            }
            statements.push("SET yb_non_ddl_txn_for_sys_tables_allowed = 1;".to_string());
            // The special row (query_id 1) and rows still in ddl_queue stay.
            statements.push(format!(
                "DELETE FROM yb_xcluster_ddl_replication.replicated_ddls WHERE \
                 replicated_ddls.ddl_end_time < {too_old} AND \
                 yb_xcluster_ddl_replication.replicated_ddls.query_id != 1 AND NOT EXISTS ( \
                 SELECT 1 FROM yb_xcluster_ddl_replication.ddl_queue WHERE \
                 ddl_queue.ddl_end_time = replicated_ddls.ddl_end_time AND \
                 ddl_queue.query_id = replicated_ddls.query_id );"
            ));
            let this = this.clone();
            tokio::spawn(async move {
                if let Err(s) = this.execute_pgsql(&name, &statements).await {
                    tracing::warn!(
                        "Error cleaning up xCluster DDL replication tables for namespace {name}: \
                         {s}"
                    );
                }
            });
        }
    }

    /// `IsTableReplicated`: an xCluster stream out of the table or into it.
    fn is_table_replicated(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
        if !crate::controller::outbound_streams(snapshot, table_id).is_empty() {
            return true;
        }
        let id = String::from_utf8_lossy(table_id);
        crate::setup::cluster_config(snapshot).is_ok_and(|c| {
            c.consumer_registry
                .producer_map
                .values()
                .flat_map(|p| p.stream_map.values())
                .any(|s| s.consumer_table_id == id)
        })
    }

    /// `IsNamespaceInAutomaticDDLMode`: every outbound group with the namespace, or every inbound
    /// one, is in automatic mode.
    pub(crate) fn is_namespace_in_automatic_mode(
        &self,
        snapshot: &CatalogSnapshot,
        ns: &str,
    ) -> bool {
        let outbound: Vec<bool> = crate::outbound::groups(snapshot)
            .into_iter()
            .filter(|(_, g)| g.namespace_infos.contains_key(ns))
            .map(|(_, g)| g.automatic_ddl_mode())
            .collect();
        if !outbound.is_empty() {
            return outbound.iter().all(|a| *a);
        }
        self.is_automatic_mode_target(snapshot, ns)
    }
}
