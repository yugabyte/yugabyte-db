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

//! The rows a snapshot holds: `CollectTables`, `CollectEntriesInternal`,
//! `CollectEntriesForSnapshot` and `CollectTablesAsOfTime`.

use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::CatalogSnapshot;
use master_table_lifecycle::lookup::{
    find_namespace, find_table, is_colocation_parent_table_id, is_system_table,
};
use protobuf::Message;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysRowEntries, SysRowEntry, SysTablesEntryPB,
    SysTabletsEntryPB,
};
use yb_proto::pb::common::QLTypePB;
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, SysRowEntryType, TableIdentifierPB,
};

use crate::{coded, error, SnapshotHost};

/// YSQL sequences' values: one table for every database, restored with each.
pub const SEQUENCES_DATA_TABLE_ID: &[u8] = b"0000ffff00003000800000000000ffff";

/// `CollectFlags`.
#[derive(Clone, Copy, Debug, Default)]
pub struct CollectFlags {
    /// `kAddIndexes`.
    pub add_indexes: bool,
    /// `kAddUDTypes`.
    pub add_ud_types: bool,
    /// `kIncludeParentColocatedTable`.
    pub include_parent_colocated_table: bool,
    /// `kSucceedIfCreateInProgress`.
    pub succeed_if_create_in_progress: bool,
    /// `kIncludeHiddenTables`.
    pub include_hidden_tables: bool,
}

/// A table row as of a time, with its tablets: (id, row, data) each.
type TableAt = (Vec<u8>, Vec<(Vec<u8>, SysTabletsEntryPB, Vec<u8>)>);

/// `TableDescription`: a table with its namespace and serving tablets.
struct Description {
    table: Arc<Entity>,
    namespace_id: Vec<u8>,
    tablets: Vec<Arc<Entity>>,
}

fn started_hiding(pb: &SysTablesEntryPB) -> bool {
    pb.hide_state() != sys_tables_entry_pb::HideState::VISIBLE
}

fn started_deleting(pb: &SysTablesEntryPB) -> bool {
    matches!(
        pb.state(),
        sys_tables_entry_pb::State::DELETING | sys_tables_entry_pb::State::DELETED
    )
}

/// `DescribeTable`.
fn describe(
    snapshot: &CatalogSnapshot,
    table: &Arc<Entity>,
    succeed_if_create_in_progress: bool,
) -> Result<Description, MasterErrorPB> {
    let hot = table.table().expect("table row");
    if !succeed_if_create_in_progress && hot.state == sys_tables_entry_pb::State::PREPARING as i32 {
        return Err(coded(
            Code::TABLE_CREATION_IS_IN_PROGRESS,
            StatusCode::IllegalState,
            format!(
                "Table creation is in progress: {}",
                String::from_utf8_lossy(&table.key.id)
            ),
        ));
    }
    let tablets = snapshot
        .hosting_tablets_of(&table.key.id)
        .into_iter()
        .cloned()
        .collect();
    if snapshot
        .get(SysRowEntryType::NAMESPACE, &hot.namespace_id)
        .is_none()
    {
        return Err(coded(
            Code::NAMESPACE_NOT_FOUND,
            StatusCode::NotFound,
            format!(
                "Keyspace identifier not found: {}",
                String::from_utf8_lossy(&hot.namespace_id)
            ),
        ));
    }
    Ok(Description {
        table: table.clone(),
        namespace_id: hot.namespace_id.clone(),
        tablets,
    })
}

fn by_id(id: &[u8], namespace_id: &[u8]) -> TableIdentifierPB {
    let mut ident = TableIdentifierPB::new();
    ident.set_table_id(id.to_vec());
    ident
        .namespace
        .mut_or_insert_default()
        .set_id(namespace_id.to_vec());
    ident
}

