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

//! `CreateTable` and `IsCreateTableDone` for YCQL tables. The table and its tablets are written
//! PREPARING in one mutation; the assignment controller ([`crate::assign`]) takes them on.

use std::collections::BTreeSet;

use master_catalog_state::defaults::TRANSACTION_TABLES_CONFIG_ID;
use master_catalog_state::CatalogSnapshot;
use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use protobuf::{Enum, EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::SysConfigEntryPB;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysTablesEntryPB,
    SysTabletsEntryPB,
};
use yb_proto::pb::common::{ColumnSchemaPB, QLTypePB};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_ddl::{
    CreateTableRequestPB, CreateTableResponsePB, IsCreateTableDoneRequestPB,
    IsCreateTableDoneResponsePB,
};
use yb_proto::pb::master_rust_peer::{PrepareTableSchemaRequestPB, PrepareTableSchemaResponsePB};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::value::PersistentDataType;

use crate::lookup::{check_visible, find_namespace, find_table};
use crate::namespaces::unused_id;
use crate::placement::{
    check_placement, cluster_config, encode, live_tservers, num_replicas, table_replication_info,
};
use crate::{error, TableLifecycle};

/// A request the Rust leader does not serve yet, naming who will.
fn not_yet(what: &str) -> MasterErrorPB {
    error(
        Code::INVALID_REQUEST,
        StatusCode::NotSupported,
        format!("{what} is not served by a Rust master leader yet"),
    )
}

/// The user-defined type ids a column type refers to, as `QLType::GetUserDefinedTypeIds`.
fn udt_ids(ty: &QLTypePB, out: &mut BTreeSet<String>) {
    if let Some(info) = ty.udtype_info.as_ref() {
        if info.has_id() {
            out.insert(info.id().to_string());
        }
    }
    for param in &ty.params {
        udt_ids(param, out);
    }
}

impl TableLifecycle {
    /// `CreateTable` for a YCQL table that is neither transactional nor an index.
    pub async fn create_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateTableRequestPB,
        resp: &mut CreateTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        find_namespace(&snapshot, &req.namespace)?;
        match req.table_type() {
            TableType::YQL_TABLE_TYPE | TableType::TRANSACTION_STATUS_TABLE_TYPE => {}
            TableType::PGSQL_TABLE_TYPE => return Err(not_yet("YSQL CREATE TABLE (W7)")),
            TableType::REDIS_TABLE_TYPE => {
                return Err(error(
                    Code::INVALID_REQUEST,
                    StatusCode::NotSupported,
                    "YEDIS is not supported by the Rust master (D16)",
                ))
            }
            other => return Err(not_yet(&format!("CREATE TABLE of {other:?}"))),
        }
        if req.schema.table_properties.is_transactional() {
            self.ensure_transaction_table(token, deadline).await?;
        }
        if req.index_info.is_some() || req.has_indexed_table_id() {
            return Err(not_yet("A YCQL index (W7)"));
        }

        let config = cluster_config(&snapshot);
        let replication = table_replication_info(req.replication_info.as_ref(), &config);
        let placement = replication.live_replicas.clone().unwrap_or_default();
        let tservers = live_tservers(&self.ts_manager, &config, placement.placement_uuid());
        let prepared = self.prepare_schema(req, tservers.len())?;

        // C++ checks the namespace's type after the schema.
        let namespace = find_namespace(&snapshot, &req.namespace)?;
        if namespace.namespace().map(|n| n.database_type)
            != Some(YQLDatabase::YQL_DATABASE_CQL as i32)
        {
            return Err(error(
                Code::NAMESPACE_NOT_FOUND,
                StatusCode::NotFound,
                "Namespace not found",
            ));
        }
        let mut udts = BTreeSet::new();
        for column in &req.schema.columns {
            udt_ids(&column.type_, &mut udts);
        }
        if udts.iter().any(|id| {
            snapshot
                .get(SysRowEntryType::UDTYPE, id.as_bytes())
                .is_none()
        }) {
            return Err(error(
                Code::INVALID_SCHEMA,
                StatusCode::InvalidArgument,
                "Referenced user-defined type not found",
            ));
        }

        let replication_factor = (self.options.replication_factor)();
        let rf = num_replicas(&placement, replication_factor);
        let num_tablets = prepared.num_tablets();
        // `CanAddPartitionsToTable` counts every live tserver, blacklisted or not.
        let max_tablets =
            (self.options.max_create_tablets_per_ts)() as i64 * self.ts_manager.live().len() as i64;
        if rf > 1 && max_tablets > 0 && num_tablets as i64 > max_tablets {
            return Err(error(
                Code::TOO_MANY_TABLETS,
                StatusCode::InvalidArgument,
                format!(
                    "The requested number of tablets ({num_tablets}) is over the permitted \
                     maximum ({max_tablets})"
                ),
            ));
        }
        if let Some(e) = prepared.partitions_error.clone().into_option() {
            return Err(e);
        }
        check_placement(
            &placement,
            &tservers,
            replication_factor,
            (self.options.check_ts_count_for_create_table)(),
        )?;

