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

//! `XClusterFailover`: an automatic-mode target leaves replication at its xCluster safe time.
//! Replication is paused, each namespace is snapshotted and restored to its safe time, then the
//! group is deleted. The steps run in memory; the row only says that a failover runs
//! (`failover_in_progress`) and how the last one ended, so a new leader marks a running one
//! aborted and the client retries from the start.

use std::sync::Arc;
use std::time::Duration;

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use master_task_runtime::BoxFuture;
use protobuf::MessageField;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State as SnapshotState;
use yb_proto::pb::master_replication::{
    DeleteUniverseReplicationResponsePB, IsXClusterFailoverDoneRequestPB,
    IsXClusterFailoverDoneResponsePB, SetUniverseReplicationEnabledRequestPB,
    XClusterFailoverRequestPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::setup::universe;
use crate::{app_status, status, status_of, unknown, XCluster};

/// `kXClusterFailoverFeatureName`.
const FEATURE: &str = "xCluster failover";

/// The snapshot coordinator and the split manager, which only the wiring holds.
pub trait FailoverSnapshots: Send + Sync + 'static {
    /// `PrepareForSnapshotRestore`: splitting is disabled and in-flight splits finish.
    fn prepare_for_restore(
        &self,
        deadline: Instant,
        feature: &'static str,
    ) -> BoxFuture<'_, Result<(), Status>>;
    /// `ReenableSplittingFor`.
    fn reenable_splitting(&self, feature: &'static str);
    /// `MasterSnapshotCoordinator::Create` of a YSQL database, with its indexes.
    fn create<'a>(
        &'a self,
        token: &'a LeaderToken,
        namespace_id: &'a str,
        retention_hours: i32,
        deadline: Instant,
    ) -> BoxFuture<'a, Result<Vec<u8>, Status>>;
    /// `WaitForSnapshotToComplete`.
    fn wait_complete<'a>(
        &'a self,
        token: &'a LeaderToken,
        snapshot_id: &'a [u8],
        deadline: Instant,
    ) -> BoxFuture<'a, Result<(), Status>>;
    /// `Restore`: the restoration id.
    fn restore<'a>(
        &'a self,
        token: &'a LeaderToken,
        snapshot_id: &'a [u8],
        restore_at: u64,
        deadline: Instant,
    ) -> BoxFuture<'a, Result<Vec<u8>, Status>>;
    /// The restoration's state, as `ListRestorations` reports it.
    fn restoration_state(
        &self,
        token: &LeaderToken,
        restoration_id: &[u8],
        snapshot_id: &[u8],
    ) -> Result<SnapshotState, Status>;
    /// `Delete`.
    fn delete<'a>(
        &'a self,
        token: &'a LeaderToken,
        snapshot_id: &'a [u8],
        deadline: Instant,
    ) -> BoxFuture<'a, Result<(), Status>>;
}

#[derive(Default)]
struct Namespace {
    id: String,
    restore_at: u64,
    snapshot_id: Vec<u8>,
    restoration_id: Vec<u8>,
}

impl XCluster {
    /// Set once, before any leader term.
    pub fn set_failover_snapshots(&self, snapshots: Arc<dyn FailoverSnapshots>) {
        let _ = self.failover_snapshots.set(snapshots);
    }

