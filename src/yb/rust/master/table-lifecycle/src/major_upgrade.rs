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

//! The DDL side of a YSQL major upgrade. While the catalog is on another major version than this
//! build, initdb creates the new version's PG catalog tables next to the old ones, and pg_upgrade
//! "drops" and "re-creates" each database: only its new-version catalog tables go and come back
//! (the `ysql_next_major_version_state` of the database), and its user tables are reused, never
//! created. A rollback deletes every new-version catalog table; the cleanup after finalize deletes
//! the old version's.

use protobuf::Message;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, SysConfigEntryPB, SysNamespaceEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB, YsqlMajorCatalogUpgradeInfoPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_ddl::{CreateTableRequestPB, CreateTableResponsePB};
use yb_proto::pb::master_rust_peer::{CreateYsqlSysTableRequestPB, CreateYsqlSysTableResponsePB};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use master_catalog_state::defaults::YSQL_CATALOG_CONFIG_ID;
use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use tokio::time::Instant;

use crate::lookup::SYS_CATALOG_TABLET_ID;
use crate::ysql::{pg_table_oid, PG_FIRST_NORMAL_OID};
use crate::{error, TableLifecycle};

type TableState = yb_proto::pb::catalog_entity_info::sys_tables_entry_pb::State;

/// `ysql_major_catalog_upgrade_info`; the default (DONE, version 11) for a catalog from before the
/// field. None without a YSQL catalog config.
pub fn upgrade_info(snapshot: &CatalogSnapshot) -> Option<YsqlMajorCatalogUpgradeInfoPB> {
    let config: SysConfigEntryPB = snapshot
        .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)?
        .parse()
        .ok()?;
    Some(
        config
            .ysql_catalog_config()
            .ysql_major_catalog_upgrade_info
            .clone()
            .unwrap_or_default(),
    )
}

/// The catalog's YSQL major version: `ysql_major_catalog_upgrade_info.catalog_version`, or 11 for
/// a catalog without it (the field came with PG15). None without a YSQL catalog config.
pub fn catalog_major_version(snapshot: &CatalogSnapshot) -> Option<u32> {
    upgrade_info(snapshot).map(|info| info.catalog_version())
}

/// `YsqlManager::IsMajorUpgradeInProgress`: the catalog is on another YSQL major version than this
/// build (`build_major_version`, None when YSQL is off).
pub fn major_upgrade_in_progress(
    snapshot: &CatalogSnapshot,
    build_major_version: Option<u32>,
) -> bool {
    match (build_major_version, catalog_major_version(snapshot)) {
        (Some(build), Some(catalog)) => build != catalog,
        _ => false,
    }
}

/// `IsYsqlCatalogTable` and the version byte of the id (`IsCurrentPgVersion`).
fn catalog_table_version_is_current(id: &[u8]) -> Option<bool> {
    if pg_table_oid(id).is_none_or(|oid| oid >= PG_FIRST_NORMAL_OID) {
        return None;
    }
    Some(
        std::str::from_utf8(&id[18..20])
            .ok()
            .and_then(|v| u8::from_str_radix(v, 16).ok())
            == Some(1),
    )
}

/// `IsCurrentVersionYsqlCatalogTable`.
pub(crate) fn is_current_version_catalog_table(id: &[u8]) -> bool {
    catalog_table_version_is_current(id) == Some(true)
}

/// `IsPriorVersionYsqlCatalogTable`.
pub(crate) fn is_prior_version_catalog_table(id: &[u8]) -> bool {
    catalog_table_version_is_current(id) == Some(false)
}

/// Which of a database's tables `delete_ysql_db_tables` deletes (`ShouldDeleteTableForYsqlDbDrop`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum DbTables {
    /// DROP DATABASE: all of them.
    All,
    /// pg_upgrade's drop of a database during a major upgrade: its new-version catalog tables, not
    /// the shared ones (they belong to template1 but are global).
    NextVersionDrop,
    /// A rollback: every new-version catalog table.
    CurrentVersion,
    /// The cleanup after finalize: the old version's catalog tables.
    PriorVersion,
}

impl DbTables {
    pub(crate) fn selects(self, id: &[u8], is_shared: impl FnOnce() -> bool) -> bool {
        match self {
            DbTables::All => true,
            DbTables::NextVersionDrop => is_current_version_catalog_table(id) && !is_shared(),
            DbTables::CurrentVersion => is_current_version_catalog_table(id),
            DbTables::PriorVersion => is_prior_version_catalog_table(id),
        }
    }

