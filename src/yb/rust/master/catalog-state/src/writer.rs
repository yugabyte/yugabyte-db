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
use std::sync::Arc;
use std::time::Duration;

use master_catalog_entity::{Entity, RowKey};
use master_leadership::{LeaderToken, Leadership};
use master_sys_catalog::{RowChange, SysCatalogStore};
use tokio::sync::{mpsc, oneshot};
use tokio::task::JoinSet;
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

/// Why a mutation was not applied.
#[derive(Clone, Debug, PartialEq)]
pub enum MutationError {
    /// It read a row that changed or is being written; rebuild it from a newer snapshot.
    Conflict,
    /// The token is not current.
    NotLeader(Status),
    /// The Raft write failed.
    Write(Status),
}

impl fmt::Display for MutationError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MutationError::Conflict => write!(f, "conflict"),
            MutationError::NotLeader(s) => write!(f, "not the leader: {s}"),
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
            MutationError::NotLeader(s) | MutationError::Write(s) => s,
        }
    }
}

/// Writer limits, read when each batch is formed so flag changes take effect.
#[derive(Clone, Copy)]
pub struct WriterOptions {
    /// `catalog_writer_max_batch_rows`.
    pub max_batch_rows: fn() -> usize,
    /// `catalog_writer_max_batch_bytes`.
    pub max_batch_bytes: fn() -> usize,
    /// `catalog_writer_max_inflight`: Raft writes outstanding at once.
    pub max_inflight: fn() -> usize,
}

impl Default for WriterOptions {
    fn default() -> Self {
        WriterOptions {
            max_batch_rows: || 1000,
            max_batch_bytes: || 4 << 20,
            max_inflight: || 4,
        }
    }
}

type Done = oneshot::Sender<Result<Arc<CatalogSnapshot>, MutationError>>;
/// A finished Raft write: the rows it held, its waiters and its outcome.
type Finished = (Vec<RowKey>, Vec<Done>, Result<(), Status>);

struct Pending {
    token: LeaderToken,
    mutation: Mutation,
    done: Done,
}

/// The only path that writes the catalog. See the crate docs.
pub struct CatalogWriter {
    catalog: Arc<Catalog>,
    queue: mpsc::UnboundedSender<Pending>,
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
        let task = WriterTask {
            catalog: catalog.clone(),
            store,
            leadership,
            options,
            pending: VecDeque::new(),
            in_flight: HashSet::new(),
            batches: JoinSet::new(),
        };
        tokio::spawn(task.run(receiver));
        Arc::new(CatalogWriter { catalog, queue })
    }

    /// Writes one mutation. On success the returned snapshot (and every later one) contains it.
    pub async fn submit(
        &self,
        token: &LeaderToken,
        mutation: Mutation,
    ) -> Result<Arc<CatalogSnapshot>, MutationError> {
        let (done, result) = oneshot::channel();
        self.queue
            .send(Pending {
                token: *token,
                mutation,
                done,
            })
            .map_err(|_| writer_stopped())?;
        result.await.unwrap_or_else(|_| Err(writer_stopped()))
    }

    /// Builds a mutation from the latest snapshot and writes it, rebuilding on conflict. `build`
    /// returns the mutation and a value for the caller; an empty mutation is not written.
    pub async fn mutate<T>(
        &self,
        token: &LeaderToken,
        mut build: impl FnMut(&CatalogSnapshot) -> Result<(Mutation, T), Status>,
    ) -> Result<(T, Arc<CatalogSnapshot>), Status> {
        let mut changes = self.catalog.subscribe();
        let deadline = tokio::time::Instant::now() + CONFLICT_RETRY_BUDGET;
        while tokio::time::Instant::now() < deadline {
            let snapshot = self.catalog.snapshot();
            changes.borrow_and_update();
            let (mut mutation, value) = build(&snapshot)?;
            mutation.generation = Some(snapshot.generation);
            if mutation.is_empty() {
                return Ok((value, snapshot));
            }
            match self.submit(token, mutation).await {
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
    pending: VecDeque<Pending>,
    /// Rows written by batches in Raft.
    in_flight: HashSet<RowKey>,
    batches: JoinSet<Finished>,
}

impl WriterTask {
    async fn run(mut self, mut receiver: mpsc::UnboundedReceiver<Pending>) {
        loop {
            let can_send = self.batches.len() < (self.options.max_inflight)().max(1);
            tokio::select! {
                received = receiver.recv(), if self.pending.is_empty() => {
                    match received {
                        Some(pending) => self.pending.push_back(pending),
                        None if self.batches.is_empty() => return,
                        None => {}
                    }
                    while let Ok(pending) = receiver.try_recv() {
                        self.pending.push_back(pending);
                    }
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
                _ = std::future::ready(()), if can_send && !self.pending.is_empty() => {
                    self.send_batch();
                }
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
        while let Some(pending) = self.pending.front() {
            let size: usize = pending.mutation.writes.iter().map(RowChange::size).sum();
            if !rows.is_empty()
                && (rows.len() + pending.mutation.writes.len() > max_rows
                    || bytes + size > max_bytes)
            {
                break;
            }
            let pending = self.pending.pop_front().unwrap();
            if let Err(status) = self.leadership.check(&pending.token) {
                let _ = pending.done.send(Err(MutationError::NotLeader(status)));
                continue;
            }
            if reloading || !snapshot.loaded || !self.is_fresh(&snapshot, &pending.mutation, &keys)
            {
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
        self.in_flight.extend(keys.iter().cloned());
        let store = self.store.clone();
        let catalog = self.catalog.clone();
        self.batches.spawn(async move {
            let result = store.write_rows(term, rows).await;
            if result.is_ok() {
                // The write's rows are on the apply stream; ack once they are in the snapshot.
                catalog.barrier().await;
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
