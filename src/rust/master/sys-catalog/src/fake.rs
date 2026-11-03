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

use std::collections::BTreeMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use tokio::sync::oneshot;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::{
    apply_channel, AppliedBatch, ApplyEvent, ApplyEvents, ApplySender, HybridTime, OpId, Row,
    RowChange, SysCatalogStore, WriteFuture,
};

/// An in-memory sys catalog with MVCC history: a Raft log of one replica. Writes in the current
/// term apply at once (or when [`FakeSysCatalog::resume_writes`] is called after
/// [`FakeSysCatalog::pause_writes`]) and appear on the apply stream like the C++ tap's events.
pub struct FakeSysCatalog {
    state: Mutex<State>,
    sender: ApplySender,
}

/// Row versions in hybrid-time order; `None` is a delete.
type Versions = Vec<(u64, Option<Vec<u8>>)>;
type HeldWrite = (Vec<RowChange>, oneshot::Sender<Result<(), Status>>);

struct State {
    open: bool,
    term: i64,
    last_op: OpId,
    clock: u64,
    /// (entry_type, id) -> versions.
    rows: BTreeMap<(i32, Vec<u8>), Versions>,
    paused: bool,
    held: Vec<HeldWrite>,
    fail_next_write: Option<Status>,
    writes: usize,
}

impl FakeSysCatalog {
    /// An open, empty catalog in term 1, and its apply stream.
    pub fn new() -> (Arc<FakeSysCatalog>, ApplyEvents) {
        let (sender, events) = apply_channel();
        let fake = Arc::new(FakeSysCatalog {
            state: Mutex::new(State {
                open: true,
                term: 1,
                last_op: OpId::default(),
                clock: 1000,
                rows: BTreeMap::new(),
                paused: false,
                held: Vec::new(),
                fail_next_write: None,
                writes: 0,
            }),
            sender,
        });
        (fake, events)
    }

    /// The sender the fake posts on; tests use it for barriers.
    pub fn sender(&self) -> &ApplySender {
        &self.sender
    }

    /// Sets the Raft term; writes in other terms fail.
    pub fn set_term(&self, term: i64) {
        self.state.lock().unwrap().term = term;
    }

    /// Applies changes as if replicated from another leader.
    pub fn apply(&self, changes: Vec<RowChange>) -> HybridTime {
        let mut state = self.state.lock().unwrap();
        self.apply_locked(&mut state, changes)
    }

    /// Applies changes at an earlier hybrid time than the clock, as a snapshot op can (it bypasses
    /// MVCC): visible to scans at or after `ht` only from now on.
    pub fn apply_at(&self, ht: HybridTime, changes: Vec<RowChange>) {
        let mut state = self.state.lock().unwrap();
        let clock = state.clock;
        state.clock = ht.0 - 10;
        self.apply_locked(&mut state, changes);
        state.clock = clock;
    }

    fn apply_locked(&self, state: &mut State, changes: Vec<RowChange>) -> HybridTime {
        state.clock += 10;
        state.last_op = OpId {
            term: state.term,
            index: state.last_op.index + 1,
        };
        let ht = state.clock;
        for change in &changes {
            let value = match change {
                RowChange::Upsert { data, .. } => Some(data.clone()),
                RowChange::Delete { .. } => None,
            };
            state
                .rows
                .entry((change.entry_type() as i32, change.id().to_vec()))
                .or_default()
                .push((ht, value));
        }
        self.sender.send(ApplyEvent::Rows(AppliedBatch {
            op_id: state.last_op,
            hybrid_time: HybridTime(ht),
            changes,
        }));
        HybridTime(ht)
    }

    /// Applies changes without posting them on the apply stream, like a write path the tap does
    /// not see.
    pub fn apply_silently(&self, changes: Vec<RowChange>) {
        let mut state = self.state.lock().unwrap();
        state.clock += 10;
        let ht = state.clock;
        for change in changes {
            let value = match &change {
                RowChange::Upsert { data, .. } => Some(data.clone()),
                RowChange::Delete { .. } => None,
            };
            state
                .rows
                .entry((change.entry_type() as i32, change.id().to_vec()))
                .or_default()
                .push((ht, value));
        }
    }

    /// Rewrites every row to its state at `at`, the way a PITR restore does: without rows on the
    /// apply stream, then a Reload.
    pub fn restore(&self, at: HybridTime) {
        let mut state = self.state.lock().unwrap();
        state.clock += 10;
        let ht = state.clock;
        for versions in state.rows.values_mut() {
            let old = versions
                .iter()
                .rev()
                .find(|(t, _)| *t <= at.0)
                .and_then(|(_, v)| v.clone());
            versions.push((ht, old));
        }
        drop(state);
        self.sender.send(ApplyEvent::Reload {
            reason: "restore".into(),
            min_time: HybridTime(ht),
        });
    }

    /// Opens or closes the tablet (shell mode), posting a Reload like the host.
    pub fn set_open(&self, open: bool) {
        self.state.lock().unwrap().open = open;
        self.sender.send(ApplyEvent::Reload {
            reason: "open changed".into(),
            min_time: HybridTime::MIN,
        });
    }

    /// Holds writes until [`FakeSysCatalog::resume_writes`].
    pub fn pause_writes(&self) {
        self.state.lock().unwrap().paused = true;
    }

