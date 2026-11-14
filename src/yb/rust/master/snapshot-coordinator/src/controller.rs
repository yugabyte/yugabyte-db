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

//! The `snapshot-coordinator` controller: `MasterSnapshotCoordinator::Poll` as a pass over the
//! rows. Each pass sends the tablet operations of snapshots and restorations whose tablets are
//! still in the initial state, writes a snapshot's or restoration's tablets once they are all
//! done, deletes expired snapshots, takes and expires schedules' snapshots, and removes the rows
//! of deleted snapshots and schedules after `snapshot_coordinator_cleanup_delay_ms`.

use std::sync::Arc;
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use tokio::time::Instant;
use yb_proto::pb::backup::tablet_snapshot_op_request_pb::Operation;
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State;
use yb_proto::pb::master_types::SysRowEntryType;

use crate::restore::{restoration_rows, RestorationRow};
use crate::schedules::{schedule_rows, schedule_snapshots};
use crate::snapshots::{has_expired, snapshot_rows, SnapshotRow};
use crate::state::{
    aggregated_state, all_done, merged, min_restore_time, restoration_initial_state,
    restoration_tablets, schedule_deleted, snapshot_tablets,
};
use crate::tablet_ops::{outcome, TabletOp};
use crate::{ids, SnapshotCoordinator};

/// The longest a PITR waits for the tablet reports that follow its catalog reload.
const RESTORE_REPORTS_WAIT: Duration = Duration::from_secs(10);

/// Runs [`SnapshotCoordinator::poll`] each term.
pub struct SnapshotController {
    coordinator: Arc<SnapshotCoordinator>,
}

impl SnapshotController {
    /// A controller for the term that just began.
    pub fn new(coordinator: Arc<SnapshotCoordinator>) -> Arc<SnapshotController> {
        Arc::new(SnapshotController { coordinator })
    }
}

impl Controller for SnapshotController {
    fn name(&self) -> &'static str {
        "snapshot-coordinator"
    }

    fn interval(&self) -> Duration {
        (self.coordinator.options.poll_interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move { self.coordinator.poll(scope) })
    }
}

impl SnapshotCoordinator {
    /// One pass.
    pub fn poll(self: &Arc<Self>, scope: &TermScope) {
        let Ok(token) = self.leadership.token() else {
            return;
        };
        let snapshot = self.catalog.snapshot();
        let now = match self.host.now() {
            Ok(now) => now,
            Err(status) => {
                tracing::warn!("Snapshot coordinator pass skipped: {status}");
                return;
            }
        };
        let rows = snapshot_rows(&snapshot);
        for row in &rows {
            self.poll_snapshot(scope, token, row, now);
        }
        self.poll_schedules(scope, token, &snapshot, &rows, now);
        self.cleanup_hidden(scope, token, &snapshot, &rows);
        for row in restoration_rows(&snapshot) {
            self.poll_restoration(scope, token, &snapshot, &rows, row);
        }
        self.poll_clones(scope, token, &snapshot);
    }

