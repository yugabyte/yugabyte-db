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

//! `CleanUpCDCSDKStreamsMetadata`: tables a CDCSDK stream names that were dropped leave it. C++
//! records them in the stream's `dropped_table_id` when they are dropped
//! (`HandleDroppedTablesForCDCSDKStreams`); here each pass also finds them from the catalog, so a
//! drop on this leader needs no hook. Their tablets' `cdc_state` rows go first, then the tables
//! leave the row.

use std::collections::{BTreeMap, BTreeSet, HashSet};

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::EnumOrUnknown;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::sys_tablets_entry_pb::State as TabletState;
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, sys_tables_entry_pb, SysCDCStreamEntryPB,
    SysTablesEntryPB,
};
use yb_proto::pb::master_rust_peer::{
    CdcStateDeleteRequestPB, CdcStateDeleteResponsePB, CdcStateEntryPB, CdcStateReadRequestPB,
    CdcStateReadResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::streams::{stream_rows, StreamRow};
use crate::Xrepl;

const SLOT_ENTRY_TABLET_ID: &[u8] = b"dummy_id_for_replication_slot";
const SYS_CATALOG_TABLET_ID: &[u8] = b"00000000000000000000000000000000";

/// A table that is gone for a stream: no row, being deleted, or hidden.
fn table_dropped(snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
    let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
        return true;
    };
    let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
        return false;
    };
    matches!(
        pb.state(),
        sys_tables_entry_pb::State::DELETING | sys_tables_entry_pb::State::DELETED
    ) || pb.hide_state() != sys_tables_entry_pb::HideState::VISIBLE
}

/// The tables a stream names (qualified and unqualified).
fn stream_tables(pb: &SysCDCStreamEntryPB) -> Vec<Vec<u8>> {
    pb.table_id
        .iter()
        .map(|t| t.as_bytes().to_vec())
        .chain(pb.unqualified_table_id.iter().cloned())
        .collect()
}

