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

//! `ListTables` and `GetTableSchema`, from the catalog snapshot.

use std::collections::BTreeSet;

use master_catalog_entity::Entity;
use master_catalog_state::CatalogSnapshot;
use protobuf::{EnumOrUnknown, MessageField};
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysTablesEntryPB,
};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_ddl::{
    list_tables_response_pb, GetTableSchemaRequestPB, GetTableSchemaResponsePB,
    ListTablesRequestPB, ListTablesResponsePB,
};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, RelationType, SysRowEntryType, TableIdentifierPB,
};

use crate::lookup::{
    check_visible, find_namespace, find_table, is_colocation_parent_table_id, is_hidden,
    is_system_table, is_visible_to_client,
};
use crate::namespaces::identifier;
use crate::{error, TableLifecycle};

/// `kYbSystemDbName`: the YSQL database of YugabyteDB's own system tables.
const YB_SYSTEM_DB_NAME: &str = "system_platform";
/// `kSystemNamespaceId`.
const SYSTEM_NAMESPACE_ID: &[u8] = b"00000000000000000000000000000001";

fn parse_table(entity: &Entity) -> Result<SysTablesEntryPB, MasterErrorPB> {
    entity
        .parse::<SysTablesEntryPB>()
        .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))
}

/// `TableInfo::IsUserCreated`: a YCQL or YSQL table a user made, not a system or colocation
/// parent table. The YSQL sequences table counts as a system table but is not served yet (W9).
fn is_user_created(snapshot: &CatalogSnapshot, entity: &Entity, pb: &SysTablesEntryPB) -> bool {
    let table_type = pb.table_type();
    if table_type != TableType::PGSQL_TABLE_TYPE && table_type != TableType::YQL_TABLE_TYPE {
        return false;
    }
    !is_system_table(snapshot, entity)
        && pb.namespace_id() != SYSTEM_NAMESPACE_ID
        && !is_colocation_parent_table_id(&entity.key.id)
        && pb.namespace_name() != YB_SYSTEM_DB_NAME.as_bytes()
}

impl TableLifecycle {
    /// `ListTables`, ordered by table id as C++ iterates `tables_`.
    pub fn list_tables(
        &self,
        snapshot: &CatalogSnapshot,
        req: &ListTablesRequestPB,
        resp: &mut ListTablesResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let filter_namespace = if req.namespace.is_some() {
            let namespace = find_namespace(snapshot, &req.namespace)?;
            if namespace.namespace().map(|n| n.state)
                != Some(sys_namespace_entry_pb::State::RUNNING as i32)
            {
                return Ok(());
            }
            Some(namespace)
        } else {
            None
        };
        let enabled: BTreeSet<i32> = if req.relation_type_filter.is_empty() {
            [
                RelationType::SYSTEM_TABLE_RELATION,
                RelationType::USER_TABLE_RELATION,
                RelationType::INDEX_TABLE_RELATION,
                RelationType::MATVIEW_TABLE_RELATION,
                RelationType::COLOCATED_PARENT_TABLE_RELATION,
            ]
            .iter()
            .filter(|t| {
                !(req.exclude_system_tables() && **t == RelationType::SYSTEM_TABLE_RELATION)
            })
            .map(|t| *t as i32)
            .collect()
        } else {
            req.relation_type_filter.iter().map(|t| t.value()).collect()
        };
        let mut tables: Vec<_> = snapshot.iter(SysRowEntryType::TABLE).collect();
        tables.sort_by(|a, b| a.key.id.cmp(&b.key.id));
        for entity in tables {
            let Some(hot) = entity.table() else { continue };
            if !req.include_not_running() && !is_visible_to_client(entity) {
                continue;
            }
            if hot.namespace_id.is_empty()
                || filter_namespace
                    .as_ref()
                    .is_some_and(|ns| *ns.key.id != *hot.namespace_id)
            {
                continue;
            }
            if req.has_name_filter() && !hot.name.contains(req.name_filter()) {
                continue;
            }
            let Ok(pb) = parse_table(entity) else {
                continue;
            };
            let user = is_user_created(snapshot, entity, &pb);
            let relation = if user && !pb.indexed_table_id().is_empty() {
                RelationType::INDEX_TABLE_RELATION
            } else if pb.is_matview() {
                RelationType::MATVIEW_TABLE_RELATION
            } else if user {
                RelationType::USER_TABLE_RELATION
            } else if is_colocation_parent_table_id(&entity.key.id)
                && enabled.contains(&(RelationType::SYSTEM_TABLE_RELATION as i32))
            {
                RelationType::COLOCATED_PARENT_TABLE_RELATION
            } else {
                RelationType::SYSTEM_TABLE_RELATION
            };
            if !enabled.contains(&(relation as i32)) {
                continue;
            }
            let namespace = match &filter_namespace {
                Some(ns) => ns.clone(),
                None => {
                    let Some(ns) = snapshot.get(SysRowEntryType::NAMESPACE, &hot.namespace_id)
                    else {
                        tracing::warn!(
                            "ListTables: namespace {} of table {} not found",
                            String::from_utf8_lossy(&hot.namespace_id),
                            String::from_utf8_lossy(&entity.key.id)
                        );
                        continue;
                    };
                    if ns.namespace().map(|n| n.state)
                        != Some(sys_namespace_entry_pb::State::RUNNING as i32)
                    {
                        continue;
                    }
                    ns.clone()
                }
            };
            let ns = namespace.namespace().expect("namespace row");
            let mut info = list_tables_response_pb::TableInfo::new();
            info.set_id(entity.key.id.to_vec());
            info.set_name(hot.name.clone());
            info.table_type = pb.table_type;
            info.relation_type = Some(EnumOrUnknown::new(relation));
            info.namespace = Some(identifier(&namespace.key.id, &ns.name, ns.database_type)).into();
            if relation == RelationType::INDEX_TABLE_RELATION {
                info.set_indexed_table_id(
                    String::from_utf8_lossy(pb.indexed_table_id()).into_owned(),
                );
            }
            info.state = pb.state;
            if pb.colocated() {
                let colocated = info.colocated_info.mut_or_insert_default();
                colocated.set_colocated(true);
                if !is_colocation_parent_table_id(&entity.key.id) && pb.has_parent_table_id() {
                    colocated.set_parent_table_id(pb.parent_table_id().to_vec());
                }
            }
            info.set_hidden(is_hidden(entity));
            resp.tables.push(info);
        }
        Ok(())
    }

