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

use std::collections::{HashSet, VecDeque};
use std::fmt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use tokio::time::Instant;

use master_catalog_entity::{Entity, RowKey};
use master_leadership::{LeaderToken, Leadership};
use master_sys_catalog::{RowChange, SysCatalogStore};
use tokio::sync::{mpsc, oneshot};
use tokio::task::JoinSet;
use yb_host_api::metrics::{
    Counter, Gauge, Histogram, InMemoryMetrics, MetricDef, MetricRegistry, MetricUnit,
};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::{Catalog, CatalogSnapshot};

/// What a mutation read and what it writes.
#[derive(Clone, Debug, Default)]
pub struct Mutation {
    reads: Vec<(RowKey, Option<u64>)>,
    writes: Vec<RowChange>,
    /// The generation of the snapshot it was built from, when known.
    generation: Option<u64>,
}

impl Mutation {
    /// An empty mutation.
    pub fn new() -> Mutation {
        Mutation::default()
    }

    /// Records that the decision depends on `entity` as read.
    pub fn read(mut self, entity: &Entity) -> Mutation {
        self.reads.push((entity.key.clone(), Some(entity.version)));
        self
    }

    /// Records that the decision depends on the row not existing.
    pub fn read_absent(mut self, entry_type: SysRowEntryType, id: &[u8]) -> Mutation {
        self.reads.push((RowKey::new(entry_type, id), None));
        self
    }

    /// Sets a row.
    pub fn upsert(mut self, entry_type: SysRowEntryType, id: &[u8], data: Vec<u8>) -> Mutation {
        self.writes.push(RowChange::Upsert {
            entry_type,
            id: id.to_vec(),
            data,
        });
        self
    }

    /// Sets a row to a message.
    pub fn upsert_message(
        self,
        entry_type: SysRowEntryType,
        id: &[u8],
        message: &impl protobuf::Message,
    ) -> Result<Mutation, Status> {
        let data = message
            .write_to_bytes()
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
        Ok(self.upsert(entry_type, id, data))
    }

    /// Deletes a row.
    pub fn delete(mut self, entry_type: SysRowEntryType, id: &[u8]) -> Mutation {
        self.writes.push(RowChange::Delete {
            entry_type,
            id: id.to_vec(),
        });
        self
    }

    /// Whether it writes nothing.
    pub fn is_empty(&self) -> bool {
        self.writes.is_empty()
    }
}

/// Which queue a mutation waits in. The writer fills each batch from the priority lane first.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Lane {
    /// Everything else.
    #[default]
    Normal,
    /// Writes that must not wait behind bulk work: YSQL lease grants, DDL completion.
    Priority,
}

/// How a mutation is written.
#[derive(Clone, Copy, Debug, Default)]
pub struct WriteOptions {
    /// The caller's deadline. A mutation still queued when it passes is dropped, not written:
    /// the caller has given up, and writing it late could surprise a retry.
    pub deadline: Option<Instant>,
    /// The queue it waits in.
    pub lane: Lane,
}

impl WriteOptions {
    /// Options with `deadline` as the time left to the caller's deadline from now.
    pub fn with_timeout(timeout: Duration) -> WriteOptions {
        WriteOptions {
            deadline: Some(Instant::now() + timeout),
            ..Default::default()
        }
    }

    /// The same options in the priority lane.
    pub fn priority(self) -> WriteOptions {
        WriteOptions {
            lane: Lane::Priority,
            ..self
        }
    }

    fn expired(&self, now: Instant) -> bool {
        self.deadline.is_some_and(|d| d <= now)
    }
}

/// Why a mutation was not applied.
#[derive(Clone, Debug, PartialEq)]
pub enum MutationError {
    /// It read a row that changed or is being written; rebuild it from a newer snapshot.
    Conflict,
    /// The token is not current.
    NotLeader(Status),
    /// Its deadline passed before it joined a Raft write; nothing was written.
    Expired,
    /// The Raft write failed.
    Write(Status),
}

impl fmt::Display for MutationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MutationError::Conflict => write!(f, "conflict"),
            MutationError::NotLeader(s) => write!(f, "not the leader: {s}"),
            MutationError::Expired => write!(f, "deadline passed before it was written"),
            MutationError::Write(s) => write!(f, "write failed: {s}"),
        }
    }
}