    /// Whether the catalog rows go eagerly: identical rows are inserted again soon after.
    pub(crate) fn deletes_rows(self) -> bool {
        matches!(self, DbTables::NextVersionDrop | DbTables::CurrentVersion)
    }
}

fn corrupt(e: impl std::fmt::Display) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
}

fn ysql_namespaces(snapshot: &CatalogSnapshot) -> Vec<Vec<u8>> {
    snapshot
        .iter(SysRowEntryType::NAMESPACE)
        .filter(|n| {
            n.namespace()
                .is_some_and(|h| h.database_type == YQLDatabase::YQL_DATABASE_PGSQL as i32)
        })
        .map(|n| n.key.id.to_vec())
        .collect()
}

impl TableLifecycle {
    /// `YsqlManager::IsMajorUpgradeInProgress`.
    pub fn ysql_major_upgrade_in_progress(&self, snapshot: &CatalogSnapshot) -> bool {
        major_upgrade_in_progress(snapshot, self.options.ysql_major_version)
    }

    /// `CreateTable` of a YSQL table during a major upgrade: a user table pg_upgrade restores
    /// already exists and is reused; only the new version's PG catalog tables are created. None
    /// when the caller should go on creating the table.
    pub(crate) fn reuse_table_during_major_upgrade(
        &self,
        snapshot: &CatalogSnapshot,
        req: &CreateTableRequestPB,
        resp: &mut CreateTableResponsePB,
    ) -> Option<Result<(), MasterErrorPB>> {
        if !self.ysql_major_upgrade_in_progress(snapshot) {
            return None;
        }
        let existing = snapshot
            .get(SysRowEntryType::TABLE, req.table_id())
            .filter(|t| {
                t.table()
                    .is_some_and(|h| h.state != TableState::DELETED as i32)
            });
        if let Some(table) = existing {
            if req.is_pg_catalog_table() {
                return Some(Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::IllegalState,
                    "Detected a pre-existing catalog table in CreateTable during a ysql major \
                     version upgrade.",
                )));
            }
            tracing::info!(
                "Reusing table {} for the new YSQL major version",
                String::from_utf8_lossy(&table.key.id)
            );
            resp.set_table_id(table.key.id.to_vec());
            return Some(Ok(()));
        }
        if !req.is_pg_catalog_table() {
            return Some(Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                "No new tables can be created during a major YSQL upgrade",
            )));
        }
        None
    }

    /// `CreateYsqlSysTable`: a PG catalog table initdb creates, hosted by the sys catalog tablet.
    /// The host adds it to the tablet; then its row and the tablet row's table ids are written.
    pub(crate) async fn create_ysql_sys_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateTableRequestPB,
        resp: &mut CreateTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let namespace = crate::lookup::find_namespace(&snapshot, &req.namespace)?;
        let namespace_name = namespace
            .namespace()
            .map(|n| n.name.clone())
            .unwrap_or_default();
        if let Some(table) = snapshot
            .get(SysRowEntryType::TABLE, req.table_id())
            .filter(|t| {
                t.table()
                    .is_some_and(|h| h.state != TableState::DELETED as i32)
            })
        {
            resp.set_table_id(table.key.id.to_vec());
            return Err(error(
                Code::OBJECT_ALREADY_PRESENT,
                StatusCode::AlreadyPresent,
                format!(
                    "YSQL table '{namespace_name}.{}' (ID: {}) already exists",
                    table.table().map_or("", |h| h.name.as_str()),
                    String::from_utf8_lossy(&table.key.id)
                ),
            ));
        }
        let mut request = req.clone();
        request.namespace.mut_or_insert_default().id = Some(namespace.key.id.to_vec());
        let mut host_req = CreateYsqlSysTableRequestPB::new();
        host_req.request = Some(request).into();
        host_req.set_namespace_name(namespace_name.into_bytes());
        host_req.set_term(token.term());
        let bytes = host_req.write_to_bytes().map_err(corrupt)?;
        let host = self.host.clone();
        let response = tokio::task::spawn_blocking(move || host.call("CreateYsqlSysTable", &bytes))
            .await
            .map_err(corrupt)?
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        let response =
            CreateYsqlSysTableResponsePB::parse_from_bytes(&response).map_err(corrupt)?;
        if let Some(e) = response.error.into_option() {
            return Err(e);
        }
        let row = response.table.into_option().unwrap_or_default();
        let table_id = row.id().to_vec();
        let table: SysTablesEntryPB = row.table.into_option().unwrap_or_default();
        self.pause_before_upsert_ysql_sys_table().await;
        self.write(token, deadline, |snapshot| {
            let mut mutation = match snapshot.get(SysRowEntryType::TABLE, &table_id) {
                Some(old) => Mutation::new().read(old),
                None => Mutation::new().read_absent(SysRowEntryType::TABLE, &table_id),
            }
            .upsert_message(SysRowEntryType::TABLE, &table_id, &table)
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            let sys_tablet = snapshot
                .get(SysRowEntryType::TABLET, SYS_CATALOG_TABLET_ID)
                .ok_or_else(|| corrupt("No sys catalog tablet row"))?;
            let mut tablet: SysTabletsEntryPB = sys_tablet.parse().map_err(corrupt)?;
            if !tablet.hosted_tables_mapped_by_parent_id() && !tablet.table_ids.contains(&table_id)
            {
                tablet.table_ids.push(table_id.clone());
                mutation = mutation
                    .read(sys_tablet)
                    .upsert_message(SysRowEntryType::TABLET, SYS_CATALOG_TABLET_ID, &tablet)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            }
            Ok((mutation, ()))
        })
        .await?;
        resp.set_table_id(table_id);
        Ok(())
    }

    /// `RollbackMajorVersionCatalogImpl`: every new-version catalog table goes, and each YSQL
    /// database is RUNNING again with its next-version state reset and no transaction.
    pub async fn rollback_ysql_major_catalog(
        &self,
        token: &LeaderToken,
    ) -> Result<(), MasterErrorPB> {
        let namespaces = ysql_namespaces(&self.catalog.snapshot());
        for id in &namespaces {
            tracing::info!(
                "Deleting ysql major catalog tables for namespace {}",
                String::from_utf8_lossy(id)
            );
            self.delete_ysql_db_tables(token, id, DbTables::CurrentVersion)
                .await?;
        }
        let deadline = Instant::now() + std::time::Duration::from_secs(60);
        self.write(token, deadline, |snapshot| {
            let mut mutation = Mutation::new();
            for id in &namespaces {
                let Some(namespace) = snapshot.get(SysRowEntryType::NAMESPACE, id) else {
                    continue;
                };
                let mut ns: SysNamespaceEntryPB = namespace.parse().map_err(corrupt)?;
                match ns.state() {
                    sys_namespace_entry_pb::State::DELETED
                    | sys_namespace_entry_pb::State::UNKNOWN => continue,
                    sys_namespace_entry_pb::State::PREPARING
                    | sys_namespace_entry_pb::State::RUNNING => {}
                    state => {
                        return Err(error(
                            Code::UNKNOWN_ERROR,
                            StatusCode::IllegalState,
                            format!(
                                "Namespace {} in state {state:?} during rollback. Expected \
                                 PREPARING or RUNNING.",
                                String::from_utf8_lossy(ns.name())
                            ),
                        ))
                    }
                }
                ns.set_state(sys_namespace_entry_pb::State::RUNNING);
                ns.set_ysql_next_major_version_state(
                    sys_namespace_entry_pb::YsqlNextMajorVersionState::NEXT_VER_RUNNING,
                );
                ns.transaction = Default::default();
                mutation = mutation
                    .read(namespace)
                    .upsert_message(SysRowEntryType::NAMESPACE, id, &ns)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            }
            Ok((mutation, ()))
        })
        .await
    }

    /// `CleanupPreviousYsqlMajorCatalog`'s deletes: the old version's catalog tables of every
    /// YSQL database. Their rows are left for compaction.
    pub async fn delete_prior_ysql_major_catalog(
        &self,
        token: &LeaderToken,
    ) -> Result<(), MasterErrorPB> {
        for id in ysql_namespaces(&self.catalog.snapshot()) {
            tracing::info!(
                "Deleting previous ysql major catalog tables for namespace {}",
                String::from_utf8_lossy(&id)
            );
            self.delete_ysql_db_tables(token, &id, DbTables::PriorVersion)
                .await?;
        }
        Ok(())
    }
}
