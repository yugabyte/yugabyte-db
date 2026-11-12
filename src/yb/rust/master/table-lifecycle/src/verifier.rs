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

//! Verification of YSQL DDL transactions whose outcome PG may never report (a backend that
//! crashed, a lost connection): C++'s `TableSchemaVerificationTask` and
//! `NamespaceVerificationTask`.
//!
//! The `ysql-ddl-verifier` controller finds every DDL transaction the catalog still carries, polls
//! its status tablet until it is no longer pending (`GetTransactionStatus`; COMMITTED counts as
//! pending until applied), and then reads the outcome from the PG catalog through the host: a
//! table's schema against `pg_class` and `pg_attribute`, a database against `pg_database`. The
//! outcome is applied as `ReportYsqlDdlTxnStatus` applies PG's report, so whichever comes first
//! wins and the other finds nothing left to do. Everything is derived from the rows, so a new
//! leader verifies what the old one left.

use std::collections::{BTreeMap, HashSet};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_leadership::LeaderToken;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use protobuf::{Enum, Message};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, SysNamespaceEntryPB, SysTablesEntryPB,
};
use yb_proto::pb::common::TransactionMetadataPB;
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_rust_peer::{
    CheckYsqlDdlSchemaRequestPB, CheckYsqlDdlSchemaResponsePB, YsqlDatabaseExistsRequestPB,
    YsqlDatabaseExistsResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::transaction::TransactionStatus;
use yb_proto::pb::tserver_service::{
    GetTransactionStatusRequestPB, GetTransactionStatusResponsePB,
};

use crate::backfill::{call_tablet_leader, corrupt, permanent, ts_status};
use master_catalog_state::Mutation;
use yb_proto::pb::master_types::master_error_pb::Code;

use crate::{TableLifecycle, TabletRpc};

/// What a DDL transaction left in the catalog.
#[derive(Default)]
struct Pending {
    status_tablet: Vec<u8>,
    tables: Vec<Vec<u8>>,
    databases: Vec<Vec<u8>>,
}

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct VerifierOptions {
    /// `ysql_transaction_bg_task_wait_ms`: before the first poll and between polls.
    pub poll_interval: fn() -> Duration,
    /// `TEST_disable_ysql_ddl_txn_verification`: start no verification.
    pub disabled: fn() -> bool,
    /// `TEST_ysql_ddl_transaction_verification_failure_probability`: fail a poll.
    pub poll_failure_probability: fn() -> f64,
}

/// The `ysql-ddl-verifier` controller for one term.
pub struct DdlVerifier(Arc<Inner>);

struct Inner {
    lifecycle: Arc<TableLifecycle>,
    rpc: TabletRpc,
    options: VerifierOptions,
    /// Transactions being verified.
    running: Mutex<HashSet<Vec<u8>>>,
}

impl DdlVerifier {
    /// A verifier for the term that just began.
    pub fn new(
        lifecycle: Arc<TableLifecycle>,
        rpc: TabletRpc,
        options: VerifierOptions,
    ) -> Arc<DdlVerifier> {
        Arc::new(DdlVerifier(Arc::new(Inner {
            lifecycle,
            rpc,
            options,
            running: Mutex::default(),
        })))
    }
}

impl Controller for DdlVerifier {
    fn name(&self) -> &'static str {
        "ysql-ddl-verifier"
    }

    fn interval(&self) -> Duration {
        Duration::from_secs(1)
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            if (self.0.options.disabled)() {
                return;
            }
            let Ok(token) = self.0.lifecycle.leadership.token() else {
                return;
            };
            for (txn_id, pending) in self.0.pending() {
                if !self.0.running.lock().unwrap().insert(txn_id.clone()) {
                    continue;
                }
                let inner = self.0.clone();
                scope.spawn("ysql-ddl-verification", async move {
                    if let Err(status) = inner.verify(&token, &txn_id, pending).await {
                        tracing::warn!(
                            "Verifying DDL transaction {} failed, will retry: {status}",
                            hex(&txn_id)
                        );
                    }
                    inner.running.lock().unwrap().remove(&txn_id);
                });
            }
        })
    }
}

fn hex(id: &[u8]) -> String {
    id.iter().map(|b| format!("{b:02x}")).collect()
}

