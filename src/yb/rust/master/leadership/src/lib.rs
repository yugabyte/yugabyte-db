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

//! The sys-catalog Raft role as a state machine, and the fencing token every catalog write
//! carries. See docs/leadership.md.
//!
//! ```text
//! Follower -> CatchingUp(t) -> Ready(t, p) <-> LeaseLapsed(t, p)
//!                                Ready(t, p) -> Reloading(t) -> Ready(t, p + 1)
//! any state -> Follower when the term changes or the peer is no longer leader
//! ```
//!
//! Entering Ready opens a new [`TermScope`]; leaving it for Follower or Reloading cancels the scope
//! before a token for the new state exists. A [`LeaderToken`] is `{term, pitr_count}`; the writer
//! refuses a mutation whose token is not current, and Raft refuses the write if the term moved.

use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_task_runtime::{TaskRegistry, TermScope};
use tokio::sync::{watch, Notify};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_types::PeerRole;

/// `consensus::LeaderStatus`, in the C++ enum's order.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LeaderStatus {
    /// `NOT_LEADER`.
    NotLeader,
    /// `LEADER_BUT_NO_OP_NOT_COMMITTED`.
    NoOpNotCommitted,
    /// `LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE`.
    OldLeaderMayHaveLease,
    /// `LEADER_BUT_NO_MAJORITY_REPLICATED_LEASE`.
    NoMajorityReplicatedLease,
    /// `LEADER_AND_READY`.
    Ready,
}

impl LeaderStatus {
    /// From the C++ enum value.
    pub fn from_i32(value: i32) -> LeaderStatus {
        match value {
            1 => LeaderStatus::NoOpNotCommitted,
            2 => LeaderStatus::OldLeaderMayHaveLease,
            3 => LeaderStatus::NoMajorityReplicatedLease,
            4 => LeaderStatus::Ready,
            _ => LeaderStatus::NotLeader,
        }
    }

    /// `LeaderState::CreateStatus`.
    fn status(self) -> Status {
        match self {
            LeaderStatus::NotLeader => Status::new(StatusCode::IllegalState, "Not the leader"),
            LeaderStatus::NoOpNotCommitted => Status::new(
                StatusCode::LeaderNotReadyToServe,
                "Leader not yet replicated NoOp to be ready to serve requests",
            ),
            LeaderStatus::OldLeaderMayHaveLease => Status::new(
                StatusCode::LeaderNotReadyToServe,
                "Previous leader's lease might still be active",
            ),
            LeaderStatus::NoMajorityReplicatedLease => Status::new(
                StatusCode::LeaderHasNoLease,
                "This leader has not yet acquired a lease.",
            ),
            LeaderStatus::Ready => Status::new(StatusCode::IllegalState, "ready"),
        }
    }
}

/// The sys-catalog peer's Raft state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RaftLeaderState {
    /// The Raft role.
    pub role: PeerRole,
    /// The current term.
    pub term: i64,
    /// The leader status; `NotLeader` unless `role` is LEADER.
    pub status: LeaderStatus,
}

/// Reads the sys-catalog peer's Raft state. Called often; must be cheap.
pub trait LeaderStateSource: Send + Sync + 'static {
    /// The current state.
    fn leader_state(&self) -> RaftLeaderState;
}

/// Where leadership is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    /// Not the leader, or leadership is not allowed.
    Follower,
    /// Elected in `term`; waiting for the lease, the catalog and the post-load hooks.
    CatchingUp {
        /// The term.
        term: i64,
    },
    /// Serving and writing.
    Ready {
        /// The term.
        term: i64,
        /// Restores completed while leading in this process.
        pitr_count: u64,
    },
    /// Same term, but the lease lapsed: no tokens until it is back; the scope keeps running.
    LeaseLapsed {
        /// The term.
        term: i64,
        /// As in Ready.
        pitr_count: u64,
        /// Why.
        status: LeaderStatus,
    },
    /// A PITR restore was applied; the catalog is reloading.
    Reloading {
        /// The term.
        term: i64,
        /// The count before the restore.
        pitr_count: u64,
    },
}

