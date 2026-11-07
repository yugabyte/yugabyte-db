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

//! `CatalogManager::FindNamespace` and `FindTable`, and the table predicates the RPCs share.

use std::sync::Arc;

use master_catalog_entity::Entity;
use master_catalog_state::defaults::SYSTEM_NAMESPACES;
use master_catalog_state::CatalogSnapshot;
use protobuf::Enum;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysTablesEntryPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, NamespaceIdentifierPB, SysRowEntryType, TableIdentifierPB,
};

use crate::error;

/// The sys-catalog tablet's id; the tables it hosts are YSQL system tables.
pub const SYS_CATALOG_TABLET_ID: &[u8] = b"00000000000000000000000000000000";

/// `GetDatabaseType`: the identifier's type, else `GetDefaultDatabaseType` of its name.
pub fn database_type(ident: &NamespaceIdentifierPB) -> YQLDatabase {
    if ident.has_database_type() {
        ident.database_type()
    } else if ident.name() == "system_redis" {
        YQLDatabase::YQL_DATABASE_REDIS
    } else {
        YQLDatabase::YQL_DATABASE_CQL
    }
}

/// `ShortDatabaseType`.
pub fn short_database_type(database_type: i32) -> String {
    match YQLDatabase::from_i32(database_type) {
        Some(YQLDatabase::YQL_DATABASE_UNKNOWN) => "UNKNOWN".into(),
        Some(YQLDatabase::YQL_DATABASE_CQL) => "YCQL".into(),
        Some(YQLDatabase::YQL_DATABASE_PGSQL) => "YSQL".into(),
        Some(YQLDatabase::YQL_DATABASE_REDIS) => "YEDIS".into(),
        None => format!("<invalid database type {database_type}>"),
    }
}

fn is_deleted_namespace(entity: &Entity) -> bool {
    entity
        .namespace()
        .is_some_and(|n| n.state == sys_namespace_entry_pb::State::DELETED as i32)
}

/// `FindNamespaceUnlocked`: by id if the identifier has one, else by name and database type. The
/// namespace's state is not checked; a deleted namespace is not found, as C++ drops it from its
/// maps.
pub fn find_namespace(
    snapshot: &CatalogSnapshot,
    ident: &NamespaceIdentifierPB,
) -> Result<Arc<Entity>, MasterErrorPB> {
    if ident.has_id() {
        return snapshot
            .get(SysRowEntryType::NAMESPACE, ident.id())
            .filter(|e| !is_deleted_namespace(e))
            .cloned()
            .ok_or_else(|| {
                error(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!(
                        "Keyspace identifier not found: {}",
                        String::from_utf8_lossy(ident.id())
                    ),
                )
            });
    }
    if ident.has_name() {
        let db_type = database_type(ident) as i32;
        return find_namespace_by_name(snapshot, ident.name(), db_type).ok_or_else(|| {
            error(
                Code::NAMESPACE_NOT_FOUND,
                StatusCode::NotFound,
                format!(
                    "{} keyspace name not found: {}",
                    short_database_type(db_type),
                    ident.name()
                ),
            )
        });
    }
    Err(error(
        Code::NAMESPACE_NOT_FOUND,
        StatusCode::NotFound,
        "Neither keyspace id nor keyspace name is specified",
    ))
}

/// The namespace named `name` of `database_type`, exactly as C++ `namespace_names_mapper_` holds
/// it: case-sensitive, in any state but DELETED.
pub fn find_namespace_by_name(
    snapshot: &CatalogSnapshot,
    name: &str,
    database_type: i32,
) -> Option<Arc<Entity>> {
    snapshot
        .iter(SysRowEntryType::NAMESPACE)
        .find(|e| {
            e.namespace()
                .is_some_and(|n| n.name == name && n.database_type == database_type)
                && !is_deleted_namespace(e)
        })
        .cloned()
}

fn table_state(entity: &Entity) -> i32 {
    entity.table().map_or(0, |t| t.state)
}

