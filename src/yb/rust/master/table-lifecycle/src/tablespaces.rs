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

//! YSQL tablespace placement: C++'s `YsqlTablespaceManager` and
//! `CatalogManager::DoRefreshTablespaceInfo`.
//!
//! PG owns tablespaces: their placement is `pg_tablespace.spcoptions`, and a relation's tablespace
//! is its `pg_class.reltablespace` (`pg_yb_tablegroup.grptablespace` for a tablegroup). The
//! `ysql-tablespaces` controller reads them through the host every
//! `ysql_tablespace_info_refresh_secs`, keeps them in memory for placement (table creation, the
//! balancer, tablet splits), records each table's tablespace in its row (`tablespace_id` or
//! `no_custom_tablespace`) so it outlives the `pg_class` row, and creates a transaction status
//! table for each tablespace that has tables (`auto_create_local_transaction_tables`).

use std::collections::{BTreeSet, HashMap};
use std::sync::{Arc, RwLock};

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use protobuf::{Enum, EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysClusterConfigEntryPB, SysTablesEntryPB,
};
use yb_proto::pb::common_net::ReplicationInfoPB;
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_ddl::{CreateTableRequestPB, CreateTableResponsePB};
use yb_proto::pb::master_rust_peer::{
    read_ysql_tablespaces_request_pb, ReadYsqlTablespacesRequestPB, ReadYsqlTablespacesResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, SysRowEntryType};

use crate::lookup::is_colocation_parent_table_id;
use crate::placement::{cluster_config, replication_info_set};
use crate::{error, SchemaHost, TableLifecycle};

/// `kYsqlNumDefaultTablespaces`: `pg_default` and `pg_global`.
const NUM_DEFAULT_TABLESPACES: usize = 2;

/// `kPgSequencesDataDatabaseOid`.
const SEQUENCES_DATA_DATABASE_OID: u32 = 65535;

/// `kTransactionTablePrefix`.
pub const TRANSACTION_TABLE_PREFIX: &str = "transactions_";

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct TablespaceOptions {
    /// `enable_ysql_tablespaces_for_placement`.
    pub enabled: fn() -> bool,
    /// `ysql_tablespace_info_refresh_secs`.
    pub refresh_interval: fn() -> std::time::Duration,
    /// `auto_create_local_transaction_tables`.
    pub auto_create_local_transaction_tables: fn() -> bool,
    /// `TEST_name_transaction_tables_with_tablespace_id`.
    pub name_transaction_tables_with_tablespace_id: fn() -> bool,
}

/// What the last refresh read.
#[derive(Debug, Default)]
struct Snapshot {
    /// Each tablespace's placement; None for no placement, or one that failed validation. None
    /// until the first refresh.
    placements: Option<HashMap<Vec<u8>, Option<ReplicationInfoPB>>>,
    /// Each table's tablespace, None for the default one; read only while there are custom
    /// tablespaces.
    tables: Option<HashMap<Vec<u8>, Option<Vec<u8>>>>,
}

/// The tablespace placement the leader knows, shared by everything that places tablets.
#[derive(Debug)]
pub struct Tablespaces {
    options: TablespaceOptions,
    state: RwLock<Arc<Snapshot>>,
}

/// `TableInfo::TableTypeUsesTablespacesForPlacement`: a YSQL table outside the sequences database
/// that is not a colocated child or a colocation parent, or a transaction status table of a
/// tablespace.
pub fn uses_tablespaces(table_id: &[u8], pb: &SysTablesEntryPB) -> bool {
    if pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE {
        return pb.transaction_table_tablespace_id.is_some();
    }
    pb.table_type() == TableType::PGSQL_TABLE_TYPE
        && crate::ysql::pg_database_oid(pb.namespace_id()) != Some(SEQUENCES_DATA_DATABASE_OID)
        && !pb.colocated()
        && !is_colocation_parent_table_id(table_id)
}

/// The persisted tablespace of a table: `Some(None)` for the default one, None when never
/// recorded.
fn persisted(pb: &SysTablesEntryPB) -> Option<Option<Vec<u8>>> {
    if pb.has_tablespace_id() {
        Some(Some(pb.tablespace_id().to_vec()))
    } else if pb.has_no_custom_tablespace() {
        Some(None)
    } else {
        None
    }
}