/// Proof that the holder decided while leader in `term` with `pitr_count` restores applied.
/// Only this crate creates one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LeaderToken {
    term: i64,
    pitr_count: u64,
}

impl LeaderToken {
    /// The leader term.
    pub fn term(&self) -> i64 {
        self.term
    }

    /// The restore count.
    pub fn pitr_count(&self) -> u64 {
        self.pitr_count
    }
}

/// A future returned by a hook.
pub type HookFuture = Pin<Box<dyn Future<Output = Result<(), Status>> + Send>>;

/// Runs after election and before Ready, with the new term; Ready waits until every hook
/// succeeds. Hooks are retried on failure.
pub type ReadyHook = Box<dyn Fn(i64) -> HookFuture + Send + Sync>;

/// How often the Raft state is polled; election and step-down also nudge.
const POLL_INTERVAL: Duration = Duration::from_millis(100);

/// The leadership state machine.
pub struct Leadership {
    source: Arc<dyn LeaderStateSource>,
    allowed: Box<dyn Fn() -> bool + Send + Sync>,
    registry: Arc<TaskRegistry>,
    inner: Mutex<Inner>,
    phase: watch::Sender<Phase>,
    nudge: Notify,
    hooks: Mutex<Vec<Arc<ReadyHook>>>,
}

struct Inner {
    phase: Phase,
    scope: Option<TermScope>,
    pitr_count: u64,
}

impl Leadership {
    /// A follower. `allowed` is read on every election (`TEST_rust_master_allow_leadership`).
    pub fn new(
        source: Arc<dyn LeaderStateSource>,
        allowed: impl Fn() -> bool + Send + Sync + 'static,
        registry: Arc<TaskRegistry>,
    ) -> Arc<Leadership> {
        Arc::new(Leadership {
            source,
            allowed: Box::new(allowed),
            registry,
            inner: Mutex::new(Inner {
                phase: Phase::Follower,
                scope: None,
                pitr_count: 0,
            }),
            phase: watch::channel(Phase::Follower).0,
            nudge: Notify::new(),
            hooks: Mutex::new(Vec::new()),
        })
    }

    /// Registers a hook that runs on every election before Ready.
    pub fn add_ready_hook(&self, hook: ReadyHook) {
        self.hooks.lock().unwrap().push(Arc::new(hook));
    }

    /// The current phase.
    pub fn phase(&self) -> Phase {
        self.inner.lock().unwrap().phase
    }

    /// Phase changes.
    pub fn subscribe(&self) -> watch::Receiver<Phase> {
        self.phase.subscribe()
    }

    /// The current term's scope, while Ready or LeaseLapsed.
    pub fn scope(&self) -> Option<TermScope> {
        self.inner.lock().unwrap().scope.clone()
    }

    /// The sys-catalog peer's Raft state now.
    pub fn raft_state(&self) -> RaftLeaderState {
        self.source.leader_state()
    }

    /// Re-reads the Raft state now instead of at the next poll.
    pub fn nudge(&self) {
        self.nudge.notify_one();
    }

    /// A token, or the status `ScopedLeaderSharedLock` reports in this state (the caller puts it
    /// in `NOT_THE_LEADER`). Checks the live Raft state, so a lapsed lease is seen at once.
    pub fn token(&self) -> Result<LeaderToken, Status> {
        let phase = self.phase();
        let (term, pitr_count) = match phase {
            Phase::Ready { term, pitr_count } => (term, pitr_count),
            other => return Err(phase_status(other)),
        };
        let live = self.source.leader_state();
        if live.role != PeerRole::LEADER || live.term != term {
            self.nudge();
            return Err(LeaderStatus::NotLeader.status());
        }
        if live.status != LeaderStatus::Ready {
            self.nudge();
            return Err(live.status.status());
        }
        Ok(LeaderToken { term, pitr_count })
    }

