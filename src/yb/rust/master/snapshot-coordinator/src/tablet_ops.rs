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

//! `AsyncTabletSnapshotOp`: one `TabletSnapshotOp` to a tablet's leader, retried as C++ retries
//! it, and the outcome rules of `SnapshotState` and `RestorationState`.

use master_catalog_state::CatalogSnapshot;
use master_leadership::LeaderToken;
use master_task_runtime::Attempt;
use protobuf::Message;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::backup::{
    tablet_snapshot_op_request_pb::Operation, TabletSnapshotOpRequestPB, TabletSnapshotOpResponsePB,
};
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State;
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, sys_tablets_entry_pb};
use yb_proto::pb::master_types::{master_error_pb::Code, SysRowEntryType};
use yb_proto::pb::tserver_types::tablet_server_error_pb::Code as TsCode;

use crate::state::Outcome;
use crate::{corrupt, internal, SnapshotCoordinator};

const BACKUP_SERVICE: &str = "yb.tserver.TabletServerBackupService";

/// `TransactionErrorCode::kSnapshotTooOld`.
const SNAPSHOT_TOO_OLD: i32 = 4;
/// `ConsensusErrorPB::TABLET_SPLIT`.
const TABLET_SPLIT: i32 = 10;

/// Why a tablet operation ended without success, with what its outcome depends on.
#[derive(Clone, Debug)]
pub(crate) struct OpError {
    pub(crate) status: Status,
    /// The tserver's error code.
    pub(crate) ts_code: Option<TsCode>,
    /// The master error code in the status (`MasterError(status)`).
    pub(crate) master_code: Option<Code>,
    /// `TransactionError(status) == kSnapshotTooOld`.
    pub(crate) snapshot_too_old: bool,
}

impl OpError {
    fn plain(status: Status) -> OpError {
        OpError {
            status,
            ts_code: None,
            master_code: None,
            snapshot_too_old: false,
        }
    }
}

/// The error codes a C++ status carries in `AppStatusPB.errors`: (category, value) for the
/// categories whose value is an `int32`. Decoding stops at a category of another shape.
pub(crate) fn error_codes(errors: &[u8]) -> Vec<(u8, i32)> {
    let mut out = Vec::new();
    let mut rest = errors;
    while let Some((&category, tail)) = rest.split_first() {
        if category == 0 {
            break;
        }
        // errno, tserver, pgsql, transaction, master, raft group state, consensus, client, CDC,
        // network, advisory locks.
        if ![1u8, 5, 6, 7, 9, 10, 11, 12, 16, 19, 24].contains(&category) || tail.len() < 4 {
            break;
        }
        out.push((
            category,
            i32::from_le_bytes([tail[0], tail[1], tail[2], tail[3]]),
        ));
        rest = &tail[4..];
    }
    out
}

/// A tserver's `TabletServerErrorPB` as an [`OpError`].
fn ts_error(e: &yb_proto::pb::tserver_types::TabletServerErrorPB) -> OpError {
    let codes = error_codes(e.status.errors());
    OpError {
        status: Status::new(
            StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
            e.status.message().to_string(),
        ),
        ts_code: Some(e.code()),
        master_code: codes
            .iter()
            .find(|(c, _)| *c == 9)
            .and_then(|(_, v)| protobuf::Enum::from_i32(*v)),
        snapshot_too_old: codes.contains(&(7, SNAPSHOT_TOO_OLD)),
    }
}

/// One tablet operation.
#[derive(Clone, Debug)]
pub(crate) struct TabletOp {
    pub(crate) operation: Operation,
    pub(crate) tablet_id: Vec<u8>,
    /// Empty when restoring a tablet the snapshot does not have.
    pub(crate) snapshot_id: Vec<u8>,
    pub(crate) schedule_id: Vec<u8>,
    pub(crate) restoration_id: Vec<u8>,
    pub(crate) snapshot_hybrid_time: u64,
    pub(crate) restoration_hybrid_time: u64,
    /// For a PITR's RESTORE_ON_TABLET: the schema and the colocated tables' metadata.
    pub(crate) request_extras: Option<TabletSnapshotOpRequestPB>,
    pub(crate) db_oid: Option<i64>,
}