/// `CollectTable`.
fn collect_table(
    snapshot: &CatalogSnapshot,
    description: Description,
    flags: CollectFlags,
    all: &mut Vec<Description>,
    parents: &mut HashSet<Vec<u8>>,
) -> Result<(), MasterErrorPB> {
    let pb: SysTablesEntryPB = description
        .table
        .parse()
        .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
    if !flags.include_hidden_tables && started_hiding(&pb) {
        return Ok(());
    }
    if started_deleting(&pb) {
        return Ok(());
    }
    let id = description.table.key.id.to_vec();
    if flags.include_parent_colocated_table && pb.colocated() {
        let mut add_parent = !is_colocation_parent_table_id(&id);
        let is_vector_index = pb
            .index_info
            .as_ref()
            .is_some_and(|i| i.vector_idx_options.is_some());
        if add_parent && is_vector_index {
            add_parent = snapshot
                .get(SysRowEntryType::TABLE, pb.indexed_table_id())
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                .is_some_and(|t| t.colocated());
        }
        let parent_id = description
            .table
            .table()
            .map(|t| t.parent_table_id.clone())
            .unwrap_or_default();
        if add_parent && !parent_id.is_empty() && parents.insert(parent_id.clone()) {
            let parent = find_table(
                snapshot,
                &by_id(&parent_id, &description.namespace_id),
                false,
            )?;
            all.push(describe(
                snapshot,
                &parent,
                flags.succeed_if_create_in_progress,
            )?);
        }
    }
    let namespace_id = description.namespace_id.clone();
    if !is_colocation_parent_table_id(&id) || parents.insert(id.clone()) {
        all.push(description);
    }
    if flags.add_indexes {
        if !pb.indexed_table_id().is_empty() {
            return Err(coded(
                Code::INVALID_TABLE_TYPE,
                StatusCode::InvalidArgument,
                format!(
                    "Expected table, but found index: {}",
                    String::from_utf8_lossy(&id)
                ),
            ));
        }
        if pb.table_type() == TableType::PGSQL_TABLE_TYPE {
            return Err(coded(
                Code::INVALID_TABLE_TYPE,
                StatusCode::InvalidArgument,
                format!(
                    "Getting indexes for YSQL table is not supported: {}",
                    String::from_utf8_lossy(&id)
                ),
            ));
        }
        let index_flags = CollectFlags {
            add_indexes: false,
            ..flags
        };
        for index in &pb.indexes {
            let table = find_table(snapshot, &by_id(index.table_id(), &namespace_id), false)?;
            let d = describe(snapshot, &table, flags.succeed_if_create_in_progress)?;
            collect_table(snapshot, d, index_flags, all, parents)?;
        }
    }
    Ok(())
}

/// `CollectTables`, with the namespaces the identifiers name.
fn collect_tables(
    snapshot: &CatalogSnapshot,
    identifiers: &[TableIdentifierPB],
    flags: CollectFlags,
    namespaces: &mut Vec<Vec<u8>>,
) -> Result<Vec<Description>, MasterErrorPB> {
    let mut with_flags: Vec<(Arc<Entity>, CollectFlags)> = Vec::new();
    for ident in identifiers {
        if ident.table_name().is_empty() && ident.table_id().is_empty() && ident.namespace.is_some()
        {
            let namespace = match find_namespace(snapshot, &ident.namespace) {
                Ok(namespace) => namespace,
                Err(e) if e.code() == Code::NAMESPACE_NOT_FOUND => continue,
                Err(e) => return Err(e),
            };
            if !namespaces.contains(&namespace.key.id.to_vec()) {
                namespaces.push(namespace.key.id.to_vec());
            }
            let ns_flags = CollectFlags {
                add_indexes: false,
                ..flags
            };
            for table in snapshot.iter(SysRowEntryType::TABLE) {
                if is_system_table(snapshot, table)
                    || table.table().map(|t| &t.namespace_id[..]) != Some(&namespace.key.id[..])
                {
                    continue;
                }
                with_flags.push((table.clone(), ns_flags));
            }
        } else {
            let table = find_table(snapshot, ident, false)?;
            with_flags.push((table, flags));
        }
    }
    with_flags.sort_by(|a, b| a.0.key.id.cmp(&b.0.key.id));
    let mut all = Vec::new();
    let mut parents = HashSet::new();
    let mut last: Option<Vec<u8>> = None;
    for (table, flags) in with_flags {
        if last.as_deref() == Some(&table.key.id[..]) {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InternalError,
                format!(
                    "Table collected twice {}",
                    String::from_utf8_lossy(&table.key.id)
                ),
            ));
        }
        last = Some(table.key.id.to_vec());
        let d = describe(snapshot, &table, flags.succeed_if_create_in_progress)?;
        collect_table(snapshot, d, flags, &mut all, &mut parents)?;
    }
    Ok(all)
}

fn entry(entry_type: SysRowEntryType, id: &[u8], data: &[u8]) -> SysRowEntry {
    let mut e = SysRowEntry::new();
    e.set_type(entry_type);
    e.set_id(String::from_utf8_lossy(id).into_owned());
    e.set_data(data.to_vec());
    e
}

