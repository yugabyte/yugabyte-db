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

//! `/api/v1/namespaces`, `/api/v1/tables` and `/dump-entities`.

use std::collections::BTreeMap;

use master_catalog_entity::Entity;
use master_catalog_state::CatalogSnapshot;
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysNamespaceEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB,
};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::classify::{
    is_system_namespace, namespace_name, pg_table_oid, table_category, TableCategory, TableRow,
};
use crate::json::{enum_name, humanize_bytes, JsonWriter, Mode};
use crate::tservers::host_port_string;
use crate::{Args, View};

/// The rows of one type in sys-catalog key order, as `SysCatalogTable::Visit` reads them.
pub(crate) fn rows_in_key_order(
    snapshot: &CatalogSnapshot,
    entry_type: SysRowEntryType,
) -> Vec<&std::sync::Arc<Entity>> {
    let mut rows: Vec<_> = snapshot.iter(entry_type).collect();
    rows.sort_by(|a, b| a.key.id.cmp(&b.key.id));
    rows
}

/// `DatabasePrefix`.
fn database_prefix(database_type: YQLDatabase) -> &'static str {
    match database_type {
        YQLDatabase::YQL_DATABASE_CQL => "ycql",
        YQLDatabase::YQL_DATABASE_PGSQL => "ysql",
        YQLDatabase::YQL_DATABASE_REDIS => "yedis",
        YQLDatabase::YQL_DATABASE_UNKNOWN => "unknown",
    }
}

/// `MasterPathHandlers::HandleNamespacesJSON`. User namespaces, then system ones (by name).
pub(crate) fn namespaces(view: &View, _args: &Args) -> Vec<u8> {
    let namespaces: Vec<(Vec<u8>, SysNamespaceEntryPB)> =
        rows_in_key_order(&view.snapshot, SysRowEntryType::NAMESPACE)
            .into_iter()
            .filter_map(|e| Some((e.key.id.to_vec(), e.parse().ok()?)))
            .collect();
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    for system in [false, true] {
        jw.string(if system {
            "System Namespaces"
        } else {
            "User Namespaces"
        });
        jw.start_array();
        for (id, ns) in namespaces
            .iter()
            .filter(|(_, ns)| is_system_namespace(ns.name()) == system)
        {
            jw.start_object();
            jw.field("name", ns.name());
            jw.field("id", id);
            jw.field("language", database_prefix(ns.database_type()));
            jw.field("state", enum_name(ns.state()));
            jw.field("colocated", if ns.colocated() { "true" } else { "false" });
            jw.end_object();
        }
        jw.end_array();
    }
    jw.end_object();
    jw.into_bytes()
}

/// `MasterPathHandlers::GetParentTableOid`: the tablegroup's oid; none for a colocated database.
pub(crate) fn parent_table_oid(parent: &TableRow) -> String {
    if parent.is_colocated_db_parent() {
        return String::new();
    }
    parent
        .id()
        .get(..32)
        .and_then(pg_table_oid)
        .map(|oid| oid.to_string())
        .unwrap_or_default()
}

/// `PersistentTableInfo::GetPgTableOid`.
pub(crate) fn pg_table_oid_of(table: &TableRow) -> Option<u32> {
    if table.pb.pg_table_id().is_empty() {
        pg_table_oid(table.id())
    } else {
        pg_table_oid(table.pb.pg_table_id())
    }
}

/// `TabletReplicaDriveInfo` summed over leaders.
#[derive(Clone, Copy, Default)]
pub(crate) struct OnDiskSize {
    pub(crate) wal_files_size: u64,
    pub(crate) sst_files_size: u64,
    pub(crate) uncompressed_sst_file_size: u64,
    pub(crate) vector_index_size: u64,
}

struct TableJson {
    keyspace: String,
    table_name: String,
    state: String,
    message: String,
    uuid: Vec<u8>,
    ysql_oid: String,
    parent_oid: String,
    colocation_id: String,
    on_disk_size: OnDiskSize,
    has_missing_size: bool,
    hidden: bool,
}