impl TabletOp {
    fn request(&self, dest_uuid: &[u8], now: u64) -> TabletSnapshotOpRequestPB {
        let mut req = self.request_extras.clone().unwrap_or_default();
        req.dest_uuid = dest_uuid.to_vec();
        req.tablet_id = vec![self.tablet_id.clone()];
        req.snapshot_id = self.snapshot_id.clone();
        req.operation = self.operation.into();
        req.schedule_id = self.schedule_id.clone();
        req.restoration_id = self.restoration_id.clone();
        req.snapshot_hybrid_time = self.snapshot_hybrid_time;
        req.restoration_hybrid_time = self.restoration_hybrid_time;
        if let Some(oid) = self.db_oid {
            req.db_oid = oid;
        }
        req.propagated_hybrid_time = now;
        req
    }
}

/// `TableInfo::is_running`: RUNNING or ALTERING.
fn table_running(snapshot: &CatalogSnapshot, tablet_id: &[u8]) -> Result<(), OpError> {
    let tablet = snapshot.get(SysRowEntryType::TABLET, tablet_id);
    let table_id = tablet
        .and_then(|t| t.tablet())
        .map(|t| t.table_id.clone())
        .unwrap_or_default();
    let state = snapshot
        .get(SysRowEntryType::TABLE, &table_id)
        .and_then(|t| t.table())
        .map(|t| t.state);
    let running = state.is_some_and(|s| {
        s == sys_tables_entry_pb::State::RUNNING as i32
            || s == sys_tables_entry_pb::State::ALTERING as i32
    });
    if running {
        return Ok(());
    }
    let message = format!(
        "Table is not running: {}: TABLE_NOT_RUNNING",
        String::from_utf8_lossy(&table_id)
    );
    Err(OpError {
        status: Status::new(StatusCode::Expired, message),
        ts_code: None,
        master_code: Some(Code::TABLE_NOT_RUNNING),
        snapshot_too_old: false,
    })
}

impl SnapshotCoordinator {
    pub(crate) fn leader_of(&self, tablet_id: &[u8]) -> Option<Vec<u8>> {
        self.ts_manager
            .locations
            .get(tablet_id)
            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
    }