    /// `GetTableSchema`. YCQL tables get the latest schema, also while an alter is in flight;
    /// other tables the fully applied one.
    pub fn get_table_schema(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetTableSchemaRequestPB,
        resp: &mut GetTableSchemaResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let entity = find_table(snapshot, &req.table, !req.check_exists_only())?;
        if req.check_exists_only() {
            return Ok(());
        }
        check_visible(&entity, req.include_hidden())?;
        let pb = parse_table(&entity)?;
        let latest =
            pb.table_type() == TableType::YQL_TABLE_TYPE || !pb.fully_applied_schema.is_some();
        if latest {
            resp.schema = pb.schema.clone();
            resp.set_version(pb.version());
            resp.indexes = pb.indexes.clone();
        } else {
            resp.schema = pb.fully_applied_schema.clone();
            resp.set_version(pb.fully_applied_schema_version());
            resp.indexes = pb.fully_applied_indexes.clone();
        }
        let index_info = if latest || !pb.fully_applied_index_info.is_some() {
            &pb.index_info
        } else {
            &pb.fully_applied_index_info
        };
        if index_info.is_some() {
            resp.index_info = index_info.clone();
        }
        if pb.has_indexed_table_id() {
            resp.set_OBSOLETE_indexed_table_id(pb.indexed_table_id().to_vec());
        }
        // Index backfill is not served yet, so nothing is backfilling.
        resp.set_is_backfilling(false);
        resp.set_is_compatible_with_previous_version(pb.updates_only_index_permissions());
        resp.partition_schema = pb.partition_schema.clone();
        if pb
            .replication_info
            .as_ref()
            .is_some_and(replication_info_set)
        {
            resp.replication_info = pb.replication_info.clone();
        }
        resp.set_create_table_done(
            pb.state() != yb_proto::pb::catalog_entity_info::sys_tables_entry_pb::State::PREPARING,
        );
        resp.table_type = pb.table_type;
        let mut ident = TableIdentifierPB::new();
        ident.set_table_name(String::from_utf8_lossy(pb.name()).into_owned());
        ident.set_table_id(entity.key.id.to_vec());
        ident
            .namespace
            .mut_or_insert_default()
            .set_id(pb.namespace_id().to_vec());
        if pb.has_wal_retention_secs() {
            resp.set_wal_retention_secs(pb.wal_retention_secs());
        }
        resp.ysql_ddl_txn_verifier_state = pb.ysql_ddl_txn_verifier_state.clone();
        let namespace = snapshot
            .get(SysRowEntryType::NAMESPACE, pb.namespace_id())
            .ok_or_else(|| {
                error(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!(
                        "Keyspace identifier not found: {}",
                        String::from_utf8_lossy(pb.namespace_id())
                    ),
                )
            })?;
        ident.namespace.mut_or_insert_default().set_name(
            namespace
                .namespace()
                .map_or(String::new(), |n| n.name.clone()),
        );
        resp.identifier = MessageField::some(ident);
        resp.set_colocated(pb.colocated());
        // A colocated table's tablegroup, from its parent's id (`GetTablegroupIdFromParentTableId`).
        if pb.colocated()
            && !is_colocation_parent_table_id(&entity.key.id)
            && crate::ysql::is_tablegroup_parent_table_id(pb.parent_table_id())
        {
            resp.set_tablegroup_id(pb.parent_table_id()[..32].to_vec());
        }
        if !pb.pg_table_id().is_empty() {
            resp.set_pg_table_id(pb.pg_table_id().to_vec());
        }
        Ok(())
    }
}