impl From<MutationError> for Status {
    fn from(e: MutationError) -> Status {
        match e {
            MutationError::Conflict => Status::new(
                StatusCode::TryAgain,
                "Catalog mutation kept conflicting with concurrent changes",
            ),
            MutationError::Expired => Status::new(
                StatusCode::TimedOut,
                "Catalog mutation not written: the request's deadline passed while it was queued",
            ),
            MutationError::NotLeader(s) | MutationError::Write(s) => s,
        }
    }
}

/// Writer limits, read when each batch is formed so flag changes take effect, and where the
/// writer's metrics go.
#[derive(Clone)]
pub struct WriterOptions {
    /// `catalog_writer_max_batch_rows`.
    pub max_batch_rows: fn() -> usize,
    /// `catalog_writer_max_batch_bytes`.
    pub max_batch_bytes: fn() -> usize,
    /// `catalog_writer_max_inflight`: Raft writes outstanding at once.
    pub max_inflight: fn() -> usize,
    /// The registry the writer's metrics are created in.
    pub metrics: Arc<dyn MetricRegistry>,
}

impl Default for WriterOptions {
    fn default() -> Self {
        WriterOptions {
            max_batch_rows: || 1000,
            max_batch_bytes: || 4 << 20,
            max_inflight: || 4,
            metrics: InMemoryMetrics::new(),
        }
    }
}

/// Upper bound of the writer's latency histograms: 60 s in microseconds.
const MAX_LATENCY_US: u64 = 60_000_000;

const QUEUE_WAIT: MetricDef = MetricDef {
    name: "rust_master_catalog_writer_queue_wait_us",
    label: "Catalog writer queue wait",
    description: "Time a catalog mutation waited in the Rust master's writer queue before it \
                  joined a Raft write, or was dropped as stale, conflicting or expired.",
    unit: MetricUnit::Microseconds,
};
const COMMIT_LATENCY: MetricDef = MetricDef {
    name: "rust_master_catalog_writer_commit_latency_us",
    label: "Catalog writer commit latency",
    description: "Time from sending a catalog Raft write until its rows are in the Rust \
                  master's in-memory catalog.",
    unit: MetricUnit::Microseconds,
};
const BATCH_ROWS: MetricDef = MetricDef {
    name: "rust_master_catalog_writer_batch_rows",
    label: "Catalog writer batch rows",
    description: "Sys catalog rows in each Raft write of the Rust master's catalog writer.",
    unit: MetricUnit::Entries,
};
const QUEUE_DEPTH: MetricDef = MetricDef {
    name: "rust_master_catalog_writer_queue_depth",
    label: "Catalog writer queue depth",
    description: "Catalog mutations waiting in the Rust master's writer queue.",
    unit: MetricUnit::Operations,
};
const CONFLICTS: MetricDef = MetricDef {
    name: "rust_master_catalog_writer_conflicts",
    label: "Catalog writer conflicts",
    description: "Catalog mutations the Rust master's writer refused because a row they read \
                  changed or was being written. Each is rebuilt from a newer snapshot.",
    unit: MetricUnit::Operations,
};
const EXPIRED: MetricDef = MetricDef {
    name: "rust_master_catalog_writer_expired",
    label: "Catalog writer expired mutations",
    description: "Catalog mutations the Rust master's writer dropped unwritten because the \
                  request's deadline passed while they were queued.",
    unit: MetricUnit::Operations,
};

/// Upper bound of the batch-rows histogram.
const MAX_BATCH_ROWS_METRIC: u64 = 1_000_000;

struct WriterMetrics {
    queue_wait: Arc<dyn Histogram>,
    commit_latency: Arc<dyn Histogram>,
    batch_rows: Arc<dyn Histogram>,
    queue_depth: Arc<dyn Gauge>,
    conflicts: Arc<dyn Counter>,
    expired: Arc<dyn Counter>,
}

impl WriterMetrics {
    fn new(registry: &dyn MetricRegistry) -> WriterMetrics {
        WriterMetrics {
            queue_wait: registry.histogram(&QUEUE_WAIT, MAX_LATENCY_US),
            commit_latency: registry.histogram(&COMMIT_LATENCY, MAX_LATENCY_US),
            batch_rows: registry.histogram(&BATCH_ROWS, MAX_BATCH_ROWS_METRIC),
            queue_depth: registry.gauge(&QUEUE_DEPTH),
            conflicts: registry.counter(&CONFLICTS),
            expired: registry.counter(&EXPIRED),
        }
    }
}

