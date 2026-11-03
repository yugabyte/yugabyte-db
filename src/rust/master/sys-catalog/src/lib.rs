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

//! The master's view of the sys-catalog tablet: typed scans at a read time, Raft writes in a term,
//! and the apply stream, the rows the tablet applies in log order on every master. See
//! docs/sys-catalog.md.
//!
//! The C++ host implements [`SysCatalogStore`] (master-server); [`FakeSysCatalog`] implements it
//! in memory for tests.

mod fake;

pub use fake::FakeSysCatalog;

use std::future::Future;
use std::pin::Pin;
use std::sync::atomic::{AtomicI64, AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::Duration;

use tokio::sync::{mpsc, oneshot, watch};
use yb_host_api::Status;
use yb_proto::pb::master_types::SysRowEntryType;

/// A hybrid time, as `HybridTime::ToUint64()` encodes it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct HybridTime(pub u64);

impl HybridTime {
    /// `HybridTime::kMin`.
    pub const MIN: HybridTime = HybridTime(0);
}

/// A Raft op id.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct OpId {
    /// The term.
    pub term: i64,
    /// The index.
    pub index: i64,
}

/// One row change, as written and as applied.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RowChange {
    /// Sets the row's metadata.
    Upsert {
        /// The row's entry type.
        entry_type: SysRowEntryType,
        /// The row's entry id.
        id: Vec<u8>,
        /// The serialized entity.
        data: Vec<u8>,
    },
    /// Deletes the row.
    Delete {
        /// The row's entry type.
        entry_type: SysRowEntryType,
        /// The row's entry id.
        id: Vec<u8>,
    },
}

impl RowChange {
    /// The row's entry type.
    pub fn entry_type(&self) -> SysRowEntryType {
        match self {
            RowChange::Upsert { entry_type, .. } | RowChange::Delete { entry_type, .. } => {
                *entry_type
            }
        }
    }

    /// The row's entry id.
    pub fn id(&self) -> &[u8] {
        match self {
            RowChange::Upsert { id, .. } | RowChange::Delete { id, .. } => id,
        }
    }

    /// Bytes this change adds to a Raft write, roughly.
    pub fn size(&self) -> usize {
        match self {
            RowChange::Upsert { id, data, .. } => id.len() + data.len() + 16,
            RowChange::Delete { id, .. } => id.len() + 16,
        }
    }
}

/// A row returned by a scan.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Row {
    /// The entry id.
    pub id: Vec<u8>,
    /// The serialized entity.
    pub data: Vec<u8>,
}

/// The rows of one applied Raft op.
#[derive(Clone, Debug)]
pub struct AppliedBatch {
    /// The op.
    pub op_id: OpId,
    /// The hybrid time its rows were written at.
    pub hybrid_time: HybridTime,
    /// Its row changes, in write order.
    pub changes: Vec<RowChange>,
}

/// What the apply stream carries.
#[derive(Debug)]
pub enum ApplyEvent {
    /// An applied op that wrote catalog rows.
    Rows(AppliedBatch),
    /// The tablet changed in a way the stream does not show as rows (opened, a PITR restore, a
    /// remote bootstrap, shell mode): scan it again at a safe time no earlier than `min_time`.
    Reload {
        /// For the log.
        reason: String,
        /// The earliest read time that covers the change.
        min_time: HybridTime,
    },
    /// Answered once every event sent before it has been processed.
    Barrier(oneshot::Sender<()>),
}

/// Queue depth above which [`ApplySender`] warns that the consumer is falling behind.
const DEPTH_WARNING: usize = 10_000;

struct StreamShared {
    depth: AtomicUsize,
    /// The op index of the last `Rows` event sent. The tap sends a batch after writing it to the
    /// tablet, so every op at or below this index is visible to a scan at its hybrid time.
    last_rows_index: AtomicI64,
    /// `Reload` events sent.
    reloads: watch::Sender<u64>,
}

/// The sending half of the apply stream. Never blocks: the tablet's apply thread sends on it.
#[derive(Clone)]
pub struct ApplySender {
    sender: mpsc::UnboundedSender<ApplyEvent>,
    shared: Arc<StreamShared>,
}

