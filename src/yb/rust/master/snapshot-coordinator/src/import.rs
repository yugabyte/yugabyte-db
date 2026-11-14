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

//! `ImportSnapshotMeta`: maps a backup's namespaces, types, tables and tablets to objects of this
//! universe, creating the YCQL ones and finding the YSQL ones ysql_dump created, so the backup's
//! tablet files can be restored into them (`DoImportSnapshotMeta` and its phases).

use std::collections::{BTreeMap, HashMap};
use std::sync::Arc;
use std::time::Duration;
use yb_proto::pb::catalog_entity_info::sys_tablets_entry_pb::State as TabletState;
use yb_proto::pb::wire_protocol::app_status_pb::ErrorCode as AppErrorCode;

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use master_table_lifecycle::lookup::{find_namespace_by_name, is_colocation_parent_table_id};
use master_table_lifecycle::{pg_database_oid, pg_table_oid, TableLifecycle};
use protobuf::{EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysNamespaceEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB, SysUDTypeEntryPB,
};
use yb_proto::pb::common::{PartitionPB, QLTypePB};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_backup::{
    import_snapshot_meta_response_pb, IdPairPB, ImportSnapshotMetaRequestPB,
    ImportSnapshotMetaResponsePB, SnapshotInfoPB,
};
use yb_proto::pb::master_ddl::{
    CreateNamespaceRequestPB, CreateNamespaceResponsePB, CreateTableRequestPB,
    CreateTableResponsePB, CreateUDTypeRequestPB, CreateUDTypeResponsePB, DeleteNamespaceRequestPB,
    DeleteTableRequestPB, DeleteTableResponsePB, DeleteUDTypeRequestPB, IsCreateTableDoneRequestPB,
    IsCreateTableDoneResponsePB,
};
use yb_proto::pb::master_rust_peer::{
    CompareImportSchemasRequestPB, CompareImportSchemasResponsePB, PgYbTablegroupOidRequestPB,
    PgYbTablegroupOidResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::{coded, corrupt, error, internal, unknown, SnapshotCoordinator};

/// `kUseRelfilenodeFormatVersion`.
const RELFILENODE_FORMAT_VERSION: i64 = 3;

/// Flags of the import, read when used.
#[derive(Clone, Copy, Debug)]
pub struct ImportOptions {
    /// `import_snapshot_max_concurrent_create_table_requests`.
    pub max_concurrent_create_table_requests: fn() -> u64,
    /// `import_snapshot_using_table_name`.
    pub using_table_name: fn() -> bool,
}

impl Default for ImportOptions {
    fn default() -> Self {
        ImportOptions {
            max_concurrent_create_table_requests: || 20,
            using_table_name: || false,
        }
    }
}

/// `ExternalNamespaceSnapshotData`.
#[derive(Clone, Debug)]
struct NamespaceData {
    new_id: Vec<u8>,
    db_type: YQLDatabase,
    just_created: bool,
}

/// `ExternalUDTypeSnapshotData`.
#[derive(Clone, Debug)]
struct TypeData {
    pb: SysUDTypeEntryPB,
    new_id: String,
    just_created: bool,
}

/// `ExternalTableSnapshotData`.
#[derive(Clone, Debug, Default)]
struct TableData {
    old_table_id: Vec<u8>,
    old_namespace_id: Vec<u8>,
    new_table_id: Vec<u8>,
    pb: SysTablesEntryPB,
    pg_schema_name: Vec<u8>,
    validate_schema: bool,
    num_tablets: usize,
    old_tablets: Vec<(Vec<u8>, PartitionPB)>,
    new_tablets: HashMap<(Vec<u8>, Vec<u8>), Vec<u8>>,
    meta: Option<import_snapshot_meta_response_pb::TableMetaPB>,
    just_created: bool,
    new_schema_version: u32,
}

impl TableData {
    fn is_index(&self) -> bool {
        !self.pb.indexed_table_id().is_empty()
            || self
                .pb
                .index_info
                .as_ref()
                .is_some_and(|i| !i.indexed_table_id().is_empty())
    }
}

fn snapshot_failed(status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    coded(Code::SNAPSHOT_FAILED, status, message)
}

/// The `GetPgsqlTableId` form of a YSQL object id: database oid, the version-3 and variant
/// markers, then the object oid.
fn pgsql_id(database_oid: u32, object_oid: u32) -> Vec<u8> {
    format!("{database_oid:08x}0000300080000000{object_oid:08x}").into_bytes()
}

fn ids(old: &[u8], new: &[u8]) -> MessageField<IdPairPB> {
    let mut pair = IdPairPB::new();
    pair.set_old_id(old.to_vec());
    pair.set_new_id(new.to_vec());
    MessageField::some(pair)
}

/// Every UD type id a type uses.
fn udt_ids(ty: &QLTypePB, out: &mut Vec<String>) {
    if let Some(info) = ty.udtype_info.as_ref() {
        if !info.id().is_empty() && !out.iter().any(|i| i == info.id()) {
            out.push(info.id().to_string());
        }
    }
    for p in &ty.params {
        udt_ids(p, out);
    }
}

/// `UpdateUDTypes`: the type's UD type ids replaced by their new ids.
fn update_udt_ids(
    ty: &mut QLTypePB,
    types: &BTreeMap<String, TypeData>,
) -> Result<(), MasterErrorPB> {
    if let Some(info) = ty.udtype_info.as_mut() {
        let old = info.id().to_string();
        let data = types.get(&old).ok_or_else(|| {
            snapshot_failed(
                StatusCode::InvalidArgument,
                format!("Not found referenced type id {old}"),
            )
        })?;
        if data.new_id.is_empty() {
            return Err(snapshot_failed(
                StatusCode::InternalError,
                format!("Unknown new id for UD type {} old id {old}", info.name()),
            ));
        }
        info.set_id(data.new_id.clone());
    }
    for p in ty.params.iter_mut() {
        update_udt_ids(p, types)?;
    }
    Ok(())
}

/// The state of one import.
struct Import<'a> {
    coordinator: &'a SnapshotCoordinator,
    tables: Arc<TableLifecycle>,
    token: LeaderToken,
    deadline: Instant,
    namespaces: BTreeMap<Vec<u8>, NamespaceData>,
    types: BTreeMap<String, TypeData>,
    tables_data: BTreeMap<Vec<u8>, TableData>,
    use_relfilenode: bool,
    /// A clone's target namespace name: the import makes the clone's objects.
    clone_target: Option<String>,
}

/// A table an import mapped, for a clone.
#[derive(Clone, Debug)]
pub(crate) struct ImportedTable {
    pub(crate) old_id: Vec<u8>,
    pub(crate) new_id: Vec<u8>,
    /// The snapshot's row of the table.
    pub(crate) pb: SysTablesEntryPB,
    pub(crate) meta: import_snapshot_meta_response_pb::TableMetaPB,
    pub(crate) new_schema_version: u32,
}

/// What an import mapped.
pub(crate) struct Imported {
    /// Old namespace id to new.
    pub(crate) namespaces: BTreeMap<Vec<u8>, Vec<u8>>,
    pub(crate) tables: Vec<ImportedTable>,
}

impl SnapshotCoordinator {
    /// `ImportSnapshotMeta`.
    pub async fn import_snapshot_meta(
        &self,
        tables: Arc<TableLifecycle>,
        token: &LeaderToken,
        deadline: Instant,
        req: &ImportSnapshotMetaRequestPB,
        resp: &mut ImportSnapshotMetaResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing ImportSnapshotMeta request");
        let snapshot_pb = req.snapshot.clone().unwrap_or_default();
        let imported = self
            .import(tables, token, deadline, &snapshot_pb, None)
            .await?;
        resp.tables_meta = imported.tables.into_iter().map(|t| t.meta).collect();
        Ok(())
    }