    /// `XClusterFailover`: marks the group's failover as running and starts it.
    pub async fn xcluster_failover(
        &self,
        token: &LeaderToken,
        req: &XClusterFailoverRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterFailover: {:?}", req);
        let id = req.replication_group_id().to_string();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let snapshots = self.failover_snapshots.get().cloned().ok_or_else(|| {
            unknown(status(
                StatusCode::IllegalState,
                "Background task pool unavailable",
            ))
        })?;
        let (_, pb) = universe(&self.catalog.snapshot(), &id).ok_or_else(|| {
            unknown(status(
                StatusCode::NotFound,
                format!("Replication group {id} not found"),
            ))
        })?;
        if pb.failover_in_progress() {
            return Err(already_running(&id));
        }
        if !pb.db_scoped_info.automatic_ddl_mode() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Replication group {id} is not an xCluster target in automatic mode"),
            )));
        }
        let mut namespaces = Vec::new();
        for i in &pb.db_scoped_info.namespace_infos {
            if i.consumer_namespace_id().is_empty() {
                return Err(unknown(status(
                    StatusCode::IllegalState,
                    format!(
                        "Replication group {id} contains a namespace entry without a consumer \
                         namespace id"
                    ),
                )));
            }
            namespaces.push(Namespace {
                id: i.consumer_namespace_id().to_string(),
                ..Default::default()
            });
        }
        if namespaces.is_empty() {
            return Err(unknown(status(
                StatusCode::IllegalState,
                format!("Replication group {id} has no namespaces to snapshot"),
            )));
        }
        let Some(this) = self.me.upgrade() else {
            return Err(unknown(status(
                StatusCode::ShutdownInProgress,
                "xCluster is shutting down",
            )));
        };
        // Registered before the row says it runs, so the controller never takes it for a
        // failover of an earlier leader.
        self.failovers
            .lock()
            .unwrap()
            .insert(id.clone(), token.term());
        let marked = self
            .writer
            .mutate(token, |snapshot| {
                let Some((entity, mut pb)) = universe(snapshot, &id) else {
                    return Err(status(
                        StatusCode::NotFound,
                        format!("Replication group {id} not found"),
                    ));
                };
                if pb.failover_in_progress() {
                    return Err(status(StatusCode::AlreadyPresent, "Failover running"));
                }
                pb.set_failover_in_progress(true);
                pb.last_failover_completion_status = MessageField::none();
                Ok((
                    Mutation::new().read(&entity).upsert_message(
                        SysRowEntryType::UNIVERSE_REPLICATION,
                        id.as_bytes(),
                        &pb,
                    )?,
                    (),
                ))
            })
            .await;
        if let Err(s) = marked {
            self.failovers.lock().unwrap().remove(&id);
            return Err(if s.code == StatusCode::AlreadyPresent {
                already_running(&id)
            } else {
                unknown(s)
            });
        }
        let token = *token;
        tokio::spawn(async move {
            this.run_failover(&token, &id, namespaces, &*snapshots)
                .await;
        });
        Ok(())
    }

    /// `IsXClusterFailoverDone`: a deleted group means the failover succeeded.
    pub fn is_xcluster_failover_done(
        &self,
        req: &IsXClusterFailoverDoneRequestPB,
        resp: &mut IsXClusterFailoverDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let Some((_, pb)) = universe(&self.catalog.snapshot(), id) else {
            resp.set_done(true);
            return Ok(());
        };
        if !pb.has_failover_in_progress() {
            return Err(unknown(status(
                StatusCode::NotFound,
                format!("No failover operation found for replication group {id}"),
            )));
        }
        resp.set_done(!pb.failover_in_progress());
        if !pb.failover_in_progress() {
            resp.failover_error = pb.last_failover_completion_status;
        }
        Ok(())
    }

    async fn run_failover(
        &self,
        token: &LeaderToken,
        id: &str,
        mut namespaces: Vec<Namespace>,
        snapshots: &dyn FailoverSnapshots,
    ) {
        let result = self
            .failover_steps(token, id, &mut namespaces, snapshots)
            .await;
        snapshots.reenable_splitting(FEATURE);
        if let Err(s) = &result {
            tracing::warn!(
                "xCluster failover task for replication group {id} failed ({s}), cleaning up \
                 snapshot state."
            );
        }
        self.cleanup_failover_snapshots(token, &namespaces, snapshots)
            .await;
        if let Err(s) = result {
            if let Err(e) = self.end_failover(token, id, Err(s)).await {
                tracing::warn!("Failed to persist failover failure status: {e}");
            }
        }
        let mut failovers = self.failovers.lock().unwrap();
        if failovers.get(id) == Some(&token.term()) {
            failovers.remove(id);
        }
    }

    async fn failover_steps(
        &self,
        token: &LeaderToken,
        id: &str,
        namespaces: &mut [Namespace],
        snapshots: &dyn FailoverSnapshots,
    ) -> Result<(), Status> {
        let deadline = Instant::now() + (self.options.failover_task_timeout)();

        // PauseReplicationAndFetchSafeTimes.
        let mut req = SetUniverseReplicationEnabledRequestPB::new();
        req.set_replication_group_id(id.to_string());
        req.set_is_enabled(false);
        self.set_universe_replication_enabled(token, deadline, &req)
            .await
            .map_err(|e| status_of(&e))?;
        snapshots.prepare_for_restore(deadline, FEATURE).await?;
        self.compute_safe_time(token).await?;
        let safe_times = crate::safe_time::safe_time_map(&self.catalog.snapshot());
        for ns in namespaces.iter_mut() {
            let ht = safe_times.get(&ns.id).copied().ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!("XCluster safe time not found for namespace {}", ns.id),
                )
            })?;
            if !crate::safe_time::is_valid(ht) {
                return Err(status(
                    StatusCode::IllegalState,
                    format!(
                        "Invalid xCluster safe time for namespace {} in replication group {id}",
                        ns.id
                    ),
                ));
            }
            ns.restore_at = ht;
        }
        tracing::info!(
            "Validating xCluster failover for replication group {id} with namespaces and target \
             safe times: {:?}",
            namespaces
                .iter()
                .map(|n| (n.id.as_str(), n.restore_at))
                .collect::<Vec<_>>()
        );

        // CreateSnapshots.
        if (self.options.failover_fail_create_snapshot)() {
            return Err(status(
                StatusCode::Aborted,
                "Simulated snapshot creation failure",
            ));
        }
        for ns in namespaces.iter_mut() {
            ns.snapshot_id = snapshots
                .create(
                    token,
                    &ns.id,
                    (self.options.failover_snapshot_retention_hours)(),
                    deadline,
                )
                .await?;
        }

        // RestoreSnapshots.
        for ns in namespaces.iter_mut() {
            snapshots
                .wait_complete(token, &ns.snapshot_id, deadline)
                .await?;
            ns.restoration_id = snapshots
                .restore(token, &ns.snapshot_id, ns.restore_at, deadline)
                .await?;
        }

        // PollForCurrentRestoration, one namespace after another.
        let restoration_deadline =
            Instant::now() + (self.options.failover_restoration_wait)() * namespaces.len() as u32;
        for ns in namespaces.iter() {
            loop {
                if Instant::now() >= restoration_deadline {
                    return Err(status(
                        StatusCode::TimedOut,
                        format!(
                            "Timed out waiting for restoration {} for snapshot {}",
                            uuid(&ns.restoration_id),
                            uuid(&ns.snapshot_id)
                        ),
                    ));
                }
                match snapshots.restoration_state(token, &ns.restoration_id, &ns.snapshot_id)? {
                    SnapshotState::RESTORED => break,
                    SnapshotState::RESTORING | SnapshotState::CREATING => {
                        tokio::time::sleep((self.options.failover_restoration_poll_interval)())
                            .await;
                    }
                    state => {
                        return Err(status(
                            StatusCode::IllegalState,
                            format!(
                                "xCluster failover restoration {} for snapshot {} failed \
                                 (state: {state:?})",
                                uuid(&ns.restoration_id),
                                uuid(&ns.snapshot_id)
                            ),
                        ));
                    }
                }
            }
            if (self.options.failover_fail_restore_namespace)() == ns.id {
                return Err(status(
                    StatusCode::IllegalState,
                    format!("Simulated failure restoring namespace {}", ns.id),
                ));
            }
            tracing::info!(
                "Restoration {} for snapshot {} completed for namespace {}",
                uuid(&ns.restoration_id),
                uuid(&ns.snapshot_id),
                ns.id
            );
        }

        // DeleteReplication.
        while (self.options.pause_failover_before_delete)() {
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
        if (self.options.failover_fail_delete_replication)() {
            return Err(status(
                StatusCode::InternalError,
                format!("Simulated DeleteUniverseReplication failure for replication group {id}"),
            ));
        }
        // The whole group goes, so no target namespace needs OIDs reserved.
        let (_, pb) = universe(&self.catalog.snapshot(), id).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("Replication group {id} not found"),
            )
        })?;
        let mut resp = DeleteUniverseReplicationResponsePB::new();
        self.delete_replication_group(
            token,
            Instant::now() + (self.options.failover_task_timeout)(),
            &pb,
            true,
            true,
            &mut resp,
        )
        .await
        .and_then(|()| match resp.error.as_ref() {
            Some(e) => Err(status_of(e)),
            None => Ok(()),
        })
        .map_err(|s| {
            status(
                s.code,
                format!(
                    "DeleteUniverseReplication failed for replication group {id}: {}",
                    s.message
                ),
            )
        })?;
        tracing::info!("Deleted xCluster replication group {id}");
        Ok(())
    }

    /// `TryCleanupSnapshots`: retention deletes what this misses.
    async fn cleanup_failover_snapshots(
        &self,
        token: &LeaderToken,
        namespaces: &[Namespace],
        snapshots: &dyn FailoverSnapshots,
    ) {
        for ns in namespaces {
            if ns.snapshot_id.is_empty() {
                continue;
            }
            let deadline = Instant::now() + (self.options.failover_snapshot_delete_retry)();
            if let Err(s) = snapshots.delete(token, &ns.snapshot_id, deadline).await {
                tracing::warn!(
                    "Failed to delete failover snapshot {}: {s}. It will be auto-deleted after \
                     retention ({} hr).",
                    uuid(&ns.snapshot_id),
                    (self.options.failover_snapshot_retention_hours)()
                );
            }
        }
    }

    /// Records how the group's failover ended, if the group is still there.
    async fn end_failover(
        &self,
        token: &LeaderToken,
        id: &str,
        result: Result<(), Status>,
    ) -> Result<(), Status> {
        self.writer
            .mutate(token, |snapshot| {
                let Some((entity, mut pb)) = universe(snapshot, id) else {
                    return Ok((Mutation::new(), ()));
                };
                pb.set_failover_in_progress(false);
                pb.last_failover_completion_status = MessageField::some(app_status(&result));
                Ok((
                    Mutation::new().read(&entity).upsert_message(
                        SysRowEntryType::UNIVERSE_REPLICATION,
                        id.as_bytes(),
                        &pb,
                    )?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// `CleanupStaleFailovers`: a failover the row says runs, but that no task of this term
    /// runs, was cut off by a leader change.
    pub(crate) async fn cleanup_stale_failovers(&self, token: &LeaderToken) -> Result<(), Status> {
        let running: Vec<String> = crate::setup::universes(&self.catalog.snapshot())
            .into_iter()
            .filter(|pb| pb.failover_in_progress())
            .map(|pb| pb.replication_group_id().to_string())
            .collect();
        for id in running {
            if self.failovers.lock().unwrap().get(&id) == Some(&token.term()) {
                continue;
            }
            tracing::warn!(
                "Detected stale in-progress failover for replication group {id} after master \
                 leader change; marking as failed."
            );
            self.end_failover(
                token,
                &id,
                Err(status(
                    StatusCode::Aborted,
                    "Master leader changed during failover operation",
                )),
            )
            .await?;
        }
        Ok(())
    }
}

fn already_running(id: &str) -> MasterErrorPB {
    master_ts_manager::master_error_coded(
        Code::OBJECT_ALREADY_PRESENT,
        StatusCode::AlreadyPresent,
        &format!("A failover operation is already in progress for replication group {id}"),
    )
}

fn uuid(id: &[u8]) -> String {
    if id.len() != 16 {
        return String::from_utf8_lossy(id).into_owned();
    }
    let hex: String = id.iter().map(|b| format!("{b:02x}")).collect();
    format!(
        "{}-{}-{}-{}-{}",
        &hex[0..8],
        &hex[8..12],
        &hex[12..16],
        &hex[16..20],
        &hex[20..32]
    )
}
