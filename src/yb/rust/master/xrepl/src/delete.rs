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

//! `DeleteCDCStream` and `CleanUpDeletedXReplStreams`: a deleted stream is marked DELETING; the
//! leader's pass then deletes its `cdc_state` rows, takes it out of the xCluster config and the
//! outbound replication groups, and deletes its row.

use std::collections::HashSet;

use master_catalog_state::{Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::{EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, SysCDCStreamEntryPB, SysXClusterConfigEntryPB,
    SysXClusterOutboundReplicationGroupEntryPB,
};
use yb_proto::pb::master_replication::{DeleteCDCStreamRequestPB, DeleteCDCStreamResponsePB};
use yb_proto::pb::master_rust_peer::{
    CdcStateDeleteStreamsRequestPB, CdcStateDeleteStreamsResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::streams::{parse_stream_id, stream_of_slot, stream_rows, StreamRow};
use crate::{error, Xrepl};

impl Xrepl {
    /// `DeleteCDCStream`: the streams are marked DELETING; [`Self::clean_up_deleted_streams`]
    /// deletes them.
    pub async fn delete_cdc_stream(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteCDCStreamRequestPB,
        resp: &mut DeleteCDCStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing DeleteCDCStream request: {:?}", req);
        if req.stream_id.is_empty() && req.cdcsdk_ysql_replication_slot_name.is_empty() {
            return Err(master_ts_manager::master_error_tagged(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                "No CDC Stream ID or YSQL Replication Slot Name given",
            ));
        }
        let rows = self.streams();
        let mut ids = Vec::new();
        for id in &req.stream_id {
            let parsed = parse_stream_id(id)?;
            match valid_for_delete(rows.iter().find(|r| r.id == parsed), req.force_delete())? {
                Some(row) => ids.push(row.id.clone()),
                None => resp.not_found_stream_ids.push(id.clone()),
            }
        }
        for slot in &req.cdcsdk_ysql_replication_slot_name {
            match valid_for_delete(stream_of_slot(&rows, slot), req.force_delete())? {
                Some(row) => ids.push(row.id.clone()),
                None => resp
                    .not_found_cdcsdk_ysql_replication_slot_names
                    .push(slot.clone()),
            }
        }
        let missing: Vec<String> = resp
            .not_found_stream_ids
            .iter()
            .map(|id| String::from_utf8_lossy(id).into_owned())
            .chain(
                resp.not_found_cdcsdk_ysql_replication_slot_names
                    .iter()
                    .cloned(),
            )
            .collect();
        if !missing.is_empty() && !req.ignore_errors() {
            return Err(master_ts_manager::master_error_tagged(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                &format!(
                    "Did not find all requested CDC streams. Missing streams: [{}]. Request: {:?}",
                    missing.join(","),
                    req
                ),
            ));
        }
        self.drop_streams(token, deadline, &ids, StreamState::DELETING)
            .await
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        tracing::info!("Successfully deleted CDC streams {:?}", ids);
        Ok(())
    }

    /// `DropXReplStreams`: the streams not yet being deleted are marked `state` (DELETING or
    /// DELETING_METADATA) in one write.
    pub async fn drop_streams(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        ids: &[String],
        state: StreamState,
    ) -> Result<(), Status> {
        if ids.is_empty() {
            return Ok(());
        }
        let mut ids = ids.to_vec();
        ids.sort();
        ids.dedup();
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let mut mutation = Mutation::new();
                    for id in &ids {
                        let Some(entity) = snapshot.get(SysRowEntryType::CDC_STREAM, id.as_bytes())
                        else {
                            continue;
                        };
                        let mut pb: SysCDCStreamEntryPB = entity.parse().map_err(corrupt)?;
                        let row = StreamRow {
                            id: id.clone(),
                            pb: pb.clone(),
                        };
                        if row.is_deleting() {
                            tracing::warn!(
                                "Stream {id} is already in {:?} state. Cannot change it's state \
                                 to {state:?}",
                                pb.state()
                            );
                            continue;
                        }
                        pb.state = Some(EnumOrUnknown::new(state));
                        mutation = mutation.read(entity).upsert_message(
                            SysRowEntryType::CDC_STREAM,
                            id.as_bytes(),
                            &pb,
                        )?;
                    }
                    Ok((mutation, ()))
                },
            )
            .await
            .map(|_| ())
    }

    /// `CleanUpDeletedXReplStreams`: the `cdc_state` rows of DELETING streams are deleted, then,
    /// in one write, the streams leave the xCluster config's paused streams and the outbound
    /// replication groups and their rows are deleted.
    pub async fn clean_up_deleted_streams(&self, token: &LeaderToken) -> Result<(), Status> {
        let deleting: Vec<StreamRow> = self
            .streams()
            .into_iter()
            .filter(|r| r.pb.state() == StreamState::DELETING)
            .collect();
        if deleting.is_empty() {
            return Ok(());
        }
        let mut req = CdcStateDeleteStreamsRequestPB::new();
        req.stream_ids = deleting.iter().map(|r| r.id.clone().into_bytes()).collect();
        let resp: CdcStateDeleteStreamsResponsePB =
            self.host_call("CdcStateDeleteStreams", req).await?;
        tracing::info!(
            "Deleted {} cdc_state rows of XRepl streams {:?}",
            resp.deleted(),
            deleting.iter().map(|r| &r.id).collect::<Vec<_>>()
        );
        let ids: HashSet<String> = deleting.iter().map(|r| r.id.clone()).collect();
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let mut mutation = Mutation::new();
                let rows: Vec<StreamRow> = stream_rows(snapshot)
                    .into_iter()
                    .filter(|r| ids.contains(&r.id) && r.pb.state() == StreamState::DELETING)
                    .collect();
                // xCluster streams (no namespace) leave the paused streams and the outbound groups
                // (`XClusterManager::RemoveStreamsFromSysCatalog`).
                let xcluster: Vec<&StreamRow> = rows
                    .iter()
                    .filter(|r| r.pb.namespace_id().is_empty())
                    .collect();
                if !xcluster.is_empty() {
                    if let Some(entity) = snapshot.get(SysRowEntryType::XCLUSTER_CONFIG, b"") {
                        let mut config: SysXClusterConfigEntryPB =
                            entity.parse().map_err(corrupt)?;
                        let paused = &mut config
                            .xcluster_producer_registry
                            .mut_or_insert_default()
                            .paused_producer_stream_ids;
                        for row in &xcluster {
                            paused.remove(&row.id);
                        }
                        config.set_version(config.version() + 1);
                        mutation = mutation.read(entity).upsert_message(
                            SysRowEntryType::XCLUSTER_CONFIG,
                            b"",
                            &config,
                        )?;
                    }
                    for group in snapshot.iter(SysRowEntryType::XCLUSTER_OUTBOUND_REPLICATION_GROUP)
                    {
                        let mut pb: SysXClusterOutboundReplicationGroupEntryPB =
                            group.parse().map_err(corrupt)?;
                        if remove_streams_from_group(&mut pb, &xcluster) {
                            mutation = mutation.read(group).upsert_message(
                                SysRowEntryType::XCLUSTER_OUTBOUND_REPLICATION_GROUP,
                                &group.key.id,
                                &pb,
                            )?;
                        }
                    }
                }
                for row in &rows {
                    let entity = snapshot
                        .get(SysRowEntryType::CDC_STREAM, row.id.as_bytes())
                        .expect("listed");
                    mutation = mutation
                        .read(entity)
                        .delete(SysRowEntryType::CDC_STREAM, row.id.as_bytes());
                }
                Ok((mutation, ()))
            })
            .await?;
        tracing::info!("Successfully deleted XRepl streams: {:?}", ids);
        Ok(())
    }

    /// One of the host's C++ steps, off the async threads.
    pub(crate) async fn host_call<Req: Message, Resp: protobuf::MessageFull>(
        &self,
        method: &'static str,
        req: Req,
    ) -> Result<Resp, Status> {
        let bytes = req
            .write_to_bytes()
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
        let host = self.host.clone();
        let out = tokio::task::spawn_blocking(move || host.call(method, &bytes))
            .await
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))??;
        let resp = Resp::parse_from_bytes(&out).map_err(corrupt)?;
        if let Some(e) = crate::host_error(&resp) {
            return Err(Status::new(
                StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                e.status.message().to_string(),
            ));
        }
        Ok(resp)
    }
}