impl Tablespaces {
    /// Nothing known until the first refresh.
    pub fn new(options: TablespaceOptions) -> Arc<Tablespaces> {
        Arc::new(Tablespaces {
            options,
            state: RwLock::new(Arc::default()),
        })
    }

    fn snapshot(&self) -> Arc<Snapshot> {
        self.state.read().unwrap().clone()
    }

    fn has_custom(placements: &HashMap<Vec<u8>, Option<ReplicationInfoPB>>) -> bool {
        placements.len() > NUM_DEFAULT_TABLESPACES
    }

    /// `GetTablespaceForTable`.
    pub fn tablespace_of(
        &self,
        table_id: &[u8],
        pb: &SysTablesEntryPB,
    ) -> Result<Option<Vec<u8>>, Status> {
        if !(self.options.enabled)() || !uses_tablespaces(table_id, pb) {
            return Ok(None);
        }
        if pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE {
            return Ok(Some(pb.transaction_table_tablespace_id().to_vec()));
        }
        let state = self.snapshot();
        let Some(placements) = &state.placements else {
            return Err(Status::new(
                StatusCode::InternalError,
                format!(
                    "Tablespace information not found for table {}",
                    String::from_utf8_lossy(table_id)
                ),
            ));
        };
        if !Self::has_custom(placements) {
            return Ok(None);
        }
        match state.tables.as_ref().and_then(|t| t.get(table_id)) {
            Some(tablespace) => Ok(tablespace.clone()),
            // The refresh has not reached it, or its pg_class row is gone (dropped, truncated or
            // rewritten): the persisted value answers.
            None => match persisted(pb) {
                Some(tablespace) => Ok(tablespace),
                None if state.tables.is_none() => Err(Status::new(
                    StatusCode::InternalError,
                    format!(
                        "Tablespace information not found for table {}",
                        String::from_utf8_lossy(table_id)
                    ),
                )),
                None => Ok(None),
            },
        }
    }

    /// `GetTablespaceReplicationInfo`: None for no placement; an error for a tablespace the last
    /// refresh did not see.
    pub fn placement(&self, tablespace_id: &[u8]) -> Result<Option<ReplicationInfoPB>, Status> {
        if !(self.options.enabled)() || tablespace_id.is_empty() {
            return Ok(None);
        }
        self.snapshot()
            .placements
            .as_ref()
            .and_then(|p| p.get(tablespace_id))
            .cloned()
            .ok_or_else(|| {
                Status::new(
                    StatusCode::InternalError,
                    format!(
                        "pg_tablespace info for tablespace {} not found",
                        String::from_utf8_lossy(tablespace_id)
                    ),
                )
            })
    }

    /// `CatalogManagerUtil::GetTableReplicationInfo`: the table's own replication info, else its
    /// tablespace's placement, else the cluster's.
    pub fn table_replication_info(
        &self,
        table_id: &[u8],
        pb: &SysTablesEntryPB,
        config: &SysClusterConfigEntryPB,
    ) -> Result<ReplicationInfoPB, Status> {
        if let Some(info) = pb
            .replication_info
            .as_ref()
            .filter(|i| replication_info_set(i))
        {
            return Ok(info.clone());
        }
        if let Some(tablespace) = self.tablespace_of(table_id, pb)? {
            if let Some(info) = self.placement(&tablespace)? {
                return Ok(info);
            }
        }
        Ok(config.replication_info.clone().unwrap_or_default())
    }

    /// [`Self::table_replication_info`], with the cluster's when the tablespace is not known yet.
    pub fn table_replication_info_or_cluster(
        &self,
        table_id: &[u8],
        pb: &SysTablesEntryPB,
        config: &SysClusterConfigEntryPB,
    ) -> ReplicationInfoPB {
        self.table_replication_info(table_id, pb, config)
            .unwrap_or_else(|_| config.replication_info.clone().unwrap_or_default())
    }

