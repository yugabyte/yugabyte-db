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

//! Removing tables from CDCSDK streams. A table to remove is added to the stream's
//! `unqualified_table_id` and stays in `table_id` until the controller has deleted its tablets'
//! `cdc_state` rows and taken it out; C++ rebuilds its map of such tables from the same rule
//! (`FindAllUnproccesedUnqualifiedTablesInCDCSDKStream`). Ineligible tables only lose their
//! `cdc_state` rows, from a map kept in memory, as in C++: the caller retries after a failover.

use std::collections::{BTreeMap, BTreeSet, HashSet};

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{SysCDCStreamEntryPB, SysTablesEntryPB};
use yb_proto::pb::master_replication::{
    cleanup_stale_cdcstreams_response_pb::{stale_entry_pb::TablePB, StaleEntryPB},
    CleanupStaleCDCStreamsRequestPB, CleanupStaleCDCStreamsResponsePB,
    RemoveTablesFromCDCSDKStreamRequestPB, RemoveUserTableFromCDCSDKStreamRequestPB,
    ValidateAndSyncCDCStateEntriesForCDCSDKStreamRequestPB,
    ValidateAndSyncCDCStateEntriesForCDCSDKStreamResponsePB,
};
use yb_proto::pb::master_rust_peer::{
    CdcStateDeleteRequestPB, CdcStateDeleteResponsePB, CdcStateEntryPB, CdcStateReadRequestPB,
    CdcStateReadResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::cdcsdk::invalid_request;
use crate::streams::{parse_stream_id, stream_rows, StreamRow};
use crate::{error, Xrepl};

fn status_error(s: Status) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, s.code, s.message)
}

fn live_table(snapshot: &CatalogSnapshot, table_id: &[u8]) -> Option<SysTablesEntryPB> {
    use yb_proto::pb::catalog_entity_info::sys_tables_entry_pb::State;
    snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
        .filter(|pb| !matches!(pb.state(), State::DELETING | State::DELETED))
}

/// `TableInfo::GetTabletsIncludeInactive`: a colocated table's are its parent's.
fn tablets_including_inactive(snapshot: &CatalogSnapshot, table_id: &[u8]) -> Vec<Vec<u8>> {
    let own = snapshot.tablets_of(table_id);
    if !own.is_empty() {
        return own.iter().map(|t| t.key.id.to_vec()).collect();
    }
    snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(|t| t.table())
        .filter(|t| !t.parent_table_id.is_empty() && t.parent_table_id != table_id)
        .map(|t| {
            snapshot
                .tablets_of(&t.parent_table_id)
                .iter()
                .map(|t| t.key.id.to_vec())
                .collect()
        })
        .unwrap_or_default()
}

fn key(tablet_id: &[u8], stream_id: &str, colocated_table_id: Option<&[u8]>) -> CdcStateEntryPB {
    let mut k = CdcStateEntryPB::new();
    k.set_tablet_id(tablet_id.to_vec());
    k.set_stream_id(stream_id.as_bytes().to_vec());
    if let Some(t) = colocated_table_id {
        k.set_colocated_table_id(t.to_vec());
    }
    k
}

const SLOT_ENTRY_TABLET_ID: &[u8] = b"dummy_id_for_replication_slot";
const SYS_CATALOG_TABLET_ID: &[u8] = b"00000000000000000000000000000000";

