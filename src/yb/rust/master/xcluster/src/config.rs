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

//! The xCluster config (`XClusterConfig`): each namespace's role and the paused streams, which
//! tservers get by heartbeat when their copy is older.

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_ts_manager::{ContributorContext, HeartbeatContributor};
use protobuf::EnumOrUnknown;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{SysXClusterConfigEntryPB, XClusterSafeTimePB};
use yb_proto::pb::common_types::xcluster_namespace_info_pb::XClusterRole;
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_replication::PauseResumeXClusterProducerStreamsRequestPB;
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};

use crate::{corrupt, status, unknown, XCluster};

fn config(snapshot: &CatalogSnapshot) -> Result<SysXClusterConfigEntryPB, Status> {
    snapshot
        .get(SysRowEntryType::XCLUSTER_CONFIG, b"")
        .ok_or_else(|| {
            status(
                StatusCode::IllegalState,
                "XCluster config is not initialized",
            )
        })?
        .parse()
        .map_err(corrupt)
}

impl XCluster {
    /// Applies `f` to the config and writes it with its version bumped
    /// (`BumpVersionUpsertAndCommit`).
    async fn update_config(
        &self,
        token: &LeaderToken,
        f: impl Fn(&mut SysXClusterConfigEntryPB) -> Result<(), Status>,
    ) -> Result<(), Status> {
        self.writer
            .mutate(token, |snapshot| {
                let entity = snapshot
                    .get(SysRowEntryType::XCLUSTER_CONFIG, b"")
                    .ok_or_else(|| {
                        status(
                            StatusCode::IllegalState,
                            "XCluster config is not initialized",
                        )
                    })?;
                let mut pb: SysXClusterConfigEntryPB = entity.parse().map_err(corrupt)?;
                f(&mut pb)?;
                pb.set_version(pb.version() + 1);
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::XCLUSTER_CONFIG,
                        b"",
                        &pb,
                    )?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// `SetXClusterRole`.
    pub(crate) async fn set_role(
        &self,
        token: &LeaderToken,
        ns: &str,
        role: XClusterRole,
    ) -> Result<(), Status> {
        self.update_config(token, |pb| {
            pb.xcluster_info_per_namespace
                .entry(ns.to_string())
                .or_default()
                .role = Some(EnumOrUnknown::new(role));
            Ok(())
        })
        .await
    }

    /// `PauseResumeXClusterProducerStreams`: the given streams (all with none given) are paused
    /// or resumed.
    pub async fn pause_resume_producer_streams(
        &self,
        token: &LeaderToken,
        req: &PauseResumeXClusterProducerStreamsRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("PauseResumeXClusterProducerStreams: {:?}", req);
        if !req.has_is_paused() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "is_paused must be set in the request",
            )));
        }
        let all: Vec<String> = self.xrepl.streams().into_iter().map(|r| r.id).collect();
        let ids = if req.stream_ids.is_empty() {
            tracing::info!(
                "{} replication for all XCluster streams.",
                if req.is_paused() {
                    "Pausing"
                } else {
                    "Resuming"
                }
            );
            all
        } else {
            let mut ids = Vec::new();
            for id in &req.stream_ids {
                let parsed = master_xrepl::parse_stream_id(id.as_bytes())?;
                if !all.contains(&parsed) {
                    return Err(unknown(status(
                        StatusCode::NotFound,
                        format!("XCluster Stream: {id} does not exists"),
                    )));
                }
                ids.push(parsed);
            }
            ids
        };
        let pause = req.is_paused();
        self.update_config(token, |pb| {
            let paused = &mut pb
                .xcluster_producer_registry
                .mut_or_insert_default()
                .paused_producer_stream_ids;
            for id in &ids {
                if pause {
                    paused.insert(id.clone(), true);
                } else {
                    paused.remove(id);
                }
            }
            Ok(())
        })
        .await
        .map_err(unknown)
    }
}

impl HeartbeatContributor for XCluster {
    /// `XClusterConfig::FillHeartbeatResponse`.
    fn contribute(
        &self,
        req: &TSHeartbeatRequestPB,
        ctx: &ContributorContext<'_>,
        resp: &mut TSHeartbeatResponsePB,
    ) -> Result<(), Status> {
        self.store_replication_status(&req.xcluster_consumer_replication_status);
        let pb = config(ctx.snapshot)?;
        if req.has_xcluster_config_version() && req.xcluster_config_version() < pb.version() {
            resp.set_xcluster_config_version(pb.version());
            resp.xcluster_producer_registry = pb.xcluster_producer_registry.clone();
        }
        if !(self.options.skip_fields_moved_to_guarded_info)() {
            let info = resp
                .DEPRECATED_xcluster_heartbeat_info
                .mut_or_insert_default();
            for (ns, i) in &pb.xcluster_info_per_namespace {
                info.xcluster_info_per_namespace
                    .insert(ns.clone(), i.clone());
            }
        }
        fill_safe_times(ctx.snapshot, resp)?;
        Ok(())
    }
}

/// `XClusterTargetManager::FillHeartbeatResponse`: the safe time of each target namespace.
fn fill_safe_times(
    snapshot: &CatalogSnapshot,
    resp: &mut TSHeartbeatResponsePB,
) -> Result<(), Status> {
    let Some(entity) = snapshot.get(SysRowEntryType::XCLUSTER_SAFE_TIME, b"") else {
        return Ok(());
    };
    let pb: XClusterSafeTimePB = entity.parse().map_err(corrupt)?;
    if !pb.safe_time_map.is_empty() {
        resp.xcluster_namespace_to_safe_time = pb.safe_time_map;
    }
    Ok(())
}