    /// `NeedsRefreshToFindTablePlacement`: the balancer leaves such a table alone until the next
    /// refresh.
    pub fn needs_refresh(&self, table_id: &[u8], pb: &SysTablesEntryPB) -> bool {
        if !(self.options.enabled)() || !uses_tablespaces(table_id, pb) {
            return false;
        }
        let state = self.snapshot();
        let Some(placements) = &state.placements else {
            return true;
        };
        if !Self::has_custom(placements) {
            return false;
        }
        let tablespace = if pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE {
            Some(pb.transaction_table_tablespace_id().to_vec())
        } else {
            let Some(tables) = &state.tables else {
                return true;
            };
            match tables.get(table_id) {
                Some(tablespace) => tablespace.clone(),
                None => match persisted(pb) {
                    Some(tablespace) => tablespace,
                    None => return true,
                },
            }
        };
        tablespace.is_some_and(|t| !placements.contains_key(&t))
    }

    fn install(
        &self,
        placements: HashMap<Vec<u8>, Option<ReplicationInfoPB>>,
        tables: Option<HashMap<Vec<u8>, Option<Vec<u8>>>>,
    ) {
        *self.state.write().unwrap() = Arc::new(Snapshot {
            placements: Some(placements),
            tables,
        });
    }

    fn install_placements(&self, placements: HashMap<Vec<u8>, Option<ReplicationInfoPB>>) {
        let mut state = self.state.write().unwrap();
        let tables = state.tables.clone();
        *state = Arc::new(Snapshot {
            placements: Some(placements),
            tables,
        });
    }
}

/// Reads `pg_tablespace`, and for `databases`, `pg_class` and `pg_yb_tablegroup`. Blocks.
fn read(
    host: &dyn SchemaHost,
    databases: Vec<read_ysql_tablespaces_request_pb::Database>,
) -> Result<ReadYsqlTablespacesResponsePB, Status> {
    let mut req = ReadYsqlTablespacesRequestPB::new();
    req.databases = databases;
    let bytes = req
        .write_to_bytes()
        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
    let response = host.call("ReadYsqlTablespaces", &bytes)?;
    let resp = ReadYsqlTablespacesResponsePB::parse_from_bytes(&response)
        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
    if let Some(e) = resp.error.as_ref() {
        return Err(Status::new(
            StatusCode::from_i32(e.status.code().value()),
            e.status.message().to_string(),
        ));
    }
    Ok(resp)
}

/// `GetUniqueReadReplicaPlacementUuid`: None without read replica clusters, an error when they
/// have different placement uuids.
fn unique_read_replica_placement(config: &SysClusterConfigEntryPB) -> Result<Option<Vec<u8>>, ()> {
    let mut uuid: Option<Vec<u8>> = None;
    for rr in &config.replication_info.read_replicas {
        match &uuid {
            None => uuid = Some(rr.placement_uuid().to_vec()),
            Some(u) if u.as_slice() != rr.placement_uuid() => return Err(()),
            _ => {}
        }
    }
    Ok(uuid)
}

/// `CatalogManager::GetYsqlTablespaceInfo`'s fill-in and `ValidateTableReplicationInfo`: the
/// cluster's placement uuids, and None for a placement that does not fit the cluster.
fn fit_to_cluster(
    placements: &mut HashMap<Vec<u8>, Option<ReplicationInfoPB>>,
    config: &SysClusterConfigEntryPB,
) {
    let live_uuid = config
        .replication_info
        .live_replicas
        .placement_uuid()
        .to_vec();
    let unique_rr = unique_read_replica_placement(config);
    for (id, placement) in placements.iter_mut() {
        let Some(info) = placement else {
            continue;
        };
        if !live_uuid.is_empty() {
            info.live_replicas
                .mut_or_insert_default()
                .set_placement_uuid(live_uuid.clone());
        }
        if info.read_replicas.len() == 1 && info.read_replicas[0].placement_uuid().is_empty() {
            if let Ok(Some(uuid)) = &unique_rr {
                if !uuid.is_empty() {
                    info.read_replicas[0].set_placement_uuid(uuid.clone());
                }
            }
        }
        if let Err(status) = crate::placement::validate_table_replication_info(info, config) {
            tracing::warn!(
                "Tablespace {} has invalid replication info: {}. Marking it as invalid.",
                String::from_utf8_lossy(id),
                status.message
            );
            *placement = None;
        }
    }
}