impl Xrepl {
    /// `CleanupStaleCDCStreams`: `cdc_state` rows of streams that are gone or being deleted, and
    /// of tablets that are gone, are reported and, unless a dry run, deleted.
    pub async fn cleanup_stale_cdc_streams(
        &self,
        req: &CleanupStaleCDCStreamsRequestPB,
        resp: &mut CleanupStaleCDCStreamsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing CleanupStaleCDCStreams request: {:?}", req);
        let not_ready = || {
            error(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                "cdc_state table does not exist or is not ready.",
            )
        };
        if !self.cdc_state_table_exists(&self.catalog.snapshot()) {
            return Err(not_ready());
        }
        let rows: CdcStateReadResponsePB = match self
            .host_call("CdcStateRead", CdcStateReadRequestPB::new())
            .await
        {
            Ok(rows) => rows,
            Err(s) if matches!(s.code, StatusCode::NotFound | StatusCode::Uninitialized) => {
                return Err(not_ready())
            }
            Err(s) => return Err(status_error(s)),
        };
        let snapshot = self.catalog.snapshot();
        let live_streams: HashSet<String> = stream_rows(&snapshot)
            .into_iter()
            .filter(|r| !r.is_deleting())
            .map(|r| r.id)
            .collect();
        let mut keys = Vec::new();
        for row in rows.entries {
            let stream_id = String::from_utf8_lossy(row.stream_id()).into_owned();
            let stream_exists = live_streams.contains(&stream_id);
            let special =
                row.tablet_id() == SLOT_ENTRY_TABLET_ID || row.tablet_id() == SYS_CATALOG_TABLET_ID;
            if special && stream_exists {
                continue;
            }
            let tablet = (!special)
                .then(|| snapshot.get(SysRowEntryType::TABLET, row.tablet_id()))
                .flatten();
            let reason = if !stream_exists {
                "stream not found"
            } else if tablet.is_none() {
                "tablet not found"
            } else {
                continue;
            };
            let mut stale = StaleEntryPB::new();
            stale.set_tablet_id(String::from_utf8_lossy(row.tablet_id()).into_owned());
            stale.set_stream_id(row.stream_id().to_vec());
            if !row.colocated_table_id().is_empty() {
                stale.set_colocated_table_id(row.colocated_table_id().to_vec());
            }
            stale.set_reason(reason.to_string());
            // The tables of a stale row's live tablet, for the report.
            if let Some(tablet) = tablet.filter(|_| !stream_exists) {
                for table_id in snapshot.hosted_table_ids(tablet) {
                    if let Some(table) = snapshot.get(SysRowEntryType::TABLE, &table_id) {
                        let mut t = TablePB::new();
                        t.set_table_id(table_id.clone());
                        t.set_table_name(
                            table
                                .parse::<SysTablesEntryPB>()
                                .map(|pb| String::from_utf8_lossy(pb.name()).into_owned())
                                .unwrap_or_default(),
                        );
                        stale.tables.push(t);
                    }
                }
            }
            resp.stale_entries.push(stale);
            keys.push(key(
                row.tablet_id(),
                &stream_id,
                (!row.colocated_table_id().is_empty()).then(|| row.colocated_table_id()),
            ));
        }
        if !req.dry_run() && !keys.is_empty() {
            let mut delete = CdcStateDeleteRequestPB::new();
            delete.keys = keys;
            let _: CdcStateDeleteResponsePB = self
                .host_call("CdcStateDelete", delete)
                .await
                .map_err(|s| {
                    error(
                        Code::UNKNOWN_ERROR,
                        s.code,
                        format!(
                            "Error deleting stale entries from cdc_state table: {}",
                            s.message
                        ),
                    )
                })?;
            resp.deleted_entries = resp.stale_entries.clone();
        }
        tracing::info!(
            "Found {} stale cdc_state entries{}",
            resp.stale_entries.len(),
            if req.dry_run() {
                " (dry run)".to_string()
            } else {
                format!(", deleted {} entries", resp.deleted_entries.len())
            }
        );
        Ok(())
    }