fn udtype_ids(ty: &QLTypePB, out: &mut Vec<Vec<u8>>) {
    if let Some(info) = ty.udtype_info.as_ref() {
        let id = info.id().as_bytes();
        if !id.is_empty() && !out.iter().any(|i| i == id) {
            out.push(id.to_vec());
        }
    }
    for p in &ty.params {
        udtype_ids(p, out);
    }
}

/// `CollectEntriesInternal`: namespaces, UD types, then each table and its tablets.
fn entries_of(
    snapshot: &CatalogSnapshot,
    tables: &[Description],
    flags: CollectFlags,
    namespaces: &mut Vec<Vec<u8>>,
) -> Result<SysRowEntries, MasterErrorPB> {
    let mut entries = SysRowEntries::new();
    let mut added = HashSet::new();
    for id in namespaces.iter() {
        if let Some(ns) = snapshot.get(SysRowEntryType::NAMESPACE, id) {
            entries
                .entries
                .push(entry(SysRowEntryType::NAMESPACE, id, &ns.raw));
            added.insert(id.clone());
        }
    }
    for t in tables {
        if added.insert(t.namespace_id.clone()) {
            namespaces.push(t.namespace_id.clone());
            if let Some(ns) = snapshot.get(SysRowEntryType::NAMESPACE, &t.namespace_id) {
                entries
                    .entries
                    .push(entry(SysRowEntryType::NAMESPACE, &t.namespace_id, &ns.raw));
            }
        }
    }
    if flags.add_ud_types {
        let mut types = Vec::new();
        for t in tables {
            if let Ok(pb) = t.table.parse::<SysTablesEntryPB>() {
                for column in pb.schema.columns.iter() {
                    if let Some(ty) = column.type_.as_ref() {
                        udtype_ids(ty, &mut types);
                    }
                }
            }
        }
        for id in types {
            let Some(udt) = snapshot.get(SysRowEntryType::UDTYPE, &id) else {
                return Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::NotFound,
                    format!("Udtype id not found: {}", String::from_utf8_lossy(&id)),
                ));
            };
            entries
                .entries
                .push(entry(SysRowEntryType::UDTYPE, &id, &udt.raw));
            let namespace_id = udt
                .parse::<yb_proto::pb::catalog_entity_info::SysUDTypeEntryPB>()
                .map(|pb| pb.namespace_id().as_bytes().to_vec())
                .unwrap_or_default();
            if !namespaces.contains(&namespace_id) {
                return Err(coded(
                    Code::SNAPSHOT_FAILED,
                    StatusCode::NotSupported,
                    format!(
                        "UDType from another keyspace is not supported: {}",
                        String::from_utf8_lossy(&namespace_id)
                    ),
                ));
            }
        }
    }
    let mut tablets = HashSet::new();
    for t in tables {
        entries
            .entries
            .push(entry(SysRowEntryType::TABLE, &t.table.key.id, &t.table.raw));
        for tablet in &t.tablets {
            if tablets.insert(tablet.key.id.to_vec()) {
                entries
                    .entries
                    .push(entry(SysRowEntryType::TABLET, &tablet.key.id, &tablet.raw));
            }
        }
    }
    Ok(entries)
}

/// `CollectEntriesFromActiveSysCatalog`.
pub(crate) fn collect_entries(
    snapshot: &CatalogSnapshot,
    identifiers: &[TableIdentifierPB],
    flags: CollectFlags,
) -> Result<SysRowEntries, MasterErrorPB> {
    let mut namespaces = Vec::new();
    let tables = collect_tables(snapshot, identifiers, flags, &mut namespaces)?;
    entries_of(snapshot, &tables, flags, &mut namespaces)
}

/// `CollectEntriesForSnapshot`: a schedule's entries, with the sequences' table for YSQL.
pub(crate) fn collect_for_snapshot(
    snapshot: &CatalogSnapshot,
    identifiers: &[TableIdentifierPB],
    include_hidden_tables: bool,
) -> Result<SysRowEntries, MasterErrorPB> {
    let flags = CollectFlags {
        add_indexes: true,
        include_parent_colocated_table: true,
        succeed_if_create_in_progress: true,
        include_hidden_tables,
        ..Default::default()
    };
    let mut entries = collect_entries(snapshot, identifiers, flags)?;
    if identifiers.iter().any(|i| {
        i.namespace
            .as_ref()
            .is_some_and(|n| n.database_type() == YQLDatabase::YQL_DATABASE_PGSQL)
    }) {
        let mut seq = TableIdentifierPB::new();
        seq.set_table_id(SEQUENCES_DATA_TABLE_ID.to_vec());
        match collect_entries(
            snapshot,
            &[seq],
            CollectFlags {
                succeed_if_create_in_progress: true,
                ..Default::default()
            },
        ) {
            Ok(seq_entries) => entries.entries.extend(seq_entries.entries),
            // No sequence exists yet.
            Err(e)
                if e.status.code()
                    == yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::NOT_FOUND => {}
            Err(e) => return Err(e),
        }
    }
    Ok(entries)
}