impl TableLifecycle {
    /// `ValidateReplicationInfo`, which PG asks for a tablespace's placement: per-block maximums,
    /// enough live tservers for a quorum in the live placement, and leader preferences inside it.
    /// Read replicas need no tservers up front.
    pub fn validate_replication_info(
        &self,
        snapshot: &CatalogSnapshot,
        info: &ReplicationInfoPB,
    ) -> Result<(), yb_proto::pb::master_types::MasterErrorPB> {
        let invalid = |message: String| {
            error(
                Code::INVALID_TABLE_REPLICATION_INFO,
                StatusCode::InvalidArgument,
                message,
            )
        };
        let status_error = |s: Status| error(Code::UNKNOWN_ERROR, s.code, s.message);
        if let Some(live) = info.live_replicas.as_ref() {
            master_ts_manager::placement::validate_max_num_replicas(live).map_err(status_error)?;
        }
        for read in &info.read_replicas {
            master_ts_manager::placement::validate_max_num_replicas(read).map_err(status_error)?;
        }
        let config = cluster_config(snapshot);
        let mut placement = info.live_replicas.clone().unwrap_or_default();
        if placement.placement_uuid().is_empty() {
            placement.set_placement_uuid(
                config
                    .replication_info
                    .live_replicas
                    .placement_uuid()
                    .to_vec(),
            );
        }
        let tservers =
            crate::placement::live_tservers(&self.ts_manager, &config, placement.placement_uuid());
        crate::placement::check_placement(
            &placement,
            &tservers,
            (self.options.replication_factor)(),
            true,
        )
        .map_err(|mut e| {
            e.set_code(Code::INVALID_TABLE_REPLICATION_INFO);
            e
        })?;
        // `CheckValidLeaderAffinity`.
        if !placement.placement_blocks.is_empty() {
            let sets: Vec<&[yb_proto::pb::common_net::CloudInfoPB]> =
                if !info.multi_affinitized_leaders.is_empty() {
                    info.multi_affinitized_leaders
                        .iter()
                        .map(|l| l.zones.as_slice())
                        .collect()
                } else {
                    vec![info.affinitized_leaders.as_slice()]
                };
            let mut seen = std::collections::HashSet::new();
            for zone in sets.into_iter().flatten() {
                let id = master_ts_manager::placement::placement_id(zone);
                if !seen.insert(id.clone()) {
                    return Err(invalid(format!(
                        "Invalid argument for preferred zone {id}, values should not repeat"
                    )));
                }
                if !master_ts_manager::placement::cloud_info_matches_placement(zone, &placement) {
                    return Err(invalid(format!(
                        "Preferred zone '{id}' not found in Placement info '{}'",
                        protobuf::text_format::print_to_string(&placement)
                    )));
                }
            }
        }
        Ok(())
    }

