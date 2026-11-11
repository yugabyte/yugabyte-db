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

//! Global DDL object locks (K3): C++'s `ObjectLockInfoManager` less the leases (`lease.rs`).
//!
//! A DDL's lock is taken on the master first, in the host's `TSLocalLockManager`, which holds only
//! DDL locks: a conflicting DDL waits there. Then it is persisted in the requesting tserver's
//! OBJECT_LOCK_ENTRY row, so a tserver granted a lease later bootstraps with it, and then it is
//! sent to every tserver with a live lease. A release is persisted as in progress first, sent to
//! every tserver, and then removed from the row and the master's lock manager; a new leader
//! relaunches the releases in progress. Locks of a lease epoch that ended are released by the
//! master, since the tserver that held them is gone or restarted.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use master_catalog_state::{Catalog, CatalogSnapshot, CatalogWriter, Mutation, WriteOptions};
use master_leadership::{LeaderToken, Leadership};
use master_task_runtime::{Attempt, BoxFuture, RetryPolicy, TsRpcExecutor};
use master_ts_manager::TsManager;
use protobuf::{Enum, Message};
use tokio::task::JoinSet;
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysObjectLockEntryPB;
use yb_proto::pb::master_ddl::{
    AcquireObjectLocksGlobalRequestPB, ReleaseObjectLocksGlobalRequestPB,
    WaitForLockersMultipleGlobalRequestPB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tserver::{
    AcquireObjectLockRequestPB, AcquireObjectLockResponsePB, DdlLockEntriesPB,
    ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB, WaitForLockersMultipleRequestPB,
    WaitForLockersMultipleResponsePB,
};
use yb_proto::pb::tserver_types::{tablet_server_error_pb, TabletServerErrorPB};

use crate::lease::{lease_epoch_floor, txn_id_bytes};

/// The master's own lock manager, in the host (`TSLocalLockManager`).
pub trait LocalLockManager: Send + Sync {
    /// Waits for the locks or the timeout (`AcquireObjectLocksAsync`).
    fn acquire(
        &self,
        req: &AcquireObjectLockRequestPB,
        timeout: Duration,
    ) -> BoxFuture<'static, Result<(), Status>>;
    /// `ReleaseObjectLocks`.
    fn release(&self, req: &ReleaseObjectLockRequestPB, timeout: Duration) -> Result<(), Status>;
    /// Starts a new lock manager holding `entries` (a new leader term's).
    fn reset(&self, entries: &DdlLockEntriesPB) -> Result<(), Status>;
}

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct LockOptions {
    /// `master_ts_rpc_timeout_ms`: one RPC to a tserver.
    pub rpc_timeout: fn() -> Duration,
}

impl Default for LockOptions {
    fn default() -> LockOptions {
        LockOptions {
            rpc_timeout: || Duration::from_secs(30),
        }
    }
}

/// How long a release fans out for (`kTserverRpcsTimeoutDefaultSecs`); it is retried by the next
/// leader if it does not finish.
const RELEASE_TIMEOUT: Duration = Duration::from_secs(60);

const NOT_THE_LEADER: &str = "Master is not the leader";

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

fn internal(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::InternalError, e.to_string())
}

/// `TransactionId::ToString`: the UUID's lowercase hex digits with dashes.
pub(crate) fn txn_id_string(bytes: &[u8]) -> Result<String, Status> {
    if bytes.len() != 16 {
        return Err(Status::new(
            StatusCode::InvalidArgument,
            format!("Bad transaction id of {} bytes", bytes.len()),
        ));
    }
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    Ok(format!(
        "{}-{}-{}-{}-{}",
        &hex[0..8],
        &hex[8..12],
        &hex[12..16],
        &hex[16..20],
        &hex[20..32]
    ))
}

fn lock_entry(snapshot: &CatalogSnapshot, uuid: &[u8]) -> Result<SysObjectLockEntryPB, Status> {
    snapshot
        .get(SysRowEntryType::OBJECT_LOCK_ENTRY, uuid)
        .map(|e| e.parse().map_err(corrupt))
        .unwrap_or_else(|| Ok(SysObjectLockEntryPB::new()))
}

/// The tservers with a live lease and its epoch, in uuid order.
fn live_leases(snapshot: &CatalogSnapshot) -> Result<Vec<(Vec<u8>, u64)>, Status> {
    let mut live = Vec::new();
    for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
        let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
        if let Some(lease) = entry.lease_info.as_ref().filter(|l| l.live_lease()) {
            live.push((row.key.id.to_vec(), lease.lease_epoch()));
        }
    }
    Ok(live)
}

