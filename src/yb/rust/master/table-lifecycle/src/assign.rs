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

//! The `tablet-assignment` controller: `ProcessPendingAssignments` and the table's promotion to
//! RUNNING.
//!
//! Each pass, from the rows alone:
//! - PREPARING tablets get replicas and become CREATING, one write per table;
//! - CREATING tablets get `CreateTablet` sent to each replica, once per term (a new leader sends
//!   again; a replica that has the tablet answers AlreadyPresent, which counts as success);
//! - a CREATING tablet still not RUNNING `tablet_creation_timeout_ms` after its first send in this
//!   term is replaced by a new PREPARING tablet for the same partition;
//! - a CREATING tablet whose voters mostly run, and that has no leader yet, gets its first
//!   election started at the replica chosen when it was assigned (`use_create_table_leader_hint`);
//! - a PREPARING table whose tablets all run becomes RUNNING (`PromoteTableToRunningState`).

use std::collections::{BTreeMap, HashMap, HashSet};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{Attempt, BoxFuture, Controller, RetryPolicy, TermScope, TsRpcExecutor};
use master_ts_manager::placement::{blacklist_addresses, is_accepting_leader_load, is_blacklisted};
use master_ts_manager::TsDescriptor;
use protobuf::{Enum, EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::consensus::{RunLeaderElectionRequestPB, RunLeaderElectionResponsePB};
use yb_proto::pb::consensus_metadata::{PeerMemberType, RaftPeerPB};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;
use yb_proto::pb::tserver_admin::{CreateTabletRequestPB, CreateTabletResponsePB};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

use crate::namespaces::unused_id;
use crate::placement::{
    cluster_config, live_tservers, select_replicas, table_replication_info, Load,
};
use crate::schedules::covering_schedules;
use crate::TableLifecycle;

use sys_tablets_entry_pb::State as TabletState;

/// When a tablet's `CreateTablet` calls were first sent this term, and whether they must be sent
/// again.
type SentCreate = (Instant, bool);

/// Assigns and creates new tablets, in one leader term.
pub struct TabletAssigner {
    lifecycle: Arc<TableLifecycle>,
    rpc: Arc<dyn RpcClient>,
    executor: Arc<TsRpcExecutor>,
    retry: RetryPolicy,
    /// `master_ts_rpc_timeout_ms`.
    rpc_timeout: fn() -> Duration,
    /// `catalog_manager_bg_task_wait_ms`.
    interval: fn() -> Duration,
    /// CREATING tablets whose `CreateTablet` calls this term sent: when they were first sent, and
    /// whether a call failed for good, so the next pass sends again. The first send time stays,
    /// so a tablet that keeps failing is still replaced at the creation timeout.
    sent: Arc<Mutex<HashMap<Vec<u8>, SentCreate>>>,
    /// The replica each tablet assigned this term should elect as its first leader
    /// (`SelectProtegeForTablet`): new tablets' leaders start out balanced.
    proteges: Mutex<HashMap<Vec<u8>, Vec<u8>>>,
    /// Tablets whose hinted election was started this term (`TabletInfo::InitiateElection`).
    elections: Mutex<HashSet<Vec<u8>>>,
}

impl TabletAssigner {
    /// An assigner for one term. `retry` bounds each `CreateTablet`; its deadline is replaced by
    /// `tablet_creation_timeout_ms`.
    pub fn new(
        lifecycle: Arc<TableLifecycle>,
        rpc: Arc<dyn RpcClient>,
        executor: Arc<TsRpcExecutor>,
        retry: RetryPolicy,
        rpc_timeout: fn() -> Duration,
        interval: fn() -> Duration,
    ) -> Arc<TabletAssigner> {
        Arc::new(TabletAssigner {
            lifecycle,
            rpc,
            executor,
            retry,
            rpc_timeout,
            interval,
            sent: Arc::default(),
            proteges: Mutex::default(),
            elections: Mutex::default(),
        })
    }

    async fn pass(&self, scope: &TermScope) {
        let Ok(token) = self.lifecycle.leadership.token() else {
            return;
        };
        let snapshot = self.lifecycle.catalog.snapshot();
        let mut preparing: BTreeMap<Vec<u8>, Vec<Arc<Entity>>> = BTreeMap::new();
        for tablet in snapshot.iter(SysRowEntryType::TABLET) {
            let Some(hot) = tablet.tablet() else { continue };
            if hot.state == TabletState::PREPARING as i32 {
                preparing
                    .entry(hot.table_id.clone())
                    .or_default()
                    .push(tablet.clone());
            }
        }
        for (table_id, tablets) in preparing {
            if let Err(status) = self.assign(&token, &snapshot, &table_id, &tablets).await {
                tracing::warn!(
                    "Assigning tablets of table {} failed, will retry: {status}",
                    String::from_utf8_lossy(&table_id)
                );
            }
        }
        // The assignment's write is in the latest snapshot.
        let snapshot = self.lifecycle.catalog.snapshot();
        let timeout = Duration::from_millis(
            (self.lifecycle.options.tablet_creation_timeout_ms)().max(0) as u64,
        );
        let mut expired = Vec::new();
        for tablet in snapshot.iter(SysRowEntryType::TABLET) {
            let Some(hot) = tablet.tablet() else { continue };
            if hot.state != TabletState::CREATING as i32 {
                continue;
            }
            let sent = self.sent.lock().unwrap().get(&*tablet.key.id).copied();
            match sent {
                Some((at, _)) if at.elapsed() >= timeout => expired.push(tablet.clone()),
                Some((_, false)) => {}
                Some((_, true)) | None => self.send_creates(scope, &token, &snapshot, tablet),
            }
        }
        for tablet in expired {
            if let Err(status) = self.replace(&token, &tablet).await {
                tracing::warn!(
                    "Replacing tablet {} failed, will retry: {status}",
                    String::from_utf8_lossy(&tablet.key.id)
                );
            }
        }
        self.promote_tables(&token).await;
    }

    /// PREPARING to CREATING: pick each tablet's replicas and write their config.
    async fn assign(
        &self,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        table_id: &[u8],
        tablets: &[Arc<Entity>],
    ) -> Result<(), Status> {
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Ok(());
        };
        if is_dropping(table) {
            return Ok(());
        }
        let table_pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
        let config = cluster_config(snapshot);
        let replication = table_replication_info(table_pb.replication_info.as_ref(), &config);
        let placement = replication.live_replicas.clone().unwrap_or_default();
        let tservers = live_tservers(
            &self.lifecycle.ts_manager,
            &config,
            placement.placement_uuid(),
        );
        // `BuildPlacementCandidates`: each read replica placement adds observers on its own
        // tservers, after the live placement's voters, all counted in one load.
        let read_replicas: Vec<_> = replication
            .read_replicas
            .iter()
            .map(|placement| {
                let tservers = live_tservers(
                    &self.lifecycle.ts_manager,
                    &config,
                    placement.placement_uuid(),
                );
                (placement, tservers)
            })
            .collect();
        let mut load = self.load(snapshot, table_id);
        let replication_factor = (self.lifecycle.options.replication_factor)();
        let mut mutation = Mutation::new().read(table);
        // `SelectProtegeForTablet`: the peer with the fewest proteges among this batch.
        let mut protege_load: HashMap<Vec<u8>, u32> = HashMap::new();
        let mut proteges = Vec::new();
        for tablet in tablets {
            let peers = select_replicas(&placement, &tservers, replication_factor, &mut load)?;
            if let Some(protege) = peers
                .iter()
                .min_by_key(|p| protege_load.get(&*p.uuid).copied().unwrap_or(0))
            {
                *protege_load.entry(protege.uuid.to_vec()).or_default() += 1;
                proteges.push((tablet.key.id.to_vec(), protege.uuid.to_vec()));
            }
            let mut pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
            pb.state = Some(EnumOrUnknown::new(TabletState::CREATING));
            pb.set_state_msg(b"Sending initial creation of tablet".to_vec());
            let cstate = pb.committed_consensus_state.mut_or_insert_default();
            let raft_config = cstate.config.mut_or_insert_default();
            raft_config.peers = peers
                .iter()
                .map(|d| raft_peer(d, PeerMemberType::VOTER))
                .collect();
            for (placement, tservers) in &read_replicas {
                // A read replica placement without live tservers gets no observers yet; the
                // balancer adds them later.
                if tservers.is_empty() {
                    continue;
                }
                let observers =
                    select_replicas(placement, tservers, replication_factor, &mut load)?;
                raft_config.peers.extend(
                    observers
                        .iter()
                        .map(|d| raft_peer(d, PeerMemberType::OBSERVER)),
                );
            }
            mutation = mutation.read(tablet).upsert(
                SysRowEntryType::TABLET,
                &tablet.key.id,
                pb.write_to_bytes().map_err(internal)?,
            );
        }
        tracing::info!(
            "Assigning {} tablets of table {}",
            tablets.len(),
            String::from_utf8_lossy(table_id)
        );
        self.lifecycle
            .writer
            .submit(token, mutation)
            .await
            .map_err(Status::from)?;
        self.proteges.lock().unwrap().extend(proteges);
        Ok(())
    }

    /// `StartElectionIfReady` for a tablet's first election: once a majority of its voters run,
    /// the protege (else the first replica that may lead) is asked to run for leader, once a
    /// term. Elections after the first are left to Raft.
    fn start_election_if_ready(
        &self,
        scope: &TermScope,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        tablet: &Arc<Entity>,
    ) {
        let tablet_id = tablet.key.id.to_vec();
        if self.elections.lock().unwrap().contains(&tablet_id) {
            return;
        }
        let ts_manager = &self.lifecycle.ts_manager;
        let Some(observed) = ts_manager.locations.get(&tablet_id) else {
            return;
        };
        if observed.leader().is_some() {
            return;
        }
        let Ok(pb) = tablet.parse::<SysTabletsEntryPB>() else {
            return;
        };
        let cstate = &pb.committed_consensus_state;
        if cstate.has_leader_uuid() || cstate.current_term() != 0 {
            return;
        }
        let voters = cstate
            .config
            .peers
            .iter()
            .filter(|p| p.member_type() == PeerMemberType::VOTER)
            .count();
        let running = observed
            .replicas
            .iter()
            .filter(|r| {
                r.member_type == PeerMemberType::VOTER && r.state == RaftGroupStatePB::RUNNING
            })
            .count();
        if running < voters / 2 + 1 {
            return;
        }
        let config = cluster_config(snapshot);
        let blacklist = blacklist_addresses(&config);
        let replication = config.replication_info.clone().unwrap_or_default();
        let possible: Vec<Vec<u8>> = observed
            .replicas
            .iter()
            .filter_map(|r| ts_manager.get(&r.ts_uuid))
            .filter(|d| d.is_live() && !is_blacklisted(d, &blacklist))
            .filter(|d| is_accepting_leader_load(d, &replication))
            .map(|d| d.uuid.to_vec())
            .collect();
        if possible.is_empty() {
            return;
        }
        self.elections.lock().unwrap().insert(tablet_id.clone());
        let protege = self
            .proteges
            .lock()
            .unwrap()
            .get(&tablet_id)
            .filter(|p| possible.contains(p))
            .cloned()
            .unwrap_or_else(|| possible[0].clone());
        tracing::info!(
            "Starting election at {} in favor of {}",
            String::from_utf8_lossy(&tablet_id),
            String::from_utf8_lossy(&protege)
        );
        let mut request = RunLeaderElectionRequestPB::new();
        request.set_dest_uuid(protege.clone());
        request.set_tablet_id(tablet_id.clone());
        request.set_initial_election(true);
        let (rpc, executor, lifecycle) = (
            self.rpc.clone(),
            self.executor.clone(),
            self.lifecycle.clone(),
        );
        let token = *token;
        let policy = RetryPolicy {
            deadline: Duration::from_millis(
                (self.lifecycle.options.tablet_creation_timeout_ms)().max(0) as u64,
            ),
            ..self.retry
        };
        let rpc_timeout = (self.rpc_timeout)();
        scope.entity().spawn("start-election", async move {
            let result = executor
                .run(&protege, "RunLeaderElection", &policy, |_| {
                    let current = lifecycle.leadership.check(&token);
                    let address = lifecycle.ts_manager.rpc_address(&protege);
                    let (rpc, request) = (rpc.clone(), request.clone());
                    async move {
                        current.map_err(Attempt::Fail)?;
                        let (host, port) = address.ok_or_else(|| {
                            Attempt::Retry(Status::new(
                                StatusCode::NotFound,
                                "tserver is not registered",
                            ))
                        })?;
                        let bytes = request
                            .write_to_bytes()
                            .map_err(|e| Attempt::Fail(internal(e)))?;
                        let response = rpc
                            .call(
                                &host,
                                port,
                                "yb.consensus.ConsensusService",
                                "RunLeaderElection",
                                &bytes,
                                rpc_timeout,
                            )
                            .await
                            .map_err(Attempt::Retry)?;
                        let response = RunLeaderElectionResponsePB::parse_from_bytes(&response)
                            .map_err(|e| Attempt::Retry(corrupt(e)))?;
                        // As `AsyncStartElection`, whose task retries until its deadline: a
                        // protege still bootstrapping refuses at first.
                        if let Some(error) = response.error.as_ref() {
                            return Err(Attempt::Retry(Status::new(
                                StatusCode::from_i32(error.status.code().value()),
                                error.status.message().to_string(),
                            )));
                        }
                        Ok(())
                    }
                })
                .await;
            if let Err(status) = result {
                tracing::warn!(
                    "RunLeaderElection RPC for tablet {} on TS {} failed: {status}",
                    String::from_utf8_lossy(&tablet_id),
                    String::from_utf8_lossy(&protege)
                );
            }
        });
    }

    /// Replica counts the choice balances: RUNNING replicas from the observed locations, of the
    /// table and of all tables.
    fn load(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> Load {
        let mut load = Load::default();
        let locations = &self.lifecycle.ts_manager.locations;
        for (_, replicas) in locations.all() {
            for replica in &replicas.replicas {
                *load.global.entry(replica.ts_uuid.to_vec()).or_default() += 1;
            }
        }
        for tablet in snapshot.tablets_of(table_id) {
            if let Some(replicas) = locations.get(&tablet.key.id) {
                for replica in &replicas.replicas {
                    *load.table.entry(replica.ts_uuid.to_vec()).or_default() += 1;
                }
            }
        }
        load
    }

    /// Sends `CreateTablet` to every replica of a CREATING tablet.
    fn send_creates(
        &self,
        scope: &TermScope,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        tablet: &Arc<Entity>,
    ) {
        let Some(request) = create_request(snapshot, tablet) else {
            return;
        };
        tracing::info!(
            "Sending CreateTablet for tablet {} to {} replicas",
            String::from_utf8_lossy(&tablet.key.id),
            request.config.peers.len()
        );
        self.sent
            .lock()
            .unwrap()
            .entry(tablet.key.id.to_vec())
            .and_modify(|(_, resend)| *resend = false)
            .or_insert((Instant::now(), false));
        let deadline = Duration::from_millis(
            (self.lifecycle.options.tablet_creation_timeout_ms)().max(0) as u64,
        );
        for peer in &request.config.peers {
            let ts_uuid = peer.permanent_uuid().to_vec();
            let mut request = request.clone();
            request.set_dest_uuid(ts_uuid.clone());
            let (rpc, executor, lifecycle) = (
                self.rpc.clone(),
                self.executor.clone(),
                self.lifecycle.clone(),
            );
            let token = *token;
            let policy = RetryPolicy {
                deadline,
                ..self.retry
            };
            let rpc_timeout = (self.rpc_timeout)();
            let tablet_id = tablet.key.id.to_vec();
            let sent = self.sent.clone();
            scope.entity().spawn("create-tablet", async move {
                let result = executor
                    .run(&ts_uuid, "CreateTablet", &policy, |attempt| {
                        let current = lifecycle.leadership.check(&token);
                        let address = lifecycle.ts_manager.rpc_address(&ts_uuid);
                        let (rpc, request) = (rpc.clone(), request.clone());
                        async move {
                            current.map_err(Attempt::Fail)?;
                            let (host, port) = address.ok_or_else(|| {
                                Attempt::Retry(Status::new(
                                    StatusCode::NotFound,
                                    "tserver is not registered",
                                ))
                            })?;
                            let result =
                                send_create(&*rpc, &host, port, &request, rpc_timeout).await;
                            if let Err(Attempt::Retry(status)) = &result {
                                // As `AsyncCreateReplica` logs each failed attempt.
                                tracing::warn!(
                                    "CreateTablet of {} on {host}:{port}, attempt {attempt}, \
                                     failed: {status}",
                                    String::from_utf8_lossy(request.tablet_id())
                                );
                            }
                            result
                        }
                    })
                    .await;
                if let Err(status) = result {
                    tracing::warn!(
                        "CreateTablet of {} on {} failed: {status}",
                        String::from_utf8_lossy(&tablet_id),
                        String::from_utf8_lossy(&ts_uuid)
                    );
                    if let Some((_, resend)) = sent.lock().unwrap().get_mut(&tablet_id) {
                        *resend = true;
                    }
                }
            });
        }
    }

    /// A CREATING tablet that did not start in time is replaced by a new tablet for the same
    /// partition, as `HandleAssignCreatingTablet` does.
    async fn replace(&self, token: &LeaderToken, tablet: &Arc<Entity>) -> Result<(), Status> {
        let old_id = tablet.key.id.to_vec();
        self.lifecycle
            .writer
            .mutate(token, |snapshot| {
                let Some(current) = snapshot.get(SysRowEntryType::TABLET, &old_id) else {
                    return Ok((Mutation::new(), ()));
                };
                if current.tablet().map(|t| t.state) != Some(TabletState::CREATING as i32) {
                    return Ok((Mutation::new(), ()));
                }
                let new_id = unused_id(snapshot, SysRowEntryType::TABLET);
                let mut old: SysTabletsEntryPB = current.parse().map_err(corrupt)?;
                let mut new = old.clone();
                old.state = Some(EnumOrUnknown::new(TabletState::REPLACED));
                old.set_state_msg(
                    format!("Replaced by {}", String::from_utf8_lossy(&new_id)).into_bytes(),
                );
                new.state = Some(EnumOrUnknown::new(TabletState::PREPARING));
                new.set_state_msg(
                    format!("Replacement for {}", String::from_utf8_lossy(&old_id)).into_bytes(),
                );
                let cstate = new.committed_consensus_state.mut_or_insert_default();
                cstate.set_current_term(0);
                let config = cstate.config.mut_or_insert_default();
                config.set_committed_op_index(-1);
                config.peers.clear();
                cstate.clear_leader_uuid();
                let mutation = Mutation::new()
                    .read(current)
                    .read_absent(SysRowEntryType::TABLET, &new_id)
                    .upsert(
                        SysRowEntryType::TABLET,
                        &old_id,
                        old.write_to_bytes().map_err(internal)?,
                    )
                    .upsert(
                        SysRowEntryType::TABLET,
                        &new_id,
                        new.write_to_bytes().map_err(internal)?,
                    );
                Ok((mutation, ()))
            })
            .await?;
        self.sent.lock().unwrap().remove(&old_id);
        Ok(())
    }

    /// PREPARING tables whose active tablets all run become RUNNING.
    async fn promote_tables(&self, token: &LeaderToken) {
        let snapshot = self.lifecycle.catalog.snapshot();
        let ready: Vec<Vec<u8>> = snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| {
                t.table()
                    .is_some_and(|h| h.state == sys_tables_entry_pb::State::PREPARING as i32)
            })
            .filter(|t| all_tablets_running(&snapshot, &t.key.id))
            .map(|t| t.key.id.to_vec())
            .collect();
        for table_id in ready {
            let result = self
                .lifecycle
                .writer
                .mutate(token, |snapshot| {
                    let Some(table) = snapshot.get(SysRowEntryType::TABLE, &table_id) else {
                        return Ok((Mutation::new(), ()));
                    };
                    if table.table().map(|t| t.state)
                        != Some(sys_tables_entry_pb::State::PREPARING as i32)
                        || !all_tablets_running(snapshot, &table_id)
                    {
                        return Ok((Mutation::new(), ()));
                    }
                    let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                    pb.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
                    let mut mutation = Mutation::new().read(table);
                    for tablet in snapshot.active_tablets_of(&table_id) {
                        mutation = mutation.read(tablet);
                    }
                    let mutation = mutation.upsert(
                        SysRowEntryType::TABLE,
                        &table_id,
                        pb.write_to_bytes().map_err(internal)?,
                    );
                    Ok((mutation, ()))
                })
                .await;
            if let Err(status) = result {
                tracing::warn!(
                    "Marking table {} RUNNING failed, will retry: {status}",
                    String::from_utf8_lossy(&table_id)
                );
            }
        }
    }
}