/// `IsReplicationInfoSet`: any placement, read replica or affinitized leader is given.
fn replication_info_set(info: &yb_proto::pb::common_net::ReplicationInfoPB) -> bool {
    let live = &info.live_replicas;
    live.as_ref().is_some_and(|l| {
        l.num_replicas() > 0 || !l.placement_blocks.is_empty() || !l.placement_uuid().is_empty()
    }) || !info.read_replicas.is_empty()
        || !info.affinitized_leaders.is_empty()
        || !info.multi_affinitized_leaders.is_empty()
}

/// `TableInfo::ShouldLookupPgSchemaName` and `GetPgTableAllOids`: the database and relation oids
/// of a YSQL relation outside the PG catalog, the sequences database and colocation parents.
fn pg_schema_lookup(table_id: &[u8], pb: &SysTablesEntryPB) -> Option<(u32, u32)> {
    if pb.table_type() != TableType::PGSQL_TABLE_TYPE
        || crate::lookup::is_colocation_parent_table_id(table_id)
    {
        return None;
    }
    let database = crate::ysql::pg_database_oid(pb.namespace_id())?;
    if database == 65535 {
        return None;
    }
    let relation = if pb.pg_table_id().is_empty() {
        crate::ysql::pg_table_oid(table_id)?
    } else {
        crate::ysql::pg_table_oid(pb.pg_table_id())?
    };
    // Below kPgFirstNormalObjectId: a PG catalog table, which lives in the sys catalog tablet.
    (relation >= 16384).then_some((database, relation))
}

impl TableLifecycle {
    /// The schema names of `tables` (id and row), empty when PG does not have them.
    async fn pg_schema_names(&self, tables: Vec<(u32, u32)>) -> Vec<String> {
        use protobuf::Message;
        use yb_proto::pb::master_rust_peer::{
            get_pg_schema_names_request_pb, GetPgSchemaNamesRequestPB, GetPgSchemaNamesResponsePB,
        };
        let mut req = GetPgSchemaNamesRequestPB::new();
        for (database, relation) in &tables {
            let mut table = get_pg_schema_names_request_pb::Table::new();
            table.set_database_oid(*database);
            table.set_pg_table_oid(*relation);
            req.tables.push(table);
        }
        let Ok(bytes) = req.write_to_bytes() else {
            return Vec::new();
        };
        let host = self.host.clone();
        let response = tokio::task::spawn_blocking(move || host.call("GetPgSchemaNames", &bytes))
            .await
            .ok()
            .and_then(|r| r.ok())
            .and_then(|r| GetPgSchemaNamesResponsePB::parse_from_bytes(&r).ok());
        match response {
            Some(resp) if resp.names.len() == tables.len() => resp.names,
            _ => {
                tracing::warn!(
                    "Unable to read the schema names of {} YSQL tables",
                    tables.len()
                );
                Vec::new()
            }
        }
    }

    /// `ListTables`' `pgschema_name` of its YSQL tables.
    pub async fn fill_list_tables_schema_names(
        &self,
        snapshot: &CatalogSnapshot,
        resp: &mut ListTablesResponsePB,
    ) {
        let mut lookups = Vec::new();
        for (index, table) in resp.tables.iter().enumerate() {
            let Some(pb) = snapshot
                .get(SysRowEntryType::TABLE, table.id())
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
            else {
                continue;
            };
            if let Some(oids) = pg_schema_lookup(table.id(), &pb) {
                lookups.push((index, oids));
            }
        }
        if lookups.is_empty() {
            return;
        }
        let names = self
            .pg_schema_names(lookups.iter().map(|(_, oids)| *oids).collect())
            .await;
        for ((index, _), name) in lookups.into_iter().zip(names) {
            if !name.is_empty() {
                resp.tables[index].set_pgschema_name(name);
            }
        }
    }