    /// Re-reads `pg_tablespace` for a tablespace the last refresh did not see
    /// (`GetTablespaceReplicationInfoWithRetry`).
    pub(crate) async fn tablespace_placement_with_retry(
        &self,
        tablespace_id: &[u8],
    ) -> Result<Option<ReplicationInfoPB>, Status> {
        if let Ok(placement) = self.tablespaces.placement(tablespace_id) {
            return Ok(placement);
        }
        let host = self.host.clone();
        let resp = tokio::task::spawn_blocking(move || read(&*host, Vec::new()))
            .await
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))??;
        let mut placements = placements_of(&resp);
        fit_to_cluster(&mut placements, &cluster_config(&self.catalog.snapshot()));
        self.tablespaces.install_placements(placements);
        self.tablespaces.placement(tablespace_id)
    }

    /// One refresh: `DoRefreshTablespaceInfo`.
    async fn refresh_tablespaces(&self, token: &LeaderToken) -> Result<(), Status> {
        let host = self.host.clone();
        let resp = tokio::task::spawn_blocking(move || read(&*host, Vec::new()))
            .await
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))??;
        let snapshot = self.catalog.snapshot();
        let mut placements = placements_of(&resp);
        fit_to_cluster(&mut placements, &cluster_config(&snapshot));

        self.drop_orphaned_transaction_tables(token, &snapshot, &placements)
            .await?;

        let tables = if Tablespaces::has_custom(&placements) {
            Some(self.read_table_tablespaces(&snapshot, &placements).await?)
        } else {
            None
        };
        let in_custom = tables
            .as_ref()
            .map_or(0, |t| t.values().filter(|ts| ts.is_some()).count());
        let previous = self.tablespaces.snapshot();
        let previous_in_custom = previous
            .tables
            .as_ref()
            .map_or(0, |t| t.values().filter(|ts| ts.is_some()).count());
        if previous.placements.as_ref().map(|p| p.len()) != Some(placements.len())
            || previous_in_custom != in_custom
        {
            tracing::info!(
                "Tablespaces refreshed: {} tablespaces, {in_custom} relations in custom ones",
                placements.len()
            );
        }
        self.tablespaces.install(placements.clone(), tables.clone());
        if let Some(tables) = tables {
            if let Err(status) = self.persist_tablespace_ids(token, &tables).await {
                tracing::warn!("Failed to persist table to tablespace mappings: {status}");
            }
            if (self
                .tablespaces
                .options
                .auto_create_local_transaction_tables)()
            {
                self.create_local_transaction_tables(token, &placements, &tables)
                    .await?;
            }
        }
        Ok(())
    }

    /// `GetYsqlTableToTablespaceMap`: every running YSQL database's relations, and the
    /// transaction status tables of known tablespaces.
    async fn read_table_tablespaces(
        &self,
        snapshot: &CatalogSnapshot,
        placements: &HashMap<Vec<u8>, Option<ReplicationInfoPB>>,
    ) -> Result<HashMap<Vec<u8>, Option<Vec<u8>>>, Status> {
        let mut databases = Vec::new();
        for namespace in snapshot.iter(SysRowEntryType::NAMESPACE) {
            let Some(hot) = namespace.namespace() else {
                continue;
            };
            if hot.state != sys_namespace_entry_pb::State::RUNNING as i32
                || hot.database_type != YQLDatabase::YQL_DATABASE_PGSQL as i32
            {
                continue;
            }
            let Some(oid) = crate::ysql::pg_database_oid(&namespace.key.id) else {
                continue;
            };
            if oid == SEQUENCES_DATA_DATABASE_OID {
                continue;
            }
            let mut database = read_ysql_tablespaces_request_pb::Database::new();
            database.set_oid(oid);
            database.set_colocated(
                namespace
                    .parse::<yb_proto::pb::catalog_entity_info::SysNamespaceEntryPB>()
                    .is_ok_and(|pb| pb.colocated()),
            );
            databases.push(database);
        }
        let host = self.host.clone();
        let resp = tokio::task::spawn_blocking(move || read(&*host, databases))
            .await
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))??;
        // The host returns every relation; those without a table here (primary key indexes,
        // relations of tables not yet created) are dropped, as C++'s ReadPgClassInfo does.
        let mut tables: HashMap<Vec<u8>, Option<Vec<u8>>> = resp
            .tables
            .iter()
            .filter(|t| snapshot.get(SysRowEntryType::TABLE, t.table_id()).is_some())
            .map(|t| {
                (
                    t.table_id().to_vec(),
                    t.tablespace_id.as_ref().map(|id| id.to_vec()),
                )
            })
            .collect();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            if pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE
                && pb.transaction_table_tablespace_id.is_some()
                && placements.contains_key(pb.transaction_table_tablespace_id())
            {
                tables.insert(
                    table.key.id.to_vec(),
                    Some(pb.transaction_table_tablespace_id().to_vec()),
                );
            }
        }
        Ok(tables)
    }

    /// `UpdateTransactionStatusTableTablespaces`: the transaction status table of a dropped
    /// tablespace loses its tablespace and is deleted.
    async fn drop_orphaned_transaction_tables(
        &self,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        placements: &HashMap<Vec<u8>, Option<ReplicationInfoPB>>,
    ) -> Result<(), Status> {
        let orphaned: Vec<Vec<u8>> = snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| {
                t.parse::<SysTablesEntryPB>().is_ok_and(|pb| {
                    pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE
                        && pb.transaction_table_tablespace_id.is_some()
                        && !placements.contains_key(pb.transaction_table_tablespace_id())
                        && pb.state() != sys_tables_entry_pb::State::DELETING
                        && pb.state() != sys_tables_entry_pb::State::DELETED
                })
            })
            .map(|t| t.key.id.to_vec())
            .collect();
        if orphaned.is_empty() {
            return Ok(());
        }
        let deadline = Instant::now() + std::time::Duration::from_secs(60);
        self.write(token, deadline, |snapshot| {
            let mut mutation = Mutation::new();
            for id in &orphaned {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, id) else {
                    continue;
                };
                let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt_error)?;
                tracing::info!(
                    "Found transaction status table {} for tablespace {} which doesn't exist, \
                     clearing tablespace id and deleting",
                    String::from_utf8_lossy(id),
                    String::from_utf8_lossy(pb.transaction_table_tablespace_id())
                );
                pb.transaction_table_tablespace_id = None;
                pb.set_version(pb.version() + 1);
                pb.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::DELETING));
                mutation = mutation
                    .read(table)
                    .upsert_message(SysRowEntryType::TABLE, id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            }
            // Tservers drop the table's status tablets on the version change.
            mutation = crate::create::bump_transaction_tables_version_by(
                snapshot,
                mutation,
                orphaned.len() as u64,
            )?;
            Ok((mutation, ()))
        })
        .await
        .map_err(|e| status_of(&e))
    }

    /// `PersistTablespaceIds`: at most 128 tables per refresh, so the first refresh after an
    /// upgrade backfills in small writes.
    async fn persist_tablespace_ids(
        &self,
        token: &LeaderToken,
        tables: &HashMap<Vec<u8>, Option<Vec<u8>>>,
    ) -> Result<(), Status> {
        const MAX_PER_REFRESH: usize = 128;
        let snapshot = self.catalog.snapshot();
        let stale: BTreeSet<Vec<u8>> = tables
            .iter()
            .filter(|(id, tablespace)| {
                snapshot
                    .get(SysRowEntryType::TABLE, id)
                    .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                    .is_some_and(|pb| {
                        pb.table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE
                            && persisted(&pb).as_ref() != Some(*tablespace)
                    })
            })
            .map(|(id, _)| id.clone())
            .take(MAX_PER_REFRESH)
            .collect();
        if stale.is_empty() {
            return Ok(());
        }
        let deadline = Instant::now() + std::time::Duration::from_secs(60);
        self.write(token, deadline, |snapshot| {
            let mut mutation = Mutation::new();
            for id in &stale {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, id) else {
                    continue;
                };
                let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt_error)?;
                if matches!(
                    pb.state(),
                    sys_tables_entry_pb::State::DELETING | sys_tables_entry_pb::State::DELETED
                ) {
                    continue;
                }
                match &tables[id] {
                    Some(tablespace) => pb.set_tablespace_id(tablespace.clone()),
                    None => {
                        pb.mut_no_custom_tablespace();
                    }
                }
                mutation = mutation
                    .read(table)
                    .upsert_message(SysRowEntryType::TABLE, id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            }
            Ok((mutation, ()))
        })
        .await
        .map_err(|e| status_of(&e))
    }

    /// `CreateTransactionStatusTablesForTablespaces`: one transaction status table for each valid
    /// tablespace some table is in.
    async fn create_local_transaction_tables(
        &self,
        token: &LeaderToken,
        placements: &HashMap<Vec<u8>, Option<ReplicationInfoPB>>,
        tables: &HashMap<Vec<u8>, Option<Vec<u8>>>,
    ) -> Result<(), Status> {
        let wanted: BTreeSet<&Vec<u8>> = tables
            .values()
            .flatten()
            .filter(|t| placements.get(*t).is_some_and(|p| p.is_some()))
            .collect();
        for tablespace in wanted {
            self.create_local_transaction_table(token, tablespace)
                .await?;
        }
        Ok(())
    }

    /// `CreateLocalTransactionStatusTableIfNeeded`.
    pub(crate) async fn create_local_transaction_table(
        &self,
        token: &LeaderToken,
        tablespace_id: &[u8],
    ) -> Result<(), Status> {
        let _creating = self.local_transaction_tables.lock().await;
        let exists = self
            .catalog
            .snapshot()
            .iter(SysRowEntryType::TABLE)
            .any(|t| {
                t.parse::<SysTablesEntryPB>().is_ok_and(|pb| {
                    pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE
                        && pb.transaction_table_tablespace_id() == tablespace_id
                        && pb.state() != sys_tables_entry_pb::State::DELETING
                        && pb.state() != sys_tables_entry_pb::State::DELETED
                })
            });
        if exists {
            return Ok(());
        }
        let name = if (self
            .tablespaces
            .options
            .name_transaction_tables_with_tablespace_id)()
        {
            let oid = std::str::from_utf8(tablespace_id.get(..8).unwrap_or_default())
                .ok()
                .and_then(|v| u32::from_str_radix(v, 16).ok())
                .ok_or_else(|| {
                    Status::new(
                        StatusCode::InvalidArgument,
                        format!(
                            "Invalid tablespace id {}",
                            String::from_utf8_lossy(tablespace_id)
                        ),
                    )
                })?;
            format!("{TRANSACTION_TABLE_PREFIX}{oid}")
        } else {
            format!(
                "{TRANSACTION_TABLE_PREFIX}{}",
                String::from_utf8_lossy(&crate::namespaces::unused_id(
                    &self.catalog.snapshot(),
                    SysRowEntryType::TABLE
                ))
            )
        };
        let mut req = CreateTableRequestPB::new();
        req.set_name(name);
        req.set_tablespace_id(tablespace_id.to_vec());
        let deadline = Instant::now() + std::time::Duration::from_secs(60);
        let mut resp = CreateTableResponsePB::new();
        match Box::pin(self.create_transaction_status_table(token, deadline, &req, &mut resp)).await
        {
            Ok(()) => Ok(()),
            Err(e) if e.code() == Code::OBJECT_ALREADY_PRESENT => Ok(()),
            Err(e) => Err(status_of(&e)),
        }
    }
}