        let use_parent_table_id = (self.options.use_parent_table_id_field)();
        let _names = self.names.lock().await;
        let created = self
            .write(token, deadline, |snapshot| {
                let namespace = find_namespace(snapshot, &req.namespace)?;
                let ns = namespace.namespace().expect("namespace row").clone();
                if !req.table_id().is_empty() {
                    let existing =
                        snapshot
                            .get(SysRowEntryType::TABLE, req.table_id())
                            .filter(|t| {
                                t.table().map(|h| h.state)
                                    != Some(sys_tables_entry_pb::State::DELETED as i32)
                            });
                    if let Some(existing) = existing {
                        let e = error(
                            Code::OBJECT_ALREADY_PRESENT,
                            StatusCode::AlreadyPresent,
                            format!(
                                "Object with id {} ('{}.{}') already exists",
                                String::from_utf8_lossy(req.table_id()),
                                ns.name,
                                existing.table().map_or("", |h| h.name.as_str())
                            ),
                        );
                        return Ok((Mutation::new(), Err((existing.key.id.to_vec(), e))));
                    }
                }
                if let Some(existing) = snapshot.table_by_name(&namespace.key.id, req.name()) {
                    let e = error(
                        Code::OBJECT_ALREADY_PRESENT,
                        StatusCode::AlreadyPresent,
                        format!("Object '{}.{}' already exists", ns.name, req.name()),
                    );
                    return Ok((Mutation::new(), Err((existing.key.id.to_vec(), e))));
                }
                use sys_namespace_entry_pb::State as NsState;
                let valid = ns.state == NsState::RUNNING as i32
                    || (ns.state == NsState::PREPARING as i32 && ns.name == "system");
                if !valid {
                    let state = NsState::from_i32(ns.state).unwrap_or(NsState::UNKNOWN);
                    let code = match state {
                        NsState::PREPARING | NsState::DELETING => Code::IN_TRANSITION_CAN_RETRY,
                        _ => Code::INTERNAL_ERROR,
                    };
                    return Err(error(
                        code,
                        StatusCode::TryAgain,
                        format!(
                            "Invalid Namespace State ({state:?}). Cannot create {}.{}",
                            ns.name,
                            req.name()
                        ),
                    ));
                }
                let table_id = if req.table_id().is_empty() {
                    unused_id(snapshot, SysRowEntryType::TABLE)
                } else {
                    req.table_id().to_vec()
                };
                let table = new_table_row(req, &prepared, &namespace.key.id, &ns.name);
                let mut mutation = Mutation::new()
                    .read(&namespace)
                    .read_absent(SysRowEntryType::TABLE, &table_id)
                    .upsert(SysRowEntryType::TABLE, &table_id, encode(&table)?);
                let mut tablet_ids: Vec<Vec<u8>> = Vec::new();
                while tablet_ids.len() < prepared.partitions.len() {
                    let id = unused_id(snapshot, SysRowEntryType::TABLET);
                    if !tablet_ids.contains(&id) {
                        tablet_ids.push(id);
                    }
                }
                tablet_ids.sort();
                for (tablet_id, partition) in tablet_ids.iter().zip(&prepared.partitions) {
                    let mut tablet = SysTabletsEntryPB::new();
                    tablet.set_table_id(table_id.clone());
                    tablet.partition = MessageField::some(partition.clone());
                    tablet.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::PREPARING));
                    if use_parent_table_id {
                        tablet.set_hosted_tables_mapped_by_parent_id(true);
                    } else {
                        tablet.table_ids.push(table_id.clone());
                    }
                    let cstate = tablet.committed_consensus_state.mut_or_insert_default();
                    cstate.set_current_term(0);
                    cstate
                        .config
                        .mut_or_insert_default()
                        .set_committed_op_index(-1);
                    mutation = mutation
                        .read_absent(SysRowEntryType::TABLET, tablet_id)
                        .upsert(SysRowEntryType::TABLET, tablet_id, encode(&tablet)?);
                }
                if req.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE {
                    // `IncrementTransactionTablesVersion`, in the same write: tservers learn of
                    // the new status table from the version in their heartbeat response.
                    mutation = bump_transaction_tables_version(snapshot, mutation)?;
                }
                Ok((mutation, Ok(table_id)))
            })
            .await?;
        match created {
            Ok(id) => {
                resp.set_table_id(id);
                Ok(())
            }
            Err((id, e)) => {
                resp.set_table_id(id);
                Err(e)
            }
        }
    }

    /// `IsCreateTableDone`: done once the table leaves PREPARING.
    pub fn is_create_table_done(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        req: &IsCreateTableDoneRequestPB,
        resp: &mut IsCreateTableDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let table = find_table(snapshot, &req.table, false)?;
        check_visible(&table, false)?;
        let state = table.table().map_or(0, |t| t.state);
        let mut done = state != sys_tables_entry_pb::State::PREPARING as i32;
        // A transactional table also waits for the transaction status table.
        if done
            && table
                .parse::<SysTablesEntryPB>()
                .is_ok_and(|pb| pb.schema.table_properties.is_transactional())
        {
            done = snapshot
                .table_by_name(SYSTEM_NAMESPACE_ID, TRANSACTIONS_TABLE)
                .and_then(|t| t.table())
                .is_some_and(|t| t.state != sys_tables_entry_pb::State::PREPARING as i32);
        }
        resp.set_done(done);
        Ok(())
    }

    /// `CreateGlobalTransactionStatusTableIfNotPresent`: `system.transactions`, which every
    /// transactional table needs.
    async fn ensure_transaction_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        if snapshot
            .table_by_name(SYSTEM_NAMESPACE_ID, TRANSACTIONS_TABLE)
            .is_some()
        {
            return Ok(());
        }
        tracing::info!("Creating transaction status table: {TRANSACTIONS_TABLE}");
        let mut req = CreateTableRequestPB::new();
        req.set_name(TRANSACTIONS_TABLE.into());
        req.namespace
            .mut_or_insert_default()
            .set_name("system".into());
        req.table_type = Some(EnumOrUnknown::new(TableType::TRANSACTION_STATUS_TABLE_TYPE));
        let num_tablets = match (self.options.transaction_table_num_tablets)() {
            n if n > 0 => n,
            // A negative count (the auto-detect default) makes the host pick, as C++ does.
            _ => {
                let config = cluster_config(&snapshot);
                let placement = config
                    .replication_info
                    .live_replicas
                    .placement_uuid()
                    .to_vec();
                live_tservers(&self.ts_manager, &config, &placement).len() as i32
                    * (self.options.transaction_table_num_tablets_per_tserver)()
            }
        };
        let schema = req.schema.mut_or_insert_default();
        schema
            .table_properties
            .mut_or_insert_default()
            .set_num_tablets(num_tablets);
        let mut key = ColumnSchemaPB::new();
        key.set_name("key".into());
        key.type_
            .mut_or_insert_default()
            .set_main(PersistentDataType::BINARY);
        key.set_is_key(true);
        key.set_is_hash_key(true);
        key.set_is_nullable(false);
        schema.columns.push(key);
        let mut resp = CreateTableResponsePB::new();
        match Box::pin(self.create_table(token, deadline, &req, &mut resp)).await {
            Err(e) if e.code() == Code::OBJECT_ALREADY_PRESENT => Ok(()),
            result => result,
        }
    }

    /// The host's schema steps of `CreateTable`.
    fn prepare_schema(
        &self,
        req: &CreateTableRequestPB,
        num_live_tservers: usize,
    ) -> Result<PrepareTableSchemaResponsePB, MasterErrorPB> {
        let mut prepare = PrepareTableSchemaRequestPB::new();
        prepare.request = MessageField::some(req.clone());
        prepare.set_num_live_tservers(num_live_tservers as u64);
        let bytes = encode(&prepare)?;
        let response = self
            .host
            .call("PrepareTableSchema", &bytes)
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        let response = PrepareTableSchemaResponsePB::parse_from_bytes(&response)
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        if let Some(e) = response.error.clone().into_option() {
            return Err(e);
        }
        Ok(response)
    }
}

