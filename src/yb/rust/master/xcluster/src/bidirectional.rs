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

//! A new index on a table in bidirectional xCluster replication
//! (`ysql_auto_add_new_index_to_bidirectional_xcluster`). Each universe creates the index on its
//! own; the two are synchronized only at the backfill, the one stage that reads and writes the
//! whole key range:
//! - as a source, the index gets a non-transactional stream, checkpointed at op 0, before it runs
//!   (`CreateXClusterStreamForBiDirectionalIndexTask`);
//! - as a target, its backfill waits for the other universe's index to start backfilling, adds
//!   that index's stream to the inbound group, then reads at a time the indexed table's
//!   replication has caught up to (`PrepareAndGetBackfillTimeForBiDirectionalIndex`).
//!
//! A colocated index needs no stream: its parent table is the one replicated.

use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use protobuf::{Enum, EnumOrUnknown};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb::State as TableState, SysNamespaceEntryPB, SysTablesEntryPB,
};
use yb_proto::pb::common_types::{IndexPermissions, TableType, YQLDatabase};
use yb_proto::pb::master_ddl::{
    GetTableSchemaRequestPB, GetTableSchemaResponsePB, ListTablesRequestPB, ListTablesResponsePB,
};
use yb_proto::pb::master_replication::{
    IdTypePB, ListCDCStreamsRequestPB, ListCDCStreamsResponsePB, WaitForReplicationDrainRequestPB,
    WaitForReplicationDrainResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::outbound::table_pb;
use crate::remote::{call_leader, check, MASTER_SERVICE};
use crate::setup::{cluster_config, SetupData, ALTER_SUFFIX};
use crate::{status, XCluster};

/// `kScheduleDelay`: between the polls of the other universe.
const POLL: Duration = Duration::from_millis(200);

/// Logged while the other universe has no running index; tests wait for it.
const WAITING_FOR_OTHER_UNIVERSE: &str = "Waiting for index to be created on other universe";

/// The index's indexed table id, as `TableInfo::is_index` reads it.
fn indexed_table_id(pb: &SysTablesEntryPB) -> &[u8] {
    let id = pb.index_info.indexed_table_id();
    if id.is_empty() {
        pb.indexed_table_id()
    } else {
        id
    }
}

impl XCluster {
    /// `ShouldAutoAddIndexesToBiDirectionalXCluster`: the feature is on and the YSQL table (its
    /// colocation parent, if colocated) is both a source and a target of xCluster replication.
    pub(crate) fn should_auto_add_index_to_bidirectional(
        &self,
        snapshot: &CatalogSnapshot,
        indexed_id: &[u8],
        indexed: &SysTablesEntryPB,
    ) -> bool {
        if !(self.options.auto_add_index_to_bidirectional)()
            || indexed.table_type() != TableType::PGSQL_TABLE_TYPE
        {
            return false;
        }
        let table_id = if indexed.colocated() {
            indexed.parent_table_id()
        } else {
            indexed_id
        };
        self.is_table_bidirectionally_replicated(snapshot, table_id)
    }

    /// The source half for a new table (`GetPostTabletCreateTasks`): a non-colocated index of a
    /// bidirectionally replicated table gets its stream. `in_groups` says whether the table was
    /// also added to outbound groups, which a bidirectional table must not have.
    pub(crate) async fn add_new_bidirectional_index(
        &self,
        table_id: &[u8],
        in_groups: bool,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let Some(pb) = table_pb(&snapshot, table_id) else {
            return Ok(());
        };
        let indexed_id = indexed_table_id(&pb).to_vec();
        if indexed_id.is_empty() || pb.colocated() {
            return Ok(());
        }
        let indexed = table_pb(&snapshot, &indexed_id).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!(
                    "Table with identifier {} not found",
                    String::from_utf8_lossy(&indexed_id)
                ),
            )
        })?;
        if !self.should_auto_add_index_to_bidirectional(&snapshot, &indexed_id, &indexed) {
            return Ok(());
        }
        let table = String::from_utf8_lossy(table_id).into_owned();
        if in_groups {
            // During a switchover bidirectional replication has DB-scoped groups too, but no DDL
            // is expected then.
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Index {table} created while its base table {} is under bi-directional \
                     xCluster replication, and has DB scoped replication groups",
                    String::from_utf8_lossy(&indexed_id)
                ),
            ));
        }
        let token = self.leadership.token()?;
        self.create_non_txn_stream_for_new_table(&token, &table)
            .await?;
        tracing::info!(
            "CreateXClusterStreamForBiDirectionalIndexTask [{table}]: Task ended successfully"
        );
        Ok(())
    }

    /// `CreateNonTxnStreamForNewTable`: an INITIATED non-transactional stream outside any
    /// outbound group, checkpointed at op 0. A stream the table already has (an earlier attempt
    /// in another term) is checkpointed again rather than duplicated.
    async fn create_non_txn_stream_for_new_table(
        &self,
        token: &LeaderToken,
        table_id: &str,
    ) -> Result<String, Status> {
        self.ensure_cdc_state_table(token).await?;
        let stream_id = self
            .writer
            .mutate(token, |snapshot| {
                if let Some(existing) =
                    crate::controller::outbound_streams(snapshot, table_id.as_bytes()).first()
                {
                    return Ok((Mutation::new(), existing.id.clone()));
                }
                let (id, mut pb) = self.new_stream(snapshot, table_id, false, false, false, &[])?;
                pb.set_transactional(false);
                pb.state = Some(EnumOrUnknown::new(
                    yb_proto::pb::catalog_entity_info::sys_cdcstream_entry_pb::State::INITIATED,
                ));
                let m = Mutation::new()
                    .read_absent(SysRowEntryType::CDC_STREAM, id.as_bytes())
                    .upsert_message(SysRowEntryType::CDC_STREAM, id.as_bytes(), &pb)?;
                Ok((m, id))
            })
            .await
            .map(|(id, _)| id)?;
        self.xrepl
            .checkpoint_streams_to_op0(&[(table_id.to_string(), stream_id.clone())])
            .await?;
        Ok(stream_id)
    }

    /// `PrepareAndGetBackfillTimeForBiDirectionalIndex`: the read time of the indexes' backfill
    /// once the other universe's indexes backfill too, are replicated here, and the indexed table
    /// has caught up to the returned time.
    pub(crate) async fn prepare_bidirectional_index_backfill(
        &self,
        index_ids: &[Vec<u8>],
        indexed_table_id: &str,
    ) -> Result<u64, Status> {
        let deadline = Instant::now() + (self.options.bidirectional_index_timeout)();
        tracing::info!(
            "Preparing indexes {:?} on table {indexed_table_id} for bi-directional xCluster \
             replication",
            index_ids
                .iter()
                .map(|i| String::from_utf8_lossy(i).into_owned())
                .collect::<Vec<_>>()
        );
        let snapshot = self.catalog.snapshot();
        // Replicating between three universes (A <=> B <=> C <=> A) is not supported.
        let streams = target_streams_of(&snapshot, indexed_table_id)?;
        if streams.len() != 1 {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Expected 1 xCluster stream for table {indexed_table_id}: {} vs. 1",
                    streams.len()
                ),
            ));
        }
        let (group, indexed_stream) = streams.into_iter().next().unwrap_or_default();
        let (_, universe) = crate::setup::universe(&snapshot, &group).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("Replication group {group} not found"),
            )
        })?;
        let data = SetupData {
            group_id: group.clone(),
            source_masters: universe.producer_master_addresses.clone(),
            ..Default::default()
        };
        for index_id in index_ids {
            self.add_bidirectional_index_to_target(&group, &data, index_id, deadline)
                .await?;
        }
        // Every index backfills on both universes now; the indexed table must catch up first.
        let backfill_ht = self.xrepl.host_now()?;
        let skew = (self.options.max_clock_skew)().as_micros() as u64;
        let target_micros = (backfill_ht >> 12) + 3 * skew;
        tracing::info!(
            "Waiting for replication of indexed table {indexed_table_id} stream {indexed_stream} \
             to catch up to backfill time {}",
            ht_string(backfill_ht)
        );
        self.wait_for_source_drain(&data, &indexed_stream, target_micros, deadline)
            .await
            .map_err(|s| {
                status(
                    s.code,
                    format!(
                        "Error waiting for replication drain of indexed table {indexed_table_id} \
                         stream {indexed_stream}: {}",
                        s.message
                    ),
                )
            })?;
        tracing::info!(
            "Indexed table {indexed_table_id} xCluster stream {indexed_stream} caught up to \
             backfill time {}",
            ht_string(backfill_ht)
        );
        Ok(backfill_ht)
    }

    /// `AddBiDirectionalIndexToXClusterTargetTask`.
    async fn add_bidirectional_index_to_target(
        &self,
        group: &str,
        data: &SetupData,
        index_id: &[u8],
        deadline: Instant,
    ) -> Result<(), Status> {
        let task = format!(
            "AddBiDirectionalIndexToXClusterTargetTask [{}]",
            String::from_utf8_lossy(index_id)
        );
        let timed_out = || status(StatusCode::TimedOut, format!("{task}:  Timed out"));
        let snapshot = self.catalog.snapshot();
        let pb = table_pb(&snapshot, index_id).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("Table {} not found", String::from_utf8_lossy(index_id)),
            )
        })?;
        if indexed_table_id(&pb).is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                format!("{} is not an index", String::from_utf8_lossy(index_id)),
            ));
        }
        if self.group_has_table(&snapshot, group, index_id)? {
            tracing::info!(
                "{task}: Table {} is already part of xCluster universe replication {group}",
                String::from_utf8_lossy(index_id)
            );
            return Ok(());
        }

        // The other universe's index, by name and schema, once it runs.
        let namespace = snapshot
            .get(SysRowEntryType::NAMESPACE, pb.namespace_id())
            .and_then(|n| n.parse::<SysNamespaceEntryPB>().ok())
            .map(|n| String::from_utf8_lossy(n.name()).into_owned())
            .unwrap_or_default();
        let name = String::from_utf8_lossy(pb.name()).into_owned();
        let schema = self.target_schema_name(index_id, &pb).await;
        let mut list = ListTablesRequestPB::new();
        list.set_name_filter(name);
        let ns = list.namespace.mut_or_insert_default();
        ns.set_name(namespace);
        ns.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_PGSQL));
        let mut last_log: Option<Instant> = None;
        let (source_index, source_indexed) = loop {
            let resp: ListTablesResponsePB = self
                .source_call(data, "ListTables", &list, deadline)
                .await
                .and_then(check)?;
            let found = resp.tables.iter().find(|t| t.pgschema_name() == schema);
            match found {
                Some(t) if t.state() == TableState::RUNNING => {
                    if t.indexed_table_id().is_empty() {
                        return Err(status(
                            StatusCode::IllegalState,
                            format!(
                                "Source table {} is not an index",
                                String::from_utf8_lossy(t.id())
                            ),
                        ));
                    }
                    break (
                        String::from_utf8_lossy(t.id()).into_owned(),
                        t.indexed_table_id().to_string(),
                    );
                }
                _ => {
                    if last_log.is_none_or(|at| at.elapsed() >= Duration::from_secs(10)) {
                        tracing::warn!("{task}: {WAITING_FOR_OTHER_UNIVERSE}");
                        last_log = Some(Instant::now());
                    }
                    self.poll_before(deadline, timed_out).await?;
                }
            }
        };

        // `WaitForBackfillIndexToStartOnSource`.
        last_log = None;
        while !self
            .is_backfill_index_started(data, &source_index, &source_indexed, deadline)
            .await?
        {
            if last_log.is_none_or(|at| at.elapsed() >= Duration::from_secs(10)) {
                tracing::info!(
                    "{task}: Waiting for backfill of index {source_index} to start on other \
                     universe"
                );
                last_log = Some(Instant::now());
            }
            self.poll_before(deadline, timed_out).await?;
        }
        if pb.colocated() {
            // The parent tablet is replicated already.
            return Ok(());
        }

        // `GetSourceIndexStreamId`.
        let mut streams_req = ListCDCStreamsRequestPB::new();
        streams_req.set_table_id(source_index.clone());
        streams_req.id_type = Some(EnumOrUnknown::new(IdTypePB::TABLE_ID));
        let streams: ListCDCStreamsResponsePB = self
            .source_call(data, "ListCDCStreams", &streams_req, deadline)
            .await
            .and_then(check)?;
        if streams.streams.len() != 1 {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Expected exactly one stream for source table {source_index}: {} vs. 1",
                    streams.streams.len()
                ),
            ));
        }
        let stream_id = String::from_utf8_lossy(streams.streams[0].stream_id()).into_owned();

        // `AddIndexToReplicationGroup`, then `WaitForSetupReplication` with health checks, so the
        // CREATE INDEX fails when replication is not healthy.
        let Some(this) = self.me.upgrade() else {
            return Err(status(
                StatusCode::ShutdownInProgress,
                "xCluster is shutting down",
            ));
        };
        let token = self.leadership.token()?;
        this.add_tables_to_replication(
            &token,
            deadline,
            crate::alter::AddTables {
                group_id: group.to_string(),
                namespace: None,
                source_table_ids: vec![source_index],
                stream_ids: vec![stream_id],
                target_table_id: None,
            },
        )
        .await
        .map_err(|e| crate::status_of(&e))?;
        let alter_id = format!("{group}{ALTER_SUFFIX}");
        loop {
            self.poll_before(deadline, timed_out).await?;
            if let Some(result) = self.setup_done(&alter_id, false)? {
                return result;
            }
        }
    }

    /// Sleeps one poll interval, failing at the deadline first (`SCHEDULE_WITH_DELAY_AND_RETURN`).
    async fn poll_before(
        &self,
        deadline: Instant,
        timed_out: impl Fn() -> Status,
    ) -> Result<(), Status> {
        if Instant::now() >= deadline {
            return Err(timed_out());
        }
        tokio::time::sleep(POLL).await;
        Ok(())
    }

    /// `HasTable`: the group replicates into the table already.
    fn group_has_table(
        &self,
        snapshot: &CatalogSnapshot,
        group: &str,
        table_id: &[u8],
    ) -> Result<bool, Status> {
        let config = cluster_config(snapshot)?;
        let Some(entry) = config.consumer_registry.producer_map.get(group) else {
            return Ok(false);
        };
        if entry.disable_stream {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Table belongs to xCluster replication group {group} which is currently \
                     disabled"
                ),
            ));
        }
        let id = String::from_utf8_lossy(table_id);
        Ok(entry.stream_map.values().any(|s| s.consumer_table_id == id))
    }

    /// `YBClient::IsBackfillIndexStarted` against the other universe: the index is backfilling
    /// or done there.
    async fn is_backfill_index_started(
        &self,
        data: &SetupData,
        index_id: &str,
        indexed_id: &str,
        deadline: Instant,
    ) -> Result<bool, Status> {
        let mut req = GetTableSchemaRequestPB::new();
        req.table
            .mut_or_insert_default()
            .set_table_id(indexed_id.as_bytes().to_vec());
        let resp: GetTableSchemaResponsePB = self
            .source_call(data, "GetTableSchema", &req, deadline)
            .await
            .and_then(check)?;
        let Some(index) = resp
            .indexes
            .iter()
            .find(|i| i.table_id() == index_id.as_bytes())
        else {
            // The indexed table's schema may not have it applied yet.
            return Ok(false);
        };
        if !index.backfill_error_message().is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Index {index_id} has backfill error: {}",
                    index.backfill_error_message()
                ),
            ));
        }
        let permissions = index.index_permissions();
        // Anything past READ_WRITE_AND_DELETE means the index is being dropped.
        if permissions.value() > IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE.value() {
            return Err(status(
                StatusCode::IllegalState,
                format!("Index permissions {permissions:?} for index {index_id} is unexpected"),
            ));
        }
        // A YSQL index goes from WRITE_AND_DELETE straight to READ_WRITE_AND_DELETE; PG tracks
        // the stages in between.
        Ok(resp.is_backfilling()
            || permissions == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE)
    }

    /// `XClusterClient::WaitForReplicationDrain` against the other universe. Its master answers
    /// near the call's deadline with what is still behind, which, as the C++ client does not
    /// read it, ends the wait as well.
    async fn wait_for_source_drain(
        &self,
        data: &SetupData,
        stream_id: &str,
        target_micros: u64,
        deadline: Instant,
    ) -> Result<(), Status> {
        let mut req = WaitForReplicationDrainRequestPB::new();
        req.stream_ids.push(stream_id.as_bytes().to_vec());
        req.set_target_time(target_micros);
        // The whole deadline, as `SyncLeaderMasterRpc` gives it: the source waits server side.
        let resp: WaitForReplicationDrainResponsePB = call_leader(
            &*self.remote,
            &data.group_id,
            &data.source_masters,
            MASTER_SERVICE,
            "WaitForReplicationDrain",
            &req,
            deadline,
        )
        .await?;
        check(resp).map(|_| ())
    }
}

/// `HybridTime::ToString`.
fn ht_string(ht: u64) -> String {
    let (physical, logical) = (ht >> 12, ht & 0xfff);
    if logical != 0 {
        format!("{{ physical: {physical} logical: {logical} }}")
    } else {
        format!("{{ physical: {physical} }}")
    }
}

/// `GetStreamIdsForTable` on the target: each inbound group replicating into the table, with its
/// stream.
fn target_streams_of(
    snapshot: &CatalogSnapshot,
    table_id: &str,
) -> Result<Vec<(String, String)>, Status> {
    let config = cluster_config(snapshot)?;
    let mut out = Vec::new();
    for (group, entry) in &config.consumer_registry.producer_map {
        for (stream_id, stream) in &entry.stream_map {
            if stream.consumer_table_id == table_id {
                out.push((group.clone(), stream_id.clone()));
            }
        }
    }
    Ok(out)
}
