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

//! Index backfill under xCluster. On a target, the backfill reads at a time xCluster picks
//! (`TryGetXClusterInfoForIndexBackfill`), or is skipped when the source's backfill writes are
//! replicated. On a source, an index's streams move past its backfill once it is done
//! (`MarkIndexBackfillCompleted`), so the target's own backfill is not applied twice.
//!
//! The bidirectional path (`ysql_auto_add_new_index_to_bi_directional_xcluster`) is not served.

use master_catalog_state::Mutation;
use master_table_lifecycle::BackfillDecision;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysTablesEntryPB;
use yb_proto::pb::master_types::SysRowEntryType;

use crate::outbound::table_pb;
use crate::{status, XCluster};

impl XCluster {
    /// `TryGetXClusterInfoForIndexBackfill`.
    pub(crate) fn backfill_decision(
        &self,
        index_ids: &[Vec<u8>],
        indexed_table_id: &[u8],
    ) -> Result<BackfillDecision, Status> {
        let snapshot = self.catalog.snapshot();
        let indexed = table_pb(&snapshot, indexed_table_id)
            .ok_or_else(|| status(StatusCode::NotFound, "Indexed table not found"))?;
        let ns = String::from_utf8_lossy(indexed.namespace_id()).into_owned();
        let indexed_id = String::from_utf8_lossy(indexed_table_id).into_owned();
        // The target's automatic mode (`XClusterTargetManager::IsNamespaceInAutomaticDDLMode`): a
        // source's indexes have no xCluster backfill time.
        if self.is_automatic_mode_target(&snapshot, &ns) {
            // The time the ddl_queue handler moves the safe time to: the safe time cannot reach
            // now, as ddl_queue waits for this index.
            let mut backfill_ht = None;
            for index_id in index_ids {
                let pb = table_pb(&snapshot, index_id)
                    .ok_or_else(|| status(StatusCode::NotFound, "Index not found"))?;
                let info = &pb.xcluster_table_info;
                if !info.has_xcluster_backfill_hybrid_time() {
                    return Err(status(
                        StatusCode::IllegalState,
                        "Index missing xcluster_backfill_hybrid_time",
                    ));
                }
                let ht = info.xcluster_backfill_hybrid_time();
                if crate::safe_time::is_valid(ht) {
                    if backfill_ht.is_some_and(|b| b != ht) {
                        return Err(status(
                            StatusCode::InvalidArgument,
                            "Indexes have different xCluster backfill hybrid times",
                        ));
                    }
                    backfill_ht = Some(ht);
                }
            }
            if let Some(ht) = backfill_ht {
                if self.target_skips_local_backfill(&snapshot, index_ids)? {
                    tracing::info!("Skipping local index backfill for indexed_table {indexed_id}");
                    // `FinalizeReplicatedIndexBackfill`: the add-table task waited for the safe
                    // time to pass the source's backfill.
                    let safe_time = self
                        .safe_time
                        .lock()
                        .unwrap()
                        .without_ddl_queue
                        .get(&ns)
                        .copied()
                        .filter(|t| crate::safe_time::is_valid(*t))
                        .ok_or_else(|| {
                            status(
                                StatusCode::TryAgain,
                                format!(
                                    "xCluster safe time for namespace {ns} is not available yet"
                                ),
                            )
                        })?;
                    if safe_time < ht {
                        return Err(status(
                            StatusCode::IllegalState,
                            format!(
                                "xCluster safe time {safe_time} has not reached source backfill \
                                 ht {ht} for replicated index backfill"
                            ),
                        ));
                    }
                    return Ok(BackfillDecision::DeferToReplicated(ht));
                }
                tracing::info!(
                    "Using provided xcluster_backfill_hybrid_time {ht} as the backfill read time"
                );
                return Ok(BackfillDecision::RunLocalAt(ht));
            }
            // An index created by hand, without the time.
            tracing::warn!(
                "No xCluster backfill hybrid time set for indexes in automatic mode, falling \
                 back to non-automatic mode flow."
            );
        }
        if indexed.colocated() {
            // Only the parent is replicated, and backfill writes are not: a time after the
            // source's, which the table's later creation here guarantees, suffices.
            return Ok(BackfillDecision::TabletSafeTime);
        }
        if self.is_consumer_table(&snapshot, &indexed_id) {
            let Some(safe_time) = crate::safe_time::safe_time_map(&snapshot).get(&ns).copied()
            else {
                // Not in transactional replication.
                return Ok(BackfillDecision::TabletSafeTime);
            };
            if !crate::safe_time::is_valid(safe_time) {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!("Invalid xCluster safe time for namespace {ns}"),
                ));
            }
            tracing::info!("Using xCluster safe time {safe_time} as the backfill read time");
            return Ok(BackfillDecision::RunLocalAt(safe_time));
        }
        Ok(BackfillDecision::TabletSafeTime)
    }

    fn is_consumer_table(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        table_id: &str,
    ) -> bool {
        crate::setup::cluster_config(snapshot).is_ok_and(|c| {
            c.consumer_registry
                .producer_map
                .values()
                .flat_map(|p| p.stream_map.values())
                .any(|s| s.consumer_table_id == table_id)
        })
    }

    /// `ShouldTargetSkipLocalIndexBackfill`: every index's stream (a colocated index's is its
    /// parent's) uses the target-applied filter, so the source replicates its backfill writes.
    fn target_skips_local_backfill(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        index_ids: &[Vec<u8>],
    ) -> Result<bool, Status> {
        let config = crate::setup::cluster_config(snapshot)?;
        for index_id in index_ids {
            let pb = table_pb(snapshot, index_id)
                .ok_or_else(|| status(StatusCode::NotFound, "Index not found"))?;
            let table = if pb.colocated() {
                String::from_utf8_lossy(pb.parent_table_id()).into_owned()
            } else {
                String::from_utf8_lossy(index_id).into_owned()
            };
            // `IsTableUsingTargetAppliedFilter`.
            let streams: Vec<_> = config
                .consumer_registry
                .producer_map
                .values()
                .flat_map(|p| p.stream_map.values())
                .filter(|s| s.consumer_table_id == table)
                .collect();
            if streams.is_empty() || !streams.iter().all(|s| s.xcluster_use_target_applied_filter) {
                return Ok(false);
            }
        }
        Ok(true)
    }

    /// `MarkIndexBackfillCompleted`: the indexes' `xcluster_table_info` goes, and each index's
    /// xCluster streams move to the end of its WAL, except those whose target applies the
    /// replicated backfill writes, WAL anchors and bidirectionally replicated indexes.
    pub(crate) async fn mark_index_backfill_completed(
        &self,
        index_ids: &[Vec<u8>],
    ) -> Result<(), Status> {
        let token = self.leadership.token()?;
        self.writer
            .mutate(&token, |snapshot| {
                let mut m = Mutation::new();
                for id in index_ids {
                    let Some(entity) = snapshot.get(SysRowEntryType::TABLE, id) else {
                        continue;
                    };
                    let mut pb: SysTablesEntryPB = entity.parse().map_err(crate::corrupt)?;
                    if pb.xcluster_table_info.is_none() {
                        continue;
                    }
                    pb.xcluster_table_info = protobuf::MessageField::none();
                    m = m
                        .read(entity)
                        .upsert_message(SysRowEntryType::TABLE, id, &pb)?;
                }
                Ok((m, ()))
            })
            .await?;
        let snapshot = self.catalog.snapshot();
        // `GetWalAnchorStreamIds`: they stay where they are.
        let anchors: std::collections::HashSet<String> = crate::outbound::groups(&snapshot)
            .iter()
            .flat_map(|(_, g)| g.namespace_infos.values())
            .flat_map(|n| n.table_infos.iter())
            .filter(|(table, _)| index_ids.iter().any(|i| i.as_slice() == table.as_bytes()))
            .map(|(_, t)| String::from_utf8_lossy(t.wal_anchor_stream_id()).into_owned())
            .filter(|id| !id.is_empty())
            .collect();
        let mut table_streams = Vec::new();
        for index_id in index_ids {
            if self.is_table_bidirectionally_replicated(&snapshot, index_id) {
                continue;
            }
            let index = String::from_utf8_lossy(index_id).into_owned();
            for stream in crate::controller::outbound_streams(&snapshot, index_id) {
                if stream.pb.xcluster_use_target_applied_filter() {
                    tracing::info!(
                        "Skipping past-backfill checkpoint of xCluster stream {} of index {index} \
                         because backfill writes are replicated to the target",
                        stream.id
                    );
                    continue;
                }
                if anchors.contains(&stream.id) {
                    continue;
                }
                tracing::info!(
                    "Checkpointing xCluster stream {} of index {index} to its end of WAL",
                    stream.id
                );
                table_streams.push((index.clone(), stream.id.clone()));
            }
        }
        if table_streams.is_empty() {
            return Ok(());
        }
        if self.bootstrap_producer(&table_streams, false).await? {
            tracing::error!(
                "Unexpectedly found bootstrap required when check_if_bootstrap_required was set \
                 to false"
            );
        }
        Ok(())
    }

    /// `IsTableBiDirectionallyReplicated`: the table is both a source and a target.
    fn is_table_bidirectionally_replicated(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        table_id: &[u8],
    ) -> bool {
        !crate::controller::outbound_streams(snapshot, table_id).is_empty()
            && self.is_consumer_table(snapshot, &String::from_utf8_lossy(table_id))
    }
}