type Done = oneshot::Sender<Result<Arc<CatalogSnapshot>, MutationError>>;
/// A finished Raft write: the rows it held, its waiters and its outcome.
type Finished = (Vec<RowKey>, Vec<Done>, Result<(), Status>);

struct Pending {
    token: LeaderToken,
    mutation: Mutation,
    options: WriteOptions,
    queued_at: Instant,
    /// Set by whichever comes first: the writer taking the mutation from the queue, or its caller
    /// giving up at the deadline. So a caller told `Expired` can rely on nothing being written.
    claimed: Arc<AtomicBool>,
    done: Done,
}

/// The only path that writes the catalog. See the crate docs.
pub struct CatalogWriter {
    catalog: Arc<Catalog>,
    queue: mpsc::UnboundedSender<Pending>,
    /// The writer's `expired` counter, for mutations their callers give up on.
    expired: Arc<dyn Counter>,
}

/// How long [`CatalogWriter::mutate`] keeps rebuilding a conflicting mutation. Bounded by time
/// rather than attempts: under contention on one row, each round lets only one writer through.
const CONFLICT_RETRY_BUDGET: Duration = Duration::from_secs(10);

impl CatalogWriter {
    /// Starts the writer task on the current runtime.
    pub fn start(
        catalog: Arc<Catalog>,
        store: Arc<dyn SysCatalogStore>,
        leadership: Arc<Leadership>,
        options: WriterOptions,
    ) -> Arc<CatalogWriter> {
        let (queue, receiver) = mpsc::unbounded_channel();
        let metrics = WriterMetrics::new(&*options.metrics);
        let expired = metrics.expired.clone();
        let task = WriterTask {
            catalog: catalog.clone(),
            store,
            leadership,
            options,
            metrics,
            priority: VecDeque::new(),
            normal: VecDeque::new(),
            in_flight: HashSet::new(),
            batches: JoinSet::new(),
        };
        tokio::spawn(task.run(receiver));
        Arc::new(CatalogWriter {
            catalog,
            queue,
            expired,
        })
    }

    /// Writes one mutation. On success the returned snapshot (and every later one) contains it.
    pub async fn submit(
        &self,
        token: &LeaderToken,
        mutation: Mutation,
    ) -> Result<Arc<CatalogSnapshot>, MutationError> {
        self.submit_with(token, mutation, WriteOptions::default())
            .await
    }

    /// [`CatalogWriter::submit`] with a deadline and lane. A mutation still queued at the deadline
    /// is answered `Expired` then, not when the writer reaches it.
    pub async fn submit_with(
        &self,
        token: &LeaderToken,
        mutation: Mutation,
        options: WriteOptions,
    ) -> Result<Arc<CatalogSnapshot>, MutationError> {
        let (done, mut result) = oneshot::channel();
        let claimed = Arc::new(AtomicBool::new(false));
        self.queue
            .send(Pending {
                token: *token,
                mutation,
                options,
                queued_at: Instant::now(),
                claimed: claimed.clone(),
                done,
            })
            .map_err(|_| writer_stopped())?;
        if let Some(deadline) = options.deadline {
            if let Ok(outcome) = tokio::time::timeout_at(deadline, &mut result).await {
                return outcome.unwrap_or_else(|_| Err(writer_stopped()));
            }
            // Once the writer has taken it, its answer is the outcome.
            if !claimed.swap(true, Ordering::AcqRel) {
                self.expired.increment();
                return Err(MutationError::Expired);
            }
        }
        result.await.unwrap_or_else(|_| Err(writer_stopped()))
    }

    /// Builds a mutation from the latest snapshot and writes it, rebuilding on conflict. `build`
    /// returns the mutation and a value for the caller; an empty mutation is not written.
    pub async fn mutate<T>(
        &self,
        token: &LeaderToken,
        build: impl FnMut(&CatalogSnapshot) -> Result<(Mutation, T), Status>,
    ) -> Result<(T, Arc<CatalogSnapshot>), Status> {
        self.mutate_with(token, WriteOptions::default(), build)
            .await
    }

