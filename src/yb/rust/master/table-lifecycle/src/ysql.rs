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

//! YSQL tables (W9): `CreateTable` and `DeleteTable` of a PGSQL table, and the DDL transaction
//! verification that decides, once the DDL's transaction ends, whether its changes stay
//! (`ReportYsqlDdlTxnStatus`, `IsYsqlDdlVerificationDone`).
//!
//! A table a DDL transaction creates or drops carries the transaction and a
//! `ysql_ddl_txn_verifier_state` until the transaction is known to have committed or aborted. On
//! commit a create stays and a drop goes through; on abort a create is dropped and a drop is
//! undone. Schema changes (ALTER) are not rolled back yet.

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use protobuf::{Enum, EnumOrUnknown};
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::YsqlDdlTxnVerifierStatePB;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysNamespaceEntryPB, SysTablesEntryPB,
};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_ddl::{
    CreateTableRequestPB, CreateTableResponsePB, DeleteTableRequestPB, DeleteTableResponsePB,
    IsYsqlDdlVerificationDoneRequestPB, IsYsqlDdlVerificationDoneResponsePB,
    ReportYsqlDdlTxnStatusRequestPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::create::add_new_tablets;
use crate::lookup::{find_namespace, find_table};
use crate::placement::{check_placement, cluster_config, live_tservers, num_replicas};
use crate::time::local_time_string;
use crate::{error, TableLifecycle};

use sys_tables_entry_pb::State as TableState;

fn not_yet(what: &str) -> MasterErrorPB {
    error(
        Code::INVALID_REQUEST,
        StatusCode::NotSupported,
        format!("{what} is not served by a Rust master leader yet"),
    )
}

fn corrupt(e: impl std::fmt::Display) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
}

/// The transaction id a table's DDL state belongs to, if any.
fn ddl_txn_id(pb: &SysTablesEntryPB) -> Option<&[u8]> {
    if pb.ysql_ddl_txn_verifier_state.is_empty() {
        return None;
    }
    pb.transaction
        .as_ref()
        .map(|t| t.transaction_id())
        .filter(|id| !id.is_empty())
}

