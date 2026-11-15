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

//! Splits of tables in CDCSDK streams. The children get `cdc_state` rows when they are registered
//! (`UpdateCDCProducerOnTabletSplit`), and the parent is hidden rather than deleted until every
//! stream is done with it (`DoProcessCDCSDKTabletDeletion`). C++ keeps the retained parents in
//! `retained_by_cdcsdk_`, rebuilt from the hidden tablets at load; here a hidden split parent of a
//! streamed table is retained until a pass of this leader released it.

use std::collections::{HashMap, HashSet};

use master_catalog_state::CatalogSnapshot;
use protobuf::MessageField;
use yb_host_api::Status;
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, SysTablesEntryPB, SysTabletsEntryPB};
use yb_proto::pb::master_rust_peer::{
    CdcStateDeleteRequestPB, CdcStateDeleteResponsePB, CdcStateEntryPB, CdcStateReadRequestPB,
    CdcStateReadResponsePB, CdcStateUpsertRequestPB, CdcStateUpsertResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::opid::OpIdPB;

use crate::create::is_logical_replication_stream;
use crate::streams::{stream_rows, StreamRow};
use crate::Xrepl;

const SLOT_ENTRY_TABLET_ID: &[u8] = b"dummy_id_for_replication_slot";

fn now_micros() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_micros() as u64)
}

/// A table being hidden or hidden after a drop (`started_hiding`).
fn table_dropped(snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
    snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
        .is_some_and(|t| t.hide_state() != sys_tables_entry_pb::HideState::VISIBLE)
}

/// `GetXReplStreamsForTable(CDCSDK)`: the live CDCSDK streams that have the table and are not
/// removing it (the table is not in their unqualified list).
fn split_streams_of_table(snapshot: &CatalogSnapshot, table_id: &[u8]) -> Vec<StreamRow> {
    cdcsdk_streams_of_table(snapshot, table_id)
        .into_iter()
        .filter(|r| !r.pb.unqualified_table_id.iter().any(|t| t == table_id))
        .collect()
}

/// `GetCDCSDKStreamsForTable`: the live CDCSDK streams that have the table.
pub(crate) fn cdcsdk_streams_of_table(
    snapshot: &CatalogSnapshot,
    table_id: &[u8],
) -> Vec<StreamRow> {
    stream_rows(snapshot)
        .into_iter()
        .filter(|r| {
            !r.is_deleting()
                && !r.pb.namespace_id().is_empty()
                && r.pb.table_id.iter().any(|t| t.as_bytes() == table_id)
        })
        .collect()
}

impl Xrepl {
    /// `CDCSDKPopulateDeleteRetainerInfoForTableDrop`: a dropped table that a logical replication
    /// stream has, with `enable_table_rewrite_for_cdcsdk_table`. Colocated tables are not
    /// retained (C++ keeps them in `colocated_tables_retained_by_cdcsdk_`).
    pub(crate) fn retains_table(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
        (self.options.table_rewrite_retention)()
            && !snapshot
                .get(SysRowEntryType::TABLE, table_id)
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                .is_some_and(|t| t.colocated())
            && cdcsdk_streams_of_table(snapshot, table_id)
                .iter()
                .any(|s| is_logical_replication_stream(snapshot, s))
    }

    /// `CDCSDKShouldRetainHiddenTablet`, not yet released: a split parent of a table in a CDCSDK
    /// stream (`CDCSDKPopulateDeleteRetainerInfoForTabletDrop`), or a tablet of a dropped table
    /// that is retained.
    pub(crate) fn retains_tablet(&self, snapshot: &CatalogSnapshot, tablet_id: &[u8]) -> bool {
        let Some(pb) = snapshot
            .get(SysRowEntryType::TABLET, tablet_id)
            .and_then(|t| t.parse::<SysTabletsEntryPB>().ok())
        else {
            return false;
        };
        if self
            .released_split_parents
            .lock()
            .unwrap()
            .contains(tablet_id)
        {
            return false;
        }
        let split_parent = pb.split_tablet_ids.len() >= 2
            && !cdcsdk_streams_of_table(snapshot, pb.table_id()).is_empty();
        split_parent
            || (table_dropped(snapshot, pb.table_id())
                && self.retains_table(snapshot, pb.table_id()))
    }