    /// A token for the ready hooks of `term`: they run while CatchingUp, after the term's NoOp is
    /// committed and the lease is held, and some must write (a new universe's default rows)
    /// before the term is Ready, as C++ writes them before setting `leader_ready_term_`. It stays
    /// valid once the term is Ready.
    pub fn catch_up_token(&self, term: i64) -> Result<LeaderToken, Status> {
        let pitr_count = {
            let inner = self.inner.lock().unwrap();
            match inner.phase {
                Phase::CatchingUp { term: current } if current == term => inner.pitr_count,
                other => return Err(phase_status(other)),
            }
        };
        let live = self.source.leader_state();
        if live.role != PeerRole::LEADER || live.term != term {
            self.nudge();
            return Err(LeaderStatus::NotLeader.status());
        }
        if live.status != LeaderStatus::Ready {
            return Err(live.status.status());
        }
        Ok(LeaderToken { term, pitr_count })
    }

    /// Whether `token` is still current.
    pub fn check(&self, token: &LeaderToken) -> Result<(), Status> {
        let current = match self.phase() {
            Phase::CatchingUp { term } if term == token.term => self.catch_up_token(term)?,
            _ => self.token()?,
        };
        if current != *token {
            return Err(Status::new(
                StatusCode::LeaderNotReadyToServe,
                format!(
                    "Leader term or restore count changed: token {token:?}, current {current:?}"
                ),
            ));
        }
        Ok(())
    }

    /// A PITR restore was applied: cancels the term's work until [`Leadership::end_reload`].
    pub fn begin_reload(&self) {
        let mut inner = self.inner.lock().unwrap();
        if let Phase::Ready { term, pitr_count }
        | Phase::LeaseLapsed {
            term, pitr_count, ..
        } = inner.phase
        {
            if let Some(scope) = inner.scope.take() {
                scope.cancel();
            }
            self.set_phase(&mut inner, Phase::Reloading { term, pitr_count });
        }
    }

    /// The catalog finished reloading after a restore.
    pub fn end_reload(&self) {
        let mut inner = self.inner.lock().unwrap();
        if let Phase::Reloading { term, pitr_count } = inner.phase {
            inner.pitr_count = pitr_count + 1;
            inner.scope = Some(TermScope::new(term, self.registry.clone()));
            let pitr_count = inner.pitr_count;
            self.set_phase(&mut inner, Phase::Ready { term, pitr_count });
        }
    }

    /// Follows the Raft state until the runtime stops.
    pub async fn run(self: Arc<Self>) {
        loop {
            self.step();
            tokio::select! {
                _ = self.nudge.notified() => {}
                _ = tokio::time::sleep(POLL_INTERVAL) => {}
            }
        }
    }

    /// One transition, if the Raft state calls for it.
    pub fn step(self: &Arc<Self>) {
        let live = self.source.leader_state();
        let leads_in = |term: i64| live.role == PeerRole::LEADER && live.term == term;
        let mut inner = self.inner.lock().unwrap();
        match inner.phase {
            Phase::Follower => {
                if live.role == PeerRole::LEADER && (self.allowed)() {
                    tracing::info!("Elected sys catalog leader in term {}", live.term);
                    self.set_phase(&mut inner, Phase::CatchingUp { term: live.term });
                    let this = self.clone();
                    tokio::spawn(async move { this.catch_up(live.term).await });
                }
            }
            Phase::CatchingUp { term } | Phase::Reloading { term, .. } => {
                if !leads_in(term) {
                    self.to_follower(&mut inner);
                }
            }
            Phase::Ready { term, pitr_count } => {
                if !leads_in(term) {
                    self.to_follower(&mut inner);
                } else if live.status != LeaderStatus::Ready {
                    let next = Phase::LeaseLapsed {
                        term,
                        pitr_count,
                        status: live.status,
                    };
                    self.set_phase(&mut inner, next);
                }
            }
            Phase::LeaseLapsed {
                term, pitr_count, ..
            } => {
                if !leads_in(term) {
                    self.to_follower(&mut inner);
                } else if live.status == LeaderStatus::Ready {
                    self.set_phase(&mut inner, Phase::Ready { term, pitr_count });
                }
            }
        }
    }

    fn to_follower(&self, inner: &mut Inner) {
        tracing::info!("No longer the sys catalog leader ({:?})", inner.phase);
        if let Some(scope) = inner.scope.take() {
            scope.cancel();
        }
        self.set_phase(inner, Phase::Follower);
    }

    fn set_phase(&self, inner: &mut Inner, phase: Phase) {
        inner.phase = phase;
        self.phase.send_replace(phase);
    }

