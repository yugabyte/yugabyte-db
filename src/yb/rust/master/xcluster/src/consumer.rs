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

//! What the target's tservers report of the source as they poll it: a source tablet's split
//! (`UpdateConsumerOnProducerSplit`), which remaps the stream's tablets, and a schema change
//! (`UpdateConsumerOnProducerMetadata`), which records the source and target schema versions
//! that go together. Both change the stream's entry in the consumer registry.

use std::collections::BTreeMap;

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;
use yb_proto::pb::cdc_consumer::{SchemaVersionsPB, StreamEntryPB};
use yb_proto::pb::master_replication::{
    UpdateConsumerOnProducerMetadataRequestPB, UpdateConsumerOnProducerMetadataResponsePB,
    UpdateConsumerOnProducerSplitRequestPB,
};
use yb_proto::pb::master_rust_peer::{
    XClusterProducerSplitRequestPB, XClusterProducerSplitResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::{corrupt, status, unknown, XCluster};

/// The start of the error `XCLUSTER_CONSUMER_SCHEMA_VERSION_TOO_OLD` stands for.
pub(crate) const SCHEMA_VERSION_TOO_OLD: &str = "Received too old consumer schema version";

/// `kColocationIdNotSet`.
const COLOCATION_ID_NOT_SET: u32 = 0;

fn invalid(m: &str, req: &impl std::fmt::Debug) -> MasterErrorPB {
    master_ts_manager::master_error_coded(
        Code::INVALID_REQUEST,
        StatusCode::InvalidArgument,
        &format!("{m}: {req:?}"),
    )
}

impl XCluster {
    /// Applies `f` to a stream's registry entry; the config is written, with its version
    /// bumped, when `f` says it changed.
    async fn update_stream_entry<T>(
        &self,
        token: &LeaderToken,
        group: &str,
        stream_id: &str,
        f: impl Fn(&mut StreamEntryPB) -> Result<(bool, T), Status>,
    ) -> Result<T, Status> {
        self.writer
            .mutate(token, |snapshot| {
                let entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut config: SysClusterConfigEntryPB = entity.parse().map_err(corrupt)?;
                let entry = config
                    .consumer_registry
                    .mut_or_insert_default()
                    .producer_map
                    .get_mut(group)
                    .ok_or_else(|| {
                        status(
                            StatusCode::NotFound,
                            format!("Unable to find the producer entry for universe {group}"),
                        )
                    })?;
                let stream = entry.stream_map.get_mut(stream_id).ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!(
                            "Unable to find the stream entry for universe {group}, stream \
                             {stream_id}"
                        ),
                    )
                })?;
                let (changed, out) = f(stream)?;
                if !changed {
                    return Ok((Mutation::new(), out));
                }
                config.set_version(config.version() + 1);
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::CLUSTER_CONFIG,
                        &entity.key.id,
                        &config,
                    )?,
                    out,
                ))
            })
            .await
            .map(|(out, _)| out)
    }

    /// `GetConsumerTableIdForStreamId`.
    fn consumer_table_of_stream(&self, group: &str, stream_id: &str) -> Result<String, Status> {
        let config = crate::setup::cluster_config(&self.catalog.snapshot())?;
        config
            .consumer_registry
            .producer_map
            .get(group)
            .and_then(|e| e.stream_map.get(stream_id))
            .map(|s| s.consumer_table_id.clone())
            .ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!("Unable to find the stream {stream_id} in replication group {group}"),
                )
            })
    }

    /// `UpdateConsumerOnProducerSplit`: the parent's place in the stream's tablet mapping goes to
    /// its children. A split processed before, or one whose children split since, changes
    /// nothing.
    pub async fn update_consumer_on_producer_split(
        &self,
        token: &LeaderToken,
        req: &UpdateConsumerOnProducerSplitRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("UpdateConsumerOnProducerSplit: {:?}", req);
        if !req.has_replication_group_id() {
            return Err(invalid("Producer universe ID must be provided", req));
        }
        if !req.has_stream_id() {
            return Err(invalid("Stream ID must be provided", req));
        }
        if req.producer_split_tablet_info.is_none() {
            return Err(invalid("Producer split tablet info must be provided", req));
        }
        let (group, stream_id) = (req.replication_group_id(), req.stream_id());
        let consumer = if req.consumer_table_id().is_empty() {
            self.consumer_table_of_stream(group, stream_id)
                .map_err(unknown)?
        } else {
            String::from_utf8_lossy(req.consumer_table_id()).into_owned()
        };
        let mut host_req = XClusterProducerSplitRequestPB::new();
        host_req.consumer_tablets = self.consumer_tablets(&consumer).map_err(unknown)?;
        host_req.split_info = req.producer_split_tablet_info.clone();
        // The mapping is computed from the entry the write reads, so a concurrent change of the
        // entry rebuilds it.
        let snapshot = self.catalog.snapshot();
        let config = crate::setup::cluster_config(&snapshot).map_err(unknown)?;
        let entry = config
            .consumer_registry
            .producer_map
            .get(group)
            .and_then(|e| e.stream_map.get(stream_id))
            .cloned()
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!(
                        "Unable to find the stream entry for universe {group}, stream {stream_id}"
                    ),
                ))
            })?;
        if entry.consumer_table_id != consumer {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Consumer table ID provided in request does not match the stream entry",
            )));
        }
        host_req.stream_entry = protobuf::MessageField::some(entry.clone());
        let resp: XClusterProducerSplitResponsePB = self
            .host_call("XClusterProducerSplit", host_req)
            .await
            .map_err(unknown)?;
        if !resp.found_source() {
            if resp.found_all_split_children() {
                tracing::info!("Already processed this tablet split: {:?}", req);
            } else {
                tracing::warn!(
                    "Unable to find matching source tablet {} for universe {group} stream \
                     {stream_id}",
                    String::from_utf8_lossy(req.producer_split_tablet_info.tablet_id())
                );
            }
            return Ok(());
        }
        let updated = resp.stream_entry.unwrap_or_default();
        self.update_stream_entry(token, group, stream_id, |stream| {
            if *stream != entry {
                return Err(status(
                    StatusCode::TryAgain,
                    "The stream entry changed while the split was applied",
                ));
            }
            *stream = updated.clone();
            Ok((true, ()))
        })
        .await
        .map_err(unknown)
    }

    /// `UpdateConsumerOnProducerMetadata`: the stream's current schema versions, with older
    /// pairs kept for rows written before (up to `xcluster_max_old_schema_versions`).
    pub async fn update_consumer_on_producer_metadata(
        &self,
        token: &LeaderToken,
        req: &UpdateConsumerOnProducerMetadataRequestPB,
        resp: &mut UpdateConsumerOnProducerMetadataResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("UpdateConsumerOnProducerMetadata: {:?}", req);
        if (self.options.skip_schema_compatibility_checks_on_alter)() {
            resp.set_should_wait(false);
            return Ok(());
        }
        let stream_id = master_xrepl::parse_stream_id(req.stream_id().as_bytes())?;
        let versions = self
            .update_schema_versions(
                token,
                req.replication_group_id(),
                &stream_id,
                req.producer_schema_version(),
                req.consumer_schema_version(),
                req.colocation_id(),
                false,
            )
            .await
            .map_err(unknown)?;
        resp.schema_versions = protobuf::MessageField::some(versions);
        Ok(())
    }

    /// `DoUpdateConsumerOnProducerMetadata`. With `check_min_consumer_version`, a target
    /// version below every one in the mapping fails with [`SCHEMA_VERSION_TOO_OLD`]: it may be
    /// garbage collected.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn update_schema_versions(
        &self,
        token: &LeaderToken,
        group: &str,
        stream_id: &str,
        producer_version: u32,
        consumer_version: u32,
        colocation_id: u32,
        check_min_consumer_version: bool,
    ) -> Result<SchemaVersionsPB, Status> {
        let consumer = self.consumer_table_of_stream(group, stream_id)?;
        if self
            .catalog
            .snapshot()
            .get(
                SysRowEntryType::TABLE,
                master_xrepl::strip_sequences_data_alias(&consumer).as_bytes(),
            )
            .is_none()
        {
            return Err(status(
                StatusCode::NotFound,
                format!("Missing table id {consumer}"),
            ));
        }
        let max_old = if (self.options.store_older_schema_versions)() {
            (self.options.max_old_schema_versions)() as usize
        } else {
            1
        };
        let colocated =
            master_table_lifecycle::lookup::is_colocation_parent_table_id(consumer.as_bytes())
                && colocation_id != COLOCATION_ID_NOT_SET;
        let consumer_for_log = consumer.clone();
        let out = self
            .update_stream_entry(token, group, stream_id, |stream| {
                let mut updated = false;
                let versions: &mut SchemaVersionsPB = if colocated {
                    if !stream
                        .colocated_schema_versions
                        .contains_key(&colocation_id)
                    {
                        // A 0:0 mapping is recorded too.
                        updated = true;
                    }
                    stream
                        .colocated_schema_versions
                        .entry(colocation_id)
                        .or_default()
                } else {
                    stream.schema_versions.mut_or_insert_default()
                };
                let mut current_producer = versions.current_producer_schema_version;
                let mut current_consumer = versions.current_consumer_schema_version;
                if check_min_consumer_version {
                    let min = versions
                        .old_consumer_schema_versions
                        .iter()
                        .copied()
                        .fold(current_consumer, u32::min);
                    if consumer_version < min {
                        return Err(status(
                            StatusCode::InvalidArgument,
                            format!(
                                "{SCHEMA_VERSION_TOO_OLD} for replication group {group}, consumer \
                                 table {consumer_for_log}. Min consumer schema version in map: \
                                 {min}, received consumer schema version: {consumer_version}"
                            ),
                        ));
                    }
                }
                let mut old: BTreeMap<u32, u32> = versions
                    .old_producer_schema_versions
                    .iter()
                    .copied()
                    .zip(versions.old_consumer_schema_versions.iter().copied())
                    .collect();
                if producer_version == current_producer || old.contains_key(&producer_version) {
                    // A tablet that has not done the ALTER yet reports an older target version.
                    let prev = old
                        .get(&producer_version)
                        .copied()
                        .unwrap_or(current_consumer);
                    if consumer_version < prev {
                        return Err(status(
                            StatusCode::InternalError,
                            format!(
                                "Received older consumer schema version for replication group \
                                 {group}, consumer table {consumer_for_log}"
                            ),
                        ));
                    }
                } else if producer_version > current_producer {
                    old.insert(current_producer, current_consumer);
                    current_producer = producer_version;
                    current_consumer = consumer_version;
                    updated = true;
                } else {
                    old.insert(producer_version, consumer_version);
                    updated = true;
                }
                while old.len() > max_old {
                    let (p, c) = old.pop_first().expect("not empty");
                    tracing::info!(
                        "Removing oldest schema version (producer version: {p}, consumer \
                         version: {c}) for replication group {group}, consumer table \
                         {consumer_for_log}"
                    );
                    updated = true;
                }
                let changed = updated
                    || versions.current_producer_schema_version != current_producer
                    || versions.current_consumer_schema_version != current_consumer;
                versions.current_producer_schema_version = current_producer;
                versions.current_consumer_schema_version = current_consumer;
                if updated {
                    versions.old_producer_schema_versions = old.keys().copied().collect();
                    versions.old_consumer_schema_versions = old.values().copied().collect();
                }
                Ok((changed, versions.clone()))
            })
            .await?;
        tracing::info!(
            "Updated the schema versions for table {consumer} with stream id {stream_id}, \
             colocation id {colocation_id}. Current producer schema version:{}, current consumer \
             schema version:{}, replication group:{group}",
            out.current_producer_schema_version,
            out.current_consumer_schema_version
        );
        Ok(out)
    }
}