    /// `UpdateCDCProducerOnTabletSplit`: a row per child and stream; a CDCSDK stream's from the
    /// parent's row.
    pub(crate) async fn update_cdc_producer_on_split(
        &self,
        table_id: &[u8],
        parent: &[u8],
        children: &[Vec<u8>],
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Ok(());
        };
        // xCluster streams' children start at 0.0 with no replication time: the parent's SPLIT_OP
        // moves them, and their first poll sets the time, which tells the parent is done with.
        let mut xcluster = CdcStateUpsertRequestPB::new();
        for stream in crate::streams::stream_rows(&snapshot)
            .into_iter()
            .filter(|r| {
                r.pb.namespace_id().is_empty()
                    && !r.is_deleting()
                    && r.pb.table_id.first().is_some_and(|t| {
                        crate::streams::strip_sequences_data_alias(t).as_bytes() == table_id
                    })
            })
        {
            for child in children {
                let mut entry = CdcStateEntryPB::new();
                entry.set_tablet_id(child.clone());
                entry.set_stream_id(stream.id.as_bytes().to_vec());
                let mut min = OpIdPB::new();
                min.set_term(0);
                min.set_index(0);
                entry.checkpoint = MessageField::some(min);
                xcluster.entries.push(entry);
            }
        }
        if !xcluster.entries.is_empty() {
            let _: CdcStateUpsertResponsePB = self.host_call("CdcStateUpsert", xcluster).await?;
        }
        // Indexes and materialized views are on their way out of the streams.
        if !self.eligible_table_unchecked(&snapshot, table) {
            tracing::info!(
                "Skipping adding children tablets to cdc state for table {} as it is not meant to \
                 part of a CDC stream",
                String::from_utf8_lossy(table_id)
            );
            return Ok(());
        }
        let mut upsert = CdcStateUpsertRequestPB::new();
        let streams = split_streams_of_table(&snapshot, table_id);
        self.sync_point("UpdateCDCProducerOnTabletSplit::FindStreamsForAddingChildEntriesComplete")
            .await;
        for stream in &streams {
            let mut read = CdcStateReadRequestPB::new();
            read.set_stream_id(stream.id.as_bytes().to_vec());
            let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
            let parent_row = rows
                .entries
                .into_iter()
                .find(|r| r.tablet_id() == parent && r.colocated_table_id().is_empty());
            let mut active_time = now_micros();
            // A consistent snapshot stream's children are of interest iff the parent is.
            if stream
                .pb
                .cdcsdk_stream_metadata
                .has_consistent_snapshot_option()
            {
                match parent_row.as_ref().filter(|r| r.has_active_time()) {
                    Some(r) => active_time = r.active_time(),
                    None => tracing::warn!(
                        "Did not find active_time value in the cdc state table for parent \
                         tablet: {} and stream: {}",
                        String::from_utf8_lossy(parent),
                        stream.id
                    ),
                }
            }
            let logical = is_logical_replication_stream(&snapshot, stream);
            for child in children {
                let mut entry = CdcStateEntryPB::new();
                entry.set_tablet_id(child.clone());
                entry.set_stream_id(stream.id.as_bytes().to_vec());
                // A logical replication stream continues from the parent's checkpoint; the
                // others from 0.0, which the parent's SPLIT_OP moves.
                entry.checkpoint = match parent_row.as_ref().filter(|_| logical) {
                    Some(r) => r.checkpoint.clone(),
                    None => {
                        let mut min = OpIdPB::new();
                        min.set_term(0);
                        min.set_index(0);
                        MessageField::some(min)
                    }
                };
                entry.set_active_time(active_time);
                entry.set_cdc_sdk_safe_time(
                    parent_row
                        .as_ref()
                        .filter(|r| r.has_cdc_sdk_safe_time())
                        .map_or(active_time, |r| r.cdc_sdk_safe_time()),
                );
                upsert.entries.push(entry);
            }
        }
        if upsert.entries.is_empty() {
            return Ok(());
        }
        let entries = upsert.entries.clone();
        let _: CdcStateUpsertResponsePB = self.host_call("CdcStateUpsert", upsert).await?;
        self.sync_point("UpdateCDCProducerOnTabletSplit::AddChildEntriesComplete")
            .await;
        self.sync_point("UpdateCDCProducerOnTabletSplit::ReVerifyStreamForAddingChildEntries")
            .await;
        // `ReVerifyChildrenEntriesOnTabletSplit`: rows of streams the table left meanwhile go.
        let still: HashSet<String> = split_streams_of_table(&self.catalog.snapshot(), table_id)
            .into_iter()
            .map(|s| s.id)
            .collect();
        let gone: Vec<CdcStateEntryPB> = entries
            .into_iter()
            .filter(|e| !still.contains(String::from_utf8_lossy(e.stream_id()).as_ref()))
            .collect();
        if !gone.is_empty() {
            tracing::info!(
                "Deleting {} state table entries as their table is being/has been removed from \
                 the stream",
                gone.len()
            );
            let mut req = CdcStateDeleteRequestPB::new();
            req.keys = gone;
            let _: CdcStateDeleteResponsePB = self.host_call("CdcStateDelete", req).await?;
        }
        Ok(())
    }

    /// One pass of `DoProcessCDCSDKTabletDeletion`: a hidden split parent is released once, for
    /// every stream of its table, its row is gone, the stream never read it, the stream reads both
    /// children, or the stream's slot moved past it. The rows of streams done with it are
    /// deleted; the hidden-objects cleanup then deletes the released tablet.
    pub async fn process_cdcsdk_hidden_tablets(&self) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let candidates: HashMap<Vec<u8>, SysTabletsEntryPB> = snapshot
            .iter(SysRowEntryType::TABLET)
            .filter(|t| t.tablet().is_some_and(|h| h.hidden))
            .filter(|t| self.retains_tablet(&snapshot, &t.key.id))
            .filter_map(|t| Some((t.key.id.to_vec(), t.parse::<SysTabletsEntryPB>().ok()?)))
            .collect();
        if candidates.is_empty() {
            return Ok(());
        }
        let mut rows_by_stream: HashMap<String, Vec<CdcStateEntryPB>> = HashMap::new();
        let mut delete = Vec::new();
        let mut released = Vec::new();
        for (tablet_id, tablet) in &candidates {
            // A parent still around is processed first.
            if candidates.contains_key(tablet.split_parent_tablet_id()) {
                continue;
            }
            let table_deleted = table_dropped(&snapshot, tablet.table_id());
            let streams = cdcsdk_streams_of_table(&snapshot, tablet.table_id());
            for stream in &streams {
                if !rows_by_stream.contains_key(&stream.id) {
                    let mut read = CdcStateReadRequestPB::new();
                    read.set_stream_id(stream.id.as_bytes().to_vec());
                    let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
                    rows_by_stream.insert(stream.id.clone(), rows.entries);
                }
            }
            let min_restart_time =
                min_restart_time_across_slots(&snapshot, &streams, &rows_by_stream);
            let mut done = 0;
            for stream in &streams {
                let rows = &rows_by_stream[&stream.id];
                let row_of = |id: &[u8]| {
                    rows.iter()
                        .find(|r| r.tablet_id() == id && r.colocated_table_id().is_empty())
                };
                // Case 1: no row.
                let Some(row) = row_of(tablet_id) else {
                    done += 1;
                    continue;
                };
                let key = || {
                    let mut k = CdcStateEntryPB::new();
                    k.set_tablet_id(tablet_id.clone());
                    k.set_stream_id(stream.id.as_bytes().to_vec());
                    k
                };
                if !table_deleted {
                    // Case 2: the stream never read the parent.
                    let cp = row.checkpoint.as_ref();
                    let unread = cp.is_some_and(|c| {
                        (c.term() == -1 && c.index() == -1)
                            || (c.term() == 0 && c.index() == 0 && !row.has_last_replication_time())
                    });
                    if unread {
                        done += 1;
                        continue;
                    }
                    // Case 3: the stream reads both children.
                    let children_read = tablet
                        .split_tablet_ids
                        .iter()
                        .all(|c| row_of(c).is_some_and(|r| r.has_last_replication_time()));
                    if children_read {
                        tracing::info!(
                            "Deleting tablet {} from stream {}. Reason: Consumer finished \
                             processing parent tablet after split.",
                            String::from_utf8_lossy(tablet_id),
                            stream.id
                        );
                        delete.push(key());
                        done += 1;
                        continue;
                    }
                }
                // Case 4: the slots moved past the hide time, or the tablet's barriers expired.
                if let Some(min_restart) = min_restart_time {
                    let expired = row
                        .active_time()
                        .saturating_add((self.options.intent_retention)().as_micros() as u64)
                        < now_micros();
                    if tablet.hide_hybrid_time() < min_restart || expired {
                        delete.push(key());
                        done += 1;
                    }
                }
            }
            if done == streams.len() {
                released.push(tablet_id.clone());
            }
        }
        if !delete.is_empty() {
            let mut req = CdcStateDeleteRequestPB::new();
            req.keys = delete;
            let _: CdcStateDeleteResponsePB = self.host_call("CdcStateDelete", req).await?;
        }
        self.released_split_parents.lock().unwrap().extend(released);
        Ok(())
    }
}

