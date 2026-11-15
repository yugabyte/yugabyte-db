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

//! Dynamic table addition (`ProcessNewTablesForCDCSDKStreams`). C++ queues a table when it is
//! created and, on load, every eligible table a stream lists neither as qualified nor as
//! unqualified (`FindAllTablesMissingInCDCSDKStream`). Here each pass applies that rule to the
//! catalog, so a create needs no hook.

use std::collections::{BTreeMap, HashSet};
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::{EnumOrUnknown, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb::State as TabletState, SysCDCStreamEntryPB,
    SysTablesEntryPB,
};
use yb_proto::pb::master_rust_peer::{
    CdcStateEntryPB, CdcStateReadRequestPB, CdcStateReadResponsePB, CdcStateUpsertRequestPB,
    CdcStateUpsertResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::opid::OpIdPB;

use crate::create::{is_logical_replication_stream, GRPC_STREAM_INDICATOR};
use crate::streams::{stream_rows, StreamRow};
use crate::Xrepl;

/// A RUNNING table whose tablets all run (`GetTableLocations` with `require_tablets_running`).
fn table_running(snapshot: &CatalogSnapshot, table_id: &[u8]) -> Option<SysTablesEntryPB> {
    let pb = snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(|t| t.parse::<SysTablesEntryPB>().ok())?;
    if pb.state() != sys_tables_entry_pb::State::RUNNING {
        return None;
    }
    let tablets = snapshot.hosting_tablets_of(table_id);
    let running = !tablets.is_empty()
        && tablets.iter().all(|t| {
            t.tablet()
                .is_some_and(|h| h.state == TabletState::RUNNING as i32)
        });
    running.then_some(pb)
}

/// `StreamRequiresReplicaIdentityMap`.
fn requires_replica_identity_map(snapshot: &CatalogSnapshot, row: &StreamRow) -> bool {
    is_logical_replication_stream(snapshot, row)
        || row.pb.cdcsdk_ysql_replication_slot_plugin_name() == GRPC_STREAM_INDICATOR
}

impl Xrepl {
    /// The eligible tables of live CDCSDK streams with dynamic table addition that the streams
    /// have not seen yet, each with its streams, up to `cdcsdk_table_processing_limit_per_run`.
    fn new_tables(&self, snapshot: &CatalogSnapshot) -> BTreeMap<Vec<u8>, Vec<StreamRow>> {
        let mut found: BTreeMap<Vec<u8>, Vec<StreamRow>> = BTreeMap::new();
        for row in stream_rows(snapshot) {
            if row.is_deleting()
                || row.pb.namespace_id().is_empty()
                || row.pb.cdcsdk_disable_dynamic_table_addition()
            {
                continue;
            }
            let known: HashSet<&[u8]> = row
                .pb
                .table_id
                .iter()
                .map(|t| t.as_bytes())
                .chain(row.pb.unqualified_table_id.iter().map(|t| t.as_slice()))
                .collect();
            for table_id in self.tables_for_cdcsdk(
                snapshot,
                row.pb.namespace_id().as_bytes(),
                row.pb.allow_tables_without_primary_key(),
            ) {
                if !known.contains(table_id.as_slice()) {
                    found.entry(table_id).or_default().push(row.clone());
                }
            }
        }
        let limit = (self.options.table_processing_limit)().max(0) as usize;
        found.into_iter().take(limit).collect()
    }

    /// One pass of `ProcessNewTablesForCDCSDKStreams`.
    pub async fn process_new_tables(&self, token: &LeaderToken) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        for (table_id, streams) in self.new_tables(&snapshot) {
            let Some(table) = table_running(&snapshot, &table_id) else {
                continue;
            };
            if let Err(s) = self
                .add_table_to_streams(token, &snapshot, &table_id, &table, &streams)
                .await
            {
                tracing::warn!(
                    "Could not add table {} to CDCSDK streams: {s}",
                    String::from_utf8_lossy(&table_id)
                );
            }
        }
        Ok(())
    }

    async fn add_table_to_streams(
        &self,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        table_id: &[u8],
        table: &SysTablesEntryPB,
        streams: &[StreamRow],
    ) -> Result<(), Status> {
        let deadline = Instant::now() + Duration::from_secs(60);
        // A namespace has logical replication streams or gRPC ones, never both. Logical
        // replication streams got their barriers and rows when the tablets were created.
        let logical = is_logical_replication_stream(snapshot, &streams[0]);
        let rows_at_creation = (self.options.replication_slot_consumption)() && logical;
        if !rows_at_creation {
            self.write_wal_retention(token, deadline, &[table_id.to_vec()])
                .await?;
            let snapshot = self.catalog.snapshot();
            let table = snapshot
                .get(SysRowEntryType::TABLE, table_id)
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                .ok_or_else(|| Status::new(StatusCode::NotFound, "table is gone"))?;
            for tablet in snapshot.hosting_tablets_of(table_id) {
                self.alter_tablet(
                    *token,
                    deadline,
                    table_id.to_vec(),
                    table.clone(),
                    tablet.key.id.to_vec(),
                    None,
                    false,
                )
                .await?;
            }
            for row in streams {
                self.add_invalid_state_rows(&snapshot, &row.id, table_id)
                    .await?;
            }
        }
        // Catalog tables are not added dynamically.
        let identity = table.schema.table_properties.ysql_replica_identity();
        let with_identity: HashSet<String> = streams
            .iter()
            .filter(|r| {
                (self.options.replica_identity)() && requires_replica_identity_map(snapshot, r)
            })
            .map(|r| r.id.clone())
            .collect();
        let ids: Vec<String> = streams.iter().map(|r| r.id.clone()).collect();
        let table_name = String::from_utf8_lossy(table_id).into_owned();
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut mutation = Mutation::new();
                for id in &ids {
                    let Some(entity) = snapshot.get(SysRowEntryType::CDC_STREAM, id.as_bytes())
                    else {
                        continue;
                    };
                    let mut pb: SysCDCStreamEntryPB = entity
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    let row = StreamRow {
                        id: id.clone(),
                        pb: pb.clone(),
                    };
                    if row.is_deleting()
                        || pb.table_id.contains(&table_name)
                        || pb.unqualified_table_id.iter().any(|t| t == table_id)
                    {
                        continue;
                    }
                    pb.table_id.push(table_name.clone());
                    if with_identity.contains(id) {
                        pb.replica_identity_map
                            .insert(table_name.clone(), EnumOrUnknown::new(identity));
                    }
                    mutation = mutation.read(entity).upsert_message(
                        SysRowEntryType::CDC_STREAM,
                        id.as_bytes(),
                        &pb,
                    )?;
                }
                Ok((mutation, ()))
            })
            .await?;
        for id in &ids {
            tracing::info!(
                "Added tablets of table: {table_name}, to cdc_state table for stream: {id}"
            );
        }
        Ok(())
    }

    /// A `cdc_state` row with an invalid checkpoint for each of the table's tablets the stream has
    /// no row for.
    async fn add_invalid_state_rows(
        &self,
        snapshot: &CatalogSnapshot,
        stream_id: &str,
        table_id: &[u8],
    ) -> Result<(), Status> {
        let mut read = CdcStateReadRequestPB::new();
        read.set_stream_id(stream_id.as_bytes().to_vec());
        let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
        let existing: HashSet<Vec<u8>> = rows
            .entries
            .iter()
            .filter(|r| r.colocated_table_id().is_empty())
            .map(|r| r.tablet_id().to_vec())
            .collect();
        let mut upsert = CdcStateUpsertRequestPB::new();
        for tablet in snapshot.hosting_tablets_of(table_id) {
            if existing.contains(&*tablet.key.id) {
                continue;
            }
            let mut entry = CdcStateEntryPB::new();
            entry.set_tablet_id(tablet.key.id.to_vec());
            entry.set_stream_id(stream_id.as_bytes().to_vec());
            let mut invalid = OpIdPB::new();
            invalid.set_term(-1);
            invalid.set_index(-1);
            entry.checkpoint = MessageField::some(invalid);
            entry.set_active_time(0);
            entry.set_cdc_sdk_safe_time(0);
            upsert.entries.push(entry);
        }
        if upsert.entries.is_empty() {
            return Ok(());
        }
        let _: CdcStateUpsertResponsePB = self.host_call("CdcStateUpsert", upsert).await?;
        Ok(())
    }
}