impl Xrepl {
    /// One pass of `CleanUpCDCSDKStreamsMetadata`.
    pub async fn clean_up_cdcsdk_streams_metadata(
        &self,
        token: &LeaderToken,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        // Per live CDCSDK stream, its dropped tables. A stream of the old cleanup (in
        // DELETING_METADATA) becomes ACTIVE with them listed.
        let mut dropped: BTreeMap<String, BTreeSet<Vec<u8>>> = BTreeMap::new();
        for row in stream_rows(&snapshot) {
            if row.pb.namespace_id().is_empty()
                || matches!(row.pb.state(), StreamState::DELETING | StreamState::DELETED)
            {
                continue;
            }
            let mut tables: BTreeSet<Vec<u8>> = row.pb.dropped_table_id.iter().cloned().collect();
            for t in stream_tables(&row.pb) {
                if table_dropped(&snapshot, &t) {
                    tables.insert(t);
                }
            }
            if !tables.is_empty() || row.pb.state() == StreamState::DELETING_METADATA {
                dropped.insert(row.id.clone(), tables);
            }
        }
        if dropped.is_empty() {
            return Ok(());
        }
        // The cdc_state rows of tablets that hold no other table of the stream.
        let mut keys = Vec::new();
        for (stream_id, tables) in &dropped {
            if tables.is_empty() {
                continue;
            }
            let row = stream_rows(&snapshot)
                .into_iter()
                .find(|r| &r.id == stream_id)
                .expect("listed");
            keys.extend(
                self.state_rows_of_dropped_tables(&snapshot, &row, tables)
                    .await?,
            );
        }
        if !keys.is_empty() {
            tracing::info!(
                "Deleting cdc_state table entries {:?}",
                keys.iter()
                    .map(|k| (
                        String::from_utf8_lossy(k.tablet_id()).into_owned(),
                        String::from_utf8_lossy(k.stream_id()).into_owned()
                    ))
                    .collect::<Vec<_>>()
            );
            let mut req = CdcStateDeleteRequestPB::new();
            req.keys = keys;
            let _: CdcStateDeleteResponsePB = self.host_call("CdcStateDelete", req).await?;
        }
        // `CleanupCDCSDKDroppedTablesFromStreamInfo`, in one write.
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut mutation = Mutation::new();
                for (stream_id, tables) in &dropped {
                    let Some(entity) =
                        snapshot.get(SysRowEntryType::CDC_STREAM, stream_id.as_bytes())
                    else {
                        continue;
                    };
                    let mut pb: SysCDCStreamEntryPB = entity
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    if matches!(pb.state(), StreamState::DELETING | StreamState::DELETED) {
                        continue;
                    }
                    let before = pb.clone();
                    pb.table_id.retain(|t| !tables.contains(t.as_bytes()));
                    pb.unqualified_table_id.retain(|t| !tables.contains(t));
                    pb.replica_identity_map
                        .retain(|t, _| !tables.contains(t.as_bytes()));
                    pb.dropped_table_id.retain(|t| !tables.contains(t));
                    if pb.state() == StreamState::DELETING_METADATA {
                        pb.state = Some(EnumOrUnknown::new(StreamState::ACTIVE));
                    }
                    if pb != before {
                        mutation = mutation.read(entity).upsert_message(
                            SysRowEntryType::CDC_STREAM,
                            stream_id.as_bytes(),
                            &pb,
                        )?;
                    }
                }
                Ok((mutation, ()))
            })
            .await?;
        Ok(())
    }

    /// The keys (tablet, stream) of the stream's `cdc_state` rows to delete: rows of tablets that
    /// none of its live tables has, whose tablet is gone or hosts only dropped or ineligible
    /// tables. The slot's row and the sys catalog tablet's are kept.
    async fn state_rows_of_dropped_tables(
        &self,
        snapshot: &CatalogSnapshot,
        row: &StreamRow,
        dropped: &BTreeSet<Vec<u8>>,
    ) -> Result<Vec<CdcStateEntryPB>, Status> {
        let mut keep: HashSet<Vec<u8>> = HashSet::new();
        for table_id in stream_tables(&row.pb) {
            if dropped.contains(&table_id) {
                continue;
            }
            for tablet in snapshot.tablets_of(&table_id) {
                keep.insert(tablet.key.id.to_vec());
            }
        }
        let mut read = CdcStateReadRequestPB::new();
        read.set_stream_id(row.id.as_bytes().to_vec());
        let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
        let mut keys = Vec::new();
        let mut seen = HashSet::new();
        for entry in rows.entries {
            let tablet_id = entry.tablet_id().to_vec();
            if tablet_id == SLOT_ENTRY_TABLET_ID
                || tablet_id == SYS_CATALOG_TABLET_ID
                || keep.contains(&tablet_id)
                || !seen.insert(tablet_id.clone())
            {
                continue;
            }
            let gone = match snapshot.get(SysRowEntryType::TABLET, &tablet_id) {
                None => true,
                Some(tablet) => {
                    let deleted = tablet
                        .tablet()
                        .is_some_and(|t| t.state == TabletState::DELETED as i32);
                    deleted
                        || snapshot
                            .hosted_table_ids(tablet)
                            .iter()
                            .all(|t| dropped.contains(t) || !self.still_eligible(snapshot, row, t))
                }
            };
            if gone {
                let mut key = CdcStateEntryPB::new();
                key.set_tablet_id(tablet_id);
                key.set_stream_id(row.id.as_bytes().to_vec());
                keys.push(key);
            }
        }
        Ok(keys)
    }

    /// Whether a table on a tablet of the stream is still one the stream may have.
    fn still_eligible(&self, snapshot: &CatalogSnapshot, row: &StreamRow, table_id: &[u8]) -> bool {
        let Some(entity) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return false;
        };
        self.eligible_table(
            snapshot,
            entity,
            row.pb.allow_tables_without_primary_key(),
            row.pb.detect_publication_changes_implicitly(),
        )
    }
}