/// `GetStreamIfValidForDelete`: None for a missing stream or one already being deleted; an
/// ACTIVE xCluster stream in WAL record format needs `force_delete`.
fn valid_for_delete(
    row: Option<&StreamRow>,
    force_delete: bool,
) -> Result<Option<&StreamRow>, MasterErrorPB> {
    let Some(row) = row.filter(|r| !r.is_deleting()) else {
        return Ok(None);
    };
    if !force_delete && row.pb.state() == StreamState::ACTIVE {
        if let Some(format) = row.pb.options.iter().find(|o| o.key() == "record_format") {
            if format.value() == b"WAL" {
                return Err(master_ts_manager::master_error_tagged(
                    Code::INVALID_REQUEST,
                    StatusCode::NotSupported,
                    "Cannot delete an xCluster Stream in replication. Use 'force_delete' to \
                     override",
                ));
            }
        }
    }
    Ok(Some(row))
}

/// `XClusterOutboundReplicationGroup::RemoveStreams`: a table whose stream goes leaves the group,
/// unless its WAL anchor stream stays; a WAL anchor stream that goes leaves the table. Whether the
/// group changed.
fn remove_streams_from_group(
    pb: &mut SysXClusterOutboundReplicationGroupEntryPB,
    streams: &[&StreamRow],
) -> bool {
    use yb_proto::pb::catalog_entity_info::sys_xcluster_outbound_replication_group_entry_pb::State;
    if pb.state() == State::DELETED {
        return false;
    }
    let going: HashSet<&str> = streams.iter().map(|s| s.id.as_str()).collect();
    let mut changed = false;
    for stream in streams {
        for table_id in &stream.pb.table_id {
            for ns_info in pb.namespace_infos.values_mut() {
                let Some(table_info) = ns_info.table_infos.get_mut(table_id) else {
                    continue;
                };
                let has_anchor = !table_info.wal_anchor_stream_id().is_empty();
                if table_info.stream_id() == stream.id.as_bytes() {
                    if has_anchor
                        && !going.contains(
                            String::from_utf8_lossy(table_info.wal_anchor_stream_id()).as_ref(),
                        )
                    {
                        table_info.clear_stream_id();
                        table_info.clear_is_checkpointing();
                    } else {
                        ns_info.table_infos.remove(table_id);
                    }
                    changed = true;
                    break;
                }
                if has_anchor && table_info.wal_anchor_stream_id() == stream.id.as_bytes() {
                    table_info.clear_wal_anchor_stream_id();
                    if table_info.stream_id().is_empty() {
                        ns_info.table_infos.remove(table_id);
                    }
                    changed = true;
                    break;
                }
            }
        }
    }
    changed
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}