/// `CollectTablesAsOfTime` and `CollectEntriesInternal`: a YSQL database's tables and tablets as
/// the sys catalog held them at `read_time`, for a backup taken while DDLs run. The namespace is
/// read from the catalog: the snapshot being created retains it.
pub(crate) fn collect_as_of(
    host: &dyn SnapshotHost,
    snapshot: &CatalogSnapshot,
    namespace_id: &[u8],
    flags: CollectFlags,
    read_time: u64,
    timeout: Duration,
) -> Result<SysRowEntries, MasterErrorPB> {
    let unknown = |s: yb_host_api::Status| error(Code::UNKNOWN_ERROR, s.code, s.message);
    let namespace = snapshot
        .get(SysRowEntryType::NAMESPACE, namespace_id)
        .ok_or_else(|| {
            coded(
                Code::NAMESPACE_NOT_FOUND,
                StatusCode::NotFound,
                format!(
                    "Keyspace identifier not found: {}",
                    String::from_utf8_lossy(namespace_id)
                ),
            )
        })?;
    let colocated = namespace
        .parse::<yb_proto::pb::catalog_entity_info::SysNamespaceEntryPB>()
        .is_ok_and(|n| n.colocated());
    // Tables on the sys catalog tablet (sys.catalog, the YSQL catalog) are never in a snapshot.
    let system: HashSet<Vec<u8>> = snapshot
        .iter(SysRowEntryType::TABLE)
        .filter(|t| is_system_table(snapshot, t))
        .map(|t| t.key.id.to_vec())
        .collect();
    let mut tables: BTreeMap<Vec<u8>, TableAt> = BTreeMap::new();
    let mut parent: Option<Vec<u8>> = None;
    for (id, data) in host
        .scan_at(SysRowEntryType::TABLE, read_time, timeout)
        .map_err(unknown)?
    {
        let pb = SysTablesEntryPB::parse_from_bytes(&data)
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        if pb.namespace_id() != namespace_id || system.contains(&id) || id == b"sys.catalog.uuid" {
            continue;
        }
        if started_deleting(&pb) {
            continue;
        }
        if pb.hide_hybrid_time() != 0 && pb.hide_hybrid_time() <= read_time {
            continue;
        }
        if colocated && is_colocation_parent_table_id(&id) {
            parent = Some(id.clone());
        }
        tables.insert(id, (data, Vec::new()));
    }
    for (id, data) in host
        .scan_at(SysRowEntryType::TABLET, read_time, timeout)
        .map_err(unknown)?
    {
        let pb = SysTabletsEntryPB::parse_from_bytes(&data)
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        if matches!(
            pb.state(),
            sys_tablets_entry_pb::State::DELETED | sys_tablets_entry_pb::State::REPLACED
        ) || !pb.split_tablet_ids.is_empty()
        {
            continue;
        }
        if let Some((_, tablets)) = tables.get_mut(pb.table_id()) {
            tablets.push((id, pb, data));
        }
    }
    // `OrderTabletsByPartitions`.
    for (_, tablets) in tables.values_mut() {
        tablets.sort_by(|a, b| {
            a.1.partition
                .partition_key_start()
                .cmp(b.1.partition.partition_key_start())
        });
    }
    let mut ordered: Vec<(Vec<u8>, TableAt)> = Vec::new();
    if let Some(parent) = parent {
        if let Some(t) = tables.remove(&parent) {
            ordered.push((parent, t));
        }
    }
    ordered.extend(tables);
    // As C++, the namespace comes with its tables: a database with none has no entries.
    let mut entries = SysRowEntries::new();
    if !ordered.is_empty() {
        entries.entries.push(entry(
            SysRowEntryType::NAMESPACE,
            namespace_id,
            &namespace.raw,
        ));
    }
    let _ = flags;
    let mut added = HashSet::new();
    for (id, (data, tablets)) in ordered {
        entries
            .entries
            .push(entry(SysRowEntryType::TABLE, &id, &data));
        for (tablet_id, _, tablet_data) in tablets {
            if added.insert(tablet_id.clone()) {
                entries
                    .entries
                    .push(entry(SysRowEntryType::TABLET, &tablet_id, &tablet_data));
            }
        }
    }
    Ok(entries)
}