    /// Sends `op` until it succeeds or fails for good: `RetryAllowed`, then `Finished`, which
    /// reports a failure of a tablet whose table stopped running as that.
    pub(crate) async fn send_tablet_op(
        &self,
        token: LeaderToken,
        op: &TabletOp,
        aborted: &(dyn Fn() -> bool + Send + Sync),
    ) -> Result<(), OpError> {
        let rpc = self.rpc().map_err(OpError::plain)?;
        let lane = self.leader_of(&op.tablet_id).unwrap_or_default();
        let rpc_timeout = (self.options.rpc_timeout)();
        let final_error = std::sync::Mutex::new(None::<OpError>);
        let result = self
            .executor
            .run(&lane, "TabletSnapshotOp", &self.retry, |_| {
                let current = self.leadership.check(&token);
                let snapshot = self.catalog.snapshot();
                let tablet_state = snapshot
                    .get(SysRowEntryType::TABLET, &op.tablet_id)
                    .and_then(|t| t.tablet())
                    .map(|t| t.state);
                let leader = self.leader_of(&op.tablet_id);
                let address = leader.as_ref().and_then(|l| self.ts_manager.rpc_address(l));
                let now = self.host.now().unwrap_or_default();
                let running = table_running(&snapshot, &op.tablet_id);
                let (rpc, final_error) = (rpc.clone(), &final_error);
                async move {
                    current.map_err(Attempt::Fail)?;
                    let gone = tablet_state.is_none_or(|s| {
                        s == sys_tablets_entry_pb::State::DELETED as i32
                            || s == sys_tablets_entry_pb::State::REPLACED as i32
                    });
                    // `HandleReplicaLookupFailure`: deleting a snapshot of a deleted tablet.
                    if gone && op.operation == Operation::DELETE_ON_TABLET {
                        return Ok(());
                    }
                    // The table's tasks are aborted when it stops running ("Table closing").
                    if op.operation != Operation::DELETE_ON_TABLET {
                        if let Err(e) = running {
                            *final_error.lock().unwrap() = Some(e.clone());
                            return Err(Attempt::Fail(e.status));
                        }
                    }
                    let (Some(leader), Some((host, port))) = (leader, address) else {
                        return Err(Attempt::Retry(Status::new(
                            StatusCode::ServiceUnavailable,
                            "No leader for the tablet",
                        )));
                    };
                    let bytes = op
                        .request(&leader, now)
                        .write_to_bytes()
                        .map_err(|e| Attempt::Fail(internal(e)))?;
                    let resp = rpc
                        .call(
                            &host,
                            port,
                            BACKUP_SERVICE,
                            "TabletSnapshotOp",
                            &bytes,
                            rpc_timeout,
                        )
                        .await
                        .map_err(Attempt::Retry)?;
                    let resp = TabletSnapshotOpResponsePB::parse_from_bytes(&resp)
                        .map_err(|e| Attempt::Retry(corrupt(e)))?;
                    let Some(e) = resp.error.as_ref() else {
                        return Ok(());
                    };
                    let error = ts_error(e);
                    let retry = match e.code() {
                        TsCode::TABLET_NOT_FOUND => false,
                        TsCode::INVALID_SNAPSHOT => op.operation != Operation::RESTORE_ON_TABLET,
                        _ => {
                            !(error.snapshot_too_old
                                || error_codes(e.status.errors()).contains(&(11, TABLET_SPLIT))
                                || (op.operation == Operation::RESTORE_ON_TABLET && aborted()))
                        }
                    };
                    if retry {
                        tracing::warn!(
                            "TabletSnapshotOp {:?} of tablet {} failed, will be retried: {}",
                            op.operation,
                            String::from_utf8_lossy(&op.tablet_id),
                            error.status
                        );
                        return Err(Attempt::Retry(error.status));
                    }
                    tracing::warn!(
                        "TabletSnapshotOp {:?} of tablet {} failed, NO retry: {}",
                        op.operation,
                        String::from_utf8_lossy(&op.tablet_id),
                        error.status
                    );
                    let status = error.status.clone();
                    *final_error.lock().unwrap() = Some(error);
                    Err(Attempt::Fail(status))
                }
            })
            .await;
        match result {
            Ok(()) => Ok(()),
            Err(status) => {
                // `Finished`: a tablet whose table stopped running reports that instead.
                let snapshot = self.catalog.snapshot();
                if let Err(e) = table_running(&snapshot, &op.tablet_id) {
                    if op.operation != Operation::DELETE_ON_TABLET
                        || final_error.lock().unwrap().is_some()
                    {
                        return Err(e);
                    }
                }
                Err(final_error
                    .into_inner()
                    .unwrap()
                    .unwrap_or_else(|| OpError::plain(status)))
            }
        }
    }
}

/// `StateWithTablets::Done` with `CheckDoneStatus` and `GetTerminalStateForStatus`: the outcome
/// of a tablet operation of an object in `initial`. A failure with no terminal state leaves the
/// tablet in `initial`, sent again on the next pass.
pub(crate) fn outcome(
    initial: State,
    result: Result<(), OpError>,
    mark_failed: bool,
) -> Option<Outcome> {
    let error = match result {
        Ok(()) => None,
        Err(e) => {
            // `SnapshotState::CheckDoneStatus`.
            if initial == State::DELETING
                && matches!(
                    e.master_code,
                    Some(Code::TABLET_NOT_RUNNING) | Some(Code::TABLE_NOT_RUNNING)
                )
            {
                None
            } else {
                Some(e)
            }
        }
    };
    let Some(e) = error else {
        return Some(Outcome {
            from: initial,
            state: crate::state::terminal_state(initial, mark_failed),
            error: None,
        });
    };
    let terminal = match initial {
        State::RESTORING => (e.status.code == StatusCode::Aborted
            || e.status.code == StatusCode::NotFound
            || e.ts_code == Some(TsCode::INVALID_SNAPSHOT)
            || e.master_code == Some(Code::TABLE_NOT_RUNNING))
        .then_some(State::FAILED),
        _ => {
            if e.status.code == StatusCode::Expired || e.snapshot_too_old {
                Some(State::FAILED)
            } else if e.ts_code == Some(TsCode::TABLET_NOT_FOUND) && initial == State::DELETING {
                Some(State::DELETED)
            } else {
                None
            }
        }
    };
    terminal.map(|state| Outcome {
        from: initial,
        state,
        error: Some(e.status),
    })
}
