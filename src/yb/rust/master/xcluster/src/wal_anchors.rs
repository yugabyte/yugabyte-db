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

//! The target's half of WAL anchor streams (`XClusterWalAnchorDeletionTask`): a table the target
//! created for an automatic-mode group carries
//! `xcluster_pending_wal_anchor_deletion_source_table_id` from the write that commits its DDL
//! until the source confirms it deleted the source table's anchor stream. The marker is the only
//! state, so a new leader picks up where the last one stopped.

use std::collections::BTreeMap;
use std::sync::atomic::Ordering;

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use tokio::time::Instant;
use yb_host_api::Status;
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb::State as TableState, SysTablesEntryPB,
};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::master_replication::{
    DeleteXClusterWalAnchorStreamsRequestPB, DeleteXClusterWalAnchorStreamsResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::remote::{call_leader, MASTER_SERVICE};
use crate::setup::{includes_consumer_namespace, universes};
use crate::{corrupt, XCluster};

/// One inbound group's tables whose anchors go in one call: the source's masters, and each
/// target table with its source table.
type Batch = (Vec<HostPortPB>, Vec<(Vec<u8>, Vec<u8>)>);

impl XCluster {
    /// Starts a deletion pass unless one is running (`DeletePendingWalAnchorStreams`).
    pub(crate) fn start_wal_anchor_deletion(&self, token: &LeaderToken) {
        if (self.options.pause_wal_anchor_deletion)() {
            return;
        }
        let Some(this) = self.me.upgrade() else {
            return;
        };
        if self
            .wal_anchor_deletion_running
            .swap(true, Ordering::AcqRel)
        {
            return;
        }
        let token = *token;
        tokio::spawn(async move {
            if let Err(s) = this.delete_pending_wal_anchor_streams(&token).await {
                tracing::warn!("Failed to delete pending xCluster WAL_ANCHOR streams: {s}");
            }
            this.wal_anchor_deletion_running
                .store(false, Ordering::Release);
        });
    }

    /// The marked tables of each automatic-mode inbound group, by group. A dropped table, or one
    /// whose group is gone, needs nothing deleted.
    fn pending_wal_anchor_batches(&self) -> Result<BTreeMap<String, Batch>, Status> {
        let snapshot = self.catalog.snapshot();
        let groups: Vec<_> = universes(&snapshot)
            .into_iter()
            .filter(|u| u.db_scoped_info.automatic_ddl_mode())
            .collect();
        let mut batches: BTreeMap<String, Batch> = BTreeMap::new();
        if groups.is_empty() {
            return Ok(batches);
        }
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else {
                continue;
            };
            if hot.state == TableState::DELETED as i32 || hot.state == TableState::DELETING as i32 {
                continue;
            }
            let ns = String::from_utf8_lossy(&hot.namespace_id).into_owned();
            let Some(group) = groups.iter().find(|g| includes_consumer_namespace(g, &ns)) else {
                continue;
            };
            let pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            let source = pb.xcluster_pending_wal_anchor_deletion_source_table_id();
            if source.is_empty() {
                continue;
            }
            batches
                .entry(group.replication_group_id().to_string())
                .or_insert_with(|| (group.producer_master_addresses.clone(), Vec::new()))
                .1
                .push((table.key.id.to_vec(), source.to_vec()));
        }
        Ok(batches)
    }

    /// Asks each group's source to delete the anchors of its marked tables, then clears the
    /// markers the source confirmed. A group whose source cannot be reached is retried next pass.
    pub(crate) async fn delete_pending_wal_anchor_streams(
        &self,
        token: &LeaderToken,
    ) -> Result<(), Status> {
        for (group, (masters, tables)) in self.pending_wal_anchor_batches()? {
            let mut req = DeleteXClusterWalAnchorStreamsRequestPB::new();
            req.set_replication_group_id(group.clone());
            req.source_table_ids = tables.iter().map(|(_, source)| source.clone()).collect();
            let deadline = Instant::now() + (self.options.remote_timeout)();
            let result: Result<DeleteXClusterWalAnchorStreamsResponsePB, Status> = call_leader(
                &*self.remote,
                &group,
                &masters,
                MASTER_SERVICE,
                "DeleteXClusterWalAnchorStreams",
                &req,
                deadline,
            )
            .await;
            if let Err(s) = result.and_then(crate::remote::check) {
                tracing::warn!(
                    "Failed to delete {} xCluster WAL_ANCHOR stream(s) on source in replication \
                     group {group}: {s}. Will retry.",
                    tables.len()
                );
                continue;
            }
            if (self.options.wal_anchor_deletion_skip_marker_clear)() {
                continue;
            }
            tracing::info!(
                "Source confirmed the anchor streams are gone, clearing the durable markers for \
                 {:?}",
                tables
                    .iter()
                    .map(|(id, _)| String::from_utf8_lossy(id))
                    .collect::<Vec<_>>()
            );
            self.writer
                .mutate(token, |snapshot| {
                    let mut mutation = Mutation::new();
                    for (id, source) in &tables {
                        let Some(table) = snapshot.get(SysRowEntryType::TABLE, id) else {
                            continue;
                        };
                        let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                        // A marker set since is for another anchor.
                        if pb.xcluster_pending_wal_anchor_deletion_source_table_id() != &source[..]
                        {
                            continue;
                        }
                        pb.clear_xcluster_pending_wal_anchor_deletion_source_table_id();
                        mutation =
                            mutation
                                .read(table)
                                .upsert_message(SysRowEntryType::TABLE, id, &pb)?;
                    }
                    Ok((mutation, ()))
                })
                .await?;
        }
        Ok(())
    }
}
