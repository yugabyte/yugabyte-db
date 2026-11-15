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

//! xCluster's background work each term, and what DDL asks of it.
//!
//! A dropped table with xCluster streams is hidden, not deleted, while the streams last: the
//! target drops its table and then deletes the stream, or after `cdc_wal_retention_time_secs`
//! the streams are dropped here (`DoProcessHiddenTablets`).

use std::sync::Arc;
use std::time::Duration;

use master_catalog_state::CatalogSnapshot;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use master_xrepl::{stream_rows, strip_sequences_data_alias, StreamRow, XreplExtension};
use yb_host_api::Status;
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, SysTablesEntryPB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::XCluster;

/// The xCluster streams out of a table that are not being deleted (`GetStreamsForTable`).
pub(crate) fn outbound_streams(snapshot: &CatalogSnapshot, table_id: &[u8]) -> Vec<StreamRow> {
    stream_rows(snapshot)
        .into_iter()
        .filter(|r| {
            r.pb.namespace_id().is_empty()
                && !r.is_deleting()
                && r.pb
                    .table_id
                    .first()
                    .is_some_and(|t| strip_sequences_data_alias(t).as_bytes() == table_id)
        })
        .collect()
}

impl XCluster {
    /// `DoProcessHiddenTablets` for dropped tables: their streams are dropped once the table has
    /// been hidden for `cdc_wal_retention_time_secs`.
    pub(crate) async fn drop_streams_of_old_hidden_tables(&self) -> Result<(), Status> {
        let token = self.leadership.token()?;
        let snapshot = self.catalog.snapshot();
        let now_micros = self.xrepl.host_now()? >> 12;
        let retention = (self.options.wal_retention)().as_micros() as u64;
        let mut expired = Vec::new();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            if !table.table().is_some_and(|t| t.hidden) {
                continue;
            }
            let streams = outbound_streams(&snapshot, &table.key.id);
            if streams.is_empty() {
                continue;
            }
            let pb: SysTablesEntryPB = match table.parse() {
                Ok(pb) => pb,
                Err(_) => continue,
            };
            if !pb.has_hide_hybrid_time() {
                continue;
            }
            if (pb.hide_hybrid_time() >> 12) + retention < now_micros {
                tracing::warn!(
                    "Table {} has been marked hidden for longer than {}s \
                     (cdc_wal_retention_time_secs). Dropping its xCluster streams {:?}",
                    String::from_utf8_lossy(&table.key.id),
                    retention / 1_000_000,
                    streams.iter().map(|s| &s.id).collect::<Vec<_>>()
                );
                expired.extend(streams.into_iter().map(|s| s.id));
            }
        }
        if expired.is_empty() {
            return Ok(());
        }
        self.xrepl
            .drop_streams(
                &token,
                tokio::time::Instant::now() + (self.options.rpc_timeout)(),
                &expired,
                StreamState::DELETING,
            )
            .await
    }
}

impl XreplExtension for XCluster {
    fn post_tablet_create(&self, table_id: Vec<u8>) -> BoxFuture<'_, Result<(), Status>> {
        Box::pin(async move { self.add_new_table(&table_id).await })
    }

    /// `PopulateTabletDeleteRetainerInfoForTableDrop`: only a colocation parent of colocated
    /// tables is replicated.
    fn retains_dropped_table(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
        let colocated = snapshot
            .get(SysRowEntryType::TABLE, table_id)
            .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
            .is_some_and(|pb| {
                pb.colocated()
                    && !master_table_lifecycle::lookup::is_colocation_parent_table_id(table_id)
            });
        if colocated {
            return false;
        }
        let streams = outbound_streams(snapshot, table_id);
        if !streams.is_empty() {
            tracing::info!(
                "Retaining dropped table {} since it has active xCluster streams {:?}",
                String::from_utf8_lossy(table_id),
                streams.iter().map(|s| &s.id).collect::<Vec<_>>()
            );
        }
        !streams.is_empty()
    }

    fn retains_hidden_tablet(&self, snapshot: &CatalogSnapshot, tablet_id: &[u8]) -> bool {
        let Some(table_id) = snapshot
            .get(SysRowEntryType::TABLET, tablet_id)
            .and_then(|t| t.tablet().map(|t| t.table_id.clone()))
        else {
            return false;
        };
        let hidden = snapshot
            .get(SysRowEntryType::TABLE, &table_id)
            .is_some_and(|t| t.table().is_some_and(|h| h.hidden));
        hidden && !outbound_streams(snapshot, &table_id).is_empty()
    }
}

/// Runs [`XCluster`]'s background steps each term.
pub struct XClusterController {
    xcluster: Arc<XCluster>,
}

impl XClusterController {
    /// A controller for the term that just began.
    pub fn new(xcluster: Arc<XCluster>) -> Arc<XClusterController> {
        Arc::new(XClusterController { xcluster })
    }
}

impl Controller for XClusterController {
    fn name(&self) -> &'static str {
        "xcluster"
    }

    fn interval(&self) -> Duration {
        (self.xcluster.options.bg_task_interval)()
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            let x = &self.xcluster;
            let Ok(token) = x.leadership.token() else {
                return;
            };
            x.start_checkpoint_tasks(&token);
            if let Err(status) = x.drop_streams_of_old_hidden_tables().await {
                tracing::warn!("Failed to process xCluster hidden tables: {status}");
            }
        })
    }
}
