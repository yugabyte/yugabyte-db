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

use std::hash::{DefaultHasher, Hash, Hasher};
use std::sync::Arc;

use master_catalog_entity::{
    decode_entity, Entity, EntityError, EntityId, HotFields, RowKey, TableHot,
};
use master_sys_catalog::{HybridTime, OpId, RowChange};
use protobuf::Enum;
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, sys_tablets_entry_pb};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_types::SysRowEntryType;

type TypeMap = imbl::HashMap<EntityId, Arc<Entity>>;

/// One consistent version of the catalog. Cloning is cheap: the maps are persistent and share
/// structure, so applying a batch costs O(changed rows * log n) and a reader's `Arc` never changes.
#[derive(Clone, Default)]
pub struct CatalogSnapshot {
    pub(crate) by_type: imbl::HashMap<i32, TypeMap>,
    /// (namespace id, name) -> table id, for tables `FindTable` finds by name: not PGSQL, not
    /// deleting and not hidden (`table_names_map_`).
    pub(crate) tables_by_name: imbl::HashMap<(Vec<u8>, String), EntityId>,
    /// Parent table id -> the tables that are not deleted and name it as `parent_table_id`.
    pub(crate) child_tables: imbl::HashMap<Vec<u8>, imbl::OrdSet<EntityId>>,
    /// Table id -> (partition start, tablet id) of every tablet that lists the table. A table row
    /// never lists its tablets.
    pub(crate) table_tablets: imbl::HashMap<Vec<u8>, imbl::OrdSet<(Vec<u8>, EntityId)>>,
    /// Whether the tablet is open and was scanned. False in shell mode and before the first load.
    pub loaded: bool,
    /// Incremented by every full load; a mutation built on an earlier generation conflicts.
    pub generation: u64,
    /// Every row written at or before this time is reflected, and none after it.
    pub as_of: HybridTime,
    /// The last applied op that wrote rows, since the last load.
    pub last_op: Option<OpId>,
    /// Incremented on every publish.
    pub seq: u64,
}

impl std::fmt::Debug for CatalogSnapshot {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("CatalogSnapshot")
            .field("loaded", &self.loaded)
            .field("generation", &self.generation)
            .field("as_of", &self.as_of)
            .field("seq", &self.seq)
            .field(
                "rows",
                &self.by_type.values().map(|m| m.len()).sum::<usize>(),
            )
            .finish()
    }
}

impl CatalogSnapshot {
    /// The entity of `entry_type` with `id`.
    pub fn get(&self, entry_type: SysRowEntryType, id: &[u8]) -> Option<&Arc<Entity>> {
        self.by_type.get(&(entry_type as i32))?.get(id)
    }

    /// Every entity of `entry_type`, in no particular order.
    pub fn iter(&self, entry_type: SysRowEntryType) -> impl Iterator<Item = &Arc<Entity>> {
        self.by_type
            .get(&(entry_type as i32))
            .into_iter()
            .flat_map(|m| m.values())
    }

    /// How many entities of `entry_type` there are.
    pub fn count(&self, entry_type: SysRowEntryType) -> usize {
        self.by_type
            .get(&(entry_type as i32))
            .map_or(0, |m| m.len())
    }

    /// Every row, as `(entry_type, id, data)`.
    pub fn rows(&self) -> impl Iterator<Item = (i32, &[u8], &[u8])> {
        self.by_type
            .iter()
            .flat_map(|(t, m)| m.values().map(move |e| (*t, &*e.key.id, &*e.raw)))
    }

    /// The table named `name` in namespace `namespace_id`.
    pub fn table_by_name(&self, namespace_id: &[u8], name: &str) -> Option<&Arc<Entity>> {
        let id = self
            .tables_by_name
            .get(&(namespace_id.to_vec(), name.to_string()))?;
        self.get(SysRowEntryType::TABLE, id)
    }

    /// The tables a tablet hosts, as `TabletInfo::GetTableIds` holds them: the row's `table_ids`,
    /// or, for a tablet mapped by parent id, the tables whose parent is its table, then that table.
    pub fn hosted_table_ids(&self, tablet: &Entity) -> Vec<Vec<u8>> {
        let Some(hot) = tablet.tablet() else {
            return Vec::new();
        };
        if !hot.mapped_by_parent_id {
            return hot.table_ids.clone();
        }
        let mut ids: Vec<Vec<u8>> = self
            .child_tables
            .get(&hot.table_id)
            .into_iter()
            .flat_map(|children| children.iter().map(|id| id.to_vec()))
            .collect();
        ids.push(hot.table_id.clone());
        ids
    }

    /// The tablets that list `table_id`, by partition start, then tablet id.
    pub fn tablets_of(&self, table_id: &[u8]) -> Vec<&Arc<Entity>> {
        self.table_tablets
            .get(table_id)
            .into_iter()
            .flat_map(|set| set.iter())
            .filter_map(|(_, id)| self.get(SysRowEntryType::TABLET, id))
            .collect()
    }