    /// `DoImportSnapshotMeta`; with `clone_target`, the objects of a clone of the snapshot's
    /// namespace under that name.
    pub(crate) async fn import(
        &self,
        tables: Arc<TableLifecycle>,
        token: &LeaderToken,
        deadline: Instant,
        snapshot_pb: &SnapshotInfoPB,
        clone_target: Option<String>,
    ) -> Result<Imported, MasterErrorPB> {
        let mut import = Import {
            coordinator: self,
            tables,
            token: *token,
            deadline,
            namespaces: BTreeMap::new(),
            types: BTreeMap::new(),
            tables_data: BTreeMap::new(),
            use_relfilenode: snapshot_pb.format_version() == RELFILENODE_FORMAT_VERSION
                && !(self.options.import.using_table_name)(),
            clone_target,
        };
        let result = import.run(snapshot_pb).await;
        if let Err(e) = result {
            import.delete_new_objects().await;
            return Err(e);
        }
        Ok(Imported {
            namespaces: import
                .namespaces
                .iter()
                .map(|(old, n)| (old.clone(), n.new_id.clone()))
                .collect(),
            tables: import
                .tables_data
                .into_values()
                .filter_map(|d| {
                    let meta = d.meta?;
                    Some(ImportedTable {
                        old_id: d.old_table_id,
                        new_id: d.new_table_id,
                        pb: d.pb,
                        meta,
                        new_schema_version: d.new_schema_version,
                    })
                })
                .collect(),
        })
    }
}