/// `FillNewTableEntry` for a YCQL table.
fn new_table_row(
    req: &CreateTableRequestPB,
    prepared: &PrepareTableSchemaResponsePB,
    namespace_id: &[u8],
    namespace_name: &str,
) -> SysTablesEntryPB {
    let mut table = SysTablesEntryPB::new();
    table.set_name(req.name().as_bytes().to_vec());
    table.table_type = req.table_type;
    table.set_namespace_id(namespace_id.to_vec());
    table.set_namespace_name(namespace_name.as_bytes().to_vec());
    table.set_version(0);
    table.set_next_column_id(prepared.next_column_id());
    table.mut_no_custom_tablespace();
    table.hosted_stateful_services = req.hosted_stateful_services.clone();
    if req.replication_info.is_some() {
        table.replication_info = req.replication_info.clone();
    }
    table.schema = prepared.schema.clone();
    table.partition_schema = prepared.partition_schema.clone();
    table.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::PREPARING));
    table
}

/// `kSystemNamespaceId`, the `system` keyspace.
pub(crate) const SYSTEM_NAMESPACE_ID: &[u8] = b"00000000000000000000000000000001";
/// `kGlobalTransactionsTableName`.
pub(crate) const TRANSACTIONS_TABLE: &str = "transactions";

/// The transaction tables config with its version + 1.
fn bump_transaction_tables_version(
    snapshot: &CatalogSnapshot,
    mutation: Mutation,
) -> Result<Mutation, MasterErrorPB> {
    let Some(row) = snapshot.get(SysRowEntryType::SYS_CONFIG, TRANSACTION_TABLES_CONFIG_ID) else {
        return Ok(mutation);
    };
    let mut pb: SysConfigEntryPB = row
        .parse()
        .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
    let config = pb.mut_transaction_tables_config();
    config.set_version(config.version() + 1);
    Ok(mutation.read(row).upsert(
        SysRowEntryType::SYS_CONFIG,
        TRANSACTION_TABLES_CONFIG_ID,
        encode(&pb)?,
    ))
}