    /// [`CatalogWriter::mutate`] with a deadline and lane. Rebuilding stops at the deadline too.
    pub async fn mutate_with<T>(
        &self,
        token: &LeaderToken,
        options: WriteOptions,
        mut build: impl FnMut(&CatalogSnapshot) -> Result<(Mutation, T), Status>,
    ) -> Result<(T, Arc<CatalogSnapshot>), Status> {
        let mut changes = self.catalog.subscribe();
        let budget = Instant::now() + CONFLICT_RETRY_BUDGET;
        let deadline = options.deadline.map_or(budget, |d| d.min(budget));
        while Instant::now() < deadline {
            let snapshot = self.catalog.snapshot();
            changes.borrow_and_update();
            let (mut mutation, value) = build(&snapshot)?;
            mutation.generation = Some(snapshot.generation);
            if mutation.is_empty() {
                return Ok((value, snapshot));
            }
            match self.submit_with(token, mutation, options).await {
                Ok(snapshot) => return Ok((value, snapshot)),
                Err(MutationError::Conflict) => {
                    // Wait for the snapshot that resolves the conflict, but not forever: the row
                    // in flight may fail to write and leave the snapshot as it was.
                    let _ =
                        tokio::time::timeout(Duration::from_millis(50), changes.changed()).await;
                }
                Err(e) => return Err(e.into()),
            }
        }
        if options.expired(Instant::now()) {
            return Err(MutationError::Expired.into());
        }
        Err(MutationError::Conflict.into())
    }
}

fn writer_stopped() -> MutationError {
    MutationError::Write(Status::new(
        StatusCode::Aborted,
        "The catalog writer stopped",
    ))
}

struct WriterTask {
    catalog: Arc<Catalog>,
    store: Arc<dyn SysCatalogStore>,
    leadership: Arc<Leadership>,
    options: WriterOptions,
    metrics: WriterMetrics,
    /// Queued mutations of [`Lane::Priority`]; each batch takes these first.
    priority: VecDeque<Pending>,
    /// Queued mutations of [`Lane::Normal`].
    normal: VecDeque<Pending>,
    /// Rows written by batches in Raft.
    in_flight: HashSet<RowKey>,
    batches: JoinSet<Finished>,
}

impl WriterTask {
    fn queued(&self) -> usize {
        self.priority.len() + self.normal.len()
    }

    fn enqueue(&mut self, pending: Pending) {
        match pending.options.lane {
            Lane::Priority => self.priority.push_back(pending),
            Lane::Normal => self.normal.push_back(pending),
        }
    }

    /// The next mutation a batch takes: the oldest of the priority lane, else of the normal one.
    fn front(&self) -> Option<&Pending> {
        self.priority.front().or_else(|| self.normal.front())
    }

    fn pop_front(&mut self) -> Option<Pending> {
        self.priority
            .pop_front()
            .or_else(|| self.normal.pop_front())
    }

    async fn run(mut self, mut receiver: mpsc::UnboundedReceiver<Pending>) {
        let mut open = true;
        loop {
            let can_send = self.batches.len() < (self.options.max_inflight)().max(1);
            let idle = self.queued() == 0;
            tokio::select! {
                // Submissions are taken into the lanes as they arrive, also while every Raft
                // write slot is busy, so the queue depth counts them and a priority mutation
                // joins the next batch ahead of the normal lane.
                received = receiver.recv(), if open => {
                    match received {
                        Some(pending) => {
                            self.enqueue(pending);
                            while let Ok(pending) = receiver.try_recv() {
                                self.enqueue(pending);
                            }
                        }
                        None => open = false,
                    }
                    self.metrics.queue_depth.set(self.queued() as i64);
                }
                Some(finished) = self.batches.join_next() => {
                    let (keys, waiters, result) = finished.expect("catalog write task panicked");
                    for key in keys {
                        self.in_flight.remove(&key);
                    }
                    let outcome = match result {
                        Ok(()) => Ok(self.catalog.snapshot()),
                        Err(status) => Err(MutationError::Write(status)),
                    };
                    for done in waiters {
                        let _ = done.send(outcome.clone());
                    }
                }
                _ = std::future::ready(()), if can_send && !idle => {
                    while let Ok(pending) = receiver.try_recv() {
                        self.enqueue(pending);
                    }
                    self.send_batch();
                    self.metrics.queue_depth.set(self.queued() as i64);
                }
                // Stopped: no more submissions, nothing queued and no write in flight.
                else => return,
            }
        }
    }