impl Import<'_> {
    fn snapshot(&self) -> Arc<CatalogSnapshot> {
        self.coordinator.catalog.snapshot()
    }

    async fn run(&mut self, snapshot_pb: &SnapshotInfoPB) -> Result<(), MasterErrorPB> {
        if !snapshot_pb.has_format_version() || snapshot_pb.format_version() < 2 {
            return Err(snapshot_failed(
                StatusCode::InternalError,
                "Expected snapshot data in format > 2",
            ));
        }
        if snapshot_pb.backup_entries.is_empty() {
            return Err(snapshot_failed(
                StatusCode::InternalError,
                "Expected snapshot data prepared for backup",
            ));
        }
        self.preprocess(snapshot_pb).await?;
        // Phase 2: UD types.
        let type_ids: Vec<String> = self.types.keys().cloned().collect();
        for id in type_ids {
            self.import_type(&id).await?;
        }
        // Phase 3: tables; phase 4: indexes.
        self.import_tables(snapshot_pb, false).await?;
        self.import_tables(snapshot_pb, true).await?;
        // Phase 5: tablets.
        for entry in &snapshot_pb.backup_entries {
            let e = entry.entry.clone().unwrap_or_default();
            if e.type_() == SysRowEntryType::TABLET {
                self.import_tablet(e.id().as_bytes(), e.data())?;
            }
        }
        // `ImportSnapshotRemoveInvalidTables`.
        let use_relfilenode = self.use_relfilenode;
        self.tables_data
            .retain(|_, d| d.meta.is_some() || !(use_relfilenode || d.is_index()));
        // Phase 6: OID counters past everything ysql_dump created, and the tservers' cached OIDs
        // dropped.
        let ysql: Vec<Vec<u8>> = self
            .namespaces
            .values()
            .filter(|n| n.db_type == YQLDatabase::YQL_DATABASE_PGSQL)
            .map(|n| n.new_id.clone())
            .collect();
        if let Some(hooks) = self.coordinator.hooks.get() {
            for namespace_id in ysql {
                hooks
                    .advance_oid_counters(namespace_id)
                    .await
                    .map_err(|s| unknown(&s))?;
            }
        }
        self.invalidate_tserver_oid_caches().await
    }

    /// Phase 1: namespaces, then the tables' and tablets' metadata.
    async fn preprocess(&mut self, snapshot_pb: &SnapshotInfoPB) -> Result<(), MasterErrorPB> {
        for entry in &snapshot_pb.backup_entries {
            let e = entry.entry.clone().unwrap_or_default();
            match e.type_() {
                SysRowEntryType::NAMESPACE => {
                    self.import_namespace(e.id().as_bytes(), e.data()).await?
                }
                SysRowEntryType::UDTYPE => {
                    if self.types.contains_key(e.id()) {
                        tracing::warn!("Ignoring duplicate type with id {}", e.id());
                    } else {
                        let pb = SysUDTypeEntryPB::parse_from_bytes(e.data())
                            .map_err(|e| unknown(&corrupt(e)))?;
                        self.types.insert(
                            e.id().to_string(),
                            TypeData {
                                pb,
                                new_id: String::new(),
                                just_created: false,
                            },
                        );
                    }
                }
                SysRowEntryType::TABLE | SysRowEntryType::TABLET => {}
                other => {
                    return Err(snapshot_failed(
                        StatusCode::InvalidArgument,
                        format!("Unexpected entry type in a backup: {other:?}"),
                    ))
                }
            }
        }
        for entry in &snapshot_pb.backup_entries {
            let e = entry.entry.clone().unwrap_or_default();
            let id = e.id().as_bytes().to_vec();
            match e.type_() {
                SysRowEntryType::TABLE => {
                    if self
                        .tables_data
                        .get(&id)
                        .is_some_and(|d| !d.old_table_id.is_empty())
                    {
                        tracing::warn!("Ignoring duplicate table with id {}", e.id());
                        continue;
                    }
                    let pb = SysTablesEntryPB::parse_from_bytes(e.data())
                        .map_err(|e| unknown(&corrupt(e)))?;
                    if pb.colocated() && is_colocated_db_parent(&id) {
                        // A legacy colocated database restored into a database without its
                        // default tablegroup has no colocated table: the parent is skipped.
                        let new_namespace = self
                            .namespaces
                            .get(pb.namespace_id())
                            .map(|n| n.new_id.clone())
                            .ok_or_else(|| {
                                coded(
                                    Code::NAMESPACE_NOT_FOUND,
                                    StatusCode::NotFound,
                                    format!(
                                        "Namespace not found: {}",
                                        String::from_utf8_lossy(pb.namespace_id())
                                    ),
                                )
                            })?;
                        if !self.legacy_colocated(&new_namespace)
                            && self.default_tablegroup_oid(&new_namespace)? == 0
                        {
                            self.tables_data.remove(&id);
                            continue;
                        }
                    }
                    let data = self.tables_data.entry(id.clone()).or_default();
                    data.old_table_id = id;
                    data.validate_schema = pb.ysql_ddl_txn_verifier_state.is_empty();
                    if !data.validate_schema {
                        tracing::info!(
                            "Marking table {} to skip schema validation due to DDL verifier state.",
                            e.id()
                        );
                    }
                    if entry.has_pg_schema_name() {
                        data.pg_schema_name = entry.pg_schema_name().to_vec();
                    }
                    data.pb = pb;
                    data.meta = Some(Default::default());
                }
                SysRowEntryType::TABLET => {
                    let tablet = SysTabletsEntryPB::parse_from_bytes(e.data())
                        .map_err(|e| unknown(&corrupt(e)))?;
                    // `PreprocessTabletEntry`; the skipped legacy parent's tablet is skipped too.
                    if tablet.colocated()
                        && is_colocated_db_parent(tablet.table_id())
                        && !self.tables_data.contains_key(tablet.table_id())
                    {
                        continue;
                    }
                    let data = self
                        .tables_data
                        .entry(tablet.table_id().to_vec())
                        .or_default();
                    if tablet.colocated() {
                        data.num_tablets = 1;
                    } else {
                        data.num_tablets += 1;
                    }
                    if let Some(p) = tablet.partition.as_ref() {
                        data.old_tablets.push((id, p.clone()));
                    }
                }
                _ => {}
            }
        }
        Ok(())
    }

    /// `ImportNamespaceEntry`: found by id or name, or created (YCQL).
    async fn import_namespace(&mut self, id: &[u8], data: &[u8]) -> Result<(), MasterErrorPB> {
        let meta = SysNamespaceEntryPB::parse_from_bytes(data).map_err(|e| unknown(&corrupt(e)))?;
        let db_type = if meta.has_database_type() {
            meta.database_type()
        } else {
            YQLDatabase::YQL_DATABASE_CQL
        };
        let snapshot = self.snapshot();
        let by_id = snapshot
            .get(SysRowEntryType::NAMESPACE, id)
            .and_then(|n| n.namespace().cloned());
        let running_by_id = by_id
            .as_ref()
            .is_some_and(|n| n.state == sys_namespace_entry_pb::State::RUNNING as i32);
        if self.clone_target.is_some() && !running_by_id {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                format!(
                    "Could not find running namespace {} to clone from.",
                    String::from_utf8_lossy(meta.name())
                ),
            ));
        }
        if self.clone_target.is_none()
            && running_by_id
            && by_id
                .as_ref()
                .is_some_and(|n| n.name.as_bytes() == meta.name())
        {
            self.namespaces.insert(
                id.to_vec(),
                NamespaceData {
                    new_id: id.to_vec(),
                    db_type,
                    just_created: false,
                },
            );
            return Ok(());
        }
        let name = self
            .clone_target
            .clone()
            .unwrap_or_else(|| String::from_utf8_lossy(meta.name()).into_owned());
        if db_type == YQLDatabase::YQL_DATABASE_PGSQL {
            // ysql_dump created the database.
            let found = find_namespace_by_name(&snapshot, &name, db_type as i32);
            let running = found.as_ref().is_some_and(|n| {
                n.namespace()
                    .is_some_and(|h| h.state == sys_namespace_entry_pb::State::RUNNING as i32)
            });
            if !running {
                let message = if found.is_none() {
                    format!("YSQL database must exist: {name}")
                } else {
                    format!("Found YSQL database must be running: {name}")
                };
                tracing::warn!("{message}");
                return Err(coded(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::InvalidArgument,
                    message,
                ));
            }
            self.namespaces.insert(
                id.to_vec(),
                NamespaceData {
                    new_id: found.unwrap().key.id.to_vec(),
                    db_type,
                    just_created: false,
                },
            );
            return Ok(());
        }
        let mut req = CreateNamespaceRequestPB::new();
        req.set_name(name.clone());
        let mut resp = CreateNamespaceResponsePB::new();
        let created = self
            .tables
            .create_namespace(&self.token, self.deadline, &req, &mut resp)
            .await;
        let just_created = match created {
            Ok(()) => true,
            Err(e) if e.status.code() == AppErrorCode::ALREADY_PRESENT => {
                if self.clone_target.is_some() {
                    return Err(error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::IllegalState,
                        format!("Namespace {name} was already created."),
                    ));
                }
                tracing::info!("Using existing namespace '{name}'");
                false
            }
            Err(e) => {
                return Err(error(
                    e.code(),
                    StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                    format!("{}: Failed to create namespace", e.status.message()),
                ))
            }
        };
        let new_id = if resp.id().is_empty() {
            find_namespace_by_name(&self.snapshot(), &name, db_type as i32)
                .map(|n| n.key.id.to_vec())
                .unwrap_or_default()
        } else {
            resp.id().to_vec()
        };
        self.namespaces.insert(
            id.to_vec(),
            NamespaceData {
                new_id,
                db_type,
                just_created,
            },
        );
        Ok(())
    }

    /// `ImportUDTypeEntry`: found by id or name, or created, with its sub-types first.
    fn import_type<'b>(
        &'b mut self,
        id: &'b str,
    ) -> std::pin::Pin<Box<dyn std::future::Future<Output = Result<(), MasterErrorPB>> + Send + 'b>>
    {
        Box::pin(async move {
            let Some(data) = self.types.get(id).cloned() else {
                return Err(snapshot_failed(
                    StatusCode::InvalidArgument,
                    format!("Not found metadata for referenced type id {id}"),
                ));
            };
            if !data.new_id.is_empty() {
                return Ok(());
            }
            let meta = data.pb.clone();
            let Some(ns) = self.namespaces.get(meta.namespace_id().as_bytes()).cloned() else {
                return Err(coded(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::InvalidArgument,
                    format!(
                        "Unknown keyspace {} referenced in UD type {} id {id}",
                        meta.namespace_id(),
                        meta.name()
                    ),
                ));
            };
            if ns.db_type != YQLDatabase::YQL_DATABASE_CQL {
                return Err(snapshot_failed(
                    StatusCode::InvalidArgument,
                    format!(
                        "UD type {} id {id} references non CQL namespace: {}",
                        meta.name(),
                        String::from_utf8_lossy(&ns.new_id)
                    ),
                ));
            }
            if meta.field_names.len() != meta.field_types.len() {
                return Err(snapshot_failed(
                    StatusCode::InvalidArgument,
                    format!(
                        "UD type {} id {id} has {} names and {} types",
                        meta.name(),
                        meta.field_names.len(),
                        meta.field_types.len()
                    ),
                ));
            }
            let snapshot = self.snapshot();
            let same = snapshot
                .get(SysRowEntryType::UDTYPE, id.as_bytes())
                .and_then(|u| u.parse::<SysUDTypeEntryPB>().ok())
                .is_some_and(|u| {
                    u.name() == meta.name() && u.namespace_id().as_bytes() == ns.new_id
                });
            let (new_id, just_created) = if same {
                (id.to_string(), false)
            } else {
                let mut subs = Vec::new();
                for t in &meta.field_types {
                    udt_ids(t, &mut subs);
                }
                for sub in subs {
                    self.import_type(&sub).await?;
                }
                let mut req = CreateUDTypeRequestPB::new();
                let n = req.namespace.mut_or_insert_default();
                n.set_id(ns.new_id.clone());
                n.set_database_type(ns.db_type);
                req.set_name(meta.name().to_string());
                for (name, ty) in meta.field_names.iter().zip(meta.field_types.iter()) {
                    req.field_names.push(name.clone());
                    let mut ty = ty.clone();
                    update_udt_ids(&mut ty, &self.types)?;
                    req.field_types.push(ty);
                }
                let mut resp = CreateUDTypeResponsePB::new();
                let created = self
                    .tables
                    .create_udtype(&self.token, self.deadline, &req, &mut resp)
                    .await;
                let just_created = match created {
                    Ok(()) => true,
                    Err(e) if e.status.code() == AppErrorCode::ALREADY_PRESENT => false,
                    Err(e) => return Err(e),
                };
                let new_id = if resp.id().is_empty() {
                    self.snapshot()
                        .iter(SysRowEntryType::UDTYPE)
                        .find(|u| {
                            u.parse::<SysUDTypeEntryPB>().is_ok_and(|p| {
                                p.name() == meta.name() && p.namespace_id().as_bytes() == ns.new_id
                            })
                        })
                        .map(|u| String::from_utf8_lossy(&u.key.id).into_owned())
                        .unwrap_or_default()
                } else {
                    String::from_utf8_lossy(resp.id()).into_owned()
                };
                (new_id, just_created)
            };
            // The type found or made must have the backup's fields.
            let found = self
                .snapshot()
                .get(SysRowEntryType::UDTYPE, new_id.as_bytes())
                .and_then(|u| u.parse::<SysUDTypeEntryPB>().ok())
                .unwrap_or_default();
            let mut expected_types = meta.field_types.clone();
            for t in expected_types.iter_mut() {
                let _ = update_udt_ids(t, &self.types);
            }
            if found.field_names != meta.field_names || found.field_types != expected_types {
                return Err(snapshot_failed(
                    StatusCode::InvalidArgument,
                    format!(
                        "UD type {} id {new_id} was changed: {{{found:?}}} expected {{{meta:?}}}",
                        meta.name()
                    ),
                ));
            }
            let entry = self.types.get_mut(id).expect("present");
            entry.new_id = new_id;
            entry.just_created = just_created;
            Ok(())
        })
    }

    /// Phases 3 and 4: `ImportSnapshotCreateAndWaitForTables` and `ImportSnapshotCreateIndexes`.
    async fn import_tables(
        &mut self,
        snapshot_pb: &SnapshotInfoPB,
        indexes: bool,
    ) -> Result<(), MasterErrorPB> {
        let mut pending: std::collections::VecDeque<Vec<u8>> = Default::default();
        let limit = (self
            .coordinator
            .options
            .import
            .max_concurrent_create_table_requests)()
        .max(1) as usize;
        for entry in &snapshot_pb.backup_entries {
            let e = entry.entry.clone().unwrap_or_default();
            let id = e.id().as_bytes().to_vec();
            if e.type_() != SysRowEntryType::TABLE {
                continue;
            }
            let Some(data) = self.tables_data.get(&id) else {
                continue;
            };
            if data.is_index() != indexes {
                continue;
            }
            if !indexes {
                while pending.len() >= limit {
                    let table = pending.pop_front().unwrap();
                    self.wait_for_create(&table).await?;
                }
            }
            let result = self.import_table(&id).await;
            if indexes {
                // An index ysql_dump left out (invalid, or backfilling) is not imported.
                if let Err(e) = &result {
                    if e.code() == Code::OBJECT_NOT_FOUND
                        && e.status.code() == AppErrorCode::INVALID_ARGUMENT
                    {
                        tracing::warn!(
                            "Index {} not found (might be backfilling, or in an invalid state); \
                             omitting it from import.",
                            e.status.message()
                        );
                        continue;
                    }
                }
                result?;
            } else {
                result?;
                // A clone's tables are not created on the tservers.
                if let Some(new_id) = self
                    .tables_data
                    .get(&id)
                    .map(|d| d.new_table_id.clone())
                    .filter(|_| self.clone_target.is_none())
                {
                    if !new_id.is_empty() {
                        pending.push_back(new_id);
                    }
                }
            }
        }
        while let Some(table) = pending.pop_front() {
            self.wait_for_create(&table).await?;
        }
        Ok(())
    }

    /// `WaitForCreateTableToFinish`.
    async fn wait_for_create(&self, table_id: &[u8]) -> Result<(), MasterErrorPB> {
        let mut req = IsCreateTableDoneRequestPB::new();
        req.table
            .mut_or_insert_default()
            .set_table_id(table_id.to_vec());
        while Instant::now() < self.deadline {
            let mut resp = IsCreateTableDoneResponsePB::new();
            self.tables
                .is_create_table_done(&self.snapshot(), &req, &mut resp)?;
            if resp.done() {
                return Ok(());
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
        Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::TimedOut,
            format!(
                "Timed out waiting for table {} to be created",
                String::from_utf8_lossy(table_id)
            ),
        ))
    }

    /// `ImportTableEntry`.
    async fn import_table(&mut self, old_id: &[u8]) -> Result<(), MasterErrorPB> {
        let mut data = self.tables_data.get(old_id).cloned().expect("listed");
        let meta = data.pb.clone();
        data.old_namespace_id = meta.namespace_id().to_vec();
        let new_namespace_id = self
            .namespaces
            .get(&data.old_namespace_id)
            .map(|n| n.new_id.clone())
            .unwrap_or_default();
        let mut parent_colocated = false;
        if self.use_relfilenode && meta.table_type() == TableType::PGSQL_TABLE_TYPE {
            let target_db = pg_database_oid(&new_namespace_id).unwrap_or_default();
            if meta.colocated() && is_tablegroup_parent(old_id) {
                data.new_table_id = restore_target_tablegroup_id(target_db, old_id)?;
                parent_colocated = true;
            } else if meta.colocated() && is_colocated_db_parent(old_id) {
                data.new_table_id = self.legacy_colocated_parent(&new_namespace_id)?;
                parent_colocated = true;
            } else {
                let relfilenode = pg_table_oid(old_id).unwrap_or_default();
                data.new_table_id = pgsql_id(target_db, relfilenode);
            }
            if self
                .snapshot()
                .get(SysRowEntryType::TABLE, &data.new_table_id)
                .is_none()
            {
                tracing::warn!(
                    "Did not find a corresponding table at restore side for the table {} from \
                     backup.",
                    String::from_utf8_lossy(old_id)
                );
                data.meta = None;
                self.tables_data.insert(old_id.to_vec(), data);
                return Ok(());
            }
        } else {
            parent_colocated = self
                .import_table_by_name(&new_namespace_id, &mut data)
                .await?;
        }
        let snapshot = self.snapshot();
        let Some(table) = snapshot
            .get(SysRowEntryType::TABLE, &data.new_table_id)
            .cloned()
        else {
            return Err(coded(
                Code::OBJECT_NOT_FOUND,
                StatusCode::InternalError,
                format!(
                    "Created table not found: {}",
                    String::from_utf8_lossy(&data.new_table_id)
                ),
            ));
        };
        let current: SysTablesEntryPB = table.parse().map_err(|e| unknown(&corrupt(e)))?;
        let is_index = !current.indexed_table_id().is_empty();
        let mut updated = current.clone();
        let mut push_schema = false;
        let is_clone = self.clone_target.is_some();
        // A clone repartitions a colocation parent too: the clone's tablets are new.
        if is_clone || !parent_colocated {
            // Schema validation; YSQL tablets repartitioned to the backup's partitions.
            let mut compare = CompareImportSchemasRequestPB::new();
            compare.persisted = current.schema.clone();
            compare.expected = meta.schema.clone();
            compare.set_is_index(is_index);
            let compared: CompareImportSchemasResponsePB =
                self.host_call("CompareImportSchemas", &compare)?;
            if data.validate_schema && !compared.matches() {
                let message = format!(
                    "Invalid created {:?} table '{}' in namespace id {}: schema={{{}}}, \
                     expected={{{}}}",
                    meta.table_type(),
                    String::from_utf8_lossy(meta.name()),
                    String::from_utf8_lossy(&new_namespace_id),
                    compared.persisted_string(),
                    compared.expected_string()
                );
                tracing::warn!("{message}");
                return Err(snapshot_failed(StatusCode::InternalError, message));
            }
            let active = snapshot.active_tablets_of(&data.new_table_id).len();
            if data.num_tablets > 0 {
                if meta.table_type() == TableType::PGSQL_TABLE_TYPE {
                    let mut changed = active != data.num_tablets;
                    if !changed {
                        let starts: Vec<&[u8]> = snapshot
                            .active_tablets_of(&data.new_table_id)
                            .iter()
                            .filter_map(|t| t.tablet().map(|h| h.partition_start.as_slice()))
                            .collect();
                        let mut wanted: Vec<&[u8]> = data
                            .old_tablets
                            .iter()
                            .map(|(_, p)| p.partition_key_start())
                            .collect();
                        wanted.sort();
                        changed = starts != wanted;
                    }
                    if changed || is_clone {
                        self.repartition(&data, changed).await?;
                    }
                } else if active != data.num_tablets {
                    let message = format!(
                        "Wrong number of tablets in created {:?} table '{}' in namespace id {}: \
                         {active} (expected {})",
                        meta.table_type(),
                        String::from_utf8_lossy(meta.name()),
                        String::from_utf8_lossy(&new_namespace_id),
                        data.num_tablets
                    );
                    tracing::warn!("{message}");
                    return Err(snapshot_failed(StatusCode::InternalError, message));
                }
            }
            // The backup's column ids, for a table whose ids ysql_dump could not keep.
            if !compared.column_ids_equal() {
                tracing::info!(
                    "Restoring column ids in {:?} table '{}'",
                    meta.table_type(),
                    String::from_utf8_lossy(meta.name())
                );
                for (i, column) in updated
                    .schema
                    .mut_or_insert_default()
                    .columns
                    .iter_mut()
                    .enumerate()
                {
                    let Some(source) = meta.schema.columns.get(i) else {
                        break;
                    };
                    if column.name() != source.name() {
                        return Err(snapshot_failed(
                            StatusCode::InternalError,
                            format!(
                                "Unexpected column name for index={i}: name={}, expected name={}",
                                source.name(),
                                column.name()
                            ),
                        ));
                    }
                    column.set_id(source.id());
                }
                updated.set_version(updated.version() + 1);
                push_schema = true;
            }
            // Missing values and next_column_id, which ysql_dump does not set.
            for (i, column) in updated
                .schema
                .mut_or_insert_default()
                .columns
                .iter_mut()
                .enumerate()
            {
                if let Some(source) = meta.schema.columns.get(i) {
                    if source.missing_value.is_some() && column.missing_value.is_none() {
                        column.missing_value = source.missing_value.clone();
                        push_schema = true;
                    }
                }
            }
            if updated.next_column_id() < meta.next_column_id() {
                updated.set_next_column_id(meta.next_column_id());
                push_schema = true;
            }
            // Properties fixed at create time.
            let (src, dst) = (
                meta.schema.table_properties.clone().unwrap_or_default(),
                updated
                    .schema
                    .mut_or_insert_default()
                    .table_properties
                    .mut_or_insert_default(),
            );
            if src.partitioning_version() != dst.partitioning_version() {
                dst.set_partitioning_version(src.partitioning_version());
                updated.set_version(updated.version() + 1);
                push_schema = true;
            }
        }
        // The schema version: past every version the backup's tablets may hold.
        if meta.table_type() == TableType::YQL_TABLE_TYPE && data.is_index() {
            if meta.version() != 0 {
                return Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::IllegalState,
                    "CQL index table should have version 0",
                ));
            }
        } else if is_clone {
            // Past every version the source table's tablets may hold.
            let source = self
                .snapshot()
                .get(SysRowEntryType::TABLE, old_id)
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                .map_or(0, |t| t.version());
            updated.set_version(source + 1);
            push_schema = true;
        } else if meta.version() >= updated.version() {
            updated.set_version(meta.version() + 1);
            push_schema = true;
        }
        data.new_schema_version = updated.version();
        if push_schema {
            self.write_table(&data.new_table_id, &current, updated)
                .await?;
        }
        let snapshot = self.snapshot();
        let mut table_meta = data.meta.clone().unwrap_or_default();
        if meta.colocated() {
            table_meta.set_table_type(if parent_colocated {
                import_snapshot_meta_response_pb::TableType::PARENT_COLOCATED_TABLE
            } else {
                import_snapshot_meta_response_pb::TableType::COLOCATED_TABLE
            });
        }
        for tablet in snapshot.hosting_tablets_of(&data.new_table_id) {
            if let Some(h) = tablet.tablet() {
                data.new_tablets.insert(
                    (h.partition_start.clone(), h.partition_end.clone()),
                    tablet.key.id.to_vec(),
                );
            }
        }
        table_meta.namespace_ids = ids(&data.old_namespace_id, &new_namespace_id);
        table_meta.table_ids = ids(old_id, &data.new_table_id);
        let mut types = Vec::new();
        for column in &meta.schema.columns {
            if let Some(ty) = column.type_.as_ref() {
                udt_ids(ty, &mut types);
            }
        }
        for t in types {
            let new = self
                .types
                .get(&t)
                .map(|d| d.new_id.clone())
                .ok_or_else(|| {
                    snapshot_failed(
                        StatusCode::InternalError,
                        format!("UDType was not imported: {t}"),
                    )
                })?;
            let mut pair = IdPairPB::new();
            pair.set_old_id(t.into_bytes());
            pair.set_new_id(new.into_bytes());
            table_meta.ud_types_ids.push(pair);
        }
        data.meta = Some(table_meta);
        self.tables_data.insert(old_id.to_vec(), data);
        Ok(())
    }

    /// `ImportTableEntryByName`: the table with the backup's id, else a YCQL table created anew or
    /// the YSQL table of that name ysql_dump created. Whether it is a colocation parent.
    async fn import_table_by_name(
        &mut self,
        new_namespace_id: &[u8],
        data: &mut TableData,
    ) -> Result<bool, MasterErrorPB> {
        let meta = data.pb.clone();
        let snapshot = self.snapshot();
        if new_namespace_id == data.old_namespace_id.as_slice() {
            if let Some(table) = snapshot.get(SysRowEntryType::TABLE, &data.old_table_id) {
                if self.check_table_for_import(&snapshot, table, data)? {
                    tracing::info!(
                        "Found existing table: {}",
                        String::from_utf8_lossy(&data.old_table_id)
                    );
                    data.new_table_id = data.old_table_id.clone();
                    return Ok(
                        meta.colocated() && is_colocation_parent_table_id(&data.old_table_id)
                    );
                }
            }
        }
        match meta.table_type() {
            TableType::YQL_TABLE_TYPE | TableType::REDIS_TABLE_TYPE => {
                self.recreate_table(new_namespace_id, data).await?;
                Ok(false)
            }
            TableType::PGSQL_TABLE_TYPE => {
                let target_db = pg_database_oid(new_namespace_id).unwrap_or_default();
                if meta.colocated() && is_colocated_db_parent(&data.old_table_id) {
                    data.new_table_id = self.legacy_colocated_parent(new_namespace_id)?;
                    return Ok(true);
                }
                if meta.colocated() && is_tablegroup_parent(&data.old_table_id) {
                    data.new_table_id =
                        restore_target_tablegroup_id(target_db, &data.old_table_id)?;
                    return Ok(true);
                }
                let mut found: Option<Vec<u8>> = None;
                for table in snapshot.iter(SysRowEntryType::TABLE) {
                    if table.table().map(|t| t.namespace_id.as_slice()) != Some(new_namespace_id) {
                        continue;
                    }
                    if !self.check_table_for_import(&snapshot, table, data)? {
                        continue;
                    }
                    // User-created: a YSQL table above the system OID range.
                    if pg_table_oid(&table.key.id).is_none_or(|oid| oid < 16384) {
                        continue;
                    }
                    match &found {
                        None => found = Some(table.key.id.to_vec()),
                        Some(other) if **other != *table.key.id => {
                            return Err(snapshot_failed(
                                StatusCode::InvalidArgument,
                                format!(
                                    "Found 2 YSQL tables with the same name: {} - {}, {}",
                                    String::from_utf8_lossy(meta.name()),
                                    String::from_utf8_lossy(other),
                                    String::from_utf8_lossy(&table.key.id)
                                ),
                            ))
                        }
                        Some(_) => {}
                    }
                }
                let Some(found) = found else {
                    let message = format!(
                        "YSQL table not found: {}",
                        String::from_utf8_lossy(meta.name())
                    );
                    tracing::warn!("{message}");
                    data.meta = None;
                    return Err(coded(
                        Code::OBJECT_NOT_FOUND,
                        StatusCode::InvalidArgument,
                        message,
                    ));
                };
                tracing::info!(
                    "Found existing table {} for {}/{} (old table {})",
                    String::from_utf8_lossy(&found),
                    String::from_utf8_lossy(new_namespace_id),
                    String::from_utf8_lossy(meta.name()),
                    String::from_utf8_lossy(&data.old_table_id)
                );
                data.new_table_id = found;
                Ok(false)
            }
            other => Err(coded(
                Code::INVALID_TABLE_TYPE,
                StatusCode::InvalidArgument,
                format!("Unexpected table type: {other:?}"),
            )),
        }
    }

    /// `CheckTableForImport`: a live table of the backup table's name and kind, and, for YSQL,
    /// its PG schema.
    fn check_table_for_import(
        &self,
        snapshot: &CatalogSnapshot,
        table: &master_catalog_entity::Entity,
        data: &TableData,
    ) -> Result<bool, MasterErrorPB> {
        if !master_table_lifecycle::lookup::is_visible_to_client(table) {
            return Ok(false);
        }
        let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
            return Ok(false);
        };
        if pb.name() != data.pb.name() {
            return Ok(false);
        }
        if data.is_index() == pb.indexed_table_id().is_empty() {
            return Ok(false);
        }
        if !data.pg_schema_name.is_empty()
            && pb.table_type() == TableType::PGSQL_TABLE_TYPE
            && !is_colocation_parent_table_id(&table.key.id)
        {
            let mut req = yb_proto::pb::master_rust_peer::GetBackupPgTableInfoRequestPB::new();
            let mut t =
                yb_proto::pb::master_rust_peer::get_backup_pg_table_info_request_pb::Table::new();
            t.set_table_id(table.key.id.to_vec());
            t.table = MessageField::some(pb);
            req.tables.push(t);
            let resp: yb_proto::pb::master_rust_peer::GetBackupPgTableInfoResponsePB =
                self.host_call("GetBackupPgTableInfo", &req)?;
            let name = resp
                .tables
                .first()
                .map(|t| t.pg_schema_name().to_vec())
                .unwrap_or_default();
            if name != data.pg_schema_name {
                tracing::info!(
                    "Schema names do not match: {} vs {} for {}",
                    String::from_utf8_lossy(&name),
                    String::from_utf8_lossy(&data.pg_schema_name),
                    String::from_utf8_lossy(&table.key.id)
                );
                return Ok(false);
            }
        }
        let _ = snapshot;
        Ok(true)
    }

    /// `RecreateTable`: a YCQL table created anew with the backup's schema and partitions.
    async fn recreate_table(
        &mut self,
        new_namespace_id: &[u8],
        data: &mut TableData,
    ) -> Result<(), MasterErrorPB> {
        let meta = data.pb.clone();
        let mut req = CreateTableRequestPB::new();
        req.set_name(String::from_utf8_lossy(meta.name()).into_owned());
        req.set_table_type(meta.table_type());
        req.set_num_tablets(data.num_tablets as i32);
        req.set_is_clone(self.clone_target.is_some());
        let mut partitions: Vec<PartitionPB> =
            data.old_tablets.iter().map(|(_, p)| p.clone()).collect();
        partitions.sort_by(|a, b| a.partition_key_start().cmp(b.partition_key_start()));
        req.partitions = partitions;
        req.namespace
            .mut_or_insert_default()
            .set_id(new_namespace_id.to_vec());
        req.partition_schema = meta.partition_schema.clone();
        req.replication_info = meta.replication_info.clone();
        let mut schema = meta.schema.clone().unwrap_or_default();
        for column in schema.columns.iter_mut() {
            if let Some(ty) = column.type_.as_mut() {
                update_udt_ids(ty, &self.types)?;
            }
        }
        req.schema = MessageField::some(schema);
        if data.is_index() {
            let old_indexed = meta.indexed_table_id().to_vec();
            let indexed = self
                .tables_data
                .get(&old_indexed)
                .map(|d| d.new_table_id.clone())
                .filter(|n| !n.is_empty())
                .unwrap_or(old_indexed);
            if self
                .snapshot()
                .get(SysRowEntryType::TABLE, &indexed)
                .is_none()
            {
                let message = format!(
                    "Indexed table not found by id: {}",
                    String::from_utf8_lossy(&indexed)
                );
                tracing::warn!("{message}");
                return Err(coded(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::InvalidArgument,
                    message,
                ));
            }
            req.set_indexed_table_id(indexed.clone());
            req.set_is_local_index(meta.is_local_index());
            req.set_is_unique_index(meta.is_unique_index());
            req.set_skip_index_backfill(true);
            let mut info = meta.index_info.clone().unwrap_or_default();
            info.clear_table_id();
            info.set_indexed_table_id(indexed);
            for c in info.columns.iter_mut() {
                c.clear_column_id();
            }
            req.index_info = MessageField::some(info);
        }
        req.set_is_matview(meta.is_matview());
        if meta.has_pg_table_id() {
            req.set_pg_table_id(meta.pg_table_id().to_vec());
        }
        let mut resp = CreateTableResponsePB::new();
        self.tables
            .create_table(&self.token, self.deadline, &req, &mut resp)
            .await?;
        data.new_table_id = resp.table_id().to_vec();
        data.just_created = true;
        tracing::info!(
            "New table id {} for {}",
            String::from_utf8_lossy(&data.new_table_id),
            String::from_utf8_lossy(&data.old_table_id)
        );
        Ok(())
    }

    /// `RepartitionTable`: a YSQL table's tablets replaced by new ones with the backup's
    /// partitions, in one write; the table goes back to PREPARING until they run.
    async fn repartition(&self, data: &TableData, changed: bool) -> Result<(), MasterErrorPB> {
        let is_clone = self.clone_target.is_some();
        tracing::info!(
            "Repartition table {} using external snapshot table {}",
            String::from_utf8_lossy(&data.new_table_id),
            String::from_utf8_lossy(&data.old_table_id)
        );
        let table_id = data.new_table_id.clone();
        let partitions: Vec<PartitionPB> =
            data.old_tablets.iter().map(|(_, p)| p.clone()).collect();
        let partition_schema = data.pb.partition_schema.clone();
        let message = format!(
            "Old tablets of table {} deleted at {}",
            String::from_utf8_lossy(&table_id),
            master_table_lifecycle::local_time_string()
        );
        let (old, _) = self
            .coordinator
            .writer
            .mutate_with(
                &self.token,
                WriteOptions {
                    deadline: Some(self.deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let table =
                        snapshot
                            .get(SysRowEntryType::TABLE, &table_id)
                            .ok_or_else(|| {
                                yb_host_api::Status::new(StatusCode::NotFound, "table is gone")
                            })?;
                    let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                    if pb.state() != sys_tables_entry_pb::State::RUNNING {
                        return Err(yb_host_api::Status::new(
                            StatusCode::IllegalState,
                            format!(
                                "Table {} not running: {:?}",
                                String::from_utf8_lossy(&table_id),
                                pb.state()
                            ),
                        ));
                    }
                    let mut mutation = Mutation::new().read(table);
                    let mut old = Vec::new();
                    let mut mapped_by_parent_id = false;
                    for tablet in snapshot.tablets_of(&table_id) {
                        let Some(hot) = tablet.tablet() else { continue };
                        if hot.table_id != table_id
                            || master_catalog_state::is_inactive_state(hot.state)
                        {
                            continue;
                        }
                        let mut tpb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                        mapped_by_parent_id |= tpb.hosted_tables_mapped_by_parent_id();
                        tpb.state = Some(EnumOrUnknown::new(TabletState::DELETED));
                        tpb.set_state_msg(message.clone().into_bytes());
                        if pb.colocated() {
                            tpb.set_table_id(Vec::new());
                        }
                        mutation = mutation.read(tablet).upsert(
                            SysRowEntryType::TABLET,
                            &tablet.key.id,
                            tpb.write_to_bytes().map_err(internal)?,
                        );
                        old.push((tablet.key.id.to_vec(), tpb));
                    }
                    let mut ids: Vec<Vec<u8>> = partitions
                        .iter()
                        .map(|_| master_table_lifecycle::generate_object_id())
                        .collect();
                    ids.sort();
                    for (id, partition) in ids.iter().zip(partitions.iter()) {
                        let mut tpb = SysTabletsEntryPB::new();
                        tpb.set_table_id(table_id.clone());
                        if mapped_by_parent_id {
                            tpb.set_hosted_tables_mapped_by_parent_id(true);
                        } else {
                            tpb.table_ids.push(table_id.clone());
                        }
                        tpb.partition = MessageField::some(partition.clone());
                        // A clone's tablets are made by the source tablets' replicas.
                        tpb.state = Some(EnumOrUnknown::new(if is_clone {
                            TabletState::CREATING
                        } else {
                            TabletState::PREPARING
                        }));
                        if is_clone {
                            tpb.set_created_by_clone(true);
                        }
                        tpb.set_colocated(pb.colocated());
                        // `SetupTabletInfo`'s initial consensus state.
                        let cstate = tpb.committed_consensus_state.mut_or_insert_default();
                        cstate.set_current_term(0);
                        cstate
                            .config
                            .mut_or_insert_default()
                            .set_committed_op_index(-1);
                        mutation = mutation.read_absent(SysRowEntryType::TABLET, id).upsert(
                            SysRowEntryType::TABLET,
                            id,
                            tpb.write_to_bytes().map_err(internal)?,
                        );
                        tracing::info!(
                            "Created tablet {} in repartitioning of table {}",
                            String::from_utf8_lossy(id),
                            String::from_utf8_lossy(&table_id)
                        );
                    }
                    pb.partition_schema = partition_schema.clone();
                    if changed {
                        pb.set_partition_list_version(pb.partition_list_version() + 1);
                    }
                    pb.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::PREPARING));
                    mutation = mutation.upsert(
                        SysRowEntryType::TABLE,
                        &table_id,
                        pb.write_to_bytes().map_err(internal)?,
                    );
                    Ok((mutation, old))
                },
            )
            .await
            .map_err(|s| unknown(&s))?;
        // The old tablets' replicas go.
        for (tablet_id, tpb) in old {
            let mut replicas: Vec<Vec<u8>> = tpb
                .committed_consensus_state
                .config
                .peers
                .iter()
                .map(|p| p.permanent_uuid().to_vec())
                .collect();
            if let Some(observed) = self.coordinator.ts_manager.locations.get(&tablet_id) {
                replicas.extend(observed.replicas.iter().map(|r| r.ts_uuid.to_vec()));
            }
            replicas.sort();
            replicas.dedup();
            for ts_uuid in replicas {
                self.coordinator
                    .ts_manager
                    .deletes
                    .push(master_ts_manager::DeleteReplica {
                        token: self.token,
                        ts_uuid: ts_uuid.into(),
                        tablet_id: tablet_id.clone(),
                        delete_type:
                            yb_proto::pb::tablet_types::TabletDataState::TABLET_DATA_DELETED,
                        cas_config_index: None,
                        hide_only: false,
                        abort_active_txns: true,
                        exclude_txn: None,
                        reason: message.clone(),
                    });
            }
        }
        // The new tablets are created by the assigner, or for a clone by the source tablets'
        // replicas; the table runs once they are.
        if is_clone {
            return Ok(());
        }
        self.wait_for_create(&table_id).await
    }

    /// Writes a table's new row; a RUNNING table becomes ALTERING so its tablets get the schema
    /// (`SendAlterTableRequest`).
    async fn write_table(
        &self,
        table_id: &[u8],
        current: &SysTablesEntryPB,
        mut updated: SysTablesEntryPB,
    ) -> Result<(), MasterErrorPB> {
        match updated.state() {
            sys_tables_entry_pb::State::RUNNING => {
                master_table_lifecycle::start_schema_push(current, &mut updated)
            }
            sys_tables_entry_pb::State::PREPARING => {
                master_table_lifecycle::defer_schema_push(current, &mut updated)
            }
            _ => {}
        }
        let table_id = table_id.to_vec();
        self.coordinator
            .writer
            .mutate_with(
                &self.token,
                WriteOptions {
                    deadline: Some(self.deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let table =
                        snapshot
                            .get(SysRowEntryType::TABLE, &table_id)
                            .ok_or_else(|| {
                                yb_host_api::Status::new(StatusCode::NotFound, "table is gone")
                            })?;
                    Ok((
                        Mutation::new().read(table).upsert_message(
                            SysRowEntryType::TABLE,
                            &table_id,
                            &updated,
                        )?,
                        (),
                    ))
                },
            )
            .await
            .map(|_| ())
            .map_err(|s| unknown(&s))
    }

    /// `ImportTabletEntry`: the backup tablet's new tablet, by partition.
    fn import_tablet(&mut self, id: &[u8], data: &[u8]) -> Result<(), MasterErrorPB> {
        let tablet = SysTabletsEntryPB::parse_from_bytes(data).map_err(|e| unknown(&corrupt(e)))?;
        if tablet.colocated()
            && is_colocated_db_parent(tablet.table_id())
            && !self.tables_data.contains_key(tablet.table_id())
        {
            return Ok(());
        }
        let use_relfilenode = self.use_relfilenode;
        let snapshot = self.snapshot();
        let Some(table_data) = self.tables_data.get_mut(tablet.table_id()) else {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                format!(
                    "Cannot find table with id {} hosted on tablet {}",
                    String::from_utf8_lossy(tablet.table_id()),
                    String::from_utf8_lossy(id)
                ),
            ));
        };
        let is_index = table_data.is_index();
        let Some(meta) = table_data.meta.as_mut() else {
            if use_relfilenode || is_index {
                return Ok(());
            }
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                format!(
                    "Missing metadata for table corresponding to snapshot table {}, id {}",
                    String::from_utf8_lossy(table_data.pb.name()),
                    String::from_utf8_lossy(&table_data.old_table_id)
                ),
            ));
        };
        if tablet.colocated() && !meta.tablets_ids.is_empty() {
            return Ok(());
        }
        if table_data.new_table_id == table_data.old_table_id
            && snapshot.get(SysRowEntryType::TABLET, id).is_some()
        {
            let mut pair = IdPairPB::new();
            pair.set_old_id(id.to_vec());
            pair.set_new_id(id.to_vec());
            meta.tablets_ids.push(pair);
            return Ok(());
        }
        let partition = tablet.partition.clone().unwrap_or_default();
        let key = (
            partition.partition_key_start().to_vec(),
            partition.partition_key_end().to_vec(),
        );
        let Some(new_id) = table_data.new_tablets.get(&key) else {
            let message = format!(
                "For new table {} (old table {}, expecting {} tablets) not found new tablet with \
                 expected [{:?}]",
                String::from_utf8_lossy(&table_data.new_table_id),
                String::from_utf8_lossy(&table_data.old_table_id),
                table_data.num_tablets,
                partition
            );
            tracing::warn!("{message}");
            return Err(coded(Code::INTERNAL_ERROR, StatusCode::NotFound, message));
        };
        let mut pair = IdPairPB::new();
        pair.set_old_id(id.to_vec());
        pair.set_new_id(new_id.clone());
        meta.tablets_ids.push(pair);
        Ok(())
    }

    /// `InvalidateTserverOidCaches`: tservers drop OIDs they cached, which may collide with the
    /// ones ysql_dump assigned.
    async fn invalidate_tserver_oid_caches(&self) -> Result<(), MasterErrorPB> {
        self.coordinator
            .writer
            .mutate(&self.token, |snapshot| {
                let config = snapshot.cluster_config().ok_or_else(|| {
                    yb_host_api::Status::new(StatusCode::IllegalState, "No cluster config")
                })?;
                let mut pb: yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB =
                    config.parse().map_err(corrupt)?;
                pb.set_oid_cache_invalidations_count(pb.oid_cache_invalidations_count() + 1);
                pb.set_version(pb.version() + 1);
                tracing::info!(
                    "Invalidating OID caches; oid_cache_invalidations_count is now {}",
                    pb.oid_cache_invalidations_count()
                );
                Ok((
                    Mutation::new().read(config).upsert(
                        SysRowEntryType::CLUSTER_CONFIG,
                        &config.key.id,
                        pb.write_to_bytes().map_err(internal)?,
                    ),
                    (),
                ))
            })
            .await
            .map(|_| ())
            .map_err(|s| unknown(&s))
    }

    /// `DeleteNewSnapshotObjects`: what a failed import created.
    async fn delete_new_objects(&self) {
        for data in self.tables_data.values().filter(|d| d.just_created) {
            let mut req = DeleteTableRequestPB::new();
            req.table
                .mut_or_insert_default()
                .set_table_id(data.new_table_id.clone());
            req.set_is_index_table(data.is_index());
            if let Err(e) = self
                .tables
                .delete_table(
                    &self.token,
                    self.deadline,
                    &req,
                    &mut DeleteTableResponsePB::new(),
                )
                .await
            {
                tracing::warn!(
                    "Failed to delete new table with id={}: {}",
                    String::from_utf8_lossy(&data.new_table_id),
                    e.status.message()
                );
            }
        }
        for data in self.types.values().filter(|d| d.just_created) {
            let mut req = DeleteUDTypeRequestPB::new();
            req.type_
                .mut_or_insert_default()
                .set_type_id(data.new_id.clone().into_bytes());
            if let Err(e) = self
                .tables
                .delete_udtype(&self.token, self.deadline, &req)
                .await
            {
                tracing::warn!(
                    "Failed to delete new UD type with id={}: {}",
                    data.new_id,
                    e.status.message()
                );
            }
        }
        for data in self.namespaces.values().filter(|d| d.just_created) {
            let mut req = DeleteNamespaceRequestPB::new();
            req.namespace
                .mut_or_insert_default()
                .set_id(data.new_id.clone());
            if let Err(e) = self
                .tables
                .delete_namespace(&self.token, self.deadline, &req)
                .await
            {
                tracing::warn!(
                    "Failed to delete new namespace with id={}: {}",
                    String::from_utf8_lossy(&data.new_id),
                    e.status.message()
                );
            }
        }
    }

    fn host_call<Req: Message, Resp: protobuf::MessageFull>(
        &self,
        method: &'static str,
        req: &Req,
    ) -> Result<Resp, MasterErrorPB> {
        let bytes = req.write_to_bytes().map_err(|e| unknown(&internal(e)))?;
        let out = self
            .coordinator
            .host
            .call(method, &bytes)
            .map_err(|s| unknown(&s))?;
        let resp = Resp::parse_from_bytes(&out).map_err(|e| unknown(&corrupt(e)))?;
        match crate::host_error(&resp) {
            Some(e) => Err(e),
            None => Ok(resp),
        }
    }

    fn legacy_colocated(&self, namespace_id: &[u8]) -> bool {
        self.snapshot()
            .get(SysRowEntryType::NAMESPACE, namespace_id)
            .and_then(|n| n.parse::<SysNamespaceEntryPB>().ok())
            .is_some_and(|n| n.colocated())
    }

    fn default_tablegroup_oid(&self, namespace_id: &[u8]) -> Result<u32, MasterErrorPB> {
        let mut req = PgYbTablegroupOidRequestPB::new();
        req.set_database_oid(pg_database_oid(namespace_id).unwrap_or_default());
        req.set_name("default".to_string());
        let resp: PgYbTablegroupOidResponsePB = self.host_call("PgYbTablegroupOid", &req)?;
        Ok(resp.oid())
    }

    /// `GetRestoreTargetParentTableForLegacyColocatedDb`.
    fn legacy_colocated_parent(&self, namespace_id: &[u8]) -> Result<Vec<u8>, MasterErrorPB> {
        if self.legacy_colocated(namespace_id) {
            let mut id = namespace_id.to_vec();
            id.extend_from_slice(b".colocated.parent.uuid");
            return Ok(id);
        }
        let oid = self.default_tablegroup_oid(namespace_id)?;
        if oid == 0 {
            return Err(coded(
                Code::INTERNAL_ERROR,
                StatusCode::InternalError,
                "Unexpected legacy colocated parent table during colocation migration. We should \
                 skip processing it.",
            ));
        }
        let mut id = pgsql_id(pg_database_oid(namespace_id).unwrap_or_default(), oid);
        id.extend_from_slice(b".colocation.parent.uuid");
        Ok(id)
    }
}

