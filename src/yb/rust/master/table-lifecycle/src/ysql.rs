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

/// The `TABLE_SCHEMA_CHANGE_IN_PROGRESS` refusal of a DDL on a table another DDL transaction
/// is still changing.
pub(crate) fn check_no_other_ddl_txn(
    pb: &SysTablesEntryPB,
    txn_id: &[u8],
) -> Result<(), MasterErrorPB> {
    match ddl_txn_id(pb) {
        Some(other) if other != txn_id => Err(error(
            Code::TABLE_SCHEMA_CHANGE_IN_PROGRESS,
            StatusCode::TryAgain,
            format!(
                "DDL transaction {} cannot continue as table {} is undergoing DDL transaction \
                 verification for {}",
                hex(txn_id),
                String::from_utf8_lossy(pb.name()),
                hex(other)
            ),
        )),
        _ => Ok(()),
    }
}

/// `AlterTable`'s DDL state: an alter in a DDL transaction records the table as it was, so an
/// abort can restore it; an alter outside one clears a stale state.
pub(crate) fn record_alter_in_ddl_txn(
    old: &SysTablesEntryPB,
    new: &mut SysTablesEntryPB,
    req: &yb_proto::pb::master_ddl::AlterTableRequestPB,
    is_pg: bool,
) {
    if !req.ysql_yb_ddl_rollback_enabled() {
        if !new.ysql_ddl_txn_verifier_state.is_empty() {
            new.ysql_ddl_txn_verifier_state.clear();
        }
        return;
    }
    if req.transaction.is_none() || !is_pg {
        return;
    }
    let sub = req.sub_transaction_id();
    let needs_state = new.ysql_ddl_txn_verifier_state.is_empty()
        || (req.has_sub_transaction_id()
            && new
                .ysql_ddl_txn_verifier_state
                .last()
                .is_some_and(|s| s.sub_transaction_id() != sub));
    if needs_state {
        new.transaction = req.transaction.clone();
        let mut state = YsqlDdlTxnVerifierStatePB::new();
        state.previous_schema = old.schema.clone();
        state.set_previous_table_name(String::from_utf8_lossy(old.name()).into_owned());
        if req.has_sub_transaction_id() {
            state.set_sub_transaction_id(sub);
        }
        new.ysql_ddl_txn_verifier_state.push(state);
    }
    new.ysql_ddl_txn_verifier_state
        .last_mut()
        .expect("a verifier state")
        .set_contains_alter_table_op(true);
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
            check_no_other_ddl_txn(&pb, &txn_id)?;
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
            Altered,
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
                // An abort restores the table as it was before the transaction's first change
                // (RollbackYsqlTxnDdlStates); a commit drops the columns marked for deletion
                // (HandleSuccessfulYsqlDdlTxn). Either is an alter of its own.
                let mut alter = false;
                if !committed {
                    let first = pb.ysql_ddl_txn_verifier_state[0].clone();
                    if first.contains_alter_table_op() {
                        pb.schema = first.previous_schema.clone();
                        pb.set_name(first.previous_table_name().as_bytes().to_vec());
                        if first.has_previous_next_column_id() {
                            pb.set_next_column_id(first.previous_next_column_id());
                        }
                        alter = true;
                    }
                } else if pb.schema.columns.iter().any(|c| c.marked_for_deletion()) {
                    pb.schema
                        .mut_or_insert_default()
                        .columns
                        .retain(|c| !c.marked_for_deletion());
                    alter = true;
                }
                if alter {
                    pb.set_version(pb.version() + 1);
                    pb.set_updates_only_index_permissions(false);
                    pb.state = Some(EnumOrUnknown::new(TableState::ALTERING));
                    pb.set_state_msg(
                        format!(
                            "Alter table version={} ts={}",
                            pb.version(),
                            local_time_string()
                        )
                        .into_bytes(),
                    );
                }
                pb.ysql_ddl_txn_verifier_state.clear();
                pb.transaction = Default::default();
                let mutation = Mutation::new()
                    .read(table)
                    .upsert_message(SysRowEntryType::TABLE, table_id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((
                    mutation,
                    if alter {
                        Outcome::Altered
                    } else {
                        Outcome::Done
                    },
                ))
            })
            .await?;
        match outcome {
            Outcome::Done => Ok(()),
            Outcome::Delete => self.mark_deleting(token, deadline, table_id).await,
            Outcome::Altered => {
                // `AddDdlTxnWaitingForSchemaVersion`: verified once the tablets have it.
                self.ddl_alters
                    .lock()
                    .unwrap()
                    .entry(txn_id.to_vec())
                    .or_default()
                    .push(table_id.to_vec());
                Ok(())
            }
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
        let mut done = tables_of_txn(snapshot, txn_id).is_empty();
        let mut alters = self.ddl_alters.lock().unwrap();
        if let Some(tables) = alters.get_mut(txn_id) {
            tables.retain(|id| {
                snapshot
                    .get(SysRowEntryType::TABLE, id)
                    .and_then(|t| t.table())
                    .is_some_and(|t| t.state == TableState::ALTERING as i32)
            });
            if tables.is_empty() {
                alters.remove(txn_id);
            } else {
                done = false;
            }
        }
        resp.set_done(done);
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

/// `kPgFirstNormalObjectId`.
const PG_FIRST_NORMAL_OID: u32 = 16384;

/// `IsPgsqlId`: a 32-digit id whose UUID version is 3, which YSQL ids repurpose.
pub(crate) fn is_pgsql_id(id: &[u8]) -> bool {
    id.len() == 32
        && std::str::from_utf8(&id[12..14])
            .ok()
            .and_then(|v| u8::from_str_radix(v, 16).ok())
            .is_some_and(|v| v >> 4 == 3)
}

fn pg_oid(id: &[u8], offset: usize) -> Option<u32> {
    if !is_pgsql_id(id) {
        return None;
    }
    u32::from_str_radix(std::str::from_utf8(&id[offset..offset + 8]).ok()?, 16).ok()
}

/// `GetPgsqlDatabaseOid` of a namespace or table id.
pub(crate) fn pg_database_oid(id: &[u8]) -> Option<u32> {
    pg_oid(id, 0)
}

/// `GetPgsqlTableOid`.
pub(crate) fn pg_table_oid(id: &[u8]) -> Option<u32> {
    pg_oid(id, 24)
}

/// `IsPriorVersionYsqlCatalogTable`: a catalog table of the previous PG major version.
fn is_prior_version_catalog_table(id: &[u8]) -> bool {
    pg_table_oid(id).is_some_and(|oid| oid < PG_FIRST_NORMAL_OID)
        && std::str::from_utf8(&id[18..20])
            .ok()
            .and_then(|v| u8::from_str_radix(v, 16).ok())
            != Some(1)
}

impl TableLifecycle {
    /// `CreateNamespace` of a YSQL database: the row is written PREPARING, the template's PG
    /// catalog tables are copied (`ProcessPendingNamespace`), and then it is RUNNING, or FAILED.
    /// Clients poll `IsCreateNamespaceDone`.
    pub(crate) async fn create_ysql_namespace(
        self: &std::sync::Arc<Self>,
        token: &LeaderToken,
        deadline: Instant,
        req: &yb_proto::pb::master_ddl::CreateNamespaceRequestPB,
        resp: &mut yb_proto::pb::master_ddl::CreateNamespaceResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let _names = self.names.lock().await;
        let pgsql = YQLDatabase::YQL_DATABASE_PGSQL;
        let source_oid = if req.source_namespace_id().is_empty() {
            None
        } else {
            Some(pg_database_oid(req.source_namespace_id()).ok_or_else(|| {
                error(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::InvalidArgument,
                    format!(
                        "Invalid PostgreSQL namespace id: {}",
                        String::from_utf8_lossy(req.source_namespace_id())
                    ),
                )
            })?)
        };
        let created = self
            .write(token, deadline, |snapshot| {
                let existing = |id: &[u8], message: String| {
                    let e = error(
                        Code::NAMESPACE_ALREADY_PRESENT,
                        StatusCode::AlreadyPresent,
                        message,
                    );
                    Ok((Mutation::new(), Err((id.to_vec(), e))))
                };
                if !req.namespace_id().is_empty() {
                    if let Some(found) = snapshot
                        .get(SysRowEntryType::NAMESPACE, req.namespace_id())
                        .filter(|e| {
                            e.namespace().is_some_and(|n| {
                                n.state != sys_namespace_entry_pb::State::DELETED as i32
                            })
                        })
                    {
                        return existing(
                            &found.key.id,
                            format!(
                                "Keyspace with id '{}' already exists",
                                String::from_utf8_lossy(req.namespace_id())
                            ),
                        );
                    }
                }
                if let Some(found) =
                    crate::lookup::find_namespace_by_name(snapshot, req.name(), pgsql as i32)
                {
                    return existing(
                        &found.key.id,
                        format!("Keyspace '{}' already exists", req.name()),
                    );
                }
                let next_oid = match source_oid {
                    None => req.next_pg_oid(),
                    Some(_) => {
                        if snapshot
                            .get(SysRowEntryType::NAMESPACE, req.source_namespace_id())
                            .is_none()
                        {
                            return Err(error(
                                Code::NAMESPACE_NOT_FOUND,
                                StatusCode::NotFound,
                                format!(
                                    "Source keyspace not found: {}",
                                    String::from_utf8_lossy(req.source_namespace_id())
                                ),
                            ));
                        }
                        // ysql_enable_pg_per_database_oid_allocator.
                        PG_FIRST_NORMAL_OID
                    }
                };
                let id = if req.namespace_id().is_empty() {
                    crate::namespaces::unused_id(snapshot, SysRowEntryType::NAMESPACE)
                } else {
                    req.namespace_id().to_vec()
                };
                let previous = snapshot
                    .get(SysRowEntryType::NAMESPACE, &id)
                    .and_then(|e| e.parse::<SysNamespaceEntryPB>().ok());
                let mut ns = SysNamespaceEntryPB::new();
                ns.set_name(req.name().as_bytes().to_vec());
                ns.set_database_type(pgsql);
                ns.set_colocated(req.colocated());
                ns.set_state(sys_namespace_entry_pb::State::PREPARING);
                ns.set_next_normal_pg_oid(
                    next_oid.max(previous.as_ref().map_or(0, |p| p.next_normal_pg_oid())),
                );
                if req.transaction.is_some() {
                    ns.transaction = req.transaction.clone();
                }
                let mutation = match snapshot.get(SysRowEntryType::NAMESPACE, &id) {
                    Some(old) => Mutation::new().read(old),
                    None => Mutation::new().read_absent(SysRowEntryType::NAMESPACE, &id),
                }
                .upsert_message(SysRowEntryType::NAMESPACE, &id, &ns)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((mutation, Ok(id)))
            })
            .await?;
        let id = match created {
            Ok(id) => id,
            Err((id, e)) => {
                resp.set_id(id);
                return Err(e);
            }
        };
        resp.set_id(id.clone());
        if req.colocated() {
            tracing::warn!(
                "Database {} is colocated; its parent table is not created by a Rust master yet",
                req.name()
            );
        }
        // The template's PG catalog tables, as the catalog has them now.
        let mut sources: Vec<(Vec<u8>, SysTablesEntryPB)> = Vec::new();
        if let Some(source_oid) = source_oid {
            for table in self.catalog.snapshot().iter(SysRowEntryType::TABLE) {
                let table_id = &table.key.id;
                if !is_pgsql_id(table_id)
                    || pg_database_oid(table_id) != Some(source_oid)
                    || is_prior_version_catalog_table(table_id)
                    || table.table().is_some_and(|t| {
                        t.state == TableState::DELETED as i32
                            || t.state == TableState::DELETING as i32
                    })
                {
                    continue;
                }
                sources.push((table_id.to_vec(), table.parse().map_err(corrupt)?));
            }
            sources.sort_by(|a, b| a.0.cmp(&b.0));
        }
        let lifecycle = self.clone();
        let token = *token;
        let name = req.name().to_string();
        let transaction = req.transaction.clone();
        tokio::spawn(async move {
            if let Err(e) = lifecycle
                .finish_ysql_namespace(&token, &id, &name, sources, transaction)
                .await
            {
                tracing::warn!(
                    "Creating YSQL database {name} failed: {}",
                    e.status.message()
                );
                let _ = lifecycle.set_namespace_state(&token, &id, false).await;
            }
        });
        Ok(())
    }

    /// `ProcessPendingNamespace`: copies the PG catalog tables, then writes their rows, the sys
    /// catalog tablet's table ids and the namespace RUNNING in one mutation.
    async fn finish_ysql_namespace(
        &self,
        token: &LeaderToken,
        id: &[u8],
        name: &str,
        sources: Vec<(Vec<u8>, SysTablesEntryPB)>,
        transaction: protobuf::MessageField<yb_proto::pb::common::TransactionMetadataPB>,
    ) -> Result<(), MasterErrorPB> {
        use protobuf::Message;
        use yb_proto::pb::master_rust_peer::{
            CopyPgsqlSysTablesRequestPB, CopyPgsqlSysTablesResponsePB, TableRowPB,
        };
        if sources.is_empty() {
            return self.set_namespace_state(token, id, true).await;
        }
        let mut copy = CopyPgsqlSysTablesRequestPB::new();
        copy.set_namespace_id(id.to_vec());
        copy.set_namespace_name(name.as_bytes().to_vec());
        copy.set_term(token.term());
        copy.transaction = transaction;
        for (table_id, table) in sources {
            let mut row = TableRowPB::new();
            row.set_id(table_id);
            row.table = Some(table).into();
            copy.source_tables.push(row);
        }
        let bytes = copy.write_to_bytes().map_err(corrupt)?;
        let host = self.host.clone();
        tracing::info!(
            "Copying {} PG catalog tables for database {name}",
            copy.source_tables.len()
        );
        let response = tokio::task::spawn_blocking(move || host.call("CopyPgsqlSysTables", &bytes))
            .await
            .map_err(corrupt)?
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        let response =
            CopyPgsqlSysTablesResponsePB::parse_from_bytes(&response).map_err(corrupt)?;
        if let Some(e) = response.error.into_option() {
            return Err(e);
        }
        let deadline = Instant::now() + std::time::Duration::from_secs(60);
        self.write(token, deadline, |snapshot| {
            let namespace = snapshot
                .get(SysRowEntryType::NAMESPACE, id)
                .ok_or_else(|| corrupt("The new database's row is gone"))?;
            let mut ns: SysNamespaceEntryPB = namespace.parse().map_err(corrupt)?;
            if ns.state() != sys_namespace_entry_pb::State::PREPARING {
                return Err(corrupt(format!(
                    "Bad keyspace state ({:?}), abandoning creation work",
                    ns.state()
                )));
            }
            ns.set_state(sys_namespace_entry_pb::State::RUNNING);
            let mut mutation = Mutation::new()
                .read(namespace)
                .upsert_message(SysRowEntryType::NAMESPACE, id, &ns)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            let sys_tablet = snapshot
                .get(SysRowEntryType::TABLET, SYS_CATALOG_TABLET_ID)
                .ok_or_else(|| corrupt("No sys catalog tablet row"))?;
            let mut tablet: yb_proto::pb::catalog_entity_info::SysTabletsEntryPB =
                sys_tablet.parse().map_err(corrupt)?;
            for row in &response.tables {
                mutation = match snapshot.get(SysRowEntryType::TABLE, row.id()) {
                    Some(old) => mutation.read(old),
                    None => mutation.read_absent(SysRowEntryType::TABLE, row.id()),
                }
                .upsert_message(SysRowEntryType::TABLE, row.id(), row.table.get_or_default())
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                if !tablet.hosted_tables_mapped_by_parent_id()
                    && !tablet.table_ids.iter().any(|t| t.as_slice() == row.id())
                {
                    tablet.table_ids.push(row.id().to_vec());
                }
            }
            mutation = mutation
                .read(sys_tablet)
                .upsert_message(SysRowEntryType::TABLET, SYS_CATALOG_TABLET_ID, &tablet)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            Ok((mutation, ()))
        })
        .await?;
        tracing::info!("Activated YSQL database {name}");
        Ok(())
    }

    /// Marks a PREPARING namespace RUNNING or FAILED.
    async fn set_namespace_state(
        &self,
        token: &LeaderToken,
        id: &[u8],
        running: bool,
    ) -> Result<(), MasterErrorPB> {
        let deadline = Instant::now() + std::time::Duration::from_secs(60);
        self.write(token, deadline, |snapshot| {
            let Some(namespace) = snapshot.get(SysRowEntryType::NAMESPACE, id) else {
                return Ok((Mutation::new(), ()));
            };
            let mut ns: SysNamespaceEntryPB = namespace.parse().map_err(corrupt)?;
            if ns.state() != sys_namespace_entry_pb::State::PREPARING {
                return Ok((Mutation::new(), ()));
            }
            ns.set_state(if running {
                sys_namespace_entry_pb::State::RUNNING
            } else {
                sys_namespace_entry_pb::State::FAILED
            });
            let mutation = Mutation::new()
                .read(namespace)
                .upsert_message(SysRowEntryType::NAMESPACE, id, &ns)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            Ok((mutation, ()))
        })
        .await
    }
}

/// `kSysCatalogTabletId`.
const SYS_CATALOG_TABLET_ID: &[u8] = b"00000000000000000000000000000000";
