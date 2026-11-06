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

//! The master's in-memory catalog. See docs/catalog-state.md.
//!
//! - The materializer builds it from the sys-catalog apply stream, on every master (L1): one
//!   snapshot per applied batch, published through an `ArcSwap`. Leader and follower run the same
//!   code from the same log.
//! - The [`CatalogWriter`] is the only path that writes. It group-commits mutations into Raft
//!   writes and acknowledges each once the materializer has applied it, so the caller's next
//!   snapshot contains the change.
//! - Mutations are optimistic (L3): one that read a stale version, or a row another mutation has
//!   in flight, conflicts and [`CatalogWriter::mutate`] rebuilds it from the newer snapshot.

mod materializer;
mod snapshot;
mod writer;

pub use materializer::run_materializer;
pub use snapshot::{is_inactive_state, rows_digest, CatalogSnapshot};
pub use writer::{CatalogWriter, Lane, Mutation, MutationError, WriteOptions, WriterOptions};

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use arc_swap::ArcSwap;
use master_sys_catalog::{ApplyEvent, ApplySender, SysCatalogStore};
use protobuf::Enum;
use tokio::sync::watch;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::master_types::SysRowEntryType;

/// Counters for metrics and tests.
#[derive(Default)]
pub struct CatalogStats {
    /// Batches applied from the apply stream.
    pub batches_applied: AtomicU64,
    /// Full loads.
    pub loads: AtomicU64,
    /// Self-checks whose digest differed from a scan.
    pub self_check_mismatches: AtomicU64,
}

/// The published catalog.
pub struct Catalog {
    current: ArcSwap<CatalogSnapshot>,
    changed: watch::Sender<u64>,
    sender: ApplySender,
    /// Reloads sent on the stream.
    reloads_sent: watch::Receiver<u64>,
    /// Reloads whose load has been published.
    reloads_done: watch::Sender<u64>,
    /// Counters.
    pub stats: CatalogStats,
}

impl Catalog {
    /// An empty, unloaded catalog fed by the stream `sender` belongs to.
    pub fn new(sender: ApplySender) -> Arc<Catalog> {
        Arc::new(Catalog {
            current: ArcSwap::from_pointee(CatalogSnapshot::default()),
            changed: watch::channel(0).0,
            reloads_sent: sender.reloads_sent(),
            reloads_done: watch::channel(0).0,
            sender,
            stats: CatalogStats::default(),
        })
    }

    /// The latest snapshot.
    pub fn snapshot(&self) -> Arc<CatalogSnapshot> {
        self.current.load_full()
    }

    /// The `seq` of every published snapshot.
    pub fn subscribe(&self) -> watch::Receiver<u64> {
        self.changed.subscribe()
    }

    /// Whether a reload was requested and its load is not published yet. The snapshot is then
    /// known to be stale, and the writer refuses mutations.
    pub fn is_reloading(&self) -> bool {
        *self.reloads_sent.borrow() > *self.reloads_done.borrow()
    }

    /// Changes when a reload is requested and when one finishes.
    pub fn subscribe_reloads(&self) -> (watch::Receiver<u64>, watch::Receiver<u64>) {
        (self.sender.reloads_sent(), self.reloads_done.subscribe())
    }

    /// Called after the load for the first `received` reloads is published.
    fn finish_reloads(&self, received: u64) {
        self.reloads_done.send_if_modified(|done| {
            let changed = *done != received;
            *done = received;
            changed
        });
    }

    /// Resolves once every apply event queued before the call is reflected in the snapshot.
    pub async fn barrier(&self) {
        self.sender.barrier().await
    }

    /// Asks the materializer for a full load.
    pub fn request_reload(&self, reason: &str) {
        self.sender.send(ApplyEvent::Reload {
            reason: reason.to_string(),
            min_time: Default::default(),
        });
    }

    fn publish(&self, mut snapshot: CatalogSnapshot) -> Arc<CatalogSnapshot> {
        snapshot.seq = self.current.load().seq + 1;
        let seq = snapshot.seq;
        let snapshot = Arc::new(snapshot);
        self.current.store(snapshot.clone());
        self.changed.send_replace(seq);
        snapshot
    }
}

/// Compares the snapshot with a scan of the local tablet at the snapshot's read point. A mismatch
/// means a row reached the tablet without passing the apply tap: it is counted, logged, and fixed
/// by a full load. Returns whether they matched. Blocks.
pub fn self_check(catalog: &Catalog, store: &dyn SysCatalogStore) -> Result<bool, Status> {
    let snapshot = catalog.snapshot();
    if !snapshot.loaded {
        return Ok(true);
    }
    let mut rows = Vec::new();
    for entry_type in SysRowEntryType::VALUES {
        if *entry_type == SysRowEntryType::UNKNOWN {
            continue;
        }
        for row in store.scan(*entry_type, snapshot.as_of)? {
            rows.push((*entry_type as i32, row.id, row.data));
        }
    }
    let scanned = rows_digest(rows.iter().map(|(t, id, d)| (*t, &id[..], &d[..])));
    let current = catalog.snapshot();
    if current.generation != snapshot.generation {
        // Reloaded meanwhile; nothing to compare.
        return Ok(true);
    }
    let materialized = snapshot.digest();
    if scanned == materialized {
        return Ok(true);
    }
    catalog
        .stats
        .self_check_mismatches
        .fetch_add(1, Ordering::Relaxed);
    tracing::error!(
        "Catalog self-check mismatch at {:?}: materialized digest {materialized:x}, scanned \
         {scanned:x}; reloading",
        snapshot.as_of
    );
    catalog.request_reload("self-check mismatch");
    Ok(false)
}

/// The status for a catalog that has not loaded yet, as `ScopedLeaderSharedLock` reports it.
pub fn not_loaded_status() -> Status {
    Status::new(
        StatusCode::ServiceUnavailable,
        "Catalog manager is not initialized. State: kStarting",
    )
}

#[cfg(test)]
mod tests;