impl TableLifecycle {
    /// `CreateTable` of a YSQL table that is not colocated, not an index, not a PG catalog table,
    /// and in the default tablespace.
    pub(crate) async fn create_ysql_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateTableRequestPB,
        resp: &mut CreateTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if req.is_pg_catalog_table() {
            return Err(not_yet("Creating a PG catalog table (initdb)"));
        }
        if req.index_info.is_some() || req.has_indexed_table_id() {
            return Err(not_yet("A YSQL index"));
        }
        if req.has_tablegroup_id() {
            return Err(not_yet("A YSQL table in a tablegroup"));
        }
        if !req.tablespace_id().is_empty() {
            return Err(not_yet("A YSQL table in a tablespace"));
        }
        if crate::placement::replication_info_set(&req.replication_info) {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                "Cannot set placement policy for YSQL tables use Tablespaces instead",
            ));
        }
        if req.table_id().is_empty() {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                "A YSQL table needs a table id",
            ));
        }
        if req.schema.table_properties.is_transactional() {
            self.ensure_transaction_table(token, deadline).await?;
        }
        let snapshot = self.catalog.snapshot();
        let namespace = find_namespace(&snapshot, &req.namespace)?;
        let ns: SysNamespaceEntryPB = namespace.parse().map_err(corrupt)?;
        if ns.database_type() != YQLDatabase::YQL_DATABASE_PGSQL {
            return Err(error(
                Code::NAMESPACE_NOT_FOUND,
                StatusCode::NotFound,
                "Namespace not found",
            ));
        }
        // `is_colocated_via_database` only opts a table out of its database's colocation.
        let opted_out = req.has_is_colocated_via_database() && !req.is_colocated_via_database();
        if ns.colocated() && !opted_out {
            return Err(not_yet("A colocated YSQL table"));
        }
        let ns_name = String::from_utf8_lossy(ns.name()).into_owned();

        let config = cluster_config(&snapshot);
        let placement = config
            .replication_info
            .live_replicas
            .clone()
            .unwrap_or_default();
        let tservers = live_tservers(&self.ts_manager, &config, placement.placement_uuid());
        let replication_factor = (self.options.replication_factor)();
        let rf = num_replicas(&placement, replication_factor);
        let max_tablets =
            (self.options.max_create_tablets_per_ts)() as i64 * self.ts_manager.live().len() as i64;
        let max_tablets = if rf > 1 { max_tablets.max(0) } else { 0 };
        let prepared = self.prepare_schema_in(
            req,
            tservers.len(),
            max_tablets,
            Some((&namespace.key.id, &ns_name)),
        )?;
        let num_tablets = prepared.num_tablets();
        if max_tablets > 0 && num_tablets as i64 > max_tablets {
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
        let mut table = prepared
            .table
            .clone()
            .into_option()
            .ok_or_else(|| corrupt("The host did not build the table's row"))?;
        // A DDL transaction's table is rolled back unless the transaction commits.
        if req.transaction.is_some() {
            table.transaction = req.transaction.clone();
            if req.ysql_yb_ddl_rollback_enabled() {
                let mut state = YsqlDdlTxnVerifierStatePB::new();
                state.set_contains_create_table_op(true);
                if req.has_sub_transaction_id() {
                    state.set_sub_transaction_id(req.sub_transaction_id());
                }
                table.ysql_ddl_txn_verifier_state.push(state);
            }
        }

        let use_parent_table_id = (self.options.use_parent_table_id_field)();
        let table_id = req.table_id().to_vec();
        let created = self
            .write(token, deadline, |snapshot| {
                let namespace = find_namespace(snapshot, &req.namespace)?;
                let ns = namespace.namespace().expect("namespace row").clone();
                // By id: YSQL names are unique in pg_class, not here.
                if let Some(existing) = snapshot
                    .get(SysRowEntryType::TABLE, &table_id)
                    .filter(|t| t.table().map(|h| h.state) != Some(TableState::DELETED as i32))
                {
                    let e = error(
                        Code::OBJECT_ALREADY_PRESENT,
                        StatusCode::AlreadyPresent,
                        format!(
                            "Object with id {} ('{}.{}') already exists",
                            String::from_utf8_lossy(&table_id),
                            ns.name,
                            existing.table().map_or("", |h| h.name.as_str())
                        ),
                    );
                    return Ok((Mutation::new(), Err(e)));
                }
                use sys_namespace_entry_pb::State as NsState;
                if ns.state != NsState::RUNNING as i32 {
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
                let mut mutation = Mutation::new().read(&namespace);
                // A dropped table's DELETED row with this id is replaced.
                mutation = match snapshot.get(SysRowEntryType::TABLE, &table_id) {
                    Some(old) => mutation.read(old),
                    None => mutation.read_absent(SysRowEntryType::TABLE, &table_id),
                };
                mutation = mutation
                    .upsert_message(SysRowEntryType::TABLE, &table_id, &table)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                mutation = add_new_tablets(
                    snapshot,
                    mutation,
                    &table_id,
                    &prepared.partitions,
                    use_parent_table_id,
                )?;
                Ok((mutation, Ok(())))
            })
            .await?;
        resp.set_table_id(table_id);
        created
    }

    /// `DeleteTable` of a YSQL table. In a DDL transaction the drop waits for the commit: the
    /// table is only marked (`contains_drop_table_op`), since a table may be dropped and created
    /// again within one statement.
    pub(crate) async fn delete_ysql_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteTableRequestPB,
        resp: &mut DeleteTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if req.is_index_table() {
            return Err(not_yet("DROP INDEX of a YSQL index"));
        }
        let snapshot = self.catalog.snapshot();
        let table = find_table(&snapshot, &req.table, false)?;
        resp.set_table_id(table.key.id.to_vec());
        if !(req.ysql_yb_ddl_rollback_enabled() && req.transaction.is_some()) {
            return self.mark_deleting(token, deadline, &table.key.id).await;
        }
        let txn_id = req.transaction.transaction_id().to_vec();
        let table_id = table.key.id.to_vec();
        self.write(token, deadline, |snapshot| {
            let table = snapshot
                .get(SysRowEntryType::TABLE, &table_id)
                .ok_or_else(|| {
                    error(
                        Code::OBJECT_NOT_FOUND,
                        StatusCode::NotFound,
                        "The object does not exist",
                    )
                })?;
            let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            if let Some(other) = ddl_txn_id(&pb).filter(|id| *id != txn_id) {
                return Err(error(
                    Code::TABLE_SCHEMA_CHANGE_IN_PROGRESS,
                    StatusCode::TryAgain,
                    format!(
                        "DDL transaction {} cannot continue as table {} is undergoing DDL \
                         transaction verification for {}",
                        hex(&txn_id),
                        String::from_utf8_lossy(pb.name()),
                        hex(other)
                    ),
                ));
            }
            let sub = req.sub_transaction_id();
            let needs_state = pb.ysql_ddl_txn_verifier_state.is_empty()
                || (req.has_sub_transaction_id()
                    && pb
                        .ysql_ddl_txn_verifier_state
                        .last()
                        .is_some_and(|s| s.sub_transaction_id() != sub));
            if needs_state {
                pb.transaction = req.transaction.clone();
                pb.ysql_ddl_txn_verifier_state
                    .push(YsqlDdlTxnVerifierStatePB::new());
            }
            let last = pb
                .ysql_ddl_txn_verifier_state
                .last_mut()
                .expect("a verifier state");
            last.set_contains_drop_table_op(true);
            if req.has_sub_transaction_id() {
                last.set_sub_transaction_id(sub);
            }
            let mutation = Mutation::new()
                .read(table)
                .upsert_message(SysRowEntryType::TABLE, &table_id, &pb)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            Ok((mutation, ()))
        })
        .await
    }

    /// Marks a YSQL table DELETING (`DeleteTableInternal`); the table deleter does the rest.
    async fn mark_deleting(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        table_id: &[u8],
    ) -> Result<(), MasterErrorPB> {
        self.write(token, deadline, |snapshot| {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                return Ok((Mutation::new(), ()));
            };
            let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            if pb.state() == TableState::DELETING || pb.state() == TableState::DELETED {
                return Ok((Mutation::new(), ()));
            }
            pb.state = Some(EnumOrUnknown::new(TableState::DELETING));
            pb.set_state_msg(format!("Started deleting at {}", local_time_string()).into_bytes());
            pb.ysql_ddl_txn_verifier_state.clear();
            pb.transaction = Default::default();
            let mutation = Mutation::new()
                .read(table)
                .upsert_message(SysRowEntryType::TABLE, table_id, &pb)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            Ok((mutation, ()))
        })
        .await
    }

    /// `ReportYsqlDdlTxnStatus`: the DDL transaction ended; each table it changed keeps or undoes
    /// the change (`YsqlDdlTxnCompleteCallbackInternal`).
    pub async fn report_ysql_ddl_txn_status(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &ReportYsqlDdlTxnStatusRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let txn_id = req.transaction_id().to_vec();
        if txn_id.is_empty() {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                "Received ReportYsqlDdlTxnStatus request without transaction id",
            ));
        }
        let committed = req.is_committed();
        let tables: Vec<Vec<u8>> = tables_of_txn(&self.catalog.snapshot(), &txn_id);
        for table_id in tables {
            self.complete_ddl_txn(token, deadline, &table_id, &txn_id, committed)
                .await?;
        }
        Ok(())
    }

    async fn complete_ddl_txn(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        table_id: &[u8],
        txn_id: &[u8],
        committed: bool,
    ) -> Result<(), MasterErrorPB> {
        enum Outcome {
            Done,
            Delete,
        }
        let outcome = self
            .write(token, deadline, |snapshot| {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                    return Ok((Mutation::new(), Outcome::Done));
                };
                let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                if ddl_txn_id(&pb) != Some(txn_id) {
                    return Ok((Mutation::new(), Outcome::Done));
                }
                let created = pb
                    .ysql_ddl_txn_verifier_state
                    .first()
                    .is_some_and(|s| s.contains_create_table_op());
                let dropped = pb
                    .ysql_ddl_txn_verifier_state
                    .last()
                    .is_some_and(|s| s.contains_drop_table_op());
                let altered = pb
                    .ysql_ddl_txn_verifier_state
                    .iter()
                    .any(|s| s.contains_alter_table_op());
                tracing::info!(
                    "DDL transaction {} {} for table {}: created {created}, altered {altered}, \
                     dropped {dropped}",
                    hex(txn_id),
                    if committed { "committed" } else { "aborted" },
                    String::from_utf8_lossy(table_id)
                );
                if (committed && dropped) || (!committed && created) {
                    return Ok((Mutation::new(), Outcome::Delete));
                }
                if altered && !committed {
                    return Err(not_yet("Rolling back an ALTER of a YSQL table"));
                }
                pb.ysql_ddl_txn_verifier_state.clear();
                pb.transaction = Default::default();
                let mutation = Mutation::new()
                    .read(table)
                    .upsert_message(SysRowEntryType::TABLE, table_id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((mutation, Outcome::Done))
            })
            .await?;
        match outcome {
            Outcome::Done => Ok(()),
            Outcome::Delete => self.mark_deleting(token, deadline, table_id).await,
        }
    }

    /// `IsYsqlDdlVerificationDone`: no table still carries the transaction's DDL state.
    pub fn is_ysql_ddl_verification_done(
        &self,
        snapshot: &CatalogSnapshot,
        req: &IsYsqlDdlVerificationDoneRequestPB,
        resp: &mut IsYsqlDdlVerificationDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let txn_id = req.transaction.transaction_id();
        resp.set_done(tables_of_txn(snapshot, txn_id).is_empty());
        Ok(())
    }
}

/// The YSQL tables whose DDL state belongs to `txn_id`, not being dropped already.
fn tables_of_txn(snapshot: &CatalogSnapshot, txn_id: &[u8]) -> Vec<Vec<u8>> {
    snapshot
        .iter(SysRowEntryType::TABLE)
        .filter(|t| {
            t.table().is_some_and(|h| {
                h.table_type == TableType::PGSQL_TABLE_TYPE.value()
                    && h.state != TableState::DELETING as i32
                    && h.state != TableState::DELETED as i32
            })
        })
        .filter(|t| {
            t.parse::<SysTablesEntryPB>()
                .is_ok_and(|pb| ddl_txn_id(&pb) == Some(txn_id))
        })
        .map(|t| t.key.id.to_vec())
        .collect()
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