fn corrupt_error(e: impl std::fmt::Display) -> yb_proto::pb::master_types::MasterErrorPB {
    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
}

fn status_of(e: &yb_proto::pb::master_types::MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(e.status.code().value()),
        e.status.message().to_string(),
    )
}

fn placements_of(
    resp: &ReadYsqlTablespacesResponsePB,
) -> HashMap<Vec<u8>, Option<ReplicationInfoPB>> {
    resp.tablespaces
        .iter()
        .map(|t| (t.id().to_vec(), t.replication_info.clone().into_option()))
        .collect()
}

/// The `ysql-tablespaces` controller for one term (`RefreshTablespaceInfoPeriodically`).
pub struct TablespaceController {
    tables: Arc<TableLifecycle>,
}

impl TablespaceController {
    /// A controller for the term that just began.
    pub fn new(tables: Arc<TableLifecycle>) -> Arc<TablespaceController> {
        Arc::new(TablespaceController { tables })
    }
}

impl Controller for TablespaceController {
    fn name(&self) -> &'static str {
        "ysql-tablespaces"
    }

    fn interval(&self) -> std::time::Duration {
        (self.tables.tablespaces.options.refresh_interval)()
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let options = &self.tables.tablespaces.options;
            if !(self.tables.options.enable_ysql)()
                || !(options.enabled)()
                || (options.refresh_interval)().is_zero()
            {
                return;
            }
            if crate::ysql::initdb_pending(&self.tables.catalog.snapshot()) {
                return;
            }
            let Ok(token) = self.tables.leadership.token() else {
                return;
            };
            if let Err(status) = self.tables.refresh_tablespaces(&token).await {
                tracing::warn!("Tablespace refresh task failed with error {status}");
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use yb_proto::pb::common_net::{PlacementBlockPB, PlacementInfoPB};

    const TABLE: &[u8] = b"000033e8000030008000000000004000";
    const CUSTOM: &[u8] = b"00004000000030008000000000000000";

    fn options() -> TablespaceOptions {
        TablespaceOptions {
            enabled: || true,
            refresh_interval: || std::time::Duration::from_secs(30),
            auto_create_local_transaction_tables: || true,
            name_transaction_tables_with_tablespace_id: || false,
        }
    }

    fn ysql_table() -> SysTablesEntryPB {
        let mut pb = SysTablesEntryPB::new();
        pb.set_table_type(TableType::PGSQL_TABLE_TYPE);
        pb.set_namespace_id(b"000033e8000030008000000000000000".to_vec());
        pb
    }

    fn placement(zone: &str) -> ReplicationInfoPB {
        let mut block = PlacementBlockPB::new();
        block
            .cloud_info
            .mut_or_insert_default()
            .set_placement_zone(zone.to_string());
        let mut live = PlacementInfoPB::new();
        live.set_num_replicas(1);
        live.placement_blocks.push(block);
        let mut info = ReplicationInfoPB::new();
        info.live_replicas = Some(live).into();
        info
    }

    fn placements(
        custom: Option<ReplicationInfoPB>,
    ) -> HashMap<Vec<u8>, Option<ReplicationInfoPB>> {
        HashMap::from([
            (b"pg_default".to_vec(), None),
            (b"pg_global".to_vec(), None),
            (CUSTOM.to_vec(), custom),
        ])
    }

    #[test]
    fn a_table_is_placed_by_its_tablespace_once_a_refresh_saw_both() {
        let tablespaces = Tablespaces::new(options());
        let pb = ysql_table();
        let config = SysClusterConfigEntryPB::new();
        // Before the first refresh nothing is known: the balancer waits.
        assert!(tablespaces.needs_refresh(TABLE, &pb));
        assert!(tablespaces.tablespace_of(TABLE, &pb).is_err());

        tablespaces.install(
            placements(Some(placement("z1"))),
            Some(HashMap::from([(TABLE.to_vec(), Some(CUSTOM.to_vec()))])),
        );
        assert!(!tablespaces.needs_refresh(TABLE, &pb));
        assert_eq!(
            tablespaces
                .table_replication_info(TABLE, &pb, &config)
                .unwrap(),
            placement("z1")
        );
    }

    #[test]
    fn a_table_missing_from_pg_class_keeps_its_persisted_tablespace() {
        let tablespaces = Tablespaces::new(options());
        let mut pb = ysql_table();
        tablespaces.install(placements(Some(placement("z1"))), Some(HashMap::new()));
        // Never recorded: the default tablespace, as C++ answers.
        assert_eq!(tablespaces.tablespace_of(TABLE, &pb).unwrap(), None);
        pb.set_tablespace_id(CUSTOM.to_vec());
        assert_eq!(
            tablespaces.tablespace_of(TABLE, &pb).unwrap(),
            Some(CUSTOM.to_vec())
        );
    }

    #[test]
    fn only_default_tablespaces_place_by_the_cluster() {
        let tablespaces = Tablespaces::new(options());
        let mut pb = ysql_table();
        pb.set_tablespace_id(CUSTOM.to_vec());
        tablespaces.install(
            HashMap::from([
                (b"pg_default".to_vec(), None),
                (b"pg_global".to_vec(), None),
            ]),
            None,
        );
        assert_eq!(tablespaces.tablespace_of(TABLE, &pb).unwrap(), None);
        assert!(!tablespaces.needs_refresh(TABLE, &pb));
    }

    #[test]
    fn colocated_children_and_other_table_types_ignore_tablespaces() {
        let mut child = ysql_table();
        child.set_colocated(true);
        assert!(!uses_tablespaces(TABLE, &child));
        let mut ycql = ysql_table();
        ycql.set_table_type(TableType::YQL_TABLE_TYPE);
        assert!(!uses_tablespaces(TABLE, &ycql));
        let mut global_txn = SysTablesEntryPB::new();
        global_txn.set_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);
        assert!(!uses_tablespaces(b"txn", &global_txn));
        global_txn.set_transaction_table_tablespace_id(CUSTOM.to_vec());
        assert!(uses_tablespaces(b"txn", &global_txn));
    }

    #[test]
    fn a_placement_outside_the_cluster_is_invalid() {
        let mut config = SysClusterConfigEntryPB::new();
        config
            .replication_info
            .mut_or_insert_default()
            .live_replicas
            .mut_or_insert_default()
            .set_placement_uuid(b"live".to_vec());
        let mut rr = PlacementInfoPB::new();
        rr.set_placement_uuid(b"rr".to_vec());
        config
            .replication_info
            .mut_or_insert_default()
            .read_replicas
            .push(rr);
        let mut other = placement("z1");
        other.read_replicas.push(PlacementInfoPB::new());
        other.read_replicas[0].set_placement_uuid(b"no-such-rr".to_vec());
        let mut map = placements(Some(other));
        map.insert(b"ok".to_vec(), Some(placement("z2")));
        fit_to_cluster(&mut map, &config);
        assert_eq!(map[CUSTOM], None);
        assert_eq!(
            map[b"ok".as_slice()]
                .as_ref()
                .unwrap()
                .live_replicas
                .placement_uuid(),
            b"live"
        );
    }
}