    /// Applies the held writes in order.
    pub fn resume_writes(&self) {
        let mut state = self.state.lock().unwrap();
        state.paused = false;
        for (rows, done) in std::mem::take(&mut state.held) {
            self.apply_locked(&mut state, rows);
            let _ = done.send(Ok(()));
        }
    }

    /// Fails the next write with `status`.
    pub fn fail_next_write(&self, status: Status) {
        self.state.lock().unwrap().fail_next_write = Some(status);
    }

    /// Raft writes that applied.
    pub fn writes(&self) -> usize {
        self.state.lock().unwrap().writes
    }

    /// Every live row at `at`: `(entry_type, id) -> data`.
    pub fn rows_at(&self, at: HybridTime) -> BTreeMap<(i32, Vec<u8>), Vec<u8>> {
        let state = self.state.lock().unwrap();
        state
            .rows
            .iter()
            .filter_map(|(key, versions)| {
                versions
                    .iter()
                    .rev()
                    .find(|(t, _)| *t <= at.0)
                    .and_then(|(_, v)| v.clone())
                    .map(|v| (key.clone(), v))
            })
            .collect()
    }

    /// The latest hybrid time written.
    pub fn now(&self) -> HybridTime {
        HybridTime(self.state.lock().unwrap().clock)
    }
}

impl SysCatalogStore for FakeSysCatalog {
    fn is_open(&self) -> bool {
        self.state.lock().unwrap().open
    }

    fn safe_time(&self, min_time: HybridTime, _timeout: Duration) -> Result<HybridTime, Status> {
        let mut state = self.state.lock().unwrap();
        state.clock = state.clock.max(min_time.0);
        Ok(HybridTime(state.clock))
    }

    fn scan(&self, entry_type: SysRowEntryType, read_time: HybridTime) -> Result<Vec<Row>, Status> {
        if !self.is_open() {
            return Err(Status::new(StatusCode::IllegalState, "not open"));
        }
        Ok(self
            .rows_at(read_time)
            .into_iter()
            .filter(|((t, _), _)| *t == entry_type as i32)
            .map(|((_, id), data)| Row { id, data })
            .collect())
    }

    fn write_rows(&self, term: i64, rows: Vec<RowChange>) -> WriteFuture {
        let mut state = self.state.lock().unwrap();
        if let Some(status) = state.fail_next_write.take() {
            return Box::pin(async move { Err(status) });
        }
        if term != state.term || !state.open {
            let message = format!("term {term} is not the current term {}", state.term);
            return Box::pin(async move { Err(Status::new(StatusCode::IllegalState, message)) });
        }
        state.writes += 1;
        if state.paused {
            let (sender, receiver) = oneshot::channel();
            state.held.push((rows, sender));
            return Box::pin(async move {
                receiver
                    .await
                    .unwrap_or_else(|_| Err(Status::new(StatusCode::Aborted, "dropped")))
            });
        }
        self.apply_locked(&mut state, rows);
        Box::pin(async { Ok(()) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn upsert(id: &str, data: &str) -> RowChange {
        RowChange::Upsert {
            entry_type: SysRowEntryType::TABLE,
            id: id.into(),
            data: data.into(),
        }
    }

    #[tokio::test]
    async fn writes_appear_on_the_stream_and_in_scans() {
        let (fake, mut events) = FakeSysCatalog::new();
        let before = fake.now();
        fake.write_rows(1, vec![upsert("t1", "a")]).await.unwrap();
        let Some(ApplyEvent::Rows(batch)) = events.recv().await else {
            panic!("expected rows");
        };
        assert_eq!(batch.changes, vec![upsert("t1", "a")]);
        assert_eq!(batch.op_id.index, 1);
        assert!(fake
            .scan(SysRowEntryType::TABLE, before)
            .unwrap()
            .is_empty());
        let rows = fake
            .scan(SysRowEntryType::TABLE, batch.hybrid_time)
            .unwrap();
        assert_eq!(rows[0].data, b"a");
        assert!(fake.write_rows(2, vec![upsert("t1", "b")]).await.is_err());
    }

    #[tokio::test]
    async fn restore_rewrites_history_and_posts_reload() {
        let (fake, mut events) = FakeSysCatalog::new();
        let t1 = fake.apply(vec![upsert("t1", "a")]);
        fake.apply(vec![upsert("t1", "b"), upsert("t2", "c")]);
        fake.restore(t1);
        while let Some(event) = events.try_recv() {
            if let ApplyEvent::Reload { min_time, .. } = event {
                let rows = fake.rows_at(min_time);
                assert_eq!(rows.len(), 1);
                assert_eq!(rows.values().next().unwrap(), b"a");
                return;
            }
        }
        panic!("no reload");
    }

    #[tokio::test]
    async fn paused_writes_complete_on_resume() {
        let (fake, _events) = FakeSysCatalog::new();
        fake.pause_writes();
        let write = fake.write_rows(1, vec![upsert("t1", "a")]);
        assert!(fake.rows_at(fake.now()).is_empty());
        fake.resume_writes();
        write.await.unwrap();
        assert_eq!(fake.rows_at(fake.now()).len(), 1);
    }
}