    /// `RemoveUserTableFromCDCSDKStream`.
    pub async fn remove_user_table_from_cdcsdk_stream(
        &self,
        token: &LeaderToken,
        deadline: tokio::time::Instant,
        req: &RemoveUserTableFromCDCSDKStreamRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!(
            "Servicing RemoveUserTableFromCDCSDKStream request: {:?}",
            req
        );
        if !req.has_stream_id() || !req.has_table_id() {
            return Err(invalid_request(
                "Both CDC Stream ID and table ID must be provided",
            ));
        }
        if !(self.options.table_cleanup)() {
            return Err(invalid_request(
                "Removal of user table from CDC stream is disallowed in the middle of an upgrade. \
                 Finalize the upgrade and try again",
            ));
        }
        let id = parse_stream_id(req.stream_id().as_bytes())?;
        let row = self.cdcsdk_stream(&id)?;
        let table_id = req.table_id().as_bytes();
        let snapshot = self.catalog.snapshot();
        let Some(table) = live_table(&snapshot, table_id) else {
            return Err(master_ts_manager::master_error_tagged(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                "Could not find table",
            ));
        };
        if !self.stream_may_have(&snapshot, &row, table_id) {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "Only allowed to remove user tables from CDC streams",
            ));
        }
        if table.namespace_id() != row.pb.namespace_id().as_bytes() {
            return Err(invalid_request(
                "Stream and Table are not under the same namespace",
            ));
        }
        self.add_unqualified_tables(token, deadline, &id, &[table_id.to_vec()])
            .await?;
        tracing::info!(
            "Successfully added table {} to unqualified list for CDC stream: {id}",
            req.table_id()
        );
        Ok(())
    }

    /// `RemoveTablesFromCDCSDKStream`: eligible tables leave the stream; ineligible ones only lose
    /// their `cdc_state` rows.
    pub async fn remove_tables_from_cdcsdk_stream(
        &self,
        token: &LeaderToken,
        deadline: tokio::time::Instant,
        req: &RemoveTablesFromCDCSDKStreamRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing RemoveTablesFromCDCSDKStream request: {:?}", req);
        if !req.has_stream_id() {
            return Err(invalid_request(
                "Stream ID is requirred for removing tables from CDCSDK stream",
            ));
        }
        let id = parse_stream_id(req.stream_id().as_bytes())?;
        if req.table_ids.is_empty() {
            return Err(invalid_request(
                "No Table ID provided for removal from CDCSDK stream",
            ));
        }
        let row = self.cdcsdk_stream(&id)?;
        let snapshot = self.catalog.snapshot();
        let mut valid = Vec::new();
        let mut ineligible = BTreeSet::new();
        for table_id in &req.table_ids {
            let table_id = table_id.as_bytes();
            if live_table(&snapshot, table_id).is_none() {
                tracing::warn!(
                    "Could not remove table: {} from stream: {id} because it was not found",
                    String::from_utf8_lossy(table_id)
                );
            }
            if self.stream_may_have(&snapshot, &row, table_id) {
                valid.push(table_id.to_vec());
            } else {
                ineligible.insert(table_id.to_vec());
            }
        }
        self.add_unqualified_tables(token, deadline, &id, &valid)
            .await?;
        if !ineligible.is_empty() {
            let mut map = self.ineligible_tables.lock().unwrap();
            for table_id in ineligible {
                map.entry(table_id).or_default().insert(id.clone());
            }
        }
        Ok(())
    }

    /// `ValidateAndSyncCDCStateEntriesForCDCSDKStream`: deletes the stream's `cdc_state` rows of
    /// tablets none of its tables has.
    pub async fn validate_and_sync_cdc_state_entries(
        &self,
        req: &ValidateAndSyncCDCStateEntriesForCDCSDKStreamRequestPB,
        resp: &mut ValidateAndSyncCDCStateEntriesForCDCSDKStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!(
            "Servicing ValidateAndSyncCDCStateEntriesForCDCSDKStream request: {:?}",
            req
        );
        if !req.has_stream_id() {
            return Err(invalid_request("CDC Stream ID must be provided"));
        }
        let id = parse_stream_id(req.stream_id().as_bytes())?;
        let row = self.cdcsdk_stream(&id)?;
        let tables: HashSet<Vec<u8>> = row
            .pb
            .table_id
            .iter()
            .map(|t| t.as_bytes().to_vec())
            .collect();
        let deleted = self
            .sync_cdc_state_entries(&id, &tables)
            .await
            .map_err(status_error)?;
        resp.deleted_tablet_entries = deleted
            .into_iter()
            .filter(|k| k.colocated_table_id().is_empty())
            .map(|k| String::from_utf8_lossy(k.tablet_id()).into_owned())
            .collect();
        tracing::info!(
            "Successfully validated and synced cdc state table entries for CDC stream: {id}"
        );
        Ok(())
    }

    /// Whether `table_id` is one the stream may have (`IsTableEligibleForCDCSDKStream`).
    fn stream_may_have(
        &self,
        snapshot: &CatalogSnapshot,
        row: &StreamRow,
        table_id: &[u8],
    ) -> bool {
        snapshot
            .get(SysRowEntryType::TABLE, table_id)
            .is_some_and(|entity| {
                self.eligible_table(
                    snapshot,
                    entity,
                    row.pb.allow_tables_without_primary_key(),
                    row.pb.detect_publication_changes_implicitly(),
                )
            })
    }

    /// `AddTableForRemovalFromCDCSDKStream`: the tables join the stream's unqualified list.
    async fn add_unqualified_tables(
        &self,
        token: &LeaderToken,
        deadline: tokio::time::Instant,
        stream_id: &str,
        table_ids: &[Vec<u8>],
    ) -> Result<(), MasterErrorPB> {
        if table_ids.is_empty() {
            return Ok(());
        }
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let entity = snapshot
                        .get(SysRowEntryType::CDC_STREAM, stream_id.as_bytes())
                        .ok_or_else(|| {
                            Status::new(StatusCode::NotFound, "Could not find CDC stream")
                        })?;
                    let mut pb: SysCDCStreamEntryPB = entity
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    let before = pb.unqualified_table_id.len();
                    for t in table_ids {
                        if !pb.unqualified_table_id.contains(t) {
                            pb.unqualified_table_id.push(t.clone());
                        }
                    }
                    if pb.unqualified_table_id.len() == before {
                        return Ok((Mutation::new(), ()));
                    }
                    Ok((
                        Mutation::new().read(entity).upsert_message(
                            SysRowEntryType::CDC_STREAM,
                            stream_id.as_bytes(),
                            &pb,
                        )?,
                        (),
                    ))
                },
            )
            .await
            .map(|_| ())
            .map_err(status_error)
    }

    /// `SyncCDCStateTableEntries`: the stream's rows of colocated tables it no longer has, and of
    /// existing tablets hosting none of its tables, are deleted and returned.
    async fn sync_cdc_state_entries(
        &self,
        stream_id: &str,
        tables: &HashSet<Vec<u8>>,
    ) -> Result<Vec<CdcStateEntryPB>, Status> {
        let mut read = CdcStateReadRequestPB::new();
        read.set_stream_id(stream_id.as_bytes().to_vec());
        let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
        let snapshot = self.catalog.snapshot();
        let mut delete = Vec::new();
        let mut seen = HashSet::new();
        for row in rows.entries {
            if !row.colocated_table_id().is_empty() {
                if !tables.contains(row.colocated_table_id()) {
                    delete.push(key(
                        row.tablet_id(),
                        stream_id,
                        Some(row.colocated_table_id()),
                    ));
                }
                continue;
            }
            if !seen.insert(row.tablet_id().to_vec()) {
                continue;
            }
            // A tablet that no longer exists is left alone.
            let Some(tablet) = snapshot.get(SysRowEntryType::TABLET, row.tablet_id()) else {
                continue;
            };
            if !snapshot
                .hosted_table_ids(tablet)
                .iter()
                .any(|t| tables.contains(t))
            {
                delete.push(key(row.tablet_id(), stream_id, None));
            }
        }
        self.delete_state_rows(stream_id, &delete).await?;
        Ok(delete)
    }

    /// `RemoveTabletEntriesInCDCState`: the stream's rows of the table's tablets, keeping a
    /// colocated tablet's row while another of the stream's tables is on it.
    async fn remove_tablet_entries(
        &self,
        snapshot: &CatalogSnapshot,
        stream_id: &str,
        tables: &HashSet<Vec<u8>>,
        table_id: &[u8],
    ) -> Result<(), Status> {
        let colocated = live_table(snapshot, table_id).is_some_and(|t| t.colocated())
            && !master_table_lifecycle::lookup::is_colocation_parent_table_id(table_id);
        let mut delete = Vec::new();
        for tablet_id in tablets_including_inactive(snapshot, table_id) {
            if colocated {
                let shared = snapshot
                    .get(SysRowEntryType::TABLET, &tablet_id)
                    .is_some_and(|t| {
                        snapshot
                            .hosted_table_ids(t)
                            .iter()
                            .any(|t| tables.contains(t))
                    });
                if !shared {
                    delete.push(key(&tablet_id, stream_id, None));
                }
                delete.push(key(&tablet_id, stream_id, Some(table_id)));
            } else {
                delete.push(key(&tablet_id, stream_id, None));
            }
        }
        self.delete_state_rows(stream_id, &delete).await
    }

    async fn delete_state_rows(
        &self,
        stream_id: &str,
        keys: &[CdcStateEntryPB],
    ) -> Result<(), Status> {
        if keys.is_empty() {
            return Ok(());
        }
        tracing::info!(
            "Deleting {} cdc state entries for CDC stream: {stream_id}",
            keys.len()
        );
        let mut req = CdcStateDeleteRequestPB::new();
        req.keys = keys.to_vec();
        let _: CdcStateDeleteResponsePB = self.host_call("CdcStateDelete", req).await?;
        Ok(())
    }

    /// One pass of `ProcessTablesToBeRemovedFromCDCSDKStreams` and
    /// `ProcessTablesToBeDeletedFromCDCStateTable`, each up to
    /// `cdcsdk_table_processing_limit_per_run` tables.
    pub async fn process_table_removals(&self, token: &LeaderToken) -> Result<(), Status> {
        let limit = (self.options.table_processing_limit)().max(0) as usize;
        let snapshot = self.catalog.snapshot();
        // `FindAllNonEligibleTablesInCDCSDKStream`: tables a stream has that may not be in it
        // (indexes, materialized views). As C++, they are looked for once per leader term, when
        // the streams are loaded; tests order themselves against a master restart on that.
        let mut found_non_eligible: BTreeMap<Vec<u8>, BTreeSet<String>> = BTreeMap::new();
        // Tables both qualified and unqualified on a live stream are still to be removed.
        let mut pending: BTreeMap<Vec<u8>, Vec<StreamRow>> = BTreeMap::new();
        for row in stream_rows(&snapshot) {
            if row.is_deleting() || row.pb.namespace_id().is_empty() {
                continue;
            }
            for t in &row.pb.table_id {
                let t = t.as_bytes();
                if live_table(&snapshot, t).is_some() && !self.stream_may_have(&snapshot, &row, t) {
                    found_non_eligible
                        .entry(t.to_vec())
                        .or_default()
                        .insert(row.id.clone());
                }
            }
            for t in &row.pb.unqualified_table_id {
                if row.pb.table_id.iter().any(|q| q.as_bytes() == t.as_slice()) {
                    pending.entry(t.clone()).or_default().push(row.clone());
                }
            }
        }
        let non_eligible: Vec<(Vec<u8>, BTreeSet<String>)> = {
            let mut state = self.non_eligible_tables.lock().unwrap();
            if state.0 != Some(token.term()) {
                for (table_id, streams) in &found_non_eligible {
                    tracing::info!(
                        "Found a non-eligible table: {} for streams {streams:?}",
                        String::from_utf8_lossy(table_id)
                    );
                }
                *state = (Some(token.term()), found_non_eligible);
            }
            state
                .1
                .iter()
                .take(limit)
                .map(|(t, s)| (t.clone(), s.clone()))
                .collect()
        };
        if (self.options.table_cleanup)() {
            for (table_id, streams) in non_eligible {
                for row in stream_rows(&snapshot)
                    .into_iter()
                    .filter(|r| streams.contains(&r.id) && !r.is_deleting())
                {
                    if live_table(&snapshot, &table_id).is_some()
                        && row.pb.table_id.iter().any(|t| t.as_bytes() == table_id)
                    {
                        self.remove_table_from_stream(token, &snapshot, &row, &table_id)
                            .await?;
                    }
                }
                self.non_eligible_tables.lock().unwrap().1.remove(&table_id);
            }
        }
        if (self.options.skip_processing_unqualified_tables)() {
            pending.clear();
        }
        for (table_id, streams) in pending.into_iter().take(limit) {
            for row in streams {
                // A dropped table leaves through `clean_up_cdcsdk_streams_metadata`.
                if live_table(&snapshot, &table_id).is_none()
                    || !self.stream_may_have(&snapshot, &row, &table_id)
                {
                    tracing::warn!(
                        "Table {} not eligible for stream {}",
                        String::from_utf8_lossy(&table_id),
                        row.id
                    );
                    continue;
                }
                self.remove_table_from_stream(token, &snapshot, &row, &table_id)
                    .await?;
            }
        }
        if !self.ineligible_tables.lock().unwrap().is_empty() {
            self.fail_for_testing("ProcessTablesToBeDeletedFromCDCStateTable::Start")
                .await?;
        }
        let ineligible: Vec<(Vec<u8>, BTreeSet<String>)> = self
            .ineligible_tables
            .lock()
            .unwrap()
            .iter()
            .take(limit)
            .map(|(t, s)| (t.clone(), s.clone()))
            .collect();
        for (table_id, streams) in ineligible {
            let mut done = BTreeSet::new();
            if snapshot.get(SysRowEntryType::TABLE, &table_id).is_none() {
                done = streams.clone();
            } else {
                // As C++, the stream's tables accumulate across the streams of the table.
                let mut tables = HashSet::new();
                for id in &streams {
                    let Some(row) = stream_rows(&snapshot)
                        .into_iter()
                        .find(|r| &r.id == id && !r.is_deleting())
                    else {
                        done.insert(id.clone());
                        continue;
                    };
                    tables.extend(row.pb.table_id.iter().map(|t| t.as_bytes().to_vec()));
                    match self
                        .remove_tablet_entries(&snapshot, id, &tables, &table_id)
                        .await
                    {
                        Ok(()) => {
                            done.insert(id.clone());
                        }
                        Err(s) => tracing::warn!(
                            "Encountered error while trying to delete tablets entries of table: \
                             {}, from cdc_state table for stream: {id} - {s}",
                            String::from_utf8_lossy(&table_id)
                        ),
                    }
                }
            }
            let mut map = self.ineligible_tables.lock().unwrap();
            if let Some(set) = map.get_mut(&table_id) {
                set.retain(|s| !done.contains(s));
                if set.is_empty() {
                    map.remove(&table_id);
                }
            }
        }
        Ok(())
    }

    /// The steps of `ProcessTablesToBeRemovedFromCDCSDKStreams` for one table and stream: its
    /// tablets' `cdc_state` rows, then the stream's `table_id`.
    async fn remove_table_from_stream(
        &self,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        row: &StreamRow,
        table_id: &[u8],
    ) -> Result<(), Status> {
        let table_name = String::from_utf8_lossy(table_id).into_owned();
        self.sync_point("ProcessTablesToBeRemovedFromCDCSDKStreams::ValidationCompleted")
            .await;
        self.sync_point("ProcessTablesToBeRemovedFromCDCSDKStreams::StartStateTableEntryUpdate")
            .await;
        if !(self.options.skip_state_entries_on_table_removal)() {
            let mut tables: HashSet<Vec<u8>> = row
                .pb
                .table_id
                .iter()
                .map(|t| t.as_bytes().to_vec())
                .collect();
            tables.remove(table_id);
            if let Err(s) = self
                .remove_tablet_entries(snapshot, &row.id, &tables, table_id)
                .await
            {
                tracing::warn!(
                    "Encountered error while trying to delete tablets entries of table: \
                     {table_name}, from cdc_state table for stream: {} - {s}",
                    row.id
                );
                return Ok(());
            }
        }
        self.sync_point(
            "ProcessTablesToBeRemovedFromCDCSDKStreams::StateTableEntryUpdateCompleted",
        )
        .await;
        self.sync_point(
            "ProcessTablesToBeRemovedFromCDCSDKStreams::StartRemovalFromQualifiedTableList",
        )
        .await;
        if (self.options.skip_table_removal_from_qualified_list)() {
            return Ok(());
        }
        self.remove_qualified_table(token, &row.id, table_id)
            .await?;
        tracing::info!(
            "Successfully removed table {table_name} from qualified table list and updated \
             corresponding cdc_state table entries for stream: {}",
            row.id
        );
        Ok(())
    }

    /// `RemoveTableFromCDCStreamMetadataAndMaps`: the table leaves `table_id`; it stays in
    /// `unqualified_table_id`.
    async fn remove_qualified_table(
        &self,
        token: &LeaderToken,
        stream_id: &str,
        table_id: &[u8],
    ) -> Result<(), Status> {
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let Some(entity) = snapshot.get(SysRowEntryType::CDC_STREAM, stream_id.as_bytes())
                else {
                    return Ok((Mutation::new(), ()));
                };
                let mut pb: SysCDCStreamEntryPB = entity
                    .parse()
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                let before = pb.table_id.len();
                pb.table_id.retain(|t| t.as_bytes() != table_id);
                if pb.table_id.len() == before {
                    return Ok((Mutation::new(), ()));
                }
                tracing::info!(
                    "Removing table {} from qualified table list of CDC stream {stream_id}",
                    String::from_utf8_lossy(table_id)
                );
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::CDC_STREAM,
                        stream_id.as_bytes(),
                        &pb,
                    )?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }
}