/// The leader replicas' sizes of a table's serving tablets; whether a tablet had no leader.
pub(crate) fn leader_sizes(view: &View, table: &TableRow) -> Option<(OnDiskSize, bool)> {
    let mut size = OnDiskSize::default();
    let mut missing = false;
    let tablets = table.active_tablets(&view.snapshot);
    for tablet in tablets {
        match view
            .locations
            .get(&tablet.key.id)
            .and_then(|r| r.leader().map(|l| l.drive.clone()))
        {
            Some(drive) => {
                size.wal_files_size += drive.wal_files_size;
                size.sst_files_size += drive.sst_files_size;
                size.uncompressed_sst_file_size += drive.uncompressed_sst_file_size;
                size.vector_index_size += drive.vector_index_size;
            }
            None => missing = true,
        }
    }
    Some((size, missing))
}

/// `MasterPathHandlers::HandleAllTablesJSON`.
pub(crate) fn tables(view: &View, args: &Args) -> Vec<u8> {
    let snapshot = &*view.snapshot;
    let only_user_tables = args.get("only_user_tables").is_some_and(parse_leading_bool);
    let mut ordered: [BTreeMap<Vec<u8>, TableJson>; 4] = Default::default();
    for entity in snapshot.iter(SysRowEntryType::TABLE) {
        let Some(table) = TableRow::new(entity) else {
            continue;
        };
        if !table.is_running() {
            continue;
        }
        let category = table_category(snapshot, &table);
        if only_user_tables && !matches!(category, TableCategory::User | TableCategory::Index) {
            continue;
        }
        let mut row = TableJson {
            keyspace: namespace_name(snapshot, table.pb.namespace_id()),
            table_name: String::from_utf8_lossy(table.pb.name()).into_owned(),
            state: enum_name(table.pb.state()),
            message: String::from_utf8_lossy(table.pb.state_msg()).into_owned(),
            uuid: table.id().to_vec(),
            ysql_oid: String::new(),
            parent_oid: String::new(),
            colocation_id: String::new(),
            on_disk_size: OnDiskSize::default(),
            has_missing_size: false,
            hidden: table.is_hidden(),
        };
        if table.pb.table_type() == TableType::PGSQL_TABLE_TYPE && category != TableCategory::Parent
        {
            if let Some(oid) = pg_table_oid_of(&table) {
                row.ysql_oid = oid.to_string();
            }
            let colocated_id = table.pb.schema.colocated_table_id.get_or_default();
            if table.pb.schema.colocated_table_id.is_some() && colocated_id.has_colocation_id() {
                row.colocation_id = colocated_id.colocation_id().to_string();
            }
            let is_vector_index =
                table.pb.index_info.is_some() && table.pb.index_info.vector_idx_options.is_some();
            if table.is_secondary() && !is_vector_index {
                if let Some(parent) = table
                    .active_tablets(snapshot)
                    .first()
                    .and_then(|t| t.tablet())
                    .and_then(|t| snapshot.get(SysRowEntryType::TABLE, &t.table_id))
                    .and_then(TableRow::new)
                {
                    row.parent_oid = parent_table_oid(&parent);
                }
            }
        } else if category == TableCategory::Parent {
            row.ysql_oid = parent_table_oid(&table);
        }
        if category != TableCategory::System && !table.is_secondary() {
            let Some((size, missing)) = leader_sizes(view, &table) else {
                continue;
            };
            row.on_disk_size = size;
            row.has_missing_size = missing;
        }
        ordered[category as usize].insert(row.uuid.clone(), row);
    }

    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    for category in TableCategory::ALL {
        if only_user_tables && !matches!(category, TableCategory::User | TableCategory::Index) {
            continue;
        }
        let rows = &ordered[category as usize];
        if rows.is_empty() && category == TableCategory::Parent {
            continue;
        }
        jw.string(category.name().to_lowercase());
        jw.start_array();
        for row in rows.values() {
            jw.start_object();
            jw.field("keyspace", &row.keyspace);
            jw.field("table_name", &row.table_name);
            jw.field("state", &row.state);
            jw.field("message", &row.message);
            jw.field("uuid", &row.uuid);
            jw.field("ysql_oid", &row.ysql_oid);
            jw.string("hidden");
            jw.bool(row.hidden);
            if matches!(category, TableCategory::User | TableCategory::Index) {
                if !row.parent_oid.is_empty() {
                    jw.field("parent_oid", &row.parent_oid);
                }
                if !row.colocation_id.is_empty() {
                    jw.field("colocation_id", &row.colocation_id);
                }
            }
            if category != TableCategory::System {
                let size = &row.on_disk_size;
                jw.string("on_disk_size");
                jw.start_object();
                for (name, bytes) in [
                    ("wal_files_size", size.wal_files_size),
                    ("sst_files_size", size.sst_files_size),
                    (
                        "uncompressed_sst_file_size",
                        size.uncompressed_sst_file_size,
                    ),
                    ("vector_index_size", size.vector_index_size),
                ] {
                    jw.field(name, humanize_bytes(bytes));
                    jw.string(format!("{name}_bytes"));
                    jw.uint(bytes);
                }
                jw.string("has_missing_size");
                jw.bool(row.has_missing_size);
                jw.end_object();
            }
            jw.end_object();
        }
        jw.end_array();
    }
    jw.end_object();
    jw.into_bytes()
}