/// `GetMinRestartTimeAcrossSlots`: the oldest restart time of the streams' slots; none for gRPC
/// streams.
fn min_restart_time_across_slots(
    snapshot: &CatalogSnapshot,
    streams: &[StreamRow],
    rows_by_stream: &HashMap<String, Vec<CdcStateEntryPB>>,
) -> Option<u64> {
    let first = streams.first()?;
    if !is_logical_replication_stream(snapshot, first) {
        return None;
    }
    let mut min: Option<u64> = None;
    for stream in streams {
        let slot = rows_by_stream
            .get(&stream.id)
            .and_then(|rows| rows.iter().find(|r| r.tablet_id() == SLOT_ENTRY_TABLET_ID))
            .filter(|r| r.has_record_id_commit_time())?;
        let time = slot.record_id_commit_time();
        min = Some(min.map_or(time, |m: u64| m.min(time)));
    }
    min
}

impl Xrepl {
    /// The hidden split parents released so far, which a tablet that is no longer hidden or
    /// streamed leaves.
    pub(crate) fn prune_released(&self, snapshot: &CatalogSnapshot) {
        let live: HashSet<Vec<u8>> = snapshot
            .iter(SysRowEntryType::TABLET)
            .filter(|t| t.tablet().is_some_and(|h| h.hidden))
            .map(|t| t.key.id.to_vec())
            .collect();
        self.released_split_parents
            .lock()
            .unwrap()
            .retain(|t| live.contains(t));
    }
}