    fn poll_snapshot(
        self: &Arc<Self>,
        scope: &TermScope,
        token: LeaderToken,
        row: &SnapshotRow,
        now: u64,
    ) {
        let initial = row.pb.state();
        let mark_failed = (self.options.mark_snapshot_as_failed)();
        let mut state = self.state(token.term());
        let tablets = merged(snapshot_tablets(&row.pb), state.outcomes.get(&row.id));
        let done = all_done(initial, &tablets);
        let pending = state.outcomes.get(&row.id).is_some_and(|o| !o.is_empty());
        if done {
            state
                .done_at
                .entry((row.id.clone(), initial as i32))
                .or_insert_with(Instant::now);
            // The namespace anchor of a namespace snapshot ends with it.
            let aggregated = aggregated_state(initial, &tablets, mark_failed);
            if aggregated.is_ok_and(|s| s != State::CREATING) {
                drop(state);
                self.release_namespace_for(&row.id);
                if !row.schedule_id().is_empty() {
                    self.schedule_snapshot_finished(token.term(), row.schedule_id(), &row.id);
                }
                state = self.state(token.term());
            }
        }
        if done && pending {
            if state.writing.insert(row.id.clone()) {
                let (this, id) = (self.clone(), row.id.clone());
                scope.entity().spawn("snapshot-update", async move {
                    if let Err(status) = this
                        .write_outcomes(token, SysRowEntryType::SNAPSHOT, id.clone())
                        .await
                    {
                        tracing::warn!(
                            "Failed to write snapshot {}: {status}",
                            ids::to_string(&id)
                        );
                    }
                    this.state(token.term()).writing.remove(&id);
                    this.wake();
                });
            }
            return;
        }
        if initial == State::DELETING && done {
            // `NeedCleanup`: the row goes once every tablet's snapshot is deleted.
            let passed = state
                .done_at
                .get(&(row.id.clone(), initial as i32))
                .is_some_and(|at| at.elapsed() >= (self.options.cleanup_delay)());
            if passed && state.writing.insert(row.id.clone()) {
                tracing::info!("Cleanup of snapshot {} started.", ids::to_string(&row.id));
                let (this, id) = (self.clone(), row.id.clone());
                scope.entity().spawn("snapshot-cleanup", async move {
                    this.remove_row(token, SysRowEntryType::SNAPSHOT, &id).await;
                });
            }
            return;
        }
        if has_expired(&row.pb, now, (self.options.treat_hours_as_milliseconds)())
            && initial != State::DELETING
        {
            drop(state);
            tracing::info!(
                "Snapshot {} has expired. Trying to delete it.",
                ids::to_string(&row.id)
            );
            self.spawn_delete(scope, token, row);
            return;
        }
        if initial != State::CREATING && initial != State::DELETING {
            return;
        }
        let limit = self.rpc_limit(
            (self.options.max_concurrent_snapshot_rpcs)(),
            (self.options.max_concurrent_snapshot_rpcs_per_tserver)(),
        );
        let running = state.running.entry(row.id.clone()).or_default();
        let mut ops = Vec::new();
        for (tablet, s) in &tablets {
            if *s != initial || running.contains(tablet) {
                continue;
            }
            if running.len() >= limit {
                break;
            }
            running.insert(tablet.clone());
            ops.push(TabletOp {
                operation: if initial == State::DELETING {
                    Operation::DELETE_ON_TABLET
                } else {
                    Operation::CREATE_ON_TABLET
                },
                tablet_id: tablet.clone(),
                snapshot_id: row.id.clone(),
                schedule_id: row.schedule_id().to_vec(),
                restoration_id: Vec::new(),
                snapshot_hybrid_time: row.pb.snapshot_hybrid_time(),
                restoration_hybrid_time: 0,
                request_extras: None,
                db_oid: None,
            });
        }
        drop(state);
        for op in ops {
            self.spawn_tablet_op(scope, token, row.id.clone(), initial, op);
        }
    }

    /// Sends one tablet operation and records its outcome.
    fn spawn_tablet_op(
        self: &Arc<Self>,
        scope: &TermScope,
        token: LeaderToken,
        object_id: Vec<u8>,
        initial: State,
        op: TabletOp,
    ) {
        let this = self.clone();
        scope.entity().spawn("tablet-snapshot-op", async move {
            let restoration_id = op.restoration_id.clone();
            let aborted_check = {
                let this = this.clone();
                move || {
                    this.catalog
                        .snapshot()
                        .get(SysRowEntryType::SNAPSHOT_RESTORATION, &restoration_id)
                        .and_then(|e| {
                            e.parse::<yb_proto::pb::master_backup::SysRestorationEntryPB>()
                                .ok()
                        })
                        .is_some_and(|pb| {
                            aggregated_state(
                                restoration_initial_state(&pb),
                                &restoration_tablets(&pb).collect::<Vec<_>>(),
                                false,
                            )
                            .is_ok_and(|s| s == State::FAILED)
                        })
                }
            };
            let result = this.send_tablet_op(token, &op, &aborted_check).await;
            if let Err(e) = &result {
                tracing::warn!(
                    "Failed to {:?} snapshot at {}: {}",
                    initial,
                    String::from_utf8_lossy(&op.tablet_id),
                    e.status
                );
            } else {
                tracing::info!(
                    "Finished {:?} at {} of {}",
                    initial,
                    String::from_utf8_lossy(&op.tablet_id),
                    ids::to_string(&object_id)
                );
            }
            let outcome = outcome(initial, result, (this.options.mark_snapshot_as_failed)());
            {
                let mut state = this.state(token.term());
                if let Some(running) = state.running.get_mut(&object_id) {
                    running.remove(&op.tablet_id);
                }
                if let Some(o) = outcome {
                    state
                        .outcomes
                        .entry(object_id.clone())
                        .or_default()
                        .insert(op.tablet_id.clone(), o);
                }
            }
            // `schedule_*_rpcs_out_of_band`: the next batch, or the write, without waiting for
            // the interval.
            this.wake();
        });
    }