    async fn catch_up(self: Arc<Self>, term: i64) {
        let still_catching_up = || self.phase() == Phase::CatchingUp { term };
        // The NoOp of the term is committed and the lease is held.
        while still_catching_up() && self.source.leader_state().status != LeaderStatus::Ready {
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        let hooks = self.hooks.lock().unwrap().clone();
        for hook in hooks {
            loop {
                if !still_catching_up() {
                    return;
                }
                match hook(term).await {
                    Ok(()) => break,
                    Err(status) => {
                        tracing::warn!("Leader initialization step failed, retrying: {status}");
                        tokio::time::sleep(Duration::from_millis(200)).await;
                    }
                }
            }
        }
        let mut inner = self.inner.lock().unwrap();
        if inner.phase == (Phase::CatchingUp { term }) {
            inner.scope = Some(TermScope::new(term, self.registry.clone()));
            let pitr_count = inner.pitr_count;
            tracing::info!("Sys catalog leader ready in term {term}");
            self.set_phase(&mut inner, Phase::Ready { term, pitr_count });
        }
    }
}

/// What `ScopedLeaderSharedLock` reports in each state other than Ready.
fn phase_status(phase: Phase) -> Status {
    match phase {
        Phase::Follower => LeaderStatus::NotLeader.status(),
        Phase::CatchingUp { .. } => Status::new(
            StatusCode::LeaderNotReadyToServe,
            "yb-master leader is initializing",
        ),
        Phase::LeaseLapsed { status, .. } => status.status(),
        Phase::Reloading { .. } => Status::new(
            StatusCode::ServiceUnavailable,
            "Catalog manager is restoring",
        ),
        Phase::Ready { .. } => Status::new(StatusCode::IllegalState, "ready"),
    }
}

/// A [`LeaderStateSource`] tests set by hand.
pub struct FakeLeaderState(Mutex<RaftLeaderState>);

impl FakeLeaderState {
    /// A follower in term 1.
    pub fn new() -> Arc<FakeLeaderState> {
        Arc::new(FakeLeaderState(Mutex::new(RaftLeaderState {
            role: PeerRole::FOLLOWER,
            term: 1,
            status: LeaderStatus::NotLeader,
        })))
    }

    /// Sets the state.
    pub fn set(&self, role: PeerRole, term: i64, status: LeaderStatus) {
        *self.0.lock().unwrap() = RaftLeaderState { role, term, status };
    }

