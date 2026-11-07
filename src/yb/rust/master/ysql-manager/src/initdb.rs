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
//! Running initdb itself (`master_auto_run_initdb`) is not served.

use std::sync::Arc;

use master_catalog_state::defaults::{SYSTEM_NAMESPACES, YSQL_CATALOG_CONFIG_ID};
use master_catalog_state::{Catalog, CatalogSnapshot};
use master_table_lifecycle::SchemaHost;
use protobuf::{Enum, Message};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{SysConfigEntryPB, SysNamespaceEntryPB};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_admin::IsInitDbDoneResponsePB;
use yb_proto::pb::master_rust_peer::{
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