/// `FindTableUnlocked`: by id, else by name in a namespace. A DELETED table is not found unless
/// `include_deleted`; a DELETING one is found by id only, since a drop removes the name.
pub fn find_table(
    snapshot: &CatalogSnapshot,
    ident: &TableIdentifierPB,
    include_deleted: bool,
) -> Result<Arc<Entity>, MasterErrorPB> {
    let deleted = |e: &Entity| {
        !include_deleted && table_state(e) == sys_tables_entry_pb::State::DELETED as i32
    };
    if ident.has_table_id() {
        return snapshot
            .get(SysRowEntryType::TABLE, ident.table_id())
            .filter(|e| !deleted(e))
            .cloned()
            .ok_or_else(|| {
                error(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    format!(
                        "Table with id {} does not exist",
                        String::from_utf8_lossy(ident.table_id())
                    ),
                )
            });
    }
    if ident.has_table_name() {
        let namespace = find_namespace(snapshot, &ident.namespace)?;
        let ns = namespace.namespace().expect("namespace row");
        if ns.database_type == YQLDatabase::YQL_DATABASE_PGSQL as i32 {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                format!("Cannot lookup YSQL table {} by name", ident.table_name()),
            ));
        }
        return snapshot
            .table_by_name(&namespace.key.id, ident.table_name())
            .filter(|e| !deleted(e))
            .cloned()
            .ok_or_else(|| {
                error(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Table {}.{} not found", ns.name, ident.table_name()),
                )
            });
    }
    Err(error(
        Code::UNKNOWN_ERROR,
        StatusCode::InvalidArgument,
        "Neither table id or table name are specified",
    ))
}

/// `IsColocationParentTableId`: a colocated database's or tablegroup's parent table.
pub fn is_colocation_parent_table_id(id: &[u8]) -> bool {
    [
        b".colocated.parent.uuid".as_slice(),
        b".tablegroup.parent.uuid",
        b".colocation.parent.uuid",
    ]
    .iter()
    .any(|suffix| id.len() == 32 + suffix.len() && id.ends_with(suffix))
}

/// `TableInfo::is_system`: a YCQL system table (in a system keyspace) or a table hosted on the
/// sys-catalog tablet.
pub fn is_system_table(snapshot: &CatalogSnapshot, table: &Entity) -> bool {
    let Some(hot) = table.table() else {
        return false;
    };
    SYSTEM_NAMESPACES
        .iter()
        .any(|(id, _)| hot.namespace_id == *id)
        || snapshot
            .tablets_of(&table.key.id)
            .iter()
            .any(|t| &*t.key.id == SYS_CATALOG_TABLET_ID)
}

/// `hide_state` is HIDDEN. A HIDING table is still visible to clients.
pub fn is_hidden(table: &Entity) -> bool {
    table.table().is_some_and(|t| t.hidden)
        && table
            .parse::<SysTablesEntryPB>()
            .is_ok_and(|pb| pb.hide_state() == sys_tables_entry_pb::HideState::HIDDEN)
}

/// `TableInfo::visible_to_client`: PREPARING, RUNNING or ALTERING, and not HIDDEN.
pub fn is_visible_to_client(table: &Entity) -> bool {
    use sys_tables_entry_pb::State;
    let state = table_state(table);
    [State::PREPARING, State::RUNNING, State::ALTERING]
        .iter()
        .any(|s| *s as i32 == state)
        && !is_hidden(table)
}

/// `CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient`, with `CheckIsRunning` for
/// `include_hidden`: the error a request about a table that is gone or not serving gets.
pub fn check_visible(entity: &Entity, include_hidden: bool) -> Result<(), MasterErrorPB> {
    use sys_tables_entry_pb::State;
    let Some(hot) = entity.table() else {
        return Ok(());
    };
    let name = || {
        format!(
            "{}.{}",
            String::from_utf8_lossy(&hot.namespace_id),
            hot.name
        )
    };
    let running = [State::PREPARING, State::RUNNING, State::ALTERING]
        .iter()
        .any(|s| *s as i32 == hot.state);
    if include_hidden {
        if !running {
            return Err(error(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                format!("The object '{}' is not running", name()),
            ));
        }
        return Ok(());
    }
    if hot.state == State::DELETING as i32 || hot.state == State::DELETED as i32 {
        return Err(error(
            Code::OBJECT_NOT_FOUND,
            StatusCode::NotFound,
            format!("The object '{}' does not exist", name()),
        ));
    }
    if !running || is_hidden(entity) {
        return Err(error(
            Code::OBJECT_NOT_FOUND,
            StatusCode::ServiceUnavailable,
            format!("The object '{}' is not running", name()),
        ));
    }
    Ok(())
}
