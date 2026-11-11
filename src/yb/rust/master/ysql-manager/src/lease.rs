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

//! YSQL operation leases (K3): `ObjectLockInfoManager::RefreshYsqlLease` and
//! `RelinquishYsqlLease`, and the expiry half of `CleanupExpiredLeaseEpochs`.
//!
//! A tserver hosts PG sessions only while it holds a lease. The lease state of each tserver is the
//! `lease_info` of its OBJECT_LOCK_ENTRY row; when it expires on the master is in memory only. A
//! refresh extends the lease without a write when it is live, for the same tserver process, and
//! the tserver reports the current epoch; anything else grants a new epoch, written in the
//! writer's priority lane, and ships the held DDL locks for the tserver to bootstrap from. A
//! spurious new lease restarts every PG session on that tserver, so the rule is C++'s exactly.

use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, CatalogWriter, Mutation, WriteOptions};
use master_leadership::{LeaderToken, Leadership};
use master_task_runtime::{BoxFuture, Controller, TermScope};
use master_ts_manager::{TsManager, TsState};
use protobuf::Message;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{sys_object_lock_entry_pb, SysObjectLockEntryPB};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::master_ysql_lease::{
    RefreshYsqlLeaseInfoPB, RefreshYsqlLeaseRequestPB, RelinquishYsqlLeaseRequestPB,
};
use yb_proto::pb::tserver::{AcquireObjectLockRequestPB, DdlLockEntriesPB, LeaseEpochFloorPB};

use sys_object_lock_entry_pb::LeaseInfoPB;

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct LeaseOptions {
    /// `master_ysql_operation_lease_ttl_ms`.
    pub ttl: fn() -> Duration,
    /// `ysql_operation_lease_ttl_client_buffer_ms`: the tserver's lease ends this much before the
    /// master's, for clock skew.
    pub client_buffer: fn() -> Duration,
    /// `object_lock_cleanup_interval_ms`.
    pub cleanup_interval: fn() -> Duration,
}

impl Default for LeaseOptions {
    fn default() -> LeaseOptions {
        LeaseOptions {
            ttl: || Duration::from_secs(30),
            client_buffer: || Duration::from_secs(2),
            cleanup_interval: || Duration::from_secs(5),
        }
    }
}

fn invalid(message: impl Into<String>) -> Status {
    Status::new(StatusCode::InvalidArgument, message)
}

