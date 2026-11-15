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

//! AutoFlags compatibility of inbound groups (`ValidateAutoFlagsInternal`): a group's producer
//! entry records the source config version it validated and the one it is compatible with;
//! tservers stop applying changes of an incompatible source. The check reruns when the source
//! reports a new version or this universe's config changes.

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;
use yb_proto::pb::master_replication::XClusterReportNewAutoFlagConfigVersionRequestPB;
use yb_proto::pb::master_rust_peer::{
    LocalAutoFlagsConfigVersionRequestPB, LocalAutoFlagsConfigVersionResponsePB,
    XClusterValidateAutoFlagsRequestPB, XClusterValidateAutoFlagsResponsePB,
};
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};

use crate::setup::{universe, universes};
use crate::{corrupt, status, unknown, XCluster};

impl XCluster {
    /// `ValidateAutoFlagsInternal` of one group, both rows written in one write when changed.
    async fn validate_group_auto_flags(
        &self,
        token: &LeaderToken,
        group: &str,
    ) -> Result<(), Status> {
        let (_, pb) = universe(&self.catalog.snapshot(), group).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("Missing replication group {group}"),
            )
        })?;
        let mut req = XClusterValidateAutoFlagsRequestPB::new();
        req.set_replication_group_id(group.to_string());
        req.master_addresses = pb.producer_master_addresses.clone();
        let result: XClusterValidateAutoFlagsResponsePB =
            self.host_call("XClusterValidateAutoFlags", req).await?;
        let local = result.local_version();
        self.writer
            .mutate(token, |snapshot| {
                let (entity, mut pb) = universe(snapshot, group).ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Missing replication group {group}"),
                    )
                })?;
                let config_entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut config: SysClusterConfigEntryPB = config_entity.parse().map_err(corrupt)?;
                let entry = config
                    .consumer_registry
                    .mut_or_insert_default()
                    .producer_map
                    .get_mut(group)
                    .ok_or_else(|| {
                        status(
                            StatusCode::NotFound,
                            format!("Missing producer entry for replication group {group}"),
                        )
                    })?;
                let mut config_changed = false;
                if result.has_valid() {
                    let source = result.source_version();
                    if entry.validated_auto_flags_config_version < source {
                        entry.validated_auto_flags_config_version = source;
                        config_changed = true;
                    }
                    if result.valid() && entry.compatible_auto_flag_config_version < source {
                        entry.compatible_auto_flag_config_version = source;
                        config_changed = true;
                    } else if !result.valid() && entry.compatible_auto_flag_config_version != 0 {
                        tracing::warn!(
                            "xCluster replication group {group} is not compatible with the source \
                             universe AutoFlags. Upgrade the universe to a version that is equal \
                             to or higher than the source universe"
                        );
                        entry.compatible_auto_flag_config_version = 0;
                        config_changed = true;
                    }
                }
                let mut m = Mutation::new();
                if !pb.has_validated_local_auto_flags_config_version()
                    || pb.validated_local_auto_flags_config_version() < local
                {
                    pb.set_validated_local_auto_flags_config_version(local);
                    m = m.read(&entity).upsert_message(
                        SysRowEntryType::UNIVERSE_REPLICATION,
                        group.as_bytes(),
                        &pb,
                    )?;
                }
                if config_changed {
                    config.set_version(config.version() + 1);
                    m = m.read(config_entity).upsert_message(
                        SysRowEntryType::CLUSTER_CONFIG,
                        &config_entity.key.id,
                        &config,
                    )?;
                }
                Ok((m, ()))
            })
            .await
            .map(|_| ())
    }

    /// `XClusterReportNewAutoFlagConfigVersion`: the source's tservers saw a new config version;
    /// a version not validated yet is checked.
    pub async fn report_new_auto_flag_config_version(
        &self,
        token: &LeaderToken,
        req: &XClusterReportNewAutoFlagConfigVersionRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterReportNewAutoFlagConfigVersion: {:?}", req);
        let group = req.replication_group_id();
        let snapshot = self.catalog.snapshot();
        if universe(&snapshot, group).is_none() {
            return Err(unknown(status(
                StatusCode::NotFound,
                format!("Missing replication group {group}"),
            )));
        }
        let validated = crate::setup::cluster_config(&snapshot)
            .map_err(unknown)?
            .consumer_registry
            .producer_map
            .get(group)
            .map(|e| e.validated_auto_flags_config_version)
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::NotFound,
                    format!("Missing producer entry for replication group {group}"),
                ))
            })?;
        if req.auto_flag_config_version() <= validated {
            return Ok(());
        }
        self.validate_group_auto_flags(token, group)
            .await
            .map_err(unknown)
    }

    /// `RefreshLocalAutoFlagConfig`: groups validated against an older local config are checked
    /// again.
    pub(crate) async fn revalidate_local_auto_flags(
        &self,
        token: &LeaderToken,
    ) -> Result<(), Status> {
        let groups = universes(&self.catalog.snapshot());
        if groups.is_empty() {
            return Ok(());
        }
        let local: LocalAutoFlagsConfigVersionResponsePB = self
            .host_call(
                "LocalAutoFlagsConfigVersion",
                LocalAutoFlagsConfigVersionRequestPB::new(),
            )
            .await?;
        let mut failed = false;
        for pb in groups {
            if pb.has_validated_local_auto_flags_config_version()
                && pb.validated_local_auto_flags_config_version() >= local.version()
            {
                continue;
            }
            if let Err(s) = self
                .validate_group_auto_flags(token, pb.replication_group_id())
                .await
            {
                tracing::warn!(
                    "Failed to handle local AutoFlags config change for replication group {}: {s}",
                    pb.replication_group_id()
                );
                failed = true;
            }
        }
        if failed {
            return Err(status(
                StatusCode::IllegalState,
                "Failed to handle local AutoFlags config change",
            ));
        }
        Ok(())
    }
}