impl TabletAssigner {
    /// The `initial-elections` controller of this term's assigner: hinted first elections, run as
    /// replicas report (C++ starts them from the tablet report), not on the assigner's interval.
    pub fn elections(self: &Arc<Self>) -> Arc<dyn Controller> {
        Arc::new(InitialElections(self.clone()))
    }
}

/// See [`TabletAssigner::elections`].
struct InitialElections(Arc<TabletAssigner>);

impl Controller for InitialElections {
    fn name(&self) -> &'static str {
        "initial-elections"
    }

    fn interval(&self) -> Duration {
        (self.0.interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let assigner = &self.0;
            if !(assigner.lifecycle.options.use_create_table_leader_hint)() {
                return;
            }
            let Ok(token) = assigner.lifecycle.leadership.token() else {
                return;
            };
            let snapshot = assigner.lifecycle.catalog.snapshot();
            for tablet in snapshot.iter(SysRowEntryType::TABLET) {
                if tablet.tablet().map(|t| t.state) == Some(TabletState::CREATING as i32) {
                    assigner.start_election_if_ready(scope, &token, &snapshot, tablet);
                }
            }
        })
    }
}

impl Controller for TabletAssigner {
    fn name(&self) -> &'static str {
        "tablet-assignment"
    }

    fn interval(&self) -> Duration {
        (self.interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(self.pass(scope))
    }
}