fn illegal(message: impl Into<String>) -> Status {
    Status::new(StatusCode::IllegalState, message)
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

/// When each tserver's lease ends on the master, for one term.
struct Deadlines {
    term: i64,
    /// A tserver not here has `term_start + ttl`: on load C++ gives every lease a full TTL.
    term_start: Instant,
    by_tserver: HashMap<Vec<u8>, Instant>,
}

/// The YSQL leases of one master.
pub struct YsqlLeases {
    writer: Arc<CatalogWriter>,
    leadership: Arc<Leadership>,
    ts_manager: Arc<TsManager>,
    options: LeaseOptions,
    deadlines: Mutex<Option<Deadlines>>,
    /// Serializes a tserver's refresh with an expiry of its lease, as C++'s `ObjectLockInfo` write
    /// lock does.
    lease_locks: Mutex<HashMap<Vec<u8>, Arc<tokio::sync::Mutex<()>>>>,
}

/// A tserver's OBJECT_LOCK_ENTRY row, or an empty one.
fn lock_entry(snapshot: &CatalogSnapshot, uuid: &[u8]) -> Result<SysObjectLockEntryPB, Status> {
    snapshot
        .get(SysRowEntryType::OBJECT_LOCK_ENTRY, uuid)
        .map(|e| e.parse().map_err(corrupt))
        .unwrap_or_else(|| Ok(SysObjectLockEntryPB::new()))
}

/// What a refresh decided.
enum Refresh {
    Extend(u64),
    NewLease(u64),
}

impl YsqlLeases {
    /// The leases of this master.
    pub fn new(
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        options: LeaseOptions,
    ) -> Arc<YsqlLeases> {
        Arc::new(YsqlLeases {
            writer,
            leadership,
            ts_manager,
            options,
            deadlines: Mutex::new(None),
            lease_locks: Mutex::default(),
        })
    }

    /// Runs `f` on this term's deadlines, starting them over in a new term.
    fn with_deadlines<T>(&self, term: i64, f: impl FnOnce(&mut Deadlines) -> T) -> T {
        let mut guard = self.deadlines.lock().unwrap();
        if guard.as_ref().is_none_or(|d| d.term != term) {
            *guard = Some(Deadlines {
                term,
                term_start: Instant::now(),
                by_tserver: HashMap::new(),
            });
        }
        f(guard.as_mut().expect("set above"))
    }

    fn deadline(&self, term: i64, uuid: &[u8]) -> Instant {
        let ttl = (self.options.ttl)();
        self.with_deadlines(term, |d| {
            d.by_tserver
                .get(uuid)
                .copied()
                .unwrap_or(d.term_start + ttl)
        })
    }

    fn lease_lock(&self, uuid: &[u8]) -> Arc<tokio::sync::Mutex<()>> {
        self.lease_locks
            .lock()
            .unwrap()
            .entry(uuid.to_vec())
            .or_default()
            .clone()
    }

    /// A response may be lost, so the deadline is only ever extended.
    fn extend_deadline(&self, term: i64, uuid: &[u8], ttl: Duration) {
        let extended = Instant::now() + ttl;
        self.with_deadlines(term, |d| {
            let deadline = d.by_tserver.entry(uuid.to_vec()).or_insert(extended);
            *deadline = (*deadline).max(extended);
        });
    }

    /// `TSManager::LookupTS(instance)`: registered, not replaced, the same process.
    fn lookup_ts(&self, uuid: &[u8], instance_seqno: i64) -> Result<(), Status> {
        let Some(ts) = self.ts_manager.get(uuid) else {
            return Err(Status::new(
                StatusCode::NotFound,
                format!(
                    "Unknown tablet server ID not in map, instance data: permanent_uuid: \"{}\" \
                     instance_seqno: {instance_seqno}",
                    String::from_utf8_lossy(uuid)
                ),
            ));
        };
        if ts.pb.state() == TsState::REPLACED {
            return Err(Status::new(
                StatusCode::NotFound,
                "Trying to lookup replaced tablet server",
            ));
        }
        if ts.pb.instance_seqno() != instance_seqno {
            return Err(Status::new(
                StatusCode::NotFound,
                format!(
                    "mismatched instance sequence number {}, instance seqno {instance_seqno}",
                    ts.pb.instance_seqno()
                ),
            ));
        }
        Ok(())
    }

    /// `RefreshYsqlLease`.
    pub async fn refresh(
        &self,
        token: &LeaderToken,
        req: &RefreshYsqlLeaseRequestPB,
    ) -> Result<RefreshYsqlLeaseInfoPB, Status> {
        if !req.has_local_request_send_time_ms() {
            return Err(invalid("Missing required local_request_send_time_ms"));
        }
        let (ttl, buffer) = ((self.options.ttl)(), (self.options.client_buffer)());
        if ttl <= buffer {
            return Err(illegal(
                "master_ysql_operation_lease_ttl_ms must exceed \
                 ysql_operation_lease_ttl_client_buffer_ms",
            ));
        }
        let mut info = RefreshYsqlLeaseInfoPB::new();
        info.set_lease_expiry_time_ms(
            req.local_request_send_time_ms() + (ttl - buffer).as_millis() as u64,
        );
        let instance = &req.instance;
        let uuid = instance.permanent_uuid().to_vec();
        let seqno = instance.instance_seqno();
        self.lookup_ts(&uuid, seqno)?;
        let term = token.term();
        let current_epoch = req.current_lease_epoch();
        let lock = self.lease_lock(&uuid);
        let _held = lock.lock().await;
        let (decision, snapshot) = self
            .writer
            .mutate_with(token, WriteOptions::default().priority(), |snapshot| {
                let entry = lock_entry(snapshot, &uuid)?;
                let lease = entry.lease_info.clone().unwrap_or_default();
                if seqno < lease.instance_seqno() {
                    return Err(illegal(format!(
                        "Cannot grant lease, instance seqno of requestor {seqno} is lower than \
                         instance seqno of a previously granted lease {}",
                        lease.instance_seqno()
                    )));
                }
                if lease.lease_relinquished() && seqno <= lease.instance_seqno() {
                    return Err(illegal(format!(
                        "Cannot grant lease, lease has been relinquished by instance_seqno {} \
                         already",
                        lease.instance_seqno()
                    )));
                }
                // As C++: before deciding, so an expiry that sees this decision sees the deadline.
                self.extend_deadline(term, &uuid, ttl);
                if lease.live_lease()
                    && lease.instance_seqno() == seqno
                    && current_epoch == lease.lease_epoch()
                {
                    return Ok((Mutation::new(), Refresh::Extend(lease.lease_epoch())));
                }
                let mut entry = entry;
                let lease = entry.lease_info.mut_or_insert_default();
                lease.set_live_lease(true);
                lease.set_lease_epoch(lease.lease_epoch() + 1);
                lease.set_instance_seqno(seqno);
                lease.set_lease_relinquished(false);
                let epoch = lease.lease_epoch();
                let existing = snapshot.get(SysRowEntryType::OBJECT_LOCK_ENTRY, &uuid);
                let mutation = match existing {
                    Some(e) => Mutation::new().read(e),
                    None => Mutation::new().read_absent(SysRowEntryType::OBJECT_LOCK_ENTRY, &uuid),
                }
                .upsert(
                    SysRowEntryType::OBJECT_LOCK_ENTRY,
                    &uuid,
                    entry
                        .write_to_bytes()
                        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?,
                );
                Ok((mutation, Refresh::NewLease(epoch)))
            })
            .await?;
        match decision {
            Refresh::Extend(epoch) => {
                info.set_new_lease(false);
                info.set_lease_epoch(epoch);
            }
            Refresh::NewLease(epoch) => {
                info.set_new_lease(true);
                info.set_lease_epoch(epoch);
                info.ddl_lock_entries = Some(export_for_tserver(&snapshot)?).into();
                tracing::info!(
                    "Granting a new ysql op lease to TS {} ({seqno}). Lease epoch {epoch}",
                    String::from_utf8_lossy(&uuid)
                );
            }
        }
        Ok(info)
    }

    /// `RelinquishYsqlLease`: a tserver shutting down gives its lease up.
    pub async fn relinquish(
        &self,
        token: &LeaderToken,
        req: &RelinquishYsqlLeaseRequestPB,
    ) -> Result<(), Status> {
        let uuid = req.instance.permanent_uuid().to_vec();
        let seqno = req.instance.instance_seqno();
        self.writer
            .mutate(token, |snapshot| {
                let mut entry = lock_entry(snapshot, &uuid)?;
                let lease = entry.lease_info.mut_or_insert_default();
                if lease.instance_seqno() > seqno {
                    return Err(invalid(format!(
                        "Relinquish lease request for a replaced tserver instance. Instance in \
                         request {seqno}, existing lease instance {}",
                        lease.instance_seqno()
                    )));
                }
                lease.set_lease_relinquished(true);
                lease.set_live_lease(false);
                let mutation = match snapshot.get(SysRowEntryType::OBJECT_LOCK_ENTRY, &uuid) {
                    Some(e) => Mutation::new().read(e),
                    None => Mutation::new().read_absent(SysRowEntryType::OBJECT_LOCK_ENTRY, &uuid),
                }
                .upsert(
                    SysRowEntryType::OBJECT_LOCK_ENTRY,
                    &uuid,
                    entry
                        .write_to_bytes()
                        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?,
                );
                Ok((mutation, ()))
            })
            .await
            .map(|_| ())
    }

    /// The expiry half of `CleanupExpiredLeaseEpochs`: live leases past their deadline become
    /// not live, in one write. Releasing the DDL locks of expired epochs is not done yet.
    pub async fn expire(&self, token: &LeaderToken) -> Result<Vec<Vec<u8>>, Status> {
        let term = token.term();
        let now = Instant::now();
        // The leases that look expired, as C++ finds them under read locks.
        let (candidates, _) = self
            .writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut ids = BTreeSet::new();
                for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
                    let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
                    if entry.lease_info.as_ref().is_some_and(|l| l.live_lease())
                        && now > self.deadline(term, &row.key.id)
                    {
                        ids.insert(row.key.id.to_vec());
                    }
                }
                Ok((Mutation::new(), ids))
            })
            .await?;
        if candidates.is_empty() {
            return Ok(Vec::new());
        }
        // Held across the re-check and the write, as C++ holds their write locks; in key order.
        let mut held = Vec::with_capacity(candidates.len());
        for id in &candidates {
            held.push(self.lease_lock(id).lock_owned().await);
        }
        let (expired, _) = self
            .writer
            .mutate_with(token, WriteOptions::default().priority(), |snapshot| {
                let mut mutation = Mutation::new();
                let mut expired = Vec::new();
                for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
                    if !candidates.contains(&row.key.id[..]) {
                        continue;
                    }
                    let mut entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
                    let lease = entry.lease_info.mut_or_insert_default();
                    if !lease.live_lease() || now <= self.deadline(term, &row.key.id) {
                        continue;
                    }
                    tracing::info!(
                        "Tserver {}, instance seqno {} with ysql lease epoch {} has just lost its \
                         lease",
                        String::from_utf8_lossy(&row.key.id),
                        lease.instance_seqno(),
                        lease.lease_epoch()
                    );
                    lease.set_live_lease(false);
                    if !entry.lease_epochs.is_empty() {
                        tracing::warn!(
                            "Tserver {} lost its ysql lease holding DDL locks; a Rust leader does \
                             not release them yet",
                            String::from_utf8_lossy(&row.key.id)
                        );
                    }
                    mutation = mutation.read(row).upsert(
                        SysRowEntryType::OBJECT_LOCK_ENTRY,
                        &row.key.id,
                        entry
                            .write_to_bytes()
                            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?,
                    );
                    expired.push(row.key.id.to_vec());
                }
                Ok((mutation, expired))
            })
            .await?;
        drop(held);
        Ok(expired)
    }
}