/// A tserver's latest lease epoch, live or not (`GetLeaseEpoch`).
fn latest_lease_epoch(snapshot: &CatalogSnapshot, uuid: &[u8]) -> Result<Option<u64>, Status> {
    Ok(lock_entry(snapshot, uuid)?
        .lease_info
        .as_ref()
        .filter(|l| l.has_lease_epoch())
        .map(|l| l.lease_epoch()))
}

fn upsert_entry(
    snapshot: &CatalogSnapshot,
    uuid: &[u8],
    entry: &SysObjectLockEntryPB,
) -> Result<Mutation, Status> {
    let mutation = match snapshot.get(SysRowEntryType::OBJECT_LOCK_ENTRY, uuid) {
        Some(e) => Mutation::new().read(e),
        None => Mutation::new().read_absent(SysRowEntryType::OBJECT_LOCK_ENTRY, uuid),
    };
    Ok(mutation.upsert(
        SysRowEntryType::OBJECT_LOCK_ENTRY,
        uuid,
        entry.write_to_bytes().map_err(internal)?,
    ))
}

/// `ExportObjectLockInfoForMaster`: every persisted lock, also those of ended epochs and those
/// being released, which the releases relaunched in this term remove.
pub fn export_for_master(snapshot: &CatalogSnapshot) -> Result<DdlLockEntriesPB, Status> {
    let mut entries = DdlLockEntriesPB::new();
    for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
        let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
        let lease = entry.lease_info.clone().unwrap_or_default();
        if !lease.has_lease_epoch() {
            continue;
        }
        let epochs: BTreeMap<_, _> = entry.lease_epochs.iter().collect();
        for txns in epochs.values() {
            let txns: BTreeMap<_, _> = txns.transactions.iter().collect();
            for (txn_str, subtxns) in txns {
                let txn_id = txn_id_bytes(txn_str)
                    .ok_or_else(|| corrupt(format!("Bad transaction id {txn_str}")))?;
                let by_subtxn: BTreeMap<_, _> = subtxns.subtxns.iter().collect();
                for (subtxn, locks) in by_subtxn {
                    let mut lock = AcquireObjectLockRequestPB::new();
                    lock.set_session_host_uuid(row.key.id.to_vec());
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
        if let Some(floor) = lease_epoch_floor(&row.key.id, &lease) {
            entries.lease_epoch_floors.push(floor);
        }
    }
    Ok(entries)
}

/// `ReleaseRequestToPersist`: without the catalog versions, which are filled when sent.
fn release_to_persist(req: &ReleaseObjectLockRequestPB) -> ReleaseObjectLockRequestPB {
    let mut persisted = req.clone();
    persisted.db_catalog_version_data = Default::default();
    persisted.db_catalog_inval_messages_data = Default::default();
    persisted.clear_recipient_lease_epoch();
    persisted
}

/// A request one tserver gets in a fan-out.
trait FanOutRequest: Message + Clone + Send + Sync + 'static {
    type Response: Message;
    const METHOD: &'static str;
    fn set_recipient_lease_epoch(&mut self, epoch: u64);
    fn error(resp: &Self::Response) -> Option<&TabletServerErrorPB>;
}

impl FanOutRequest for AcquireObjectLockRequestPB {
    type Response = AcquireObjectLockResponsePB;
    const METHOD: &'static str = "AcquireObjectLocks";
    fn set_recipient_lease_epoch(&mut self, epoch: u64) {
        AcquireObjectLockRequestPB::set_recipient_lease_epoch(self, epoch);
    }
    fn error(resp: &Self::Response) -> Option<&TabletServerErrorPB> {
        resp.error.as_ref()
    }
}

impl FanOutRequest for ReleaseObjectLockRequestPB {
    type Response = ReleaseObjectLockResponsePB;
    const METHOD: &'static str = "ReleaseObjectLocks";
    fn set_recipient_lease_epoch(&mut self, epoch: u64) {
        ReleaseObjectLockRequestPB::set_recipient_lease_epoch(self, epoch);
    }
    fn error(resp: &Self::Response) -> Option<&TabletServerErrorPB> {
        resp.error.as_ref()
    }
}

impl FanOutRequest for WaitForLockersMultipleRequestPB {
    type Response = WaitForLockersMultipleResponsePB;
    const METHOD: &'static str = "WaitForLockersMultiple";
    fn set_recipient_lease_epoch(&mut self, epoch: u64) {
        WaitForLockersMultipleRequestPB::set_recipient_lease_epoch(self, epoch);
    }
    fn error(resp: &Self::Response) -> Option<&TabletServerErrorPB> {
        resp.error.as_ref()
    }
}

/// A transaction's tserver and lease epoch.
type TxnHost = (Vec<u8>, u64);

/// The global DDL locks of one master.
pub struct ObjectLocks {
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    leadership: Arc<Leadership>,
    ts_manager: Arc<TsManager>,
    local: Arc<dyn LocalLockManager>,
    executor: Arc<TsRpcExecutor>,
    rpc: OnceLock<Arc<dyn RpcClient>>,
    options: LockOptions,
    next_request_id: AtomicU64,
    /// Which tserver and lease epoch each transaction holding locks came from (`txn_host_info_map_`),
    /// for releases that name only the transaction.
    txn_hosts: Mutex<HashMap<Vec<u8>, TxnHost>>,
    /// Tservers whose ended lease epochs' locks are being released.
    cleanups: Mutex<HashSet<Vec<u8>>>,
    /// The term whose in-progress releases were relaunched.
    relaunched_term: Mutex<i64>,
}

impl ObjectLocks {
    /// The locks of this master.
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        local: Arc<dyn LocalLockManager>,
        executor: Arc<TsRpcExecutor>,
        options: LockOptions,
    ) -> Arc<ObjectLocks> {
        Arc::new(ObjectLocks {
            catalog,
            writer,
            leadership,
            ts_manager,
            local,
            executor,
            rpc: OnceLock::new(),
            options,
            next_request_id: AtomicU64::new(1),
            txn_hosts: Mutex::default(),
            cleanups: Mutex::default(),
            relaunched_term: Mutex::new(-1),
        })
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    fn rpc(&self) -> Result<Arc<dyn RpcClient>, Status> {
        self.rpc
            .get()
            .cloned()
            .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No RPC client yet"))
    }

    fn check_leader(&self, token: &LeaderToken) -> Result<(), Status> {
        self.leadership
            .check(token)
            .map_err(|_| Status::new(StatusCode::IllegalState, NOT_THE_LEADER))
    }

    /// A new term's start, before it is ready (`BootstrapLocksPostLoad`, `UpdateObjectLocks`):
    /// the master's lock manager starts over with every persisted lock, and the transactions'
    /// hosts and the next request id are read back.
    pub fn on_term_start(&self, snapshot: &CatalogSnapshot) -> Result<(), Status> {
        self.local.reset(&export_for_master(snapshot)?)?;
        let mut hosts = HashMap::new();
        let mut max_request_id = 0;
        for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
            let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
            for (epoch, txns) in &entry.lease_epochs {
                for txn_str in txns.transactions.keys() {
                    if let Some(txn) = txn_id_bytes(txn_str) {
                        hosts.insert(txn, (row.key.id.to_vec(), *epoch));
                    }
                }
            }
            for request in entry.in_progress_release_request.values() {
                max_request_id = max_request_id.max(request.request_id());
            }
        }
        *self.txn_hosts.lock().unwrap() = hosts;
        self.next_request_id
            .fetch_max(max_request_id + 1, Ordering::SeqCst);
        self.cleanups.lock().unwrap().clear();
        Ok(())
    }

    fn next_request_id(&self) -> u64 {
        self.next_request_id.fetch_add(1, Ordering::SeqCst)
    }

    /// `AcquireObjectLocksGlobal`.
    pub async fn acquire_global(
        self: &Arc<Self>,
        token: &LeaderToken,
        master_req: &AcquireObjectLocksGlobalRequestPB,
        deadline: Instant,
    ) -> Result<(), Status> {
        let mut req = AcquireObjectLockRequestPB::new();
        req.set_txn_id(master_req.txn_id().to_vec());
        req.set_subtxn_id(master_req.subtxn_id());
        req.set_session_host_uuid(master_req.session_host_uuid().to_vec());
        req.object_locks = master_req.object_locks.clone();
        req.set_lease_epoch(master_req.lease_epoch());
        if master_req.has_ignore_after_hybrid_time() {
            req.set_ignore_after_hybrid_time(master_req.ignore_after_hybrid_time());
        }
        if master_req.has_propagated_hybrid_time() {
            req.set_propagated_hybrid_time(master_req.propagated_hybrid_time());
        }
        req.set_status_tablet(master_req.status_tablet().to_vec());
        if master_req.has_background_transaction_id() {
            req.set_background_transaction_id(master_req.background_transaction_id().to_vec());
        }
        let txn_str = txn_id_string(req.txn_id())?;
        // ValidateLockRequest.
        if req.subtxn_id() == 0 {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                "subtxn_id not set for exclusive object lock req",
            ));
        }
        let snapshot = self.catalog.snapshot();
        let latest = latest_lease_epoch(&snapshot, req.session_host_uuid())?;
        if latest != Some(req.lease_epoch()) {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                format!(
                    "Requestor has a lease epoch of {} but the latest valid lease epoch for this \
                     tserver is {latest:?}",
                    req.lease_epoch()
                ),
            ));
        }
        self.check_leader(token)?;

        let timeout = deadline.saturating_duration_since(Instant::now());
        self.local.acquire(&req, timeout).await?;

        // PersistRequest.
        let host = req.session_host_uuid().to_vec();
        let epoch = req.lease_epoch();
        self.txn_hosts
            .lock()
            .unwrap()
            .insert(req.txn_id().to_vec(), (host.clone(), epoch));
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut entry = lock_entry(snapshot, &host)?;
                let txns = entry.lease_epochs.entry(epoch).or_default();
                let subtxns = txns.transactions.entry(txn_str.clone()).or_default();
                subtxns.set_status_tablet(req.status_tablet().to_vec());
                if req.has_background_transaction_id() {
                    subtxns.set_background_transaction_id(req.background_transaction_id().to_vec());
                }
                subtxns
                    .subtxns
                    .entry(req.subtxn_id())
                    .or_default()
                    .locks
                    .extend(req.object_locks.iter().cloned());
                Ok((upsert_entry(snapshot, &host, &entry)?, ()))
            })
            .await?;

        // UpdateAllTServers<AcquireObjectLockRequestPB>: to every tserver with a live lease, with
        // every tserver's lease epoch floor. A tserver whose lease changed during the fan-out
        // never acked, so the others still hold its old floor: relaunch to all with fresh floors.
        let mut tracked: Option<Vec<(Vec<u8>, u64)>> = None;
        loop {
            let snapshot = self.catalog.snapshot();
            let live = live_leases(&snapshot)?;
            if !live.iter().any(|(uuid, e)| *uuid == host && *e == epoch) {
                return Err(Status::new(
                    StatusCode::InvalidArgument,
                    format!(
                        "Requestor {} no longer has a live lease with lease epoch {epoch}",
                        String::from_utf8_lossy(&host)
                    ),
                ));
            }
            let mut fanned = req.clone();
            fanned.lease_epoch_floors = export_floors(&snapshot)?;
            self.fan_out(token, fanned, &live, deadline).await?;
            self.check_leader(token)?;
            let tracked = tracked.get_or_insert_with(|| live.clone());
            let now_live = live_leases(&self.catalog.snapshot())?;
            let before = tracked.len();
            tracked.retain(|entry| now_live.contains(entry));
            if tracked.len() == before {
                return Ok(());
            }
            tracing::info!(
                "YSQL leases changed during the object lock fanout of txn {txn_str}; relaunching \
                 with fresh lease epoch floors"
            );
        }
    }

    /// `ReleaseObjectLocksGlobal`. Answers once the release is persisted, unless
    /// `wait_for_completion` (yb-admin) asks to wait for it.
    pub async fn release_global(
        self: &Arc<Self>,
        token: &LeaderToken,
        master_req: &ReleaseObjectLocksGlobalRequestPB,
        deadline: Instant,
    ) -> Result<(), Status> {
        let mut req = ReleaseObjectLockRequestPB::new();
        req.set_txn_id(master_req.txn_id().to_vec());
        if master_req.has_subtxn_id() {
            req.set_subtxn_id(master_req.subtxn_id());
        }
        req.set_session_host_uuid(master_req.session_host_uuid().to_vec());
        req.set_lease_epoch(master_req.lease_epoch());
        if master_req.has_apply_after_hybrid_time() {
            req.set_apply_after_hybrid_time(master_req.apply_after_hybrid_time());
        }
        if master_req.has_propagated_hybrid_time() {
            req.set_propagated_hybrid_time(master_req.propagated_hybrid_time());
        }
        req.set_request_id(self.next_request_id());
        req.object_locks = master_req.object_locks.clone();
        let release = self.start_release(token, req).await?;
        if master_req.wait_for_completion() {
            return tokio::time::timeout_at(deadline, release)
                .await
                .map_err(|_| {
                    Status::new(
                        StatusCode::TimedOut,
                        "Timed out waiting for unlock object to finish",
                    )
                })?
                .unwrap_or_else(|e| Err(internal(e)));
        }
        Ok(())
    }

    /// `UnlockObject` up to the launch: the request gets its host if it names only the
    /// transaction, and is persisted as in progress; the fan-out and the rest run in the
    /// returned task.
    async fn start_release(
        self: &Arc<Self>,
        token: &LeaderToken,
        mut req: ReleaseObjectLockRequestPB,
    ) -> Result<tokio::task::JoinHandle<Result<(), Status>>, Status> {
        if req.session_host_uuid().is_empty() {
            // Released through yb-admin by transaction id alone (`PopulateHostInfo`).
            let hosts = self.txn_hosts.lock().unwrap();
            let (host, epoch) = hosts.get(req.txn_id()).ok_or_else(|| {
                Status::new(
                    StatusCode::NotFound,
                    "Transaction not found in host session map",
                )
            })?;
            req.set_session_host_uuid(host.clone());
            req.set_lease_epoch(*epoch);
        }
        txn_id_string(req.txn_id())?;
        self.check_leader(token)?;
        // AddToInProgress: a request id already there is a relaunch.
        let host = req.session_host_uuid().to_vec();
        let persisted = release_to_persist(&req);
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut entry = lock_entry(snapshot, &host)?;
                if entry
                    .in_progress_release_request
                    .contains_key(&req.request_id())
                {
                    return Ok((Mutation::new(), ()));
                }
                entry
                    .in_progress_release_request
                    .insert(req.request_id(), persisted.clone());
                Ok((upsert_entry(snapshot, &host, &entry)?, ()))
            })
            .await?;
        let locks = self.clone();
        let token = *token;
        Ok(tokio::spawn(async move {
            let result = locks.finish_release(&token, req).await;
            if let Err(status) = &result {
                tracing::warn!(
                    "Object lock release failed; it was persisted and will be retried upon \
                     master failover: {status}"
                );
            }
            result
        }))
    }

    /// The rest of `UnlockObject`: the fan-out, then the row and the master's lock manager.
    async fn finish_release(
        &self,
        token: &LeaderToken,
        req: ReleaseObjectLockRequestPB,
    ) -> Result<(), Status> {
        let deadline = Instant::now() + RELEASE_TIMEOUT;
        let live = live_leases(&self.catalog.snapshot())?;
        self.fan_out(token, req.clone(), &live, deadline).await?;
        self.check_leader(token)?;
        // PersistRequest for a release.
        let host = req.session_host_uuid().to_vec();
        let txn_str = txn_id_string(req.txn_id())?;
        let erase_txn = req.subtxn_id() == 0 && req.object_locks.is_empty();
        if erase_txn {
            self.txn_hosts.lock().unwrap().remove(req.txn_id());
        }
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut entry = lock_entry(snapshot, &host)?;
                let txns = entry.lease_epochs.entry(req.lease_epoch()).or_default();
                if erase_txn {
                    txns.transactions.remove(&txn_str);
                } else if req.subtxn_id() != 0 {
                    txns.transactions
                        .entry(txn_str.clone())
                        .or_default()
                        .subtxns
                        .remove(&req.subtxn_id());
                }
                if txns.transactions.is_empty() {
                    entry.lease_epochs.remove(&req.lease_epoch());
                }
                entry.in_progress_release_request.remove(&req.request_id());
                Ok((upsert_entry(snapshot, &host, &entry)?, ()))
            })
            .await?;
        self.local.release(&req, RELEASE_TIMEOUT)
    }

    /// `WaitForLockersMultipleGlobal`: waits until no transaction on any tserver holds a lock
    /// conflicting with these.
    pub async fn wait_for_lockers_global(
        &self,
        token: &LeaderToken,
        master_req: &WaitForLockersMultipleGlobalRequestPB,
        deadline: Instant,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let latest = latest_lease_epoch(&snapshot, master_req.session_host_uuid())?;
        if latest != Some(master_req.lease_epoch()) {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                format!(
                    "Requestor has a lease epoch of {} but the latest valid lease epoch for this \
                     tserver is {latest:?}",
                    master_req.lease_epoch()
                ),
            ));
        }
        self.check_leader(token)?;
        let mut req = WaitForLockersMultipleRequestPB::new();
        req.set_session_host_uuid(master_req.session_host_uuid().to_vec());
        req.set_lease_epoch(master_req.lease_epoch());
        req.object_locks = master_req.object_locks.clone();
        if master_req.has_propagated_hybrid_time() {
            req.set_propagated_hybrid_time(master_req.propagated_hybrid_time());
        }
        if master_req.has_background_transaction_id() {
            req.set_background_transaction_id(master_req.background_transaction_id().to_vec());
            req.set_background_transaction_status_tablet(
                master_req.background_transaction_status_tablet().to_vec(),
            );
        }
        let live = live_leases(&snapshot)?;
        self.fan_out(token, req, &live, deadline).await?;
        self.check_leader(token)
    }

    /// Relaunches the releases a previous leader persisted (`RelaunchInProgressRequests`), once
    /// per term, and releases the locks of lease epochs that ended (the rest of
    /// `CleanupExpiredLeaseEpochs`). Run by the lease expiry controller.
    pub async fn release_ended(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let relaunch = {
            let mut term = self.relaunched_term.lock().unwrap();
            let first = *term != token.term();
            *term = token.term();
            first
        };
        if relaunch {
            for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
                let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
                let requests: BTreeMap<_, _> = entry.in_progress_release_request.iter().collect();
                for request in requests.into_values() {
                    if let Err(status) = self.start_release(token, request.clone()).await {
                        tracing::warn!("Failed to relaunch an object lock release: {status}");
                    }
                }
            }
        }
        for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
            let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
            let lease = entry.lease_info.clone().unwrap_or_default();
            let keep_from = if lease.live_lease() {
                lease.lease_epoch()
            } else {
                u64::MAX
            };
            let Some(max_to_clean) = entry
                .lease_epochs
                .keys()
                .copied()
                .filter(|e| *e < keep_from)
                .max()
            else {
                continue;
            };
            let uuid = row.key.id.to_vec();
            if !self.cleanups.lock().unwrap().insert(uuid.clone()) {
                continue;
            }
            // ReleaseLocksHeldByExpiredLeaseEpoch: every transaction of the ended epochs.
            let mut handles = Vec::new();
            let epochs: BTreeMap<_, _> = entry.lease_epochs.iter().collect();
            for (epoch, txns) in epochs {
                if *epoch > max_to_clean {
                    continue;
                }
                let txns: BTreeMap<_, _> = txns.transactions.iter().collect();
                for txn_str in txns.into_keys() {
                    let Some(txn_id) = txn_id_bytes(txn_str) else {
                        continue;
                    };
                    let mut req = ReleaseObjectLockRequestPB::new();
                    req.set_session_host_uuid(uuid.clone());
                    req.set_txn_id(txn_id);
                    req.set_lease_epoch(*epoch);
                    req.set_request_id(self.next_request_id());
                    req.set_populate_db_catalog_info(handles.is_empty());
                    req.set_ignore_lease_epochs_before(max_to_clean + 1);
                    match self.start_release(token, req).await {
                        Ok(handle) => handles.push(handle),
                        Err(status) => {
                            tracing::warn!("Failed to release an ended lease's locks: {status}")
                        }
                    }
                }
            }
            tracing::info!(
                "Releasing the object locks of {} transactions of tserver {} up to lease epoch \
                 {max_to_clean}",
                handles.len(),
                String::from_utf8_lossy(&uuid)
            );
            let locks = self.clone();
            tokio::spawn(async move {
                for handle in handles {
                    let _ = handle.await;
                }
                locks.cleanups.lock().unwrap().remove(&uuid);
            });
        }
        Ok(())
    }

    /// `UpdateAllTServers`: `req` to each of `targets` (tserver, lease epoch) at once, each
    /// retried until it succeeds, fails for good, or the tserver no longer has that live lease.
    async fn fan_out<R: FanOutRequest>(
        &self,
        token: &LeaderToken,
        req: R,
        targets: &[(Vec<u8>, u64)],
        deadline: Instant,
    ) -> Result<(), Status> {
        let rpc = self.rpc()?;
        let mut calls = JoinSet::new();
        for (uuid, epoch) in targets {
            let Some((host, port)) = self.ts_manager.rpc_address(uuid) else {
                // Leases are granted only to registered tservers; one that registered after this
                // read is bootstrapped with the persisted lock.
                tracing::warn!(
                    "TServer {} has a live YSQL lease but no tserver descriptor, skipping it in \
                     the object lock fanout",
                    String::from_utf8_lossy(uuid)
                );
                continue;
            };
            let mut req = req.clone();
            req.set_recipient_lease_epoch(*epoch);
            let bytes = req.write_to_bytes().map_err(internal)?;
            let (uuid, epoch) = (uuid.clone(), *epoch);
            let (rpc, executor, catalog, leadership) = (
                rpc.clone(),
                self.executor.clone(),
                self.catalog.clone(),
                self.leadership.clone(),
            );
            let rpc_timeout = (self.options.rpc_timeout)();
            let token = *token;
            calls.spawn(async move {
                let policy = RetryPolicy {
                    max_attempts: u32::MAX,
                    deadline: deadline.saturating_duration_since(Instant::now()),
                    ..RetryPolicy::default()
                };
                let still_live = |catalog: &Catalog| {
                    live_leases(&catalog.snapshot())
                        .is_ok_and(|live| live.contains(&(uuid.clone(), epoch)))
                };
                executor
                    .run(&uuid, R::METHOD, &policy, |_| {
                        let (rpc, bytes, host) = (rpc.clone(), bytes.clone(), host.clone());
                        let leader = leadership.check(&token);
                        let live = still_live(&catalog);
                        let timeout =
                            rpc_timeout.min(deadline.saturating_duration_since(Instant::now()));
                        async move {
                            leader.map_err(|_| {
                                Attempt::Fail(Status::new(StatusCode::IllegalState, NOT_THE_LEADER))
                            })?;
                            if !live {
                                // UpdateAllTServers::Done ignores a tserver without a live lease.
                                return Ok(());
                            }
                            let resp = rpc
                                .call(
                                    &host,
                                    port,
                                    "yb.tserver.TabletServerService",
                                    R::METHOD,
                                    &bytes,
                                    timeout,
                                )
                                .await
                                .map_err(Attempt::Retry)?;
                            let resp = R::Response::parse_from_bytes(&resp)
                                .map_err(|e| Attempt::Retry(corrupt(e)))?;
                            let Some(error) = R::error(&resp) else {
                                return Ok(());
                            };
                            let status = Status::new(
                                StatusCode::from_i32(error.status.code().value()),
                                error.status.message().to_string(),
                            );
                            // A lease change fails outstanding requests with ShutdownInProgress,
                            // and a tserver that restarted or raced a new lease answers
                            // INVALID_YSQL_LEASE: retry both.
                            if status.code == StatusCode::ShutdownInProgress
                                || error.code() == tablet_server_error_pb::Code::INVALID_YSQL_LEASE
                            {
                                return Err(Attempt::Retry(status));
                            }
                            Err(Attempt::Fail(status))
                        }
                    })
                    .await
                    .or_else(|status| {
                        if still_live(&catalog) {
                            Err(status)
                        } else {
                            Ok(())
                        }
                    })
            });
        }
        let mut first_error = None;
        while let Some(result) = calls.join_next().await {
            let result = result.unwrap_or_else(|e| Err(internal(e)));
            if let Err(status) = result {
                first_error.get_or_insert(status);
            }
        }
        match first_error {
            Some(status) => {
                tracing::warn!(
                    "Error from a tserver for a forwarded {} object lock request: {status}",
                    R::METHOD
                );
                Err(status)
            }
            None => Ok(()),
        }
    }
}

/// Every tserver's lease epoch floor (`PopulateLeaseEpochFloorsUnlocked`).
fn export_floors(
    snapshot: &CatalogSnapshot,
) -> Result<Vec<yb_proto::pb::tserver::LeaseEpochFloorPB>, Status> {
    let mut floors = Vec::new();
    for row in snapshot.iter(SysRowEntryType::OBJECT_LOCK_ENTRY) {
        let entry: SysObjectLockEntryPB = row.parse().map_err(corrupt)?;
        if let Some(floor) =
            lease_epoch_floor(&row.key.id, &entry.lease_info.clone().unwrap_or_default())
        {
            floors.push(floor);
        }
    }
    Ok(floors)
}
