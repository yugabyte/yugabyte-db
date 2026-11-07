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

//! `MasterSnapshotCoordinator::GetSnapshotSchedules`: the snapshot schedules that cover a
//! namespace or table, from the `SNAPSHOT_SCHEDULE` rows.
//!
//! C++ asks `CollectEntriesForSnapshot` for each schedule's entries, which needs a leader
//! `CatalogManager`; the host's is never one, so the entries a schedule's filter selects are
//! derived here from the snapshot, as `CollectTables` selects them: a namespace-only identifier
//! covers the namespace and every non-system table in it; a table identifier covers the table, its
//! indexes and its namespace.

use std::collections::HashSet;

use master_catalog_state::CatalogSnapshot;
use yb_proto::pb::catalog_entity_info::SysTablesEntryPB;
use yb_proto::pb::master_backup::{snapshot_schedule_filter_pb, SnapshotScheduleOptionsPB};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::lookup::{find_namespace, find_table, is_system_table};

/// The ids (the 16 bytes of the UUID) of the live schedules whose entries include the `entry_type` row
/// `id`. A schedule whose filter names an object that no longer exists covers nothing through it.
pub fn covering_schedules(
    snapshot: &CatalogSnapshot,
    entry_type: SysRowEntryType,
    id: &[u8],
) -> Vec<Vec<u8>> {
    let mut result = Vec::new();
    for schedule in snapshot.iter(SysRowEntryType::SNAPSHOT_SCHEDULE) {
        let Ok(options) = schedule.parse::<SnapshotScheduleOptionsPB>() else {
            continue;
        };
        if options.delete_time() != 0 {
            continue;
        }
        let Some(snapshot_schedule_filter_pb::Filter::Tables(tables)) =
            options.filter.as_ref().and_then(|f| f.filter.clone())
        else {
            continue;
        };
        let (namespaces, table_ids) = collect(snapshot, &tables.tables);
        let covered = match entry_type {
            SysRowEntryType::NAMESPACE => namespaces.contains(id),
            SysRowEntryType::TABLE => table_ids.contains(id),
            _ => false,
        };
        if covered {
            result.push(schedule.key.id.to_vec());
        }
    }
    result
}

/// The namespace and table ids a filter selects.
fn collect(
    snapshot: &CatalogSnapshot,
    identifiers: &[yb_proto::pb::master_types::TableIdentifierPB],
) -> (HashSet<Vec<u8>>, HashSet<Vec<u8>>) {
    let mut namespaces = HashSet::new();
    let mut tables = HashSet::new();
    for ident in identifiers {
        if ident.table_name().is_empty() && ident.table_id().is_empty() && ident.namespace.is_some()
        {
            let Ok(namespace) = find_namespace(snapshot, &ident.namespace) else {
                continue;
            };
            for table in snapshot.iter(SysRowEntryType::TABLE) {
                if table
                    .table()
                    .is_some_and(|t| t.namespace_id == *namespace.key.id)
                    && !is_system_table(snapshot, table)
                {
                    tables.insert(table.key.id.to_vec());
                }
            }
            namespaces.insert(namespace.key.id.to_vec());
            continue;
        }
        let Ok(table) = find_table(snapshot, ident, false) else {
            continue;
        };
        if let Some(hot) = table.table() {
            namespaces.insert(hot.namespace_id.to_vec());
        }
        if let Ok(pb) = table.parse::<SysTablesEntryPB>() {
            tables.extend(pb.indexes.iter().map(|i| i.table_id().to_vec()));
        }
        tables.insert(table.key.id.to_vec());
    }
    (namespaces, tables)
}

/// `AsString` of schedule ids, as C++ errors print them: `[<uuid>, ...]`.
pub fn schedule_list(ids: &[Vec<u8>]) -> String {
    let ids: Vec<String> = ids.iter().map(|id| uuid_string(id)).collect();
    format!("[{}]", ids.join(", "))
}

/// `SnapshotScheduleId::ToString`: the 16 id bytes as a dashed UUID.
fn uuid_string(bytes: &[u8]) -> String {
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    if hex.len() != 32 {
        return hex;
    }
    format!(
        "{}-{}-{}-{}-{}",
        &hex[0..8],
        &hex[8..12],
        &hex[12..16],
        &hex[16..20],
        &hex[20..32]
    )
}
