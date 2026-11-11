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

//! What the YCQL virtual tables show (`system.local`, `system_schema.tables` and the rest of
//! `SystemTableDefs`). The host serves their reads and formats the rows; this gathers what each
//! table reads from the catalog and the tserver registry, as the C++ `YQL*VTable` classes read it
//! from `CatalogManager`.

use master_catalog_state::CatalogSnapshot;
use master_table_lifecycle::lookup::{is_visible_to_client, is_ycql_virtual_table};
use protobuf::Enum;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, SysNamespaceEntryPB, SysRoleEntryPB, SysTablesEntryPB, SysUDTypeEntryPB,
};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::consensus_metadata::ConsensusStatePB;
use yb_proto::pb::master_rust_peer::{ycql_virtual_table_input_pb, YcqlVirtualTableInputPB};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::leader::LeaderServices;

/// `kSysCatalogTableId`.
const SYS_CATALOG_TABLE_ID: &[u8] = b"sys.catalog.uuid";
/// `tserver::kPgAdvisoryLocksTableName`, an internal YCQL table drivers cannot parse.
const PG_ADVISORY_LOCKS_TABLE_NAME: &str = "pg_advisory_locks";

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

impl LeaderServices {
    /// The input of the virtual table whose tablet is `tablet_id`, or `None` when it is not one.
    /// `masters` is the master Raft config, the replicas of every virtual table's tablet.
    pub fn ycql_virtual_table_input(
        &self,
        tablet_id: &[u8],
        masters: Option<&ConsensusStatePB>,
    ) -> Result<Option<YcqlVirtualTableInputPB>, Status> {
        let (_token, snapshot) = self.ready().map_err(|e| crate::status_of(&e))?;
        let Some(tablet) = snapshot.get(SysRowEntryType::TABLET, tablet_id) else {
            return Ok(None);
        };
        let Some(table) = tablet
            .tablet()
            .and_then(|t| snapshot.get(SysRowEntryType::TABLE, &t.table_id))
        else {
            return Ok(None);
        };
        let Some(hot) = table.table().filter(|t| is_ycql_virtual_table(t)) else {
            return Ok(None);
        };
        let mut input = YcqlVirtualTableInputPB::new();
        input.set_namespace_name(namespace_name(&snapshot, &hot.namespace_id)?);
        input.set_table_name(hot.name.clone());
        match (input.namespace_name(), hot.name.as_str()) {
            ("system", "local") | ("system", "peers") => {
                let mut tservers = self.ts_manager.live();
                tservers.sort_by(|a, b| a.uuid.cmp(&b.uuid));
                input.tservers = tservers.iter().map(|d| d.info()).collect();
            }
            ("system_schema", "keyspaces") => {
                let replication_factor = self.replication_factor_of_cluster(&snapshot);
                for namespace in snapshot.iter(SysRowEntryType::NAMESPACE) {
                    let pb: SysNamespaceEntryPB = namespace.parse().map_err(corrupt)?;
                    if pb.state() != sys_namespace_entry_pb::State::RUNNING
                        || pb.database_type() != YQLDatabase::YQL_DATABASE_CQL
                    {
                        continue;
                    }
                    let mut keyspace = ycql_virtual_table_input_pb::Keyspace::new();
                    keyspace.set_name(String::from_utf8_lossy(pb.name()).into_owned());
                    keyspace.set_replication_factor(replication_factor);
                    input.keyspaces.push(keyspace);
                }
            }
            ("system_schema", "tables")
            | ("system_schema", "columns")
            | ("system_schema", "indexes") => {
                input.tables = self.ycql_tables(&snapshot, None)?;
            }
            ("system", "partitions") | ("system", "size_estimates") => {
                input.tables = self.ycql_tables(&snapshot, Some(masters))?;
            }
            ("system_schema", "types") => {
                for entity in snapshot.iter(SysRowEntryType::UDTYPE) {
                    let pb: SysUDTypeEntryPB = entity.parse().map_err(corrupt)?;
                    let mut ty = ycql_virtual_table_input_pb::Type::new();
                    ty.set_namespace_name(namespace_name(&snapshot, pb.namespace_id().as_bytes())?);
                    ty.type_ = Some(pb).into();
                    input.types.push(ty);
                }
            }
            ("system_auth", _) => {
                for entity in snapshot.iter(SysRowEntryType::ROLE) {
                    input
                        .roles
                        .push(entity.parse::<SysRoleEntryPB>().map_err(corrupt)?);
                }
            }
            _ => {}
        }
        Ok(Some(input))
    }

    /// `GetReplicationFactor`: the live placement's replica count, else `replication_factor`.
    fn replication_factor_of_cluster(&self, snapshot: &CatalogSnapshot) -> u32 {
        let configured = Self::cluster_config(snapshot)
            .ok()
            .map_or(0, |c| c.replication_info.live_replicas.num_replicas());
        if configured > 0 {
            configured as u32
        } else {
            (self.replication_factor)().max(0) as u32
        }
    }

    /// The YCQL tables and indexes visible to clients (`IsYcqlTable`), with their tablets'
    /// locations when `locations` is set; a tablet without locations (not running, or gone) is
    /// left out, as the C++ tables skip it.
    fn ycql_tables(
        &self,
        snapshot: &CatalogSnapshot,
        locations: Option<Option<&ConsensusStatePB>>,
    ) -> Result<Vec<ycql_virtual_table_input_pb::Table>, Status> {
        let mut tables = Vec::new();
        for entity in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = entity.table() else { continue };
            if hot.table_type != TableType::YQL_TABLE_TYPE.value()
                || &*entity.key.id == SYS_CATALOG_TABLE_ID
                || hot.name == PG_ADVISORY_LOCKS_TABLE_NAME
                || !is_visible_to_client(entity)
            {
                continue;
            }
            let pb: SysTablesEntryPB = entity.parse().map_err(corrupt)?;
            let mut table = ycql_virtual_table_input_pb::Table::new();
            table.set_id(entity.key.id.to_vec());
            // `namespace_name` is unset on tables older than 2.3.
            table.set_namespace_name(if pb.namespace_name().is_empty() {
                namespace_name(snapshot, &hot.namespace_id)?
            } else {
                String::from_utf8_lossy(pb.namespace_name()).into_owned()
            });
            if let Some(masters) = locations {
                for tablet in snapshot.active_tablets_of(&entity.key.id) {
                    if let Ok(location) =
                        self.tablet_locations(snapshot, masters, tablet, false, false)
                    {
                        table.tablets.push(location);
                    }
                }
            }
            table.table = Some(pb).into();
            tables.push(table);
        }
        Ok(tables)
    }
}

fn namespace_name(snapshot: &CatalogSnapshot, namespace_id: &[u8]) -> Result<String, Status> {
    let namespace = snapshot
        .get(SysRowEntryType::NAMESPACE, namespace_id)
        .ok_or_else(|| {
            Status::new(
                StatusCode::NotFound,
                format!(
                    "Keyspace identifier not found: {}",
                    String::from_utf8_lossy(namespace_id)
                ),
            )
        })?;
    let pb: SysNamespaceEntryPB = namespace.parse().map_err(corrupt)?;
    Ok(String::from_utf8_lossy(pb.name()).into_owned())
}
