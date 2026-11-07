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
use yb_proto::pb::catalog_entity_info::{sys_namespace_entry_pb, SysTablesEntryPB};
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