impl Xrepl {
    /// The live CDCSDK streams of the table's namespace that may have it.
    fn streams_for_new_table(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> Vec<StreamRow> {
        let Some(entity) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Vec::new();
        };
        let Some(namespace_id) = entity.table().map(|t| t.namespace_id.clone()) else {
            return Vec::new();
        };
        if (self.options.add_dynamic_index_to_state_table)() {
            return stream_rows(snapshot)
                .into_iter()
                .filter(|r| !r.is_deleting() && !r.pb.namespace_id().is_empty())
                .collect();
        }
        stream_rows(snapshot)
            .into_iter()
            .filter(|r| !r.is_deleting() && r.pb.namespace_id().as_bytes() == namespace_id)
            .filter(|r| {
                self.eligible_table(
                    snapshot,
                    entity,
                    r.pb.allow_tables_without_primary_key(),
                    r.pb.detect_publication_changes_implicitly(),
                )
            })
            .collect()
    }
}

impl master_table_lifecycle::XreplHooks for Xrepl {
    /// Barriers when a logical replication stream on the namespace may have the table and slots
    /// are consumed (`SendCreateTabletRequests`).
    fn retention_barriers(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
        (self.options.replication_slot_consumption)()
            && ((self.options.add_dynamic_index_to_state_table)()
                || self
                    .streams_for_new_table(snapshot, table_id)
                    .iter()
                    .any(|r| is_logical_replication_stream(snapshot, r)))
    }