    fn spawn_delete(self: &Arc<Self>, scope: &TermScope, token: LeaderToken, row: &SnapshotRow) {
        if self.try_start_delete(token.term(), row).is_err() {
            return;
        }
        let (this, id) = (self.clone(), row.id.clone());
        scope.entity().spawn("snapshot-delete", async move {
            let deadline = Instant::now() + Duration::from_secs(60);
            if let Err(e) = this.submit_delete(&token, &id, deadline).await {
                tracing::warn!(
                    "Failed to submit delete operation for snapshot {}: {:?}",
                    ids::to_string(&id),
                    e.status
                );
                this.state(token.term()).deleting.remove(&id);
            }
        });
    }

    /// `CleanupObject`: the row's tombstone.
    async fn remove_row(&self, token: LeaderToken, entry_type: SysRowEntryType, id: &[u8]) {
        let result = self
            .writer
            .mutate(&token, |snapshot| {
                let Some(entity) = snapshot.get(entry_type, id) else {
                    return Ok((Mutation::new(), ()));
                };
                Ok((Mutation::new().read(entity).delete(entry_type, id), ()))
            })
            .await;
        match result {
            Ok(_) => tracing::info!("Finished cleanup of object {}", ids::to_string(id)),
            Err(status) => {
                tracing::info!(
                    "Aborting cleanup of object {}: {status}",
                    ids::to_string(id)
                );
            }
        }
        self.state(token.term()).writing.remove(id);
    }

    /// `PollSchedulesPrepare` and `PollSchedulesComplete`.
    fn poll_schedules(
        self: &Arc<Self>,
        scope: &TermScope,
        token: LeaderToken,
        snapshot: &CatalogSnapshot,
        rows: &[SnapshotRow],
        now: u64,
    ) {
        for schedule in schedule_rows(snapshot) {
            let snapshots: Vec<&SnapshotRow> = schedule_snapshots(rows, &schedule.id).collect();
            if schedule_deleted(&schedule.options) {
                for s in &snapshots {
                    if s.pb.state() != State::DELETING
                        && !self.state(token.term()).deleting.contains(&s.id)
                    {
                        self.spawn_delete(scope, token, s);
                    }
                }
                // `kCleanup` once the delay since the delete passed and no snapshot is creating.
                let delete_time = schedule.options.delete_time();
                let cleanup_delay = (self.options.cleanup_delay)().as_micros() as u64;
                let creating = self.state(token.term()).creating.contains_key(&schedule.id);
                if !creating
                    && now > delete_time.saturating_add(cleanup_delay << 12)
                    && self.state(token.term()).writing.insert(schedule.id.clone())
                {
                    tracing::info!(
                        "Snapshot Schedule {} cleanup started.",
                        ids::to_string(&schedule.id)
                    );
                    let (this, id) = (self.clone(), schedule.id.clone());
                    scope.entity().spawn("schedule-cleanup", async move {
                        this.remove_row(token, SysRowEntryType::SNAPSHOT_SCHEDULE, &id)
                            .await;
                    });
                }
                continue;
            }
            let mut last_time = 0;
            if let Some((last, older)) = snapshots.split_last() {
                // Every snapshot but the last that is older than the retention.
                let gc_limit = now
                    .saturating_sub((schedule.options.retention_duration_sec() * 1_000_000) << 12);
                let first_live = older
                    .iter()
                    .position(|s| s.pb.state() != State::DELETING)
                    .unwrap_or(older.len());
                for s in &older[first_live..] {
                    if s.pb.snapshot_hybrid_time() >= gc_limit {
                        break;
                    }
                    if s.pb.state() != State::DELETING
                        && !self.state(token.term()).deleting.contains(&s.id)
                    {
                        self.spawn_delete(scope, token, s);
                    }
                }
                last_time = last.pb.snapshot_hybrid_time();
                let _ = min_restore_time(&last.pb);
            }
            if last_time != 0
                && last_time.saturating_add((schedule.options.interval_sec() * 1_000_000) << 12)
                    > now
            {
                continue;
            }
            let Ok(id) = self.claim_schedule_snapshot(token.term(), snapshot, &schedule.id) else {
                continue;
            };
            let this = self.clone();
            scope.entity().spawn("schedule-snapshot", async move {
                let deadline = Instant::now() + Duration::from_secs(60);
                if let Err(e) = this
                    .create_schedule_snapshot(&token, &schedule, &id, last_time, deadline)
                    .await
                {
                    tracing::warn!(
                        "Failed to execute operation on {}: {:?}",
                        ids::to_string(&schedule.id),
                        e.status
                    );
                }
            });
        }
    }