/// A `TransactionId::ToString` (a UUID's hex digits and dashes) as its 16 bytes.
fn txn_id_bytes(s: &str) -> Option<Vec<u8>> {
    let hex: Vec<u8> = s.bytes().filter(|b| *b != b'-').collect();
    if hex.len() != 32 {
        return None;
    }
    hex.chunks(2)
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).ok()?, 16).ok())
        .collect()
}

#[cfg(test)]
pub(crate) fn tests_txn_bytes(s: &str) -> Vec<u8> {
    txn_id_bytes(s).expect("a transaction id")
}

/// `AddLeaseEpochFloor`.
fn lease_epoch_floor(uuid: &[u8], lease: &LeaseInfoPB) -> Option<LeaseEpochFloorPB> {
    if !lease.has_lease_epoch() {
        return None;
    }
    let mut before = lease.lease_epoch();
    if !lease.live_lease() && before != u64::MAX {
        before += 1;
    }
    if before == 0 {
        return None;
    }
    let mut floor = LeaseEpochFloorPB::new();
    floor.set_session_host_uuid(uuid.to_vec());
    floor.set_ignore_lease_epochs_before(before);
    Some(floor)
}

/// `ExportObjectLockInfoForTServer`: the DDL locks held under live leases' current epochs, less
/// releases in progress (they will go through), and every tserver's lease epoch floor.
pub fn export_for_tserver(snapshot: &CatalogSnapshot) -> Result<DdlLockEntriesPB, Status> {
    let mut rows = Vec::new();
    for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
        let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
        rows.push((row.key.id.to_vec(), entry));
    }
    // Releases in progress: by transaction, the subtransactions (0 for the whole transaction).
    let mut releasing: HashMap<Vec<u8>, BTreeSet<u32>> = HashMap::new();
    for (_, entry) in &rows {
        for release in entry.in_progress_release_request.values() {
            releasing
                .entry(release.txn_id().to_vec())
                .or_default()
                .insert(if release.has_subtxn_id() {
                    release.subtxn_id()
                } else {
                    0
                });
        }
    }
    let mut entries = DdlLockEntriesPB::new();
    for (uuid, entry) in &rows {
        let lease = entry.lease_info.clone().unwrap_or_default();
        if !lease.has_lease_epoch() || !lease.live_lease() {
            continue;
        }
        let Some(txns) = entry.lease_epochs.get(&lease.lease_epoch()) else {
            continue;
        };
        // Sorted, so the export does not depend on map order.
        let txns: BTreeMap<_, _> = txns.transactions.iter().collect();
        for (txn_str, subtxns) in txns {
            let txn_id = txn_id_bytes(txn_str)
                .ok_or_else(|| corrupt(format!("Bad transaction id {txn_str}")))?;
            let released = releasing.get(&txn_id);
            if released.is_some_and(|r| r.contains(&0)) {
                continue;
            }
            let by_subtxn: BTreeMap<_, _> = subtxns.subtxns.iter().collect();
            for (subtxn, locks) in by_subtxn {
                if released.is_some_and(|r| r.contains(subtxn)) {
                    continue;
                }
                let mut lock = AcquireObjectLockRequestPB::new();
                lock.set_session_host_uuid(uuid.clone());
                lock.set_txn_id(txn_id.clone());
                lock.set_status_tablet(subtxns.status_tablet().to_vec());
                lock.set_subtxn_id(*subtxn);
                lock.object_locks = locks.locks.clone();
                if subtxns.has_background_transaction_id() {
                    lock.set_background_transaction_id(
                        subtxns.background_transaction_id().to_vec(),
                    );
                }
                entries.lock_entries.push(lock);
            }
        }
    }
    for (uuid, entry) in &rows {
        if let Some(floor) = lease_epoch_floor(uuid, &entry.lease_info.clone().unwrap_or_default())
        {
            entries.lease_epoch_floors.push(floor);
        }
    }
    Ok(entries)
}

/// The `ysql-lease-expiry` controller for one term.
pub struct LeaseExpiryController {
    leases: Arc<YsqlLeases>,
}

impl LeaseExpiryController {
    /// A controller for the term that just began.
    pub fn new(leases: Arc<YsqlLeases>) -> Arc<LeaseExpiryController> {
        Arc::new(LeaseExpiryController { leases })
    }
}

impl Controller for LeaseExpiryController {
    fn name(&self) -> &'static str {
        "ysql-lease-expiry"
    }

    fn interval(&self) -> Duration {
        (self.leases.options.cleanup_interval)()
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let Ok(token) = self.leases.leadership.token() else {
                return;
            };
            if let Err(status) = self.leases.expire(&token).await {
                tracing::warn!("Failed to mark expired ysql leases: {status}");
            }
        })
    }
}