fn is_colocated_db_parent(id: &[u8]) -> bool {
    id.ends_with(b".colocated.parent.uuid")
}

fn is_tablegroup_parent(id: &[u8]) -> bool {
    id.ends_with(b".tablegroup.parent.uuid") || id.ends_with(b".colocation.parent.uuid")
}

/// `GetRestoreTargetTablegroupId`: ysql_dump keeps tablegroup oids.
fn restore_target_tablegroup_id(target_db: u32, source: &[u8]) -> Result<Vec<u8>, MasterErrorPB> {
    let tablegroup = source.get(..32).ok_or_else(|| {
        error(
            Code::UNKNOWN_ERROR,
            StatusCode::InvalidArgument,
            format!("Invalid tablegroup id {}", String::from_utf8_lossy(source)),
        )
    })?;
    let oid = pg_table_oid(tablegroup).ok_or_else(|| {
        error(
            Code::UNKNOWN_ERROR,
            StatusCode::InvalidArgument,
            format!("Invalid tablegroup id {}", String::from_utf8_lossy(source)),
        )
    })?;
    let mut id = pgsql_id(target_db, oid);
    id.extend_from_slice(&source[32..]);
    Ok(id)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn restore_target_ids() {
        // The sequences table: database and table oid 0xffff.
        assert_eq!(
            pgsql_id(0xffff, 0xffff),
            crate::SEQUENCES_DATA_TABLE_ID.to_vec()
        );
        let source = b"000033e80000300080000000000040 0a.tablegroup.parent.uuid"
            .iter()
            .filter(|b| **b != b' ')
            .copied()
            .collect::<Vec<u8>>();
        let mut expected = pgsql_id(0x5000, 0x400a);
        expected.extend_from_slice(b".tablegroup.parent.uuid");
        assert_eq!(
            restore_target_tablegroup_id(0x5000, &source).unwrap(),
            expected
        );
    }
}