    /// `PopulateCDCStateTableOnNewTableCreation`: a row per stream for the tablet, from the safe
    /// op id and the snapshot time for a consistent snapshot stream, else invalid.
    fn created(
        &self,
        table_id: Vec<u8>,
        tablet_id: Vec<u8>,
        safe_op_id: OpIdPB,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>> {
        Box::pin(async move {
            self.sync_point("PopulateCDCStateTableOnNewTableCreation::Start")
                .await;
            let snapshot = self.catalog.snapshot();
            let streams = self.streams_for_new_table(&snapshot, &table_id);
            if streams.is_empty() {
                return Err(Status::new(
                    StatusCode::NotFound,
                    format!(
                        "Did not find any stream for table {}",
                        String::from_utf8_lossy(&table_id)
                    ),
                ));
            }
            let mut upsert = CdcStateUpsertRequestPB::new();
            for row in &streams {
                let mut entry = CdcStateEntryPB::new();
                entry.set_tablet_id(tablet_id.clone());
                entry.set_stream_id(row.id.as_bytes().to_vec());
                let meta = &row.pb.cdcsdk_stream_metadata;
                if meta.has_consistent_snapshot_option() {
                    let time = meta.snapshot_time();
                    entry.checkpoint = MessageField::some(safe_op_id.clone());
                    entry.set_active_time(now_micros());
                    entry.set_cdc_sdk_safe_time(time);
                    entry.set_last_replication_time(time >> 12);
                } else {
                    let mut invalid = OpIdPB::new();
                    invalid.set_term(-1);
                    invalid.set_index(-1);
                    entry.checkpoint = MessageField::some(invalid);
                    entry.set_active_time(0);
                    entry.set_cdc_sdk_safe_time(0);
                }
                tracing::info!(
                    "Table id: {}, tablet id: {}, stream id: {}, Safe OpId: {} and {}",
                    String::from_utf8_lossy(&table_id),
                    String::from_utf8_lossy(&tablet_id),
                    row.id,
                    safe_op_id.term(),
                    safe_op_id.index()
                );
                upsert.entries.push(entry);
            }
            let _: CdcStateUpsertResponsePB = self.host_call("CdcStateUpsert", upsert).await?;
            self.sync_point("PopulateCDCStateTableOnNewTableCreation::End")
                .await;
            Ok(())
        })
    }

    fn split_registered(
        &self,
        table_id: Vec<u8>,
        parent: Vec<u8>,
        children: Vec<Vec<u8>>,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>> {
        Box::pin(async move {
            self.update_cdc_producer_on_split(&table_id, &parent, &children)
                .await?;
            // `ProcessSplitTabletResult`'s point after the streams were updated.
            self.sync_point("Tabletsplit::AddedChildrenTabletStateTableEntries")
                .await;
            Ok(())
        })
    }

    fn retains_hidden_tablet(&self, snapshot: &CatalogSnapshot, tablet_id: &[u8]) -> bool {
        self.retains_tablet(snapshot, tablet_id)
            || self
                .extension
                .get()
                .is_some_and(|x| x.retains_hidden_tablet(snapshot, tablet_id))
    }

    fn retains_dropped_table(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
        self.retains_table(snapshot, table_id)
            || self
                .extension
                .get()
                .is_some_and(|x| x.retains_dropped_table(snapshot, table_id))
    }

    fn post_tablet_create(
        &self,
        table_id: Vec<u8>,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>> {
        Box::pin(async move {
            match self.extension.get() {
                Some(x) => x.post_tablet_create(table_id).await,
                None => Ok(()),
            }
        })
    }
}

fn now_micros() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_micros() as u64)
}