/// DELETING or DELETED: the table-deletes controller owns its tablets.
fn is_dropping(table: &Entity) -> bool {
    table.table().is_some_and(|t| {
        t.state == sys_tables_entry_pb::State::DELETING as i32
            || t.state == sys_tables_entry_pb::State::DELETED as i32
    })
}

/// `TableInfo::AreAllTabletsRunning`, for a table with at least one tablet.
fn all_tablets_running(snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
    let tablets = snapshot.active_tablets_of(table_id);
    !tablets.is_empty()
        && tablets.iter().all(|t| {
            t.tablet()
                .is_some_and(|h| h.state == TabletState::RUNNING as i32)
        })
}

/// The config peer for a chosen tserver, as `SelectReplica` fills it.
fn raft_peer(descriptor: &TsDescriptor, member_type: PeerMemberType) -> RaftPeerPB {
    let reg = &descriptor.pb.registration;
    let mut peer = RaftPeerPB::new();
    peer.set_permanent_uuid(descriptor.uuid.to_vec());
    peer.member_type = Some(EnumOrUnknown::new(member_type));
    peer.last_known_private_addr = reg.private_rpc_addresses.clone();
    peer.last_known_broadcast_addr = reg.broadcast_addresses.clone();
    if reg.cloud_info.is_some() {
        peer.cloud_info = reg.cloud_info.clone();
    }
    peer
}