    /// Leader and ready in `term`.
    pub fn lead(&self, term: i64) {
        self.set(PeerRole::LEADER, term, LeaderStatus::Ready);
    }
}

impl LeaderStateSource for FakeLeaderState {
    fn leader_state(&self) -> RaftLeaderState {
        *self.0.lock().unwrap()
    }
}

/// A leadership that is Ready in `term`, for tests of the crates that need a token.
pub async fn ready_for_tests(term: i64) -> (Arc<Leadership>, Arc<FakeLeaderState>) {
    let source = FakeLeaderState::new();
    let leadership = Leadership::new(source.clone(), || true, Arc::default());
    source.lead(term);
    leadership.step();
    let mut phases = leadership.subscribe();
    while !matches!(*phases.borrow_and_update(), Phase::Ready { .. }) {
        phases.changed().await.unwrap();
    }
    (leadership, source)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    async fn wait_for(leadership: &Leadership, phase: impl Fn(Phase) -> bool) {
        let mut phases = leadership.subscribe();
        tokio::time::timeout(Duration::from_secs(5), async {
            while !phase(*phases.borrow_and_update()) {
                phases.changed().await.unwrap();
            }
        })
        .await
        .unwrap();
    }

    #[tokio::test]
    async fn election_runs_hooks_then_issues_tokens() {
        let source = FakeLeaderState::new();
        let leadership = Leadership::new(source.clone(), || true, Arc::default());
        let calls = Arc::new(AtomicUsize::new(0));
        let c = calls.clone();
        leadership.add_ready_hook(Box::new(move |term| {
            let c = c.clone();
            Box::pin(async move {
                assert_eq!(term, 4);
                // Fails once, to check the retry.
                if c.fetch_add(1, Ordering::SeqCst) == 0 {
                    Err(Status::new(StatusCode::TryAgain, "not yet"))
                } else {
                    Ok(())
                }
            })
        }));
        assert_eq!(
            leadership.token().unwrap_err().code,
            StatusCode::IllegalState
        );

        source.set(PeerRole::LEADER, 4, LeaderStatus::NoOpNotCommitted);
        leadership.step();
        assert_eq!(leadership.phase(), Phase::CatchingUp { term: 4 });
        assert_eq!(
            leadership.token().unwrap_err().code,
            StatusCode::LeaderNotReadyToServe
        );
        source.lead(4);
        wait_for(&leadership, |p| matches!(p, Phase::Ready { .. })).await;
        assert_eq!(calls.load(Ordering::SeqCst), 2);
        let token = leadership.token().unwrap();
        assert_eq!(token.term(), 4);
        let scope = leadership.scope().unwrap();

        // A lapsed lease withholds tokens but keeps the scope.
        source.set(PeerRole::LEADER, 4, LeaderStatus::NoMajorityReplicatedLease);
        assert_eq!(
            leadership.token().unwrap_err().code,
            StatusCode::LeaderHasNoLease
        );
        leadership.step();
        assert!(matches!(leadership.phase(), Phase::LeaseLapsed { .. }));
        assert!(!scope.is_cancelled());
        source.lead(4);
        leadership.step();
        assert!(leadership.check(&token).is_ok());

        // A new term cancels the scope; the old token is dead.
        source.set(PeerRole::FOLLOWER, 5, LeaderStatus::NotLeader);
        leadership.step();
        assert_eq!(leadership.phase(), Phase::Follower);
        assert!(scope.is_cancelled());
        assert!(leadership.check(&token).is_err());
    }

    #[tokio::test]
    async fn hooks_write_with_a_catch_up_token() {
        let source = FakeLeaderState::new();
        let leadership = Leadership::new(source.clone(), || true, Arc::default());
        let (sender, mut hook_tokens) = tokio::sync::mpsc::unbounded_channel();
        let weak = Arc::downgrade(&leadership);
        leadership.add_ready_hook(Box::new(move |term| {
            let (weak, sender) = (weak.clone(), sender.clone());
            Box::pin(async move {
                let leadership = weak.upgrade().unwrap();
                let token = leadership.catch_up_token(term)?;
                // Valid while catching up, which a plain token never is.
                leadership.check(&token)?;
                assert!(leadership.token().is_err());
                sender.send(token).unwrap();
                Ok(())
            })
        }));
        source.lead(3);
        leadership.step();
        wait_for(&leadership, |p| matches!(p, Phase::Ready { .. })).await;
        let token = hook_tokens.recv().await.unwrap();
        // The same token once Ready.
        assert_eq!(leadership.token().unwrap(), token);
        assert!(leadership.check(&token).is_ok());
        // Not for another term, and not outside CatchingUp.
        assert!(leadership.catch_up_token(3).is_err());
        source.set(PeerRole::FOLLOWER, 4, LeaderStatus::NotLeader);
        leadership.step();
        assert!(leadership.check(&token).is_err());
    }

    #[tokio::test]
    async fn restore_bumps_the_pitr_count() {
        let (leadership, _source) = ready_for_tests(2).await;
        let token = leadership.token().unwrap();
        let scope = leadership.scope().unwrap();
        leadership.begin_reload();
        assert!(scope.is_cancelled());
        assert_eq!(
            leadership.token().unwrap_err().code,
            StatusCode::ServiceUnavailable
        );
        leadership.end_reload();
        let new_token = leadership.token().unwrap();
        assert_eq!(new_token.pitr_count(), 1);
        assert!(leadership.check(&token).is_err());
        assert!(!leadership.scope().unwrap().is_cancelled());
    }

    #[tokio::test]
    async fn not_allowed_stays_follower() {
        let source = FakeLeaderState::new();
        let leadership = Leadership::new(source.clone(), || false, Arc::default());
        source.lead(3);
        leadership.step();
        assert_eq!(leadership.phase(), Phase::Follower);
    }
}
