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

//! The `TableInfo` predicates the C++ pages sort tables by (`catalog_entity_info.cc`).

use std::sync::Arc;

use master_catalog_entity::Entity;
use master_catalog_state::CatalogSnapshot;
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, SysTablesEntryPB};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_types::SysRowEntryType;

use master_table_lifecycle as lifecycle;

pub(crate) const SYSTEM_NAMESPACE_ID: &[u8] = b"00000000000000000000000000000001";
const SYSTEM_PLATFORM_NAMESPACE: &str = "system_platform";
const YB_SYSTEM_DB_NAME: &str = "yb_system";
const PG_SEQUENCES_DATA_OID: u32 = 0xFFFF;

/// `IsSystemNamespace`.
pub(crate) fn is_system_namespace(name: &[u8]) -> bool {
    matches!(
        name,
        b"system" | b"system_auth" | b"system_distributed" | b"system_schema" | b"system_traces"
    )
}

/// One table row, parsed once, with what the pages ask of it.
pub(crate) struct TableRow {
    pub(crate) entity: Arc<Entity>,
    pub(crate) pb: SysTablesEntryPB,
}

impl TableRow {
    pub(crate) fn new(entity: &Arc<Entity>) -> Option<TableRow> {
        Some(TableRow {
            entity: entity.clone(),
            pb: entity.parse().ok()?,
        })
    }

    pub(crate) fn id(&self) -> &[u8] {
        &self.entity.key.id
    }

    /// `TableInfo::is_running`: PREPARING, RUNNING or ALTERING.
    pub(crate) fn is_running(&self) -> bool {
        use sys_tables_entry_pb::State;
        matches!(
            self.pb.state(),
            State::PREPARING | State::RUNNING | State::ALTERING
        )
    }

    /// `is_hidden`: `hide_state` is HIDDEN.
    pub(crate) fn is_hidden(&self) -> bool {
        self.pb.hide_state() == sys_tables_entry_pb::HideState::HIDDEN
    }

    /// `PersistentTableInfo::indexed_table_id`.
    pub(crate) fn indexed_table_id(&self) -> &[u8] {
        if self.pb.index_info.is_some() {
            self.pb.index_info.indexed_table_id()
        } else {
            self.pb.indexed_table_id()
        }
    }

    pub(crate) fn is_colocation_parent(&self) -> bool {
        lifecycle::lookup::is_colocation_parent_table_id(self.id())
    }

    /// `IsColocatedDbParentTableId`.
    pub(crate) fn is_colocated_db_parent(&self) -> bool {
        self.id().ends_with(b".colocated.parent.uuid")
    }

    /// `TableInfo::IsSecondaryTable`: colocated, and not the parent.
    pub(crate) fn is_secondary(&self) -> bool {
        self.pb.colocated() && !self.is_colocation_parent()
    }

    /// `TableInfo::is_system`: hosted on the sys catalog tablet, or a YCQL virtual table.
    pub(crate) fn is_system(&self, snapshot: &CatalogSnapshot) -> bool {
        lifecycle::lookup::is_system_table(snapshot, &self.entity)
    }

    fn is_sequences_system_table(&self) -> bool {
        self.pb.table_type() == TableType::PGSQL_TABLE_TYPE
            && !self.is_colocation_parent()
            && pg_database_oid(self.pb.namespace_id()) == Some(PG_SEQUENCES_DATA_OID)
            && pg_table_oid(self.id()) == Some(PG_SEQUENCES_DATA_OID)
    }

    /// `TableInfo::IsUserCreated`.
    pub(crate) fn is_user_created(&self, snapshot: &CatalogSnapshot) -> bool {
        let table_type = self.pb.table_type();
        if table_type != TableType::PGSQL_TABLE_TYPE && table_type != TableType::YQL_TABLE_TYPE {
            return false;
        }
        !self.is_system(snapshot)
            && !self.is_sequences_system_table()
            && self.pb.namespace_id() != SYSTEM_NAMESPACE_ID
            && !self.is_colocation_parent()
            && self.pb.namespace_name() != YB_SYSTEM_DB_NAME.as_bytes()
    }

    /// `TableInfo::IsUserTable`.
    pub(crate) fn is_user_table(&self, snapshot: &CatalogSnapshot) -> bool {
        self.is_user_created(snapshot) && self.indexed_table_id().is_empty()
    }

    /// `TableInfo::IsUserIndex`.
    pub(crate) fn is_user_index(&self, snapshot: &CatalogSnapshot) -> bool {
        self.is_user_created(snapshot) && !self.indexed_table_id().is_empty()
    }

    /// The tablets that list the table, the inactive ones (split parents) too
    /// (`GetTabletsIncludeInactive`). A colocated table's are its parent's.
    pub(crate) fn tablets<'a>(&self, snapshot: &'a CatalogSnapshot) -> Vec<&'a Arc<Entity>> {
        let own = snapshot.tablets_of(self.id());
        let parent = self.entity.table().map(|t| &t.parent_table_id);
        match parent {
            Some(parent) if own.is_empty() && !parent.is_empty() && parent != self.id() => {
                snapshot.tablets_of(parent)
            }
            _ => own,
        }
    }

    /// `TableInfo::GetTablets`: the serving tablets, a colocated table's parent's.
    pub(crate) fn active_tablets<'a>(&self, snapshot: &'a CatalogSnapshot) -> Vec<&'a Arc<Entity>> {
        snapshot.hosting_tablets_of(self.id())
    }
}

/// `MasterPathHandlers::TableType`, in the order the pages list them.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub(crate) enum TableCategory {
    User = 0,
    Index = 1,
    Parent = 2,
    System = 3,
}

impl TableCategory {
    pub(crate) const ALL: [TableCategory; 4] = [
        TableCategory::User,
        TableCategory::Index,
        TableCategory::Parent,
        TableCategory::System,
    ];

    /// `table_type_`, which the JSON keys lower-case.
    pub(crate) fn name(self) -> &'static str {
        match self {
            TableCategory::User => "User",
            TableCategory::Index => "Index",
            TableCategory::Parent => "Parent",
            TableCategory::System => "System",
        }
    }
}

/// `MasterPathHandlers::GetTableType`. Platform tables count as system tables.
pub(crate) fn table_category(snapshot: &CatalogSnapshot, table: &TableRow) -> TableCategory {
    if namespace_name(snapshot, table.pb.namespace_id()) == SYSTEM_PLATFORM_NAMESPACE {
        TableCategory::System
    } else if table.is_user_index(snapshot) {
        TableCategory::Index
    } else if table.is_user_table(snapshot) {
        TableCategory::User
    } else if table.is_colocation_parent() {
        TableCategory::Parent
    } else {
        TableCategory::System
    }
}

/// `CatalogManager::GetNamespaceName`: empty when the namespace is not known.
pub(crate) fn namespace_name(snapshot: &CatalogSnapshot, namespace_id: &[u8]) -> String {
    snapshot
        .get(SysRowEntryType::NAMESPACE, namespace_id)
        .and_then(|e| e.namespace().map(|n| n.name.clone()))
        .unwrap_or_default()
}

/// `GetPgsqlDatabaseOid`.
pub(crate) fn pg_database_oid(id: &[u8]) -> Option<u32> {
    lifecycle::pg_database_oid(id)
}

/// `GetPgsqlTableOid`.
pub(crate) fn pg_table_oid(id: &[u8]) -> Option<u32> {
    lifecycle::pg_table_oid(id)
}