/// `ParseLeadingBoolValue(value, false)`.
pub(crate) fn parse_leading_bool(value: &str) -> bool {
    let value = value.trim_start().to_ascii_lowercase();
    ["true", "yes", "1", "t", "y"]
        .iter()
        .any(|p| value.starts_with(p))
}

/// `MasterPathHandlers::HandleDumpEntities`: running keyspaces, tables and tablets from the rows.
pub(crate) fn dump_entities(view: &View, _args: &Args) -> Vec<u8> {
    let snapshot = &*view.snapshot;
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();

    jw.string("keyspaces");
    jw.start_array();
    for entity in rows_in_key_order(snapshot, SysRowEntryType::NAMESPACE) {
        let Ok(ns) = entity.parse::<SysNamespaceEntryPB>() else {
            continue;
        };
        jw.start_object();
        jw.field("keyspace_id", &*entity.key.id);
        jw.field("keyspace_name", ns.name());
        jw.field("keyspace_type", database_type_name(ns.database_type()));
        jw.end_object();
    }
    jw.end_array();

    jw.string("tables");
    jw.start_array();
    for entity in rows_in_key_order(snapshot, SysRowEntryType::TABLE) {
        let Ok(table) = entity.parse::<SysTablesEntryPB>() else {
            continue;
        };
        if table.state() != sys_tables_entry_pb::State::RUNNING {
            continue;
        }
        jw.start_object();
        jw.field("table_id", &*entity.key.id);
        jw.field("keyspace_id", table.namespace_id());
        jw.field("table_name", table.name());
        jw.field("state", enum_name(table.state()));
        jw.end_object();
    }
    jw.end_array();

    jw.string("tablets");
    jw.start_array();
    for entity in rows_in_key_order(snapshot, SysRowEntryType::TABLET) {
        let Ok(tablet) = entity.parse::<SysTabletsEntryPB>() else {
            continue;
        };
        if tablet.state() != sys_tablets_entry_pb::State::RUNNING {
            continue;
        }
        jw.start_object();
        jw.field("table_id", tablet.table_id());
        jw.field("tablet_id", &*entity.key.id);
        jw.field("state", enum_name(tablet.state()));
        if let Some(cs) = tablet.committed_consensus_state.as_ref() {
            jw.string("replicas");
            jw.start_array();
            for peer in &cs.config.peers {
                jw.start_object();
                jw.field("type", enum_name(peer.member_type()));
                jw.field("server_uuid", peer.permanent_uuid());
                jw.field(
                    "addr",
                    peer.last_known_private_addr
                        .first()
                        .map(host_port_string)
                        .unwrap_or_else(|| ":0".to_string()),
                );
                jw.end_object();
            }
            jw.end_array();
            if cs.has_leader_uuid() {
                jw.field("leader", cs.leader_uuid());
            }
        }
        jw.end_object();
    }
    jw.end_array();

    jw.end_object();
    jw.into_bytes()
}

/// `DatabaseTypeName`.
fn database_type_name(database_type: YQLDatabase) -> &'static str {
    match database_type {
        YQLDatabase::YQL_DATABASE_CQL => "ycql",
        YQLDatabase::YQL_DATABASE_PGSQL => "ysql",
        YQLDatabase::YQL_DATABASE_REDIS => "yedis",
        YQLDatabase::YQL_DATABASE_UNKNOWN => "unknown",
    }
}
