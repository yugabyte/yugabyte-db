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

//! initdb: a new YSQL universe's first leader restores the shipped initial sys catalog snapshot
//! (`CatalogManager::MaybeRestoreInitialSysCatalogSnapshotAndReloadSysCatalog`) instead of running
//! initdb. The host restores the sys catalog tablet and its metadata; the leader then writes, with
//! its default rows, a fresh cluster config, xCluster config and persistence block map, and
//! `initdb_done` (`defaults::default_rows` with `after_initial_snapshot`).
//!
//! With `master_auto_run_initdb`, a universe that did not restore the snapshot runs initdb
//! instead (`MajorUpgrade::start_new_cluster_initdb_if_needed`); that is how the build makes the
//! snapshot.

use std::sync::Arc;

use master_catalog_state::defaults::{SYSTEM_NAMESPACES, YSQL_CATALOG_CONFIG_ID};
use master_catalog_state::{Catalog, CatalogSnapshot, CatalogWriter, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use master_table_lifecycle::lookup::SYS_CATALOG_TABLET_ID;
use master_table_lifecycle::SchemaHost;
use protobuf::{Enum, Message};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    SysConfigEntryPB, SysNamespaceEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_admin::IsInitDbDoneResponsePB;
use yb_proto::pb::master_rust_peer::{
    MakeSysCatalogTablesTransactionalRequestPB, MakeSysCatalogTablesTransactionalResponsePB,
    RestoreInitialSysCatalogSnapshotRequestPB, RestoreInitialSysCatalogSnapshotResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

/// `YsqlCatalogConfig::IsInitDbDone`: none while initdb is not done, else its outcome.
pub fn initdb_status(snapshot: &CatalogSnapshot) -> Option<Result<(), String>> {
    let config: SysConfigEntryPB = snapshot
        .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)?
        .parse()
        .ok()?;
    let ysql = config.ysql_catalog_config();
    if !ysql.initdb_done() {
        return None;
    }
    if ysql.has_initdb_error() && !ysql.initdb_error().is_empty() {
        return Some(Err(ysql.initdb_error().to_string()));
    }
    Some(Ok(()))
}

/// `CatalogManager::IsInitDbDone`.
pub fn is_initdb_done(snapshot: &CatalogSnapshot, resp: &mut IsInitDbDoneResponsePB) {
    match initdb_status(snapshot) {
        None => resp.set_done(false),
        Some(outcome) => {
            resp.set_done(true);
            if let Err(error) = outcome {
                resp.set_initdb_error(error);
            }
        }
    }
}

/// A YCQL keyspace other than the system ones: data a restore would wipe. C++ restores whenever
/// initdb is not done, but a universe a Rust leader created before it restored the snapshot has
/// no `initdb_done` and may hold YCQL data, so it is never restored.
fn has_ycql_data(snapshot: &CatalogSnapshot) -> bool {
    snapshot.iter(SysRowEntryType::NAMESPACE).any(|namespace| {
        !SYSTEM_NAMESPACES
            .iter()
            .any(|(id, _)| *namespace.key.id == **id)
            && namespace
                .parse::<SysNamespaceEntryPB>()
                .is_ok_and(|pb| pb.database_type() == YQLDatabase::YQL_DATABASE_CQL)
    })
}

/// Restores the initial sys catalog snapshot if this universe needs one, and waits for the
/// catalog to reload from the restored rows. True if it restored.
pub async fn restore_initial_snapshot_if_needed(
    catalog: &Catalog,
    host: Arc<dyn SchemaHost>,
    term: i64,
) -> Result<bool, Status> {
    let snapshot = catalog.snapshot();
    if initdb_status(&snapshot).is_some() {
        return Ok(false);
    }
    if has_ycql_data(&snapshot) {
        tracing::warn!(
            "initdb is not done, but the catalog has YCQL keyspaces: not restoring the initial \
             sys catalog snapshot"
        );
        return Ok(false);
    }
    let mut req = RestoreInitialSysCatalogSnapshotRequestPB::new();
    req.set_term(term);
    let req = req
        .write_to_bytes()
        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
    // The host waits for the restore and the metadata changes to replicate.
    let resp =
        tokio::task::spawn_blocking(move || host.call("RestoreInitialSysCatalogSnapshot", &req))
            .await
            .map_err(|e| Status::new(StatusCode::Aborted, e.to_string()))??;
    let resp = RestoreInitialSysCatalogSnapshotResponsePB::parse_from_bytes(&resp)
        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
    if let Some(error) = resp.error.as_ref() {
        return Err(Status::new(
            StatusCode::from_i32(error.status.code().value()),
            error.status.message().to_string(),
        ));
    }
    if !resp.restored() {
        return Ok(false);
    }
    // The host posted a reload before answering; the barrier waits for it.
    catalog.barrier().await;
    tracing::info!("Restoring snapshot completed, considering initdb finished");
    Ok(true)
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

/// The PG catalog tables on the sys catalog tablet that are not yet transactional YSQL catalog
/// tables, with their rows marked so.
fn untransactional_catalog_tables(
    snapshot: &CatalogSnapshot,
) -> Result<Vec<(Vec<u8>, SysTablesEntryPB)>, Status> {
    let Some(tablet) = snapshot.get(SysRowEntryType::TABLET, SYS_CATALOG_TABLET_ID) else {
        return Ok(Vec::new());
    };
    let tablet: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
    let mut tables = Vec::new();
    for id in &tablet.table_ids {
        let Some(row) = snapshot.get(SysRowEntryType::TABLE, id) else {
            continue;
        };
        let mut table: SysTablesEntryPB = row.parse().map_err(corrupt)?;
        if table.table_type() != TableType::PGSQL_TABLE_TYPE {
            continue;
        }
        let properties = table
            .schema
            .mut_or_insert_default()
            .table_properties
            .mut_or_insert_default();
        if properties.is_ysql_catalog_table() && properties.is_transactional() {
            continue;
        }
        properties.set_is_ysql_catalog_table(true);
        properties.set_is_transactional(true);
        tables.push((id.to_vec(), table));
    }
    Ok(tables)
}

/// `MakeYsqlSysCatalogTablesTransactional`, which C++ runs on every leader load that does not run
/// initdb: initdb and the initial sys catalog snapshot leave the PG catalog tables
/// non-transactional. The sys catalog tablet's metadata changes first (on the host), then the
/// table rows and `transactional_sys_catalog_enabled`, so a leader that fails in between redoes
/// both.
pub async fn make_catalog_transactional(
    catalog: &Catalog,
    writer: &CatalogWriter,
    host: Arc<dyn SchemaHost>,
    token: &LeaderToken,
) -> Result<(), Status> {
    let snapshot = catalog.snapshot();
    let config: Option<SysConfigEntryPB> = snapshot
        .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)
        .map(|row| row.parse())
        .transpose()
        .map_err(corrupt)?;
    if config
        .as_ref()
        .is_none_or(|c| c.ysql_catalog_config().transactional_sys_catalog_enabled())
    {
        return Ok(());
    }
    let tables = untransactional_catalog_tables(&snapshot)?;
    if !tables.is_empty() {
        let mut req = MakeSysCatalogTablesTransactionalRequestPB::new();
        req.table_ids = tables.iter().map(|(id, _)| id.clone()).collect();
        req.set_term(token.term());
        let req = req.write_to_bytes().map_err(corrupt)?;
        let resp = tokio::task::spawn_blocking(move || {
            host.call("MakeSysCatalogTablesTransactional", &req)
        })
        .await
        .map_err(|e| Status::new(StatusCode::Aborted, e.to_string()))??;
        let resp = MakeSysCatalogTablesTransactionalResponsePB::parse_from_bytes(&resp)
            .map_err(corrupt)?;
        if let Some(error) = resp.error.as_ref() {
            return Err(Status::new(
                StatusCode::from_i32(error.status.code().value()),
                error.status.message().to_string(),
            ));
        }
    }
    let (updated, _) = writer
        .mutate_with(token, WriteOptions::default(), |snapshot| {
            let mut mutation = Mutation::new();
            let tables = untransactional_catalog_tables(snapshot)?;
            for (id, table) in &tables {
                let row = snapshot
                    .get(SysRowEntryType::TABLE, id)
                    .ok_or_else(|| Status::new(StatusCode::NotFound, "Catalog table is gone"))?;
                mutation = mutation
                    .read(row)
                    .upsert_message(SysRowEntryType::TABLE, id, table)?;
            }
            let row = snapshot
                .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)
                .ok_or_else(|| Status::new(StatusCode::NotFound, "No YSQL catalog config"))?;
            let mut config: SysConfigEntryPB = row.parse().map_err(corrupt)?;
            config
                .mut_ysql_catalog_config()
                .set_transactional_sys_catalog_enabled(true);
            mutation = mutation.read(row).upsert_message(
                SysRowEntryType::SYS_CONFIG,
                YSQL_CATALOG_CONFIG_ID,
                &config,
            )?;
            Ok((mutation, tables.len()))
        })
        .await?;
    tracing::info!("Made {updated} YSQL sys catalog tables transactional");
    Ok(())
}