impl Inner {
    /// Every DDL transaction the catalog carries, with what it touched.
    fn pending(&self) -> BTreeMap<Vec<u8>, Pending> {
        let snapshot = self.lifecycle.catalog.snapshot();
        let mut pending: BTreeMap<Vec<u8>, Pending> = BTreeMap::new();
        let mut add = |txn: &TransactionMetadataPB| -> Option<Vec<u8>> {
            if txn.transaction_id().is_empty() || txn.status_tablet().is_empty() {
                return None;
            }
            let entry = pending.entry(txn.transaction_id().to_vec()).or_default();
            entry.status_tablet = txn.status_tablet().as_bytes().to_vec();
            Some(txn.transaction_id().to_vec())
        };
        let mut tables = Vec::new();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            if pb.ysql_ddl_txn_verifier_state.is_empty() {
                continue;
            }
            if let Some(txn) = pb.transaction.as_ref().and_then(&mut add) {
                tables.push((txn, table.key.id.to_vec()));
            }
        }
        let mut databases = Vec::new();
        for namespace in snapshot.iter(SysRowEntryType::NAMESPACE) {
            let Ok(pb) = namespace.parse::<SysNamespaceEntryPB>() else {
                continue;
            };
            if pb.database_type() != YQLDatabase::YQL_DATABASE_PGSQL
                || pb.state() != sys_namespace_entry_pb::State::RUNNING
            {
                continue;
            }
            if let Some(txn) = pb.transaction.as_ref().and_then(&mut add) {
                databases.push((txn, namespace.key.id.to_vec()));
            }
        }
        for (txn, id) in tables {
            pending.entry(txn).or_default().tables.push(id);
        }
        for (txn, id) in databases {
            pending.entry(txn).or_default().databases.push(id);
        }
        pending
    }

    /// `PollTransactionStatusBase::VerifyTransaction`: whether the transaction may still commit.
    async fn is_pending(
        &self,
        token: &LeaderToken,
        status_tablet: &[u8],
        txn_id: &[u8],
    ) -> Result<bool, Status> {
        let probability = (self.options.poll_failure_probability)();
        if probability > 0.0 && (probability >= 1.0 || rand::random::<f64>() < probability) {
            // A failed poll is retried, not the end of the verification.
            tracing::info!("TEST: injected a transaction status poll failure");
            return Ok(true);
        }
        let ctx = self.rpc.context(&self.lifecycle);
        call_tablet_leader(
            &ctx,
            token,
            status_tablet,
            "yb.tserver.TabletServerService",
            "GetTransactionStatus",
            |_leader| {
                let mut req = GetTransactionStatusRequestPB::new();
                req.set_tablet_id(status_tablet.to_vec());
                req.transaction_id.push(txn_id.to_vec());
                req.write_to_bytes().map_err(corrupt)
            },
            |bytes| {
                let resp =
                    GetTransactionStatusResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
                if let Some(e) = resp.error.as_ref() {
                    let status = ts_status(&e.status);
                    return Ok(Err((status, permanent(e.code()))));
                }
                Ok(Ok(resp.status.first().is_some_and(|s| {
                    matches!(
                        TransactionStatus::from_i32(s.value()),
                        Some(TransactionStatus::PENDING | TransactionStatus::COMMITTED)
                    )
                })))
            },
            self.rpc.retry,
        )
        .await
    }

    async fn verify(
        self: &Arc<Self>,
        token: &LeaderToken,
        txn_id: &[u8],
        pending: Pending,
    ) -> Result<(), Status> {
        tracing::info!(
            "Verifying DDL transaction {}: {} tables, {} databases, status tablet {}",
            hex(txn_id),
            pending.tables.len(),
            pending.databases.len(),
            String::from_utf8_lossy(&pending.status_tablet)
        );
        // As C++'s tasks, the first poll waits one interval: PG normally reports first.
        tokio::time::sleep((self.options.poll_interval)()).await;
        while self
            .is_pending(token, &pending.status_tablet, txn_id)
            .await?
        {
            self.lifecycle.leadership.check(token)?;
            tokio::time::sleep((self.options.poll_interval)()).await;
        }
        if !pending.tables.is_empty() {
            self.verify_tables(token, txn_id).await?;
        }
        for namespace_id in pending.databases {
            self.verify_database(token, txn_id, &namespace_id).await?;
        }
        Ok(())
    }

    /// `YsqlTableSchemaChecker`: the first table whose schema tells decides; a transaction that
    /// changed no DocDB schema is applied as committed, which keeps every table as it is.
    async fn verify_tables(&self, token: &LeaderToken, txn_id: &[u8]) -> Result<(), Status> {
        let snapshot = self.lifecycle.catalog.snapshot();
        let mut committed = None;
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            if pb.ysql_ddl_txn_verifier_state.is_empty()
                || pb.transaction.transaction_id() != txn_id
            {
                continue;
            }
            let mut req = CheckYsqlDdlSchemaRequestPB::new();
            req.set_table_id(table.key.id.to_vec());
            req.set_is_system(crate::ysql::pg_table_oid(&table.key.id).is_some_and(|o| o < 16384));
            req.table = Some(pb).into();
            let bytes = req.write_to_bytes().map_err(corrupt)?;
            let host = self.lifecycle.host.clone();
            let response =
                tokio::task::spawn_blocking(move || host.call("CheckYsqlDdlSchema", &bytes))
                    .await
                    .map_err(corrupt)??;
            let resp =
                CheckYsqlDdlSchemaResponsePB::parse_from_bytes(&response).map_err(corrupt)?;
            if let Some(e) = resp.error.as_ref() {
                return Err(ts_status(&e.status));
            }
            if resp.committed.is_some() {
                committed = Some(resp.committed());
                break;
            }
        }
        let committed = committed.unwrap_or(true);
        tracing::info!(
            "DDL transaction {} was not reported; the PG catalog says it {}",
            hex(txn_id),
            if committed { "committed" } else { "aborted" }
        );
        let mut req = yb_proto::pb::master_ddl::ReportYsqlDdlTxnStatusRequestPB::new();
        req.set_transaction_id(txn_id.to_vec());
        req.set_is_committed(committed);
        let deadline = Instant::now() + Duration::from_secs(60);
        self.lifecycle
            .report_ysql_ddl_txn_status(token, deadline, &req)
            .await
            .map_err(|e| ts_status(&e.status))
    }

    /// `NamespaceVerificationTask`: a database whose creating transaction committed keeps no
    /// transaction; one PG does not have was rolled back, and is dropped.
    async fn verify_database(
        &self,
        token: &LeaderToken,
        txn_id: &[u8],
        namespace_id: &[u8],
    ) -> Result<(), Status> {
        let mut req = YsqlDatabaseExistsRequestPB::new();
        req.set_namespace_id(namespace_id.to_vec());
        let bytes = req.write_to_bytes().map_err(corrupt)?;
        let host = self.lifecycle.host.clone();
        let response = tokio::task::spawn_blocking(move || host.call("YsqlDatabaseExists", &bytes))
            .await
            .map_err(corrupt)??;
        let resp = YsqlDatabaseExistsResponsePB::parse_from_bytes(&response).map_err(corrupt)?;
        if let Some(e) = resp.error.as_ref() {
            return Err(ts_status(&e.status));
        }
        let exists = resp.exists();
        tracing::info!(
            "Database {} of DDL transaction {} {}",
            String::from_utf8_lossy(namespace_id),
            hex(txn_id),
            if exists {
                "committed"
            } else {
                "was rolled back; dropping it"
            }
        );
        let deadline = Instant::now() + Duration::from_secs(60);
        self.lifecycle
            .write(token, deadline, |snapshot| {
                let Some(namespace) = snapshot.get(SysRowEntryType::NAMESPACE, namespace_id) else {
                    return Ok((Mutation::new(), ()));
                };
                let mut pb: SysNamespaceEntryPB = namespace.parse().map_err(|e| {
                    crate::error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                })?;
                if pb.transaction.transaction_id() != txn_id {
                    return Ok((Mutation::new(), ()));
                }
                pb.transaction = Default::default();
                if !exists {
                    pb.set_state(sys_namespace_entry_pb::State::DELETING);
                }
                let mutation = Mutation::new()
                    .read(namespace)
                    .upsert_message(SysRowEntryType::NAMESPACE, namespace_id, &pb)
                    .map_err(|s| crate::error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((mutation, ()))
            })
            .await
            .map_err(|e| ts_status(&e.status))
    }
}