    /// `GetTableSchema`'s `deprecated_pgschema_name` of a YSQL table.
    pub async fn fill_table_schema_name(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetTableSchemaRequestPB,
        resp: &mut GetTableSchemaResponsePB,
    ) {
        if req.check_exists_only() || resp.schema.is_none() {
            return;
        }
        let Ok(entity) = find_table(snapshot, &req.table, true) else {
            return;
        };
        let Ok(pb) = parse_table(&entity) else {
            return;
        };
        let Some(oids) = pg_schema_lookup(&entity.key.id, &pb) else {
            return;
        };
        if let Some(name) = self.pg_schema_names(vec![oids]).await.pop() {
            if !name.is_empty() {
                resp.schema
                    .mut_or_insert_default()
                    .set_DEPRECATED_pgschema_name(name);
            }
        }
    }
}

impl TableLifecycle {
    /// `GetTableDiskSize`: the leaders' WAL and SST sizes from the tservers' metrics heartbeats; a
    /// tablet without a leader counts as missing. A colocated child table has no size of its own.
    pub fn get_table_disk_size(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_ddl::GetTableDiskSizeRequestPB,
        resp: &mut yb_proto::pb::master_ddl::GetTableDiskSizeResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let table_id = req.table.table_id();
        let Some(entity) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Err(error(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                format!(
                    "Table with id {} does not exist",
                    String::from_utf8_lossy(table_id)
                ),
            ));
        };
        let pb = parse_table(entity)?;
        let mut size = 0i64;
        let mut missing = 0i32;
        if !pb.colocated() || crate::lookup::is_colocation_parent_table_id(table_id) {
            for tablet in snapshot.active_tablets_of(table_id) {
                let leader = self
                    .ts_manager
                    .locations
                    .get(&tablet.key.id)
                    .and_then(|r| r.leader().map(|l| l.drive.clone()));
                match leader {
                    Some(drive) => {
                        size += drive.wal_files_size as i64 + drive.sst_files_size as i64;
                    }
                    None => missing += 1,
                }
            }
        }
        resp.set_size(size);
        resp.set_num_missing_tablets(missing);
        Ok(())
    }

    /// The `GetTableSchema` of each of `table_ids`, with schema names, for the tablegroup and
    /// colocated schema RPCs; the first failure is `OBJECT_NOT_FOUND`.
    async fn table_schemas(
        &self,
        snapshot: &CatalogSnapshot,
        table_ids: Vec<Vec<u8>>,
    ) -> Result<Vec<GetTableSchemaResponsePB>, MasterErrorPB> {
        let mut schemas = Vec::new();
        for table_id in table_ids {
            let mut req = GetTableSchemaRequestPB::new();
            req.table.mut_or_insert_default().set_table_id(table_id);
            let mut resp = GetTableSchemaResponsePB::new();
            if let Err(mut e) = self.get_table_schema(snapshot, &req, &mut resp) {
                e.set_code(Code::OBJECT_NOT_FOUND);
                return Err(e);
            }
            self.fill_table_schema_name(snapshot, &req, &mut resp).await;
            schemas.push(resp);
        }
        Ok(schemas)
    }

    /// `GetTablegroupSchema`: the schemas of the tables in the tablegroup.
    pub async fn get_tablegroup_schema(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_ddl::GetTablegroupSchemaRequestPB,
        resp: &mut yb_proto::pb::master_ddl::GetTablegroupSchemaResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.tablegroup.id();
        if id.is_empty() {
            return Err(error(
                Code::INVALID_SCHEMA,
                StatusCode::InvalidArgument,
                "Invalid GetTablegroupSchema request (missing ID)",
            ));
        }
        let parent = [id, b".tablegroup.parent.uuid".as_slice()].concat();
        let colocation_parent = [id, b".colocation.parent.uuid".as_slice()].concat();
        let parent = [parent, colocation_parent]
            .into_iter()
            .find(|p| snapshot.get(SysRowEntryType::TABLE, p).is_some())
            .ok_or_else(|| {
                error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::NotFound,
                    format!(
                        "Tablegroup with ID {} not found",
                        String::from_utf8_lossy(id)
                    ),
                )
            })?;
        let children: Vec<Vec<u8>> = snapshot
            .children_of(&parent)
            .into_iter()
            .filter(|t| {
                t.table().is_some_and(|h| {
                    h.state != sys_tables_entry_pb::State::DELETED as i32
                        && h.state != sys_tables_entry_pb::State::DELETING as i32
                })
            })
            .map(|t| t.key.id.to_vec())
            .collect();
        resp.get_table_schema_response_pbs = self.table_schemas(snapshot, children).await?;
        Ok(())
    }

    /// `GetColocatedTabletSchema`: the schemas of the colocated tables of a database colocated
    /// "via database", given its parent table.
    pub async fn get_colocated_tablet_schema(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_ddl::GetColocatedTabletSchemaRequestPB,
        resp: &mut yb_proto::pb::master_ddl::GetColocatedTabletSchemaResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let parent = find_table(snapshot, &req.parent_colocated_table, false)?;
        check_visible(&parent, false)?;
        if !parent.key.id.ends_with(b".colocated.parent.uuid") {
            return Err(error(
                Code::INVALID_TABLE_TYPE,
                StatusCode::InvalidArgument,
                "Table provided is not a parent colocated table",
            ));
        }
        let namespace_id = parse_table(&parent)?.namespace_id().to_vec();
        let mut children: Vec<Vec<u8>> = snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| {
                t.parse::<SysTablesEntryPB>().is_ok_and(|pb| {
                    pb.namespace_id() == namespace_id.as_slice()
                        && pb.colocated()
                        && pb.table_type() == TableType::PGSQL_TABLE_TYPE
                        && crate::ysql::pg_table_oid(&t.key.id).is_some_and(|o| o >= 16384)
                        && !matches!(
                            pb.state(),
                            sys_tables_entry_pb::State::DELETED
                                | sys_tables_entry_pb::State::DELETING
                        )
                })
            })
            .map(|t| t.key.id.to_vec())
            .collect();
        children.sort();
        resp.get_table_schema_response_pbs = self.table_schemas(snapshot, children).await?;
        Ok(())
    }

    /// `GetIndexBackfillProgress`, in request order: from the indexed table's backfill job while
    /// it runs, else from the index's `IndexInfoPB`; `u64::MAX` and -1 for an unknown index.
    pub fn get_index_backfill_progress(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_client::GetIndexBackfillProgressRequestPB,
        resp: &mut yb_proto::pb::master_client::GetIndexBackfillProgressResponsePB,
    ) {
        for index_id in &req.index_ids {
            let indexed = snapshot
                .get(SysRowEntryType::TABLE, index_id.as_bytes())
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                .map(|pb| {
                    if pb.index_info.is_some() {
                        pb.index_info.indexed_table_id().to_vec()
                    } else {
                        pb.indexed_table_id().to_vec()
                    }
                })
                .and_then(|id| snapshot.get(SysRowEntryType::TABLE, &id))
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok());
            let Some(indexed) = indexed else {
                resp.num_rows_read_from_table_for_backfill.push(u64::MAX);
                resp.num_rows_backfilled_in_index.push(-1.0);
                continue;
            };
            if let Some(job) = indexed
                .backfill_jobs
                .first()
                .filter(|j| j.backfill_state.contains_key(index_id))
            {
                resp.num_rows_read_from_table_for_backfill
                    .push(job.num_rows_read_from_table_for_backfill());
                resp.num_rows_backfilled_in_index.push(
                    job.num_rows_backfilled_in_index
                        .get(index_id)
                        .copied()
                        .unwrap_or(0.0),
                );
                continue;
            }
            if let Some(info) = indexed
                .indexes
                .iter()
                .find(|i| i.table_id() == index_id.as_bytes())
            {
                resp.num_rows_read_from_table_for_backfill
                    .push(info.num_rows_read_from_table_for_backfill());
                resp.num_rows_backfilled_in_index
                    .push(info.num_rows_backfilled_in_index());
            }
        }
    }
}

impl TableLifecycle {
    /// `IsObjectPartOfXRepl`: the table is replicated by xCluster or in a CDCSDK stream. PG asks
    /// before a DDL that rewrites a table.
    pub fn is_object_part_of_xrepl(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_replication::IsObjectPartOfXReplRequestPB,
        resp: &mut yb_proto::pb::master_replication::IsObjectPartOfXReplResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let table_id = req.table_id().as_bytes();
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::NotFound,
                format!(
                    "Table with id {} does not exist",
                    String::from_utf8_lossy(table_id)
                ),
            ));
        };
        let namespace_id = parse_table(table)?.namespace_id().to_vec();
        resp.set_is_object_part_of_xrepl(
            crate::drop::xcluster_replicated(snapshot, table_id)
                || crate::truncate::cdcsdk_streamed(snapshot, &namespace_id, table_id),
        );
        Ok(())
    }
}