    /// The tablets of a restoration whose sys catalog phase is done; its finish once they are.
    fn poll_restoration(
        self: &Arc<Self>,
        scope: &TermScope,
        token: LeaderToken,
        snapshot: &CatalogSnapshot,
        rows: &[SnapshotRow],
        row: RestorationRow,
    ) {
        if row.pb.complete_time_ht() != 0 || !row.pb.is_sys_catalog_restored() {
            return;
        }
        let Some(snapshot_row) = rows.iter().find(|r| r.id == row.pb.snapshot_id()) else {
            tracing::warn!(
                "Snapshot not found for pending restore with id {}",
                ids::to_string(&row.id)
            );
            return;
        };
        let initial = restoration_initial_state(&row.pb);
        let mut state = self.state(token.term());
        let tablets = merged(restoration_tablets(&row.pb), state.outcomes.get(&row.id));
        if all_done(initial, &tablets) {
            if state.writing.insert(row.id.clone()) {
                drop(state);
                let this = self.clone();
                scope.entity().spawn("restoration-finish", async move {
                    let id = row.id.clone();
                    if let Err(status) = this.finish_restoration(token, row).await {
                        tracing::warn!(
                            "Failed to finish restoration {}: {status}",
                            ids::to_string(&id)
                        );
                    }
                    this.state(token.term()).writing.remove(&id);
                });
            }
            return;
        }
        // The catalog reload made every tserver send a full report, which deletes the replicas of
        // tablets the restore removed (split children, tablets of tables created later). Clients
        // reading through a stale cache must not find them serving once the restore completes; C++
        // gets this from its poll interval, which the sys catalog restore waits out.
        let since = *state
            .reports_wait
            .entry(row.id.clone())
            .or_insert_with(Instant::now);
        if !row.schedule_id().is_empty()
            && !self.ts_manager.reports_reconciled()
            && since.elapsed() < RESTORE_REPORTS_WAIT
        {
            return;
        }
        let limit = self.rpc_limit(
            (self.options.max_concurrent_restoration_rpcs)(),
            (self.options.max_concurrent_restoration_rpcs_per_tserver)(),
        );
        let running = state.running.entry(row.id.clone()).or_default();
        let mut to_send = Vec::new();
        for (tablet, s) in &tablets {
            if *s != initial || running.contains(tablet) {
                continue;
            }
            if running.len() >= limit {
                break;
            }
            running.insert(tablet.clone());
            to_send.push(tablet.clone());
        }
        drop(state);
        for op in self.restore_ops(snapshot, &row, snapshot_row, &to_send) {
            self.spawn_tablet_op(scope, token, row.id.clone(), initial, op);
        }
    }
}
