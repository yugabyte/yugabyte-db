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

//! Splits of replicated tables. On the target, a consumer tablet's split remaps the stream's
//! tablets (`HandleTabletSplit`). On the source, a split parent stays hidden until the target has
//! read up to its SPLIT_OP and polled both children (`ProcessSplitChildStreams`); then its
//! `cdc_state` rows go and it is released to the hidden-objects cleanup.

use std::collections::HashMap;

use master_catalog_state::CatalogSnapshot;
use master_leadership::LeaderToken;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysTabletsEntryPB;
use yb_proto::pb::master_rust_peer::{
    CdcStateDeleteRequestPB, CdcStateDeleteResponsePB, CdcStateEntryPB, CdcStateReadRequestPB,
    CdcStateReadResponsePB, XClusterConsumerSplitRequestPB, XClusterConsumerSplitResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::controller::outbound_streams;
use crate::{status, XCluster};

impl XCluster {
    /// `HandleTabletSplit` of a consumer table: each stream into it gets the new mapping.
    pub(crate) async fn handle_consumer_split(
        &self,
        token: &LeaderToken,
        table_id: &[u8],
        parent: &[u8],
        children: &[Vec<u8>],
    ) -> Result<(), Status> {
        let table = String::from_utf8_lossy(table_id).into_owned();
        let config = crate::setup::cluster_config(&self.catalog.snapshot())?;
        let streams: Vec<(String, String)> = config
            .consumer_registry
            .producer_map
            .iter()
            .flat_map(|(group, e)| {
                e.stream_map
                    .iter()
                    .filter(|(_, s)| s.consumer_table_id == table)
                    .map(move |(id, _)| (group.clone(), id.clone()))
            })
            .collect();
        if streams.is_empty() {
            return Ok(());
        }
        let tablets = self.consumer_tablets(&table)?;
        let mut updates = Vec::new();
        for (group, stream_id) in &streams {
            let Some(stream) = config
                .consumer_registry
                .producer_map
                .get(group)
                .and_then(|e| e.stream_map.get(stream_id))
            else {
                continue;
            };
            let mut req = XClusterConsumerSplitRequestPB::new();
            req.consumer_tablets = tablets.clone();
            req.set_source_tablet_id(parent.to_vec());
            req.child_tablet_ids = children.to_vec();
            req.stream_entry = protobuf::MessageField::some(stream.clone());
            let resp: XClusterConsumerSplitResponsePB =
                self.host_call("XClusterConsumerSplit", req).await?;
            updates.push((
                group.clone(),
                stream_id.clone(),
                stream.clone(),
                resp.stream_entry.unwrap_or_default(),
            ));
        }
        self.update_cluster_config(token, |config| {
            let registry = config.consumer_registry.mut_or_insert_default();
            for (group, stream_id, old, new) in &updates {
                let Some(stream) = registry
                    .producer_map
                    .get_mut(group)
                    .and_then(|e| e.stream_map.get_mut(stream_id))
                else {
                    tracing::warn!(
                        "Unable to find the producer entry for universe {group}, stream \
                         {stream_id}"
                    );
                    continue;
                };
                if stream != old {
                    return Err(status(
                        StatusCode::TryAgain,
                        "The stream entry changed while the split was applied",
                    ));
                }
                *stream = new.clone();
            }
            Ok(!updates.is_empty())
        })
        .await
    }

    /// Whether `tablet` is a split parent of a table with xCluster streams that the target has
    /// not finished with.
    pub(crate) fn retains_split_parent(
        &self,
        snapshot: &CatalogSnapshot,
        tablet_id: &[u8],
    ) -> bool {
        let Some(pb) = snapshot
            .get(SysRowEntryType::TABLET, tablet_id)
            .and_then(|t| t.parse::<SysTabletsEntryPB>().ok())
        else {
            return false;
        };
        pb.split_tablet_ids.len() >= 2
            && !outbound_streams(snapshot, pb.table_id()).is_empty()
            && !self
                .released_split_parents
                .lock()
                .unwrap()
                .contains(tablet_id)
    }

    /// `DoProcessHiddenTablets` for split parents: a parent is released once, for each stream,
    /// its row is gone, or it was polled and both children were polled (its row is deleted). A
    /// parent waits for its own hidden parent.
    pub(crate) async fn process_split_parents(&self) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        {
            // Released parents that are gone need no record.
            let mut released = self.released_split_parents.lock().unwrap();
            released.retain(|t| snapshot.get(SysRowEntryType::TABLET, t).is_some());
        }
        let mut parents: HashMap<Vec<u8>, SysTabletsEntryPB> = HashMap::new();
        for tablet in snapshot.iter(SysRowEntryType::TABLET) {
            if !tablet.tablet().is_some_and(|t| t.hidden) {
                continue;
            }
            if !self.retains_split_parent(&snapshot, &tablet.key.id) {
                continue;
            }
            if let Ok(pb) = tablet.parse::<SysTabletsEntryPB>() {
                parents.insert(tablet.key.id.to_vec(), pb);
            }
        }
        if parents.is_empty() {
            return Ok(());
        }
        let mut rows_by_stream: HashMap<String, Vec<CdcStateEntryPB>> = HashMap::new();
        let mut to_delete = Vec::new();
        let mut released = Vec::new();
        for (tablet_id, pb) in &parents {
            if parents.contains_key(pb.split_parent_tablet_id()) {
                continue;
            }
            let streams = outbound_streams(&snapshot, pb.table_id());
            let mut done = 0;
            for stream in &streams {
                if !rows_by_stream.contains_key(&stream.id) {
                    let mut read = CdcStateReadRequestPB::new();
                    read.set_stream_id(stream.id.as_bytes().to_vec());
                    let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
                    rows_by_stream.insert(stream.id.clone(), rows.entries);
                }
                let rows = &rows_by_stream[&stream.id];
                let row_of = |t: &[u8]| rows.iter().find(|r| r.tablet_id() == t);
                let Some(parent_row) = row_of(tablet_id) else {
                    done += 1;
                    continue;
                };
                if !parent_row.has_last_replication_time() {
                    break;
                }
                let children_polled = pb
                    .split_tablet_ids
                    .iter()
                    .all(|c| row_of(c).is_some_and(|r| r.has_last_replication_time()));
                if children_polled {
                    tracing::info!(
                        "Deleting tablet {} from stream {}. Reason: Consumer finished processing \
                         parent tablet after split.",
                        String::from_utf8_lossy(tablet_id),
                        stream.id
                    );
                    let mut key = CdcStateEntryPB::new();
                    key.set_tablet_id(tablet_id.clone());
                    key.set_stream_id(stream.id.as_bytes().to_vec());
                    to_delete.push(key);
                    done += 1;
                }
            }
            if done == streams.len() {
                released.push(tablet_id.clone());
            }
        }
        if !to_delete.is_empty() {
            let mut req = CdcStateDeleteRequestPB::new();
            req.keys = to_delete;
            let _: CdcStateDeleteResponsePB = self.host_call("CdcStateDelete", req).await?;
        }
        self.released_split_parents.lock().unwrap().extend(released);
        Ok(())
    }
}