    /// Validates pending mutations in order and sends the valid ones as one Raft write.
    fn send_batch(&mut self) {
        // Checked before taking the snapshot: a reload publishes its snapshot before it stops
        // reloading.
        let reloading = self.catalog.is_reloading();
        let snapshot = self.catalog.snapshot();
        let max_rows = (self.options.max_batch_rows)().max(1);
        let max_bytes = (self.options.max_batch_bytes)().max(1);
        let mut rows = Vec::new();
        let mut bytes = 0;
        let mut keys = Vec::new();
        let mut waiters = Vec::new();
        let mut term = None;
        let now = Instant::now();
        while let Some(pending) = self.front() {
            let size: usize = pending.mutation.writes.iter().map(RowChange::size).sum();
            if !rows.is_empty()
                && (rows.len() + pending.mutation.writes.len() > max_rows
                    || bytes + size > max_bytes)
            {
                break;
            }
            let pending = self.pop_front().unwrap();
            // Its caller gave up at the deadline, and counted it as expired.
            if pending.claimed.swap(true, Ordering::AcqRel) {
                continue;
            }
            self.metrics
                .queue_wait
                .record_duration(now.saturating_duration_since(pending.queued_at));
            if pending.options.expired(now) {
                self.metrics.expired.increment();
                let _ = pending.done.send(Err(MutationError::Expired));
                continue;
            }
            if let Err(status) = self.leadership.check(&pending.token) {
                let _ = pending.done.send(Err(MutationError::NotLeader(status)));
                continue;
            }
            if reloading || !snapshot.loaded || !self.is_fresh(&snapshot, &pending.mutation, &keys)
            {
                self.metrics.conflicts.increment();
                let _ = pending.done.send(Err(MutationError::Conflict));
                continue;
            }
            let changes: Vec<RowChange> = pending
                .mutation
                .writes
                .into_iter()
                .filter(|change| changes_row(&snapshot, change))
                .collect();
            if changes.is_empty() {
                let _ = pending.done.send(Ok(snapshot.clone()));
                continue;
            }
            term = Some(pending.token.term());
            for change in changes {
                bytes += change.size();
                keys.push(RowKey::new(change.entry_type(), change.id()));
                rows.push(change);
            }
            waiters.push(pending.done);
        }
        let Some(term) = term else { return };
        self.metrics.batch_rows.record(rows.len() as u64);
        self.in_flight.extend(keys.iter().cloned());
        let store = self.store.clone();
        let catalog = self.catalog.clone();
        let commit_latency = self.metrics.commit_latency.clone();
        self.batches.spawn(async move {
            let sent = Instant::now();
            let result = store.write_rows(term, rows).await;
            if result.is_ok() {
                // The write's rows are on the apply stream; ack once they are in the snapshot.
                catalog.barrier().await;
                commit_latency.record_duration(sent.elapsed());
            }
            (keys, waiters, result)
        });
    }

    /// Whether everything the mutation read is unchanged and nothing it touches is being written.
    fn is_fresh(&self, snapshot: &CatalogSnapshot, mutation: &Mutation, batch: &[RowKey]) -> bool {
        let busy = |key: &RowKey| self.in_flight.contains(key) || batch.contains(key);
        mutation.generation.is_none_or(|g| g == snapshot.generation)
            && mutation.reads.iter().all(|(key, version)| {
                !busy(key) && snapshot.get(key.entry_type, &key.id).map(|e| e.version) == *version
            })
            && mutation
                .writes
                .iter()
                .all(|w| !busy(&RowKey::new(w.entry_type(), w.id())))
    }
}

/// False for an upsert of the bytes already there and a delete of a missing row: C++ skips writes
/// whose new message equals the old one.
fn changes_row(snapshot: &CatalogSnapshot, change: &RowChange) -> bool {
    let current = snapshot.get(change.entry_type(), change.id());
    match change {
        RowChange::Upsert { data, .. } => current.is_none_or(|e| *e.raw != data[..]),
        RowChange::Delete { .. } => current.is_some(),
    }
}
