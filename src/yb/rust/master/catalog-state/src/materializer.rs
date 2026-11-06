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

use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;

use master_sys_catalog::{
    AppliedBatch, ApplyEvent, ApplyEvents, HybridTime, RowChange, SysCatalogStore,
};
use protobuf::Enum;
use yb_host_api::Status;
use yb_proto::pb::master_types::SysRowEntryType;

use crate::{Catalog, CatalogSnapshot};

const SAFE_TIME_TIMEOUT: Duration = Duration::from_secs(30);

/// Builds the catalog from the apply stream until the stream closes.
///
/// A load subscribes first (the stream is already buffering), waits for the tablet's safe time T
/// to pass every batch already consumed, notes the op index A of the last batch the tap has sent,
/// and scans every entry type in parallel at T. A batch is in the scan when its hybrid time is at
/// most T and its op index at most A: the tap sends a batch after writing it, and MVCC makes every
/// write at or below T visible, except snapshot ops, which bypass MVCC and can apply at or below
/// a safe time already reached. Every other batch is applied and published as one snapshot.
/// Batches that arrive while a load is pending wait for it. A Reload starts a new load, which
/// gives every entity a new version.
pub async fn run_materializer(
    catalog: Arc<Catalog>,
    store: Arc<dyn SysCatalogStore>,
    mut events: ApplyEvents,
) {
    let mut materializer = Materializer {
        catalog,
        store,
        next_version: 1,
        consumed: HybridTime::MIN,
        scanned: (HybridTime::MIN, 0),
        generation: 0,
        reloads_received: 0,
    };
    // Barriers and batches that arrived while a load was pending; handled once it is done.
    let mut waiting: Vec<tokio::sync::oneshot::Sender<()>> = Vec::new();
    let mut held: Vec<AppliedBatch> = Vec::new();
    let mut load_at = Some(HybridTime::MIN);
    loop {
        if let Some(min_time) = load_at {
            match materializer.load(min_time, &events).await {
                Ok(()) => {
                    load_at = None;
                    for batch in held.drain(..) {
                        materializer.apply(batch);
                    }
                    materializer
                        .catalog
                        .finish_reloads(materializer.reloads_received);
                    for done in waiting.drain(..) {
                        let _ = done.send(());
                    }
                }
                Err(status) => {
                    tracing::warn!("Loading the catalog failed, retrying: {status}");
                    tokio::time::sleep(Duration::from_millis(500)).await;
                }
            }
        }
        let event = if load_at.is_some() {
            match events.try_recv() {
                Some(event) => event,
                None => continue,
            }
        } else {
            match events.recv().await {
                Some(event) => event,
                None => return,
            }
        };
        match event {
            ApplyEvent::Rows(batch) if load_at.is_some() => held.push(batch),
            ApplyEvent::Rows(batch) => materializer.apply(batch),
            ApplyEvent::Reload { reason, min_time } => {
                tracing::info!("Reloading the catalog: {reason}");
                materializer.reloads_received += 1;
                load_at = Some(load_at.map_or(min_time, |t: HybridTime| t.max(min_time)));
            }
            ApplyEvent::Barrier(done) if load_at.is_some() => waiting.push(done),
            ApplyEvent::Barrier(done) => {
                let _ = done.send(());
            }
        }
    }
}

struct Materializer {
    catalog: Arc<Catalog>,
    store: Arc<dyn SysCatalogStore>,
    next_version: u64,
    /// The latest hybrid time of a batch taken off the stream; a load reads no earlier.
    consumed: HybridTime,
    /// The last load's read time and op index: batches at or below both are in its scan.
    scanned: (HybridTime, i64),
    generation: u64,
    reloads_received: u64,
}

impl Materializer {
    async fn load(&mut self, min_time: HybridTime, events: &ApplyEvents) -> Result<(), Status> {
        self.generation += 1;
        let store = self.store.clone();
        if !store.is_open() {
            let snapshot = CatalogSnapshot {
                generation: self.generation,
                ..Default::default()
            };
            self.catalog.publish(snapshot);
            return Ok(());
        }
        let min_time = min_time.max(self.consumed);
        let read_time = {
            let store = store.clone();
            tokio::task::spawn_blocking(move || store.safe_time(min_time, SAFE_TIME_TIMEOUT))
                .await
                .expect("safe_time panicked")?
        };
        let scanned_index = events.last_rows_index_sent();
        let scans: Vec<_> = SysRowEntryType::VALUES
            .iter()
            .copied()
            .filter(|t| *t != SysRowEntryType::UNKNOWN)
            .map(|entry_type| {
                let store = store.clone();
                (
                    entry_type,
                    tokio::task::spawn_blocking(move || store.scan(entry_type, read_time)),
                )
            })
            .collect();
        let mut snapshot = CatalogSnapshot {
            loaded: true,
            generation: self.generation,
            as_of: read_time,
            ..Default::default()
        };
        let mut rows = 0;
        for (entry_type, scan) in scans {
            for row in scan.await.expect("scan panicked")? {
                let change = RowChange::Upsert {
                    entry_type,
                    id: row.id,
                    data: row.data,
                };
                self.apply_change(&mut snapshot, &change);
                rows += 1;
            }
        }
        self.scanned = (read_time, scanned_index);
        self.catalog.stats.loads.fetch_add(1, Ordering::Relaxed);
        tracing::info!(
            "Loaded {rows} catalog rows at {read_time:?}, op index {scanned_index} (generation {})",
            self.generation
        );
        self.catalog.publish(snapshot);
        Ok(())
    }

    fn apply(&mut self, batch: AppliedBatch) {
        self.consumed = self.consumed.max(batch.hybrid_time);
        let current = self.catalog.snapshot();
        let (scan_time, scan_index) = self.scanned;
        if !current.loaded || (batch.hybrid_time <= scan_time && batch.op_id.index <= scan_index) {
            // Before the first load, or already in the load's scan.
            return;
        }
        let mut snapshot = (*current).clone();
        for change in &batch.changes {
            self.apply_change(&mut snapshot, change);
        }
        snapshot.as_of = snapshot.as_of.max(batch.hybrid_time);
        snapshot.last_op = Some(batch.op_id);
        self.catalog
            .stats
            .batches_applied
            .fetch_add(1, Ordering::Relaxed);
        self.catalog.publish(snapshot);
    }

    fn apply_change(&mut self, snapshot: &mut CatalogSnapshot, change: &RowChange) {
        let version = self.next_version;
        self.next_version += 1;
        if let Err(e) = snapshot.apply(change, version) {
            // Only rows refused by design (D16, YEDIS) get here: running on with a catalog that
            // lacks them would serve wrong answers.
            panic!(
                "Cannot load sys catalog row {:?} {}: {e}",
                change.entry_type(),
                hex(change.id())
            );
        }
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