/// `AsyncCreateReplica::SendRequest`'s request, without `dest_uuid`. None for a tablet the master
/// does not create: a split child or a clone, which the source tablet's replicas create (and
/// `ShouldReplaceCreatingTablet` never replaces), or one of a table being dropped.
fn create_request(snapshot: &CatalogSnapshot, tablet: &Entity) -> Option<CreateTabletRequestPB> {
    let tablet_pb: SysTabletsEntryPB = tablet.parse().ok()?;
    if tablet_pb.has_split_parent_tablet_id() || tablet_pb.created_by_clone() {
        return None;
    }
    let table = snapshot.get(SysRowEntryType::TABLE, tablet_pb.table_id())?;
    if is_dropping(table) {
        return None;
    }
    let table_pb: SysTablesEntryPB = table.parse().ok()?;
    let mut req = CreateTabletRequestPB::new();
    req.set_table_id(table.key.id.to_vec());
    req.set_tablet_id(tablet.key.id.to_vec());
    req.table_type = table_pb.table_type;
    req.partition = tablet_pb.partition.clone();
    req.set_namespace_id(table_pb.namespace_id().to_vec());
    req.set_namespace_name(table_pb.namespace_name().to_vec());
    req.set_table_name(String::from_utf8_lossy(table_pb.name()).into_owned());
    req.schema = table_pb.schema.clone();
    req.partition_schema = table_pb.partition_schema.clone();
    req.config = MessageField::some(
        tablet_pb
            .committed_consensus_state
            .config
            .clone()
            .unwrap_or_default(),
    );
    req.set_colocated(tablet_pb.colocated());
    if table_pb.index_info.is_some() {
        req.index_info = table_pb.index_info.clone();
    }
    if table_pb.has_wal_retention_secs() {
        req.set_wal_retention_secs(table_pb.wal_retention_secs());
    }
    req.snapshot_schedules = covering_schedules(snapshot, SysRowEntryType::TABLE, &table.key.id);
    req.hosted_stateful_services = table_pb.hosted_stateful_services.clone();
    if !table_pb.pg_table_id().is_empty() {
        req.set_pg_table_id(table_pb.pg_table_id().to_vec());
    }
    Some(req)
}

/// One `CreateTablet` call. A replica that already has the tablet counts as created.
async fn send_create(
    rpc: &dyn RpcClient,
    host: &str,
    port: u16,
    request: &CreateTabletRequestPB,
    timeout: Duration,
) -> Result<(), Attempt> {
    let bytes = request
        .write_to_bytes()
        .map_err(|e| Attempt::Fail(internal(e)))?;
    let response = rpc
        .call(
            host,
            port,
            "yb.tserver.TabletServerAdminService",
            "CreateTablet",
            &bytes,
            timeout,
        )
        .await
        .map_err(Attempt::Retry)?;
    let response = CreateTabletResponsePB::parse_from_bytes(&response)
        .map_err(|e| Attempt::Retry(Status::new(StatusCode::Corruption, e.to_string())))?;
    let Some(error) = response.error.as_ref() else {
        return Ok(());
    };
    let code = StatusCode::from_i32(error.status.code().value());
    if code == StatusCode::AlreadyPresent
        || error.code() == tablet_server_error_pb::Code::TABLET_ALREADY_EXISTS
    {
        return Ok(());
    }
    Err(Attempt::Retry(Status::new(
        code,
        error.status.message().to_string(),
    )))
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

fn internal(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::InternalError, e.to_string())
}