/// The receiving half of the apply stream.
pub struct ApplyEvents {
    receiver: mpsc::UnboundedReceiver<ApplyEvent>,
    shared: Arc<StreamShared>,
}

/// Creates the apply stream.
pub fn apply_channel() -> (ApplySender, ApplyEvents) {
    let (sender, receiver) = mpsc::unbounded_channel();
    let shared = Arc::new(StreamShared {
        depth: AtomicUsize::new(0),
        last_rows_index: AtomicI64::new(0),
        reloads: watch::channel(0).0,
    });
    (
        ApplySender {
            sender,
            shared: shared.clone(),
        },
        ApplyEvents { receiver, shared },
    )
}

impl ApplySender {
    /// Queues an event. Dropped when the consumer is gone (shutdown).
    pub fn send(&self, event: ApplyEvent) {
        match &event {
            ApplyEvent::Rows(batch) => {
                self.shared
                    .last_rows_index
                    .fetch_max(batch.op_id.index, Ordering::AcqRel);
            }
            ApplyEvent::Reload { .. } => {
                self.shared.reloads.send_modify(|n| *n += 1);
            }
            ApplyEvent::Barrier(_) => {}
        }
        let depth = self.shared.depth.fetch_add(1, Ordering::Relaxed) + 1;
        if depth % DEPTH_WARNING == 0 {
            tracing::warn!("{depth} sys catalog apply events are queued for the Rust catalog");
        }
        if self.sender.send(event).is_err() {
            self.shared.depth.fetch_sub(1, Ordering::Relaxed);
        }
    }

    /// Resolves once every event sent before this call has been processed.
    pub async fn barrier(&self) {
        let (sender, receiver) = oneshot::channel();
        self.send(ApplyEvent::Barrier(sender));
        // An error means the consumer stopped, so nothing will be processed anyway.
        let _ = receiver.await;
    }

    /// Events queued and not yet received.
    pub fn depth(&self) -> usize {
        self.shared.depth.load(Ordering::Relaxed)
    }

    /// How many `Reload` events were sent; changes as soon as one is sent, before it is received.
    pub fn reloads_sent(&self) -> watch::Receiver<u64> {
        self.shared.reloads.subscribe()
    }
}

impl ApplyEvents {
    /// The next event; `None` once every sender is gone.
    pub async fn recv(&mut self) -> Option<ApplyEvent> {
        let event = self.receiver.recv().await;
        if event.is_some() {
            self.shared.depth.fetch_sub(1, Ordering::Relaxed);
        }
        event
    }

    /// The next event if one is queued.
    pub fn try_recv(&mut self) -> Option<ApplyEvent> {
        let event = self.receiver.try_recv().ok();
        if event.is_some() {
            self.shared.depth.fetch_sub(1, Ordering::Relaxed);
        }
        event
    }

    /// The op index of the last `Rows` event sent, received or not.
    pub fn last_rows_index_sent(&self) -> i64 {
        self.shared.last_rows_index.load(Ordering::Acquire)
    }
}

/// The outcome of a Raft write.
pub type WriteFuture = Pin<Box<dyn Future<Output = Result<(), Status>> + Send>>;

/// The sys-catalog tablet. Applied rows arrive on the [`ApplySender`] the store was built with.
pub trait SysCatalogStore: Send + Sync + 'static {
    /// Whether the tablet is open (not in shell mode). Scans and writes fail otherwise.
    fn is_open(&self) -> bool;
    /// The tablet's safe time, waiting up to `timeout` for it to reach `min_time`. Blocks.
    fn safe_time(&self, min_time: HybridTime, timeout: Duration) -> Result<HybridTime, Status>;
    /// The rows of one entry type as of `read_time`. Blocks.
    fn scan(&self, entry_type: SysRowEntryType, read_time: HybridTime) -> Result<Vec<Row>, Status>;
    /// One Raft write in `term`, which Raft refuses once the term has changed. Resolves once the
    /// write is applied locally, so its rows are already on the apply stream.
    fn write_rows(&self, term: i64, rows: Vec<RowChange>) -> WriteFuture;
}