    /// The table's serving tablets in partition order: for each partition start the deepest split
    /// that is not deleted or replaced, as the C++ master's `TableInfo::partitions_` holds them.
    pub fn active_tablets_of(&self, table_id: &[u8]) -> Vec<&Arc<Entity>> {
        let mut result: Vec<&Arc<Entity>> = Vec::new();
        for tablet in self.tablets_of(table_id) {
            let Some(hot) = tablet.tablet() else { continue };
            if is_inactive_state(hot.state) {
                continue;
            }
            match result.last() {
                Some(last)
                    if last.tablet().map(|t| &t.partition_start) == Some(&hot.partition_start) =>
                {
                    if last.tablet().map_or(0, |t| t.split_depth) < hot.split_depth {
                        *result.last_mut().unwrap() = tablet;
                    }
                }
                _ => result.push(tablet),
            }
        }
        result
    }

    /// The cluster config, whose entry id is empty.
    pub fn cluster_config(&self) -> Option<&Arc<Entity>> {
        self.get(SysRowEntryType::CLUSTER_CONFIG, b"")
    }

    /// An order-independent digest of every row, for comparing with a scan.
    pub fn digest(&self) -> u64 {
        rows_digest(self.rows())
    }

    /// Applies one row change. `version` becomes the entity's version.
    pub(crate) fn apply(&mut self, change: &RowChange, version: u64) -> Result<(), EntityError> {
        let entry_type = change.entry_type();
        if entry_type == SysRowEntryType::UNKNOWN {
            // A type newer than this build, which the C++ loader of this release skips too.
            tracing::warn!(
                "Ignoring a sys catalog row of an entry type this build does not know: {}",
                String::from_utf8_lossy(change.id())
            );
            return Ok(());
        }
        let id = change.id();
        if let Some(old) = self.get(entry_type, id).cloned() {
            self.unindex(&old);
        }
        let map = self.by_type.entry(entry_type as i32).or_default();
        match change {
            RowChange::Upsert { data, .. } => {
                let entity = match decode_entity(entry_type, id, data, version) {
                    Ok(entity) => entity,
                    Err(e @ EntityError::Unsupported(_)) => return Err(e),
                    Err(e) => {
                        // A C++ follower applies such a row without decoding it; keep it so the
                        // catalog still matches the tablet, and let readers that parse it fail.
                        tracing::error!(
                            "Sys catalog row {entry_type:?} {} does not decode: {e}",
                            String::from_utf8_lossy(id)
                        );
                        Entity {
                            key: RowKey::new(entry_type, id),
                            raw: data.as_slice().into(),
                            hot: HotFields::Other,
                            version,
                        }
                    }
                };
                let entity = Arc::new(entity);
                map.insert(entity.key.id.clone(), entity.clone());
                self.index(&entity);
            }
            RowChange::Delete { .. } => {
                map.remove(id);
            }
        }
        Ok(())
    }

    fn index(&mut self, entity: &Arc<Entity>) {
        match &entity.hot {
            HotFields::Table(table) => {
                if is_found_by_name(table) {
                    self.tables_by_name.insert(
                        (table.namespace_id.clone(), table.name.clone()),
                        entity.key.id.clone(),
                    );
                }
                if !table.parent_table_id.is_empty()
                    && table.state != sys_tables_entry_pb::State::DELETED.value()
                {
                    self.child_tables
                        .entry(table.parent_table_id.clone())
                        .or_default()
                        .insert(entity.key.id.clone());
                }
            }
            HotFields::Tablet(tablet) => {
                for table_id in &tablet.table_ids {
                    self.table_tablets
                        .entry(table_id.clone())
                        .or_default()
                        .insert((tablet.partition_start.clone(), entity.key.id.clone()));
                }
            }
            _ => {}
        }
    }

    fn unindex(&mut self, entity: &Arc<Entity>) {
        match &entity.hot {
            HotFields::Table(table) => {
                let key = (table.namespace_id.clone(), table.name.clone());
                if self.tables_by_name.get(&key) == Some(&entity.key.id) {
                    self.tables_by_name.remove(&key);
                }
                if let Some(children) = self.child_tables.get_mut(&table.parent_table_id) {
                    children.remove(&entity.key.id);
                    if children.is_empty() {
                        self.child_tables.remove(&table.parent_table_id);
                    }
                }
            }
            HotFields::Tablet(tablet) => {
                for table_id in &tablet.table_ids {
                    if let Some(set) = self.table_tablets.get_mut(table_id) {
                        set.remove(&(tablet.partition_start.clone(), entity.key.id.clone()));
                        if set.is_empty() {
                            self.table_tablets.remove(table_id);
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

fn is_found_by_name(table: &TableHot) -> bool {
    use sys_tables_entry_pb::State;
    table.table_type != TableType::PGSQL_TABLE_TYPE.value()
        && table.state != State::DELETING.value()
        && table.state != State::DELETED.value()
        && !table.hidden
}

/// Deleted and replaced tablets no longer serve their partition (`is_deleted`).
pub fn is_inactive_state(state: i32) -> bool {
    state == sys_tablets_entry_pb::State::DELETED.value()
        || state == sys_tablets_entry_pb::State::REPLACED.value()
}

/// An order-independent digest of rows.
pub fn rows_digest<'a>(rows: impl Iterator<Item = (i32, &'a [u8], &'a [u8])>) -> u64 {
    rows.fold(0u64, |sum, (entry_type, id, data)| {
        let mut hasher = DefaultHasher::new();
        (entry_type, id, data).hash(&mut hasher);
        sum.wrapping_add(hasher.finish())
    })
}
