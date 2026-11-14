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

//! The DDL log: a `DDL_LOG_ENTRY` row per dropped or altered table (`DdlLogEntry`), keyed by the
//! DocDB encoding of its hybrid time, which the host computes, so the newest sorts first.

use master_catalog_state::{CatalogSnapshot, Mutation};
use protobuf::Message;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, DdlLogEntryPB, SysTablesEntryPB};
use yb_proto::pb::master_admin::{DdlLogRequestPB, DdlLogResponsePB};
use yb_proto::pb::master_rust_peer::{NewDdlLogEntryRequestPB, NewDdlLogEntryResponsePB};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::{error, TableLifecycle};

/// A DDL log row: its id and entry.
pub(crate) type DdlLogRow = (Vec<u8>, DdlLogEntryPB);

/// The row in `mutation`.
pub(crate) fn add(mutation: Mutation, row: &DdlLogRow) -> Result<Mutation, MasterErrorPB> {
    mutation
        .upsert_message(SysRowEntryType::DDL_LOG_ENTRY, &row.0, &row.1)
        .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))
}

impl TableLifecycle {
    /// A `DdlLogEntry` of the table at the current hybrid time.
    pub(crate) fn new_ddl_log_entry(
        &self,
        table_id: &[u8],
        table: &SysTablesEntryPB,
        action: &str,
    ) -> Result<DdlLogRow, MasterErrorPB> {
        let mut req = NewDdlLogEntryRequestPB::new();
        req.set_table_id(table_id.to_vec());
        req.table = Some(table.clone()).into();
        req.set_action(action.to_string());
        let bytes = req.write_to_bytes().map_err(|e| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::InternalError,
                e.to_string(),
            )
        })?;
        let resp = self
            .host
            .call("NewDdlLogEntry", &bytes)
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        let resp = NewDdlLogEntryResponsePB::parse_from_bytes(&resp)
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        Ok((resp.id().to_vec(), resp.entry.unwrap_or_default()))
    }

    /// The DDL log entries a drop writes (`DeleteTableInMemory`): "Drop" for a table and "Drop
    /// index <name>" on it for each index going with it; an index dropped alone gets only the
    /// latter, on its indexed table.
    pub(crate) fn drop_ddl_log_entries(
        &self,
        snapshot: &CatalogSnapshot,
        table_id: &[u8],
        table: &SysTablesEntryPB,
    ) -> Result<Vec<DdlLogRow>, MasterErrorPB> {
        let parse = |entity: &master_catalog_entity::Entity| {
            entity
                .parse::<SysTablesEntryPB>()
                .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))
        };
        let drop_index = |name: &[u8]| format!("Drop index {}", String::from_utf8_lossy(name));
        let indexed_id = crate::ysql::indexed_table_of(table);
        if !indexed_id.is_empty() {
            if let Some(indexed) = snapshot.get(SysRowEntryType::TABLE, &indexed_id) {
                let entry = self.new_ddl_log_entry(
                    &indexed_id,
                    &parse(indexed)?,
                    &drop_index(table.name()),
                )?;
                return Ok(vec![entry]);
            }
        }
        let mut rows = vec![self.new_ddl_log_entry(table_id, table, "Drop")?];
        for index in &table.indexes {
            let Some(index_table) = snapshot.get(SysRowEntryType::TABLE, index.table_id()) else {
                continue;
            };
            let index_pb = parse(index_table)?;
            if matches!(
                index_pb.state(),
                sys_tables_entry_pb::State::DELETING | sys_tables_entry_pb::State::DELETED
            ) {
                continue;
            }
            rows.push(self.new_ddl_log_entry(table_id, table, &drop_index(index_pb.name()))?);
        }
        Ok(rows)
    }

    /// `DdlLog`: the entries, newest first.
    pub fn ddl_log(
        &self,
        snapshot: &CatalogSnapshot,
        _req: &DdlLogRequestPB,
        resp: &mut DdlLogResponsePB,
    ) -> Result<(), MasterErrorPB> {
        // In row id order, as the sys catalog enumerates them.
        let mut rows: Vec<_> = snapshot.iter(SysRowEntryType::DDL_LOG_ENTRY).collect();
        rows.sort_by(|a, b| a.key.id.cmp(&b.key.id));
        for row in rows {
            let entry: DdlLogEntryPB = row
                .parse()
                .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
            resp.entries.push(entry);
        }
        Ok(())
    }
}
