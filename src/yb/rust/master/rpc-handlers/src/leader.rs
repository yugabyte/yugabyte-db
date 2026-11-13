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

//! The core RPCs a layer-2 Rust leader serves, from the catalog snapshot, the tserver registry and
//! the observed locations. Responses mirror the C++ handlers field for field; see
//! docs/catalog-state.md for what a layer-2 leader leaves to later layers.

use std::collections::HashSet;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};

use master_catalog_entity::Entity;
use master_catalog_state::is_inactive_state;
use master_catalog_state::{Catalog, CatalogSnapshot, CatalogWriter, Mutation};
use master_leadership::{LeaderToken, Leadership};
use master_table_lifecycle::TableLifecycle;
use master_ts_manager::placement::{
    blacklist_addresses, cloud_info_matches_placement, is_blacklisted, placement_id,
    validate_placement,
};
use master_ts_manager::{consensus_role, FollowerStreamState, HeartbeatContributor, TsManager};
use protobuf::{Enum, EnumOrUnknown};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB, SysXClusterConfigEntryPB,
};
use yb_proto::pb::common_net::CloudInfoPB;
use yb_proto::pb::consensus_metadata::{ConsensusStatePB, PeerMemberType};
use yb_proto::pb::master_client::{
    get_tablet_locations_response_pb, tablet_locations_pb, GetTableLocationsRequestPB,
    GetTableLocationsResponsePB, GetTabletLocationsRequestPB, GetTabletLocationsResponsePB,
    GetTransactionStatusTabletsResponsePB, TSInfoPB, TabletLocationsPB,
};
use yb_proto::pb::master_cluster::{
    list_live_tablet_servers_response_pb, list_tablet_servers_response_pb,
    ChangeMasterClusterConfigRequestPB, ChangeMasterClusterConfigResponsePB,
    GetMasterClusterConfigResponsePB, GetMasterXClusterConfigResponsePB,
    ListLiveTabletServersResponsePB, ListTabletServersRequestPB, ListTabletServersResponsePB,
    RemoveTabletServerRequestPB, RemoveTabletServerResponsePB, SetPreferredZonesRequestPB,
    SetPreferredZonesResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::wire_protocol::AppStatusPB;

use crate::{master_error, master_error_from};

/// The leader's components, shared by every master service handler.
pub struct LeaderServices {
    /// The catalog.
    pub catalog: Arc<Catalog>,
    /// Its writer.
    pub writer: Arc<CatalogWriter>,
    /// Leadership.
    pub leadership: Arc<Leadership>,
    /// Namespace and table DDL (W7).
    pub tables: Arc<TableLifecycle>,
    /// The tserver registry and locations.
    pub ts_manager: Arc<TsManager>,
    /// Heartbeat parts owned elsewhere (L7).
    pub contributors: Vec<Arc<dyn HeartbeatContributor>>,
    /// What this master applied of the location stream, as a follower.
    pub follower_stream: Mutex<FollowerStreamState>,
    /// `ChangeLoadBalancerState`, held in memory on every master as C++ does.
    pub load_balancer_enabled: AtomicBool,
    /// `replication_factor`, for tables and clusters with no replication info.
    pub replication_factor: fn() -> i32,
    /// The cluster balancer's recent runs, for its RPCs.
    pub balancer: Arc<master_cluster_balancer::controller::BalancerStatus>,
    /// `enable_load_balancing`.
    pub load_balancing_flag: fn() -> bool,
    /// `transaction_tables_use_preferred_zones`.
    pub transaction_tables_use_preferred_zones: fn() -> bool,
    /// Tablet splits.
    pub splits: Arc<master_tablet_split_manager::TabletSplitManager>,
    /// YSQL operation leases.
    pub leases: Arc<master_ysql_manager::lease::YsqlLeases>,
    /// Global DDL object locks.
    pub locks: Arc<master_ysql_manager::locks::ObjectLocks>,
    /// `WaitForYsqlBackendsCatalogVersion`.
    pub backends: Arc<master_ysql_manager::backends::YsqlBackends>,
    /// `TruncateTable`.
    pub truncates: Arc<master_table_lifecycle::Truncates>,
    /// `FlushTables`.
    pub flushes: Arc<master_table_lifecycle::Flushes>,
    /// `blacklist_progress_initial_delay_secs`.
    pub blacklist_progress_initial_delay: fn() -> std::time::Duration,
    /// `delay_leader_blacklist_completion_percent_until_tservers_heartbeat`.
    pub delay_leader_blacklist_completion: fn() -> bool,
    /// `enable_tablespace_based_transaction_placement`.
    pub tablespace_based_transaction_placement: fn() -> bool,
    /// This build's YSQL major version; None with YSQL off.
    pub ysql_major_version: Option<u32>,
    /// The YSQL major catalog upgrade.
    pub major_upgrade: Arc<master_ysql_manager::major_upgrade::MajorUpgrade>,
}

/// A placement's transaction status tables, each with whether it stays in one region.
type LocalStatusTables = (
    yb_proto::pb::common_net::PlacementInfoPB,
    Vec<(Vec<u8>, bool)>,
);

/// `PlacementInfoSpansMultipleRegions`.
fn spans_regions(placement: &yb_proto::pb::common_net::PlacementInfoPB) -> bool {
    let mut blocks = placement
        .placement_blocks
        .iter()
        .map(|b| b.cloud_info.get_or_default());
    let Some(first) = blocks.next() else {
        return false;
    };
    blocks.any(|ci| {
        ci.placement_cloud() != first.placement_cloud()
            || ci.placement_region() != first.placement_region()
    })
}

/// Who serves a leader method a layer-2 Rust leader does not.
pub fn owner(method: &str) -> &'static str {
    match method {
        "RedisConfigSet" | "RedisConfigGet" => "nobody: YEDIS is not supported (D16)",
        _ => "a later layer",
    }
}

impl LeaderServices {
    /// A token if this master is a ready leader with a loaded catalog, else the error
    /// `ScopedLeaderSharedLock` reports, as `NOT_THE_LEADER`.
    pub fn ready(&self) -> Result<(LeaderToken, Arc<CatalogSnapshot>), MasterErrorPB> {
        // The token first: a snapshot taken after it has every row written before Ready, such as
        // a new universe's default rows.
        let token = self
            .leadership
            .token()
            .map_err(|status| master_error_from(master_error_pb::Code::NOT_THE_LEADER, &status))?;
        let snapshot = self.catalog.snapshot();
        if !snapshot.loaded {
            return Err(master_error_from(
                master_error_pb::Code::NOT_THE_LEADER,
                &master_catalog_state::not_loaded_status(),
            ));
        }
        Ok((token, snapshot))
    }

    /// `IsNamespacePartOfCDCSDK`: a CDC stream on the namespace that is not being deleted.
    pub(crate) fn namespace_has_cdc_stream(
        snapshot: &CatalogSnapshot,
        namespace_id: &[u8],
    ) -> bool {
        snapshot.iter(SysRowEntryType::CDC_STREAM).any(|row| {
            row.parse::<yb_proto::pb::catalog_entity_info::SysCDCStreamEntryPB>()
                .is_ok_and(|pb| {
                    pb.state()
                        != yb_proto::pb::catalog_entity_info::sys_cdcstream_entry_pb::State::DELETING
                        && !pb.namespace_id().is_empty()
                        && pb.namespace_id().as_bytes() == namespace_id
                })
        })
    }

    pub(crate) fn cluster_config(
        snapshot: &CatalogSnapshot,
    ) -> Result<SysClusterConfigEntryPB, Status> {
        snapshot
            .cluster_config()
            .ok_or_else(|| Status::new(StatusCode::IllegalState, "No cluster config"))?
            .parse()
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))
    }

    /// `ListTabletServers`.
    pub fn list_tablet_servers(
        &self,
        snapshot: &CatalogSnapshot,
        req: &ListTabletServersRequestPB,
        resp: &mut ListTabletServersResponsePB,
    ) -> Result<(), Status> {
        let descriptors = if req.primary_only() {
            let placement = primary_placement(snapshot)?;
            self.ts_manager
                .live()
                .into_iter()
                .filter(|d| d.placement_uuid().is_empty() || d.placement_uuid() == placement)
                .collect()
        } else {
            self.ts_manager.all()
        };
        for descriptor in descriptors {
            let mut entry = list_tablet_servers_response_pb::Entry::new();
            entry.instance_id = Some(descriptor.instance()).into();
            entry.registration = Some(descriptor.registration()).into();
            if let Some(last) = descriptor.last_heartbeat {
                entry.set_millis_since_heartbeat(
                    last.elapsed().as_millis().min(i32::MAX as u128) as i32
                );
            }
            entry.set_alive(descriptor.is_live());
            if let Some(metrics) = descriptor.metrics.clone() {
                entry.metrics = Some(metrics).into();
            }
            resp.servers.push(entry);
        }
        Ok(())
    }

    /// `ListLiveTabletServers`.
    pub fn list_live_tablet_servers(
        &self,
        snapshot: &CatalogSnapshot,
        resp: &mut ListLiveTabletServersResponsePB,
    ) -> Result<(), Status> {
        let placement = primary_placement(snapshot)?;
        let blacklist = Self::cluster_config(snapshot)
            .map(|c| blacklist_addresses(&c))
            .unwrap_or_default();
        for descriptor in self.ts_manager.live() {
            if is_blacklisted(&descriptor, &blacklist) && descriptor.num_live_replicas == 0 {
                continue;
            }
            let mut entry = list_live_tablet_servers_response_pb::Entry::new();
            entry.instance_id = Some(descriptor.instance()).into();
            entry.registration = Some(descriptor.registration()).into();
            entry.set_isFromReadReplica(descriptor.placement_uuid() != placement);
            resp.servers.push(entry);
        }
        Ok(())
    }

    /// `RemoveTabletServer`.
    pub async fn remove_tablet_server(
        &self,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        req: &RemoveTabletServerRequestPB,
        resp: &mut RemoveTabletServerResponsePB,
    ) -> Result<(), Status> {
        let blacklist = blacklist_addresses(&Self::cluster_config(snapshot)?);
        let blacklisted = self
            .ts_manager
            .get(req.permanent_uuid())
            .is_some_and(|d| is_blacklisted(&d, &blacklist));
        match self
            .ts_manager
            .remove(snapshot, req.permanent_uuid(), blacklisted)
        {
            Ok(()) => {
                // Deletes the registration row when the registry is persisted.
                if let Err(status) = self
                    .ts_manager
                    .persist(&self.writer, token, &[req.permanent_uuid().into()])
                    .await
                {
                    tracing::warn!(
                        "Failed to remove tablet server {} from sys catalog: {status}",
                        String::from_utf8_lossy(req.permanent_uuid())
                    );
                }
                Ok(())
            }
            Err((status, true)) => {
                resp.error = Some(master_error_from(
                    master_error_pb::Code::TABLET_SERVER_NOT_FOUND,
                    &status,
                ))
                .into();
                Err(status)
            }
            Err((status, false)) => Err(status),
        }
    }

    /// `GetMasterClusterConfig`.
    pub fn get_cluster_config(
        &self,
        snapshot: &CatalogSnapshot,
        resp: &mut GetMasterClusterConfigResponsePB,
    ) -> Result<(), Status> {
        resp.cluster_config = Some(Self::cluster_config(snapshot)?).into();
        Ok(())
    }

    /// `GetMasterXClusterConfig`.
    pub fn get_xcluster_config(
        &self,
        snapshot: &CatalogSnapshot,
        resp: &mut GetMasterXClusterConfigResponsePB,
    ) -> Result<(), Status> {
        let config = snapshot
            .iter(SysRowEntryType::XCLUSTER_CONFIG)
            .next()
            .ok_or_else(|| Status::new(StatusCode::IllegalState, "XCluster config not found"))?
            .parse::<SysXClusterConfigEntryPB>()
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        resp.xcluster_config = Some(config).into();
        Ok(())
    }

    /// `ChangeMasterClusterConfig` (`MasterClusterHandler::SetClusterConfig`).
    pub async fn change_cluster_config(
        &self,
        token: &LeaderToken,
        req: &ChangeMasterClusterConfigRequestPB,
        resp: &mut ChangeMasterClusterConfigResponsePB,
    ) -> Result<(), Status> {
        let mut config = req.cluster_config.clone().unwrap_or_default();
        let snapshot = self.catalog.snapshot();
        let load = |blacklist: &yb_proto::pb::catalog_entity_info::BlacklistPB, leaders_only| {
            i32::try_from(self.relevant_replicas(&snapshot, blacklist, leaders_only))
                .unwrap_or(i32::MAX)
        };
        if let Some(blacklist) = config.server_blacklist.as_mut() {
            blacklist.set_initial_replica_load(load(blacklist, false));
        }
        if let Some(blacklist) = config.leader_blacklist.as_mut() {
            blacklist.set_initial_leader_load(load(blacklist, true));
        }
        let error_code = Mutex::new(None);
        let result = self
            .writer
            .mutate(token, |snapshot| {
                *error_code.lock().unwrap() = None;
                let entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| Status::new(StatusCode::IllegalState, "No cluster config"))?;
                let current: SysClusterConfigEntryPB = entity
                    .parse()
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                let fail = |code, status: Status| {
                    *error_code.lock().unwrap() = Some(code);
                    Err(status)
                };
                if config.version() != current.version() {
                    return fail(
                        master_error_pb::Code::CONFIG_VERSION_MISMATCH,
                        Status::new(
                            StatusCode::IllegalState,
                            format!(
                                "Config version does not match, got {}, but most recent one is \
                                 {}. Should call Get again",
                                config.version(),
                                current.version()
                            ),
                        ),
                    );
                }
                if config.cluster_uuid() != current.cluster_uuid() {
                    return fail(
                        master_error_pb::Code::INVALID_CLUSTER_CONFIG,
                        Status::new(
                            StatusCode::InvalidArgument,
                            "Config cluster UUID cannot be updated",
                        ),
                    );
                }
                if config.universe_uuid() != current.universe_uuid() {
                    return fail(
                        master_error_pb::Code::INVALID_CLUSTER_CONFIG,
                        Status::new(
                            StatusCode::InvalidArgument,
                            "Config Universe UUID cannot be updated",
                        ),
                    );
                }
                for read_replica in &config.replication_info.read_replicas {
                    if let Err(status) = validate_placement(read_replica) {
                        return fail(master_error_pb::Code::INVALID_CLUSTER_CONFIG, status);
                    }
                    if !read_replica.has_placement_uuid() {
                        return fail(
                            master_error_pb::Code::INVALID_CLUSTER_CONFIG,
                            Status::new(
                                StatusCode::IllegalState,
                                "All read-only clusters must have a placement uuid specified",
                            ),
                        );
                    }
                }
                if let Some(live) = config.replication_info.live_replicas.as_ref() {
                    if let Err(status) = validate_placement(live) {
                        return fail(master_error_pb::Code::INVALID_CLUSTER_CONFIG, status);
                    }
                }
                let mut next = config.clone();
                next.set_version(config.version() + 1);
                // `ToBlacklistSet` sizes: a host listed twice counts once.
                let hosts = |c: &SysClusterConfigEntryPB| {
                    c.leader_blacklist
                        .hosts
                        .iter()
                        .map(|h| (h.host().to_string(), h.port()))
                        .collect::<std::collections::HashSet<_>>()
                        .len()
                };
                let grew = hosts(&config) > hosts(&current);
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::CLUSTER_CONFIG,
                        b"",
                        &next,
                    )?,
                    grew,
                ))
            })
            .await;
        if let (Err(status), Some(code)) = (&result, *error_code.lock().unwrap()) {
            resp.error = Some(master_error_from(code, status)).into();
        }
        if let Ok((true, _)) = &result {
            self.ts_manager.mark_leader_blacklist_notification();
        }
        result.map(|_| ())
    }

    /// `SetPreferredZones`.
    pub async fn set_preferred_zones(
        &self,
        token: &LeaderToken,
        req: &SetPreferredZonesRequestPB,
        resp: &mut SetPreferredZonesResponsePB,
    ) -> Result<(), Status> {
        let invalid = Mutex::new(false);
        let result = self
            .writer
            .mutate(token, |snapshot| {
                *invalid.lock().unwrap() = false;
                let entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| Status::new(StatusCode::IllegalState, "No cluster config"))?;
                let mut config: SysClusterConfigEntryPB = entity
                    .parse()
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                let info = config.replication_info.mut_or_insert_default();
                info.affinitized_leaders.clear();
                info.multi_affinitized_leaders.clear();
                let sets: Vec<Vec<CloudInfoPB>> = if !req.multi_preferred_zones.is_empty() {
                    req.multi_preferred_zones
                        .iter()
                        .map(|l| l.zones.clone())
                        .collect()
                } else if !req.preferred_zones.is_empty() {
                    vec![req.preferred_zones.clone()]
                } else {
                    vec![]
                };
                let placement = info.live_replicas.clone().unwrap_or_default();
                let mut seen = HashSet::new();
                for set in sets {
                    if set.is_empty() {
                        return Err(Status::new(
                            StatusCode::InvalidArgument,
                            "Preferred zones list cannot be empty",
                        ));
                    }
                    let mut list = yb_proto::pb::common_net::CloudInfoListPB::new();
                    for zone in set {
                        // ValidateAndAddPreferredZone.
                        let id = placement_id(&zone);
                        if seen.contains(&id) {
                            return Err(Status::new(
                                StatusCode::InvalidArgument,
                                format!(
                                    "Invalid argument for preferred zone {id}, values should not \
                                     repeat"
                                ),
                            ));
                        }
                        if !cloud_info_matches_placement(&zone, &placement) {
                            return Err(Status::new(
                                StatusCode::InvalidArgument,
                                format!(
                                    "Preferred zone '{id}' not found in Placement info '{}'",
                                    protobuf::text_format::print_to_string(&placement)
                                ),
                            ));
                        }
                        seen.insert(id);
                        list.zones.push(zone);
                    }
                    info.multi_affinitized_leaders.push(list);
                }
                config.set_version(config.version() + 1);
                *invalid.lock().unwrap() = true;
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::CLUSTER_CONFIG,
                        b"",
                        &config,
                    )?,
                    (),
                ))
            })
            .await;
        if let Err(status) = &result {
            if *invalid.lock().unwrap() {
                // The upsert failed: C++ reports it as an invalid cluster config.
                resp.error = Some(master_error_from(
                    master_error_pb::Code::INVALID_CLUSTER_CONFIG,
                    status,
                ))
                .into();
            }
        }
        result.map(|_| ())
    }

    /// `GetTabletLocations`.
    pub fn get_tablet_locations(
        &self,
        snapshot: &CatalogSnapshot,
        masters: Option<&ConsensusStatePB>,
        req: &GetTabletLocationsRequestPB,
        resp: &mut GetTabletLocationsResponsePB,
    ) {
        if req.has_table_id() {
            if let Some(table) = snapshot
                .get(SysRowEntryType::TABLE, req.table_id())
                .and_then(|t| t.table())
            {
                resp.set_partition_list_version(table.partition_list_version);
            }
        }
        for tablet_id in &req.tablet_ids {
            let result = self.tablet_entity(snapshot, tablet_id).and_then(|tablet| {
                let table = snapshot.get(
                    SysRowEntryType::TABLE,
                    &tablet.tablet().expect("a tablet").table_id,
                );
                let (live, read) = table.map_or((0, 0), |t| self.expected_replicas(snapshot, t));
                let mut locations =
                    self.tablet_locations(snapshot, masters, tablet, req.include_hidden(), false)?;
                locations.set_expected_live_replicas(live);
                locations.set_expected_read_replicas(read);
                Ok(locations)
            });
            match result {
                Ok(locations) => resp.tablet_locations.push(locations),
                Err(e) if req.include_deleted() && e.status.code == StatusCode::Deleted => {
                    let mut locations = TabletLocationsPB::new();
                    locations.set_tablet_id(tablet_id.clone());
                    locations.set_stale(false);
                    locations.set_is_deleted(true);
                    resp.tablet_locations.push(locations);
                }
                Err(e) => {
                    let mut error = get_tablet_locations_response_pb::Error::new();
                    error.set_tablet_id(tablet_id.clone());
                    error.status = Some(e.app_status()).into();
                    resp.errors.push(error);
                }
            }
        }
    }

    /// `GetTabletInfo`.
    fn tablet_entity<'a>(
        &self,
        snapshot: &'a CatalogSnapshot,
        tablet_id: &[u8],
    ) -> Result<&'a Arc<Entity>, LocationError> {
        snapshot
            .get(SysRowEntryType::TABLET, tablet_id)
            .filter(|t| t.tablet().is_some())
            .ok_or_else(|| {
                Status::new(
                    StatusCode::NotFound,
                    format!("Tablet {} not found", String::from_utf8_lossy(tablet_id)),
                )
                .into()
            })
    }

    /// `GetTableLocations`.
    pub fn get_table_locations(
        &self,
        snapshot: &CatalogSnapshot,
        masters: Option<&ConsensusStatePB>,
        req: &GetTableLocationsRequestPB,
        resp: &mut GetTableLocationsResponsePB,
    ) -> Result<(), Status> {
        if req.has_partition_key_start()
            && req.has_partition_key_end()
            && req.partition_key_start() > req.partition_key_end()
        {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                "start partition key is greater than the end partition key",
            ));
        }
        if req.max_returned_locations() == 0 {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                "max_returned_locations must be greater than 0",
            ));
        }
        // `FindTable`'s error, with its code, as the C++ handler returns it.
        let table = master_table_lifecycle::lookup::find_table(snapshot, &req.table, false)
            .map_err(|error| {
                let status = crate::status_of(&error);
                resp.error = Some(error).into();
                status
            })?;
        let hot = table.table().expect("a table");
        if hot.state == sys_tables_entry_pb::State::PREPARING.value() {
            resp.set_creating(true);
        }
        if hot.state == sys_tables_entry_pb::State::DELETING.value()
            || hot.state == sys_tables_entry_pb::State::DELETED.value()
        {
            let status = Status::new(
                StatusCode::NotFound,
                format!("The object '{}' does not exist", hot.name),
            );
            resp.error = Some(master_error_from(
                master_error_pb::Code::OBJECT_NOT_FOUND,
                &status,
            ))
            .into();
            return Err(status);
        }
        resp.table_type = Some(EnumOrUnknown::from_i32(hot.table_type));
        resp.set_partition_list_version(hot.partition_list_version);

        let max = req.max_returned_locations() as usize;
        let start = req.partition_key_start();
        let end = req.partition_key_end();
        let table_id =
            if snapshot.tablets_of(&table.key.id).is_empty() && !hot.parent_table_id.is_empty() {
                hot.parent_table_id.clone()
            } else {
                table.key.id.to_vec()
            };
        let tablets: Vec<&Arc<Entity>> = if req.include_inactive() {
            let mut all = snapshot.tablets_of(&table_id);
            all.sort_by(|a, b| a.key.id.cmp(&b.key.id));
            all.into_iter()
                .filter(|t| {
                    let s = &t.tablet().expect("a tablet").partition_start;
                    (start.is_empty() || s.as_slice() >= start)
                        && (end.is_empty() || s.as_slice() <= end)
                })
                .take(max)
                .collect()
        } else {
            let active = snapshot.active_tablets_of(&table_id);
            // upper_bound(start) - 1: the tablet whose partition contains start.
            let first = active
                .iter()
                .rposition(|t| t.tablet().expect("a tablet").partition_start.as_slice() <= start)
                .unwrap_or(0);
            active[first..]
                .iter()
                .take_while(|t| {
                    end.is_empty()
                        || t.tablet().expect("a tablet").partition_start.as_slice() <= end
                })
                .take(max)
                .copied()
                .collect()
        };
        let (live, read) = self.expected_replicas(snapshot, &table);
        for tablet in tablets {
            match self.tablet_locations(snapshot, masters, tablet, true, req.partitions_only()) {
                Ok(mut locations) => {
                    locations.set_expected_live_replicas(live);
                    locations.set_expected_read_replicas(read);
                    resp.tablet_locations.push(locations);
                }
                Err(e) if req.require_tablets_running() => {
                    resp.tablet_locations.clear();
                    resp.error = Some(master_error_from(
                        master_error_pb::Code::OBJECT_NOT_FOUND,
                        &e.status,
                    ))
                    .into();
                    return Err(e.status);
                }
                Err(_) => {}
            }
        }
        Ok(())
    }

    /// `GetStatefulServiceLocation`: the tserver leading the one tablet of the service's table.
    pub fn get_stateful_service_location(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_client::GetStatefulServiceLocationRequestPB,
        resp: &mut yb_proto::pb::master_client::GetStatefulServiceLocationResponsePB,
    ) -> Result<(), Status> {
        if req.service_kind.is_none() {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                "Service kind is not specified",
            ));
        }
        let kind = req.service_kind();
        let not_found =
            |resp: &mut yb_proto::pb::master_client::GetStatefulServiceLocationResponsePB,
             code: StatusCode,
             message: String| {
                resp.error = Some(master_error(
                    master_error_pb::Code::OBJECT_NOT_FOUND,
                    code,
                    &message,
                ))
                .into();
                Ok(())
            };
        let table = snapshot
            .table_by_name(
                b"00000000000000000000000000000001",
                &master_table_lifecycle::stateful_service_table_name(kind),
            )
            .filter(|t| {
                t.table().is_some_and(|h| {
                    h.state == sys_tables_entry_pb::State::RUNNING as i32
                        || h.state == sys_tables_entry_pb::State::ALTERING as i32
                })
            });
        let Some(table) = table else {
            return not_found(
                resp,
                StatusCode::NotFound,
                format!("Stateful Service of kind {kind:?} does not exist"),
            );
        };
        let tablets = snapshot.tablets_of(&table.key.id);
        let [tablet] = tablets.as_slice() else {
            return not_found(
                resp,
                StatusCode::NotFound,
                format!(
                    "Stateful Service of kind {kind:?} expected to have one tablet, but {} were \
                     found",
                    tablets.len()
                ),
            );
        };
        let leader = self
            .ts_manager
            .locations
            .get(&tablet.key.id)
            .and_then(|replicas| replicas.leader().map(|r| r.ts_uuid.clone()))
            .and_then(|uuid| self.ts_manager.get(&uuid));
        let Some(leader) = leader else {
            return not_found(
                resp,
                StatusCode::TryAgain,
                format!("Leader not found for Stateful Service of kind {kind:?}"),
            );
        };
        let info = resp.service_info.mut_or_insert_default();
        info.set_permanent_uuid(leader.uuid.to_vec());
        let registration = leader.registration();
        let common = registration.common.get_or_default();
        info.private_rpc_addresses = common.private_rpc_addresses.clone();
        info.broadcast_addresses = common.broadcast_addresses.clone();
        info.cloud_info = common.cloud_info.clone();
        Ok(())
    }

    /// `GetTransactionStatusTablets`, for the global transaction table.
    pub fn get_transaction_status_tablets(
        &self,
        snapshot: &CatalogSnapshot,
        req: &yb_proto::pb::master_client::GetTransactionStatusTabletsRequestPB,
        resp: &mut GetTransactionStatusTabletsResponsePB,
    ) -> Result<(), Status> {
        let table = snapshot
            .iter(SysRowEntryType::NAMESPACE)
            .filter(|n| n.namespace().is_some_and(|n| n.name == "system"))
            .find_map(|n| snapshot.table_by_name(&n.key.id, "transactions"))
            .ok_or_else(|| {
                let status = Status::new(
                    StatusCode::NotFound,
                    "Table system.transactions does not exist",
                );
                resp.error = Some(master_error_from(
                    master_error_pb::Code::OBJECT_NOT_FOUND,
                    &status,
                ))
                .into();
                status
            })?;
        for tablet in snapshot.active_tablets_of(&table.key.id) {
            if self
                .tablet_locations(snapshot, None, tablet, false, true)
                .is_ok()
            {
                resp.global_tablet_id.push(tablet.key.id.to_vec());
            }
        }
        if let Some(placement) = req.placement.as_ref() {
            self.placement_local_transaction_tablets(snapshot, placement, resp);
        }
        Ok(())
    }

    /// `GetPlacementLocalTransactionStatusTables` and `...Tablets`: the transaction status tables
    /// whose placement (their own, or their tablespace's) contains the requestor's, by tablespace,
    /// and those within one region. A table still being created is left out until it is done.
    fn placement_local_transaction_tablets(
        &self,
        snapshot: &CatalogSnapshot,
        placement: &CloudInfoPB,
        resp: &mut GetTransactionStatusTabletsResponsePB,
    ) {
        use yb_proto::pb::master_client::get_transaction_status_tablets_response_pb::TablespaceInfoPB;
        let tablespaces = self.tables.tablespaces();
        // By tablespace oid; 0 (kInvalidOid) for tables placed by their own replication info.
        let mut by_tablespace: std::collections::BTreeMap<u32, LocalStatusTables> =
            Default::default();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            if pb.table_type()
                != yb_proto::pb::common_types::TableType::TRANSACTION_STATUS_TABLE_TYPE
                || pb.state() != sys_tables_entry_pb::State::RUNNING
            {
                continue;
            }
            let (oid, txn_placement) = if pb
                .replication_info
                .as_ref()
                .is_some_and(master_table_lifecycle::replication_info_set)
            {
                (
                    0,
                    pb.replication_info
                        .live_replicas
                        .clone()
                        .unwrap_or_default(),
                )
            } else if pb.transaction_table_tablespace_id.is_some() {
                let id = pb.transaction_table_tablespace_id();
                let Some(oid) = std::str::from_utf8(id.get(..8).unwrap_or_default())
                    .ok()
                    .and_then(|v| u32::from_str_radix(v, 16).ok())
                else {
                    continue;
                };
                match tablespaces.placement(id) {
                    Ok(Some(info)) if master_table_lifecycle::replication_info_set(&info) => {
                        (oid, info.live_replicas.unwrap_or_default())
                    }
                    _ => continue,
                }
            } else {
                continue;
            };
            if !master_ts_manager::placement::placement_contains(&txn_placement, placement) {
                continue;
            }
            let region_local = !spans_regions(&txn_placement);
            by_tablespace
                .entry(oid)
                .or_insert_with(|| (txn_placement.clone(), Vec::new()))
                .1
                .push((table.key.id.to_vec(), region_local));
        }
        let add_tablespaces = (self.tablespace_based_transaction_placement)();
        for (oid, (txn_placement, tables)) in by_tablespace {
            let mut tablespace = (add_tablespaces && oid != 0).then(|| {
                let mut info = TablespaceInfoPB::new();
                info.set_tablespace_oid(oid);
                info.placement = Some(txn_placement.clone()).into();
                info
            });
            for (table_id, region_local) in tables {
                if tablespace.is_none() && !region_local {
                    continue;
                }
                let usable: Vec<Vec<u8>> = snapshot
                    .active_tablets_of(&table_id)
                    .into_iter()
                    .filter(|t| {
                        self.tablet_locations(snapshot, None, t, false, true)
                            .is_ok()
                    })
                    .map(|t| t.key.id.to_vec())
                    .collect();
                if region_local {
                    resp.region_local_tablet_id.extend(usable.iter().cloned());
                }
                if let Some(tablespace) = tablespace.as_mut() {
                    tablespace.tablet_id.extend(usable);
                }
            }
            if let Some(tablespace) = tablespace {
                resp.placement_local_tablespace.push(tablespace);
            }
        }
    }

    fn expected_replicas(&self, snapshot: &CatalogSnapshot, table: &Entity) -> (i32, i32) {
        let table_info = table
            .parse::<SysTablesEntryPB>()
            .ok()
            .and_then(|t| t.replication_info.into_option());
        let info = table_info.or_else(|| {
            Self::cluster_config(snapshot)
                .ok()
                .and_then(|c| c.replication_info.into_option())
        });
        let live = info
            .as_ref()
            .map(|i| i.live_replicas.num_replicas())
            .filter(|n| *n > 0)
            .unwrap_or_else(self.replication_factor);
        let read = info.map_or(0, |i| {
            i.read_replicas.iter().map(|r| r.num_replicas()).sum()
        });
        (live, read)
    }

    /// `BuildLocationsForTablet`. `masters` is the master quorum's committed consensus state, the
    /// replicas of system tablets (`BuildLocationsForSystemTablet`).
    pub(crate) fn tablet_locations(
        &self,
        snapshot: &CatalogSnapshot,
        masters: Option<&ConsensusStatePB>,
        tablet: &Entity,
        include_hidden: bool,
        partitions_only: bool,
    ) -> Result<TabletLocationsPB, LocationError> {
        let tablet_id = &tablet.key.id;
        let name = String::from_utf8_lossy(tablet_id);
        let pb: SysTabletsEntryPB = tablet
            .parse()
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        let hot = tablet.tablet().expect("a tablet");
        if hot.hidden && !include_hidden {
            return Err(LocationError {
                status: Status::new(StatusCode::NotFound, format!("Tablet {name} hidden")),
                split_children: Some(pb.split_tablet_ids.clone()),
            });
        }
        if is_inactive_state(hot.state) {
            return Err(LocationError {
                status: Status::new(
                    StatusCode::Deleted,
                    format!(
                        "Tablet deleted: {}",
                        String::from_utf8_lossy(pb.state_msg())
                    ),
                ),
                split_children: Some(pb.split_tablet_ids.clone()),
            });
        }
        if hot.state != sys_tablets_entry_pb::State::RUNNING.value() {
            return Err(Status::new(
                StatusCode::ServiceUnavailable,
                format!(
                    "Tablet {name} not running ({:?}/{})",
                    pb.state(),
                    String::from_utf8_lossy(pb.state_msg())
                ),
            )
            .into());
        }
        let mut locations = TabletLocationsPB::new();
        locations.set_table_id(pb.table_id().to_vec());
        locations.set_tablet_id(tablet_id.to_vec());
        locations.partition = pb.partition.clone();
        locations.set_split_depth(pb.split_depth());
        if pb.has_split_parent_tablet_id() {
            locations.set_split_parent_tablet_id(pb.split_parent_tablet_id().to_vec());
        }
        let observed = self.ts_manager.locations.get(tablet_id).unwrap_or_default();
        locations.set_stale(observed.replicas.is_empty());
        locations
            .set_raft_config_opid_index(pb.committed_consensus_state.config.committed_op_index());
        if partitions_only {
            return Ok(locations);
        }
        locations.table_ids = snapshot.hosted_table_ids(tablet);
        locations.split_tablet_ids = pb.split_tablet_ids.clone();
        let system_cstate = masters.filter(|_| is_system_tablet(snapshot, tablet));
        if system_cstate.is_some() {
            locations.set_stale(false);
        }
        if system_cstate.is_none() && !observed.replicas.is_empty() {
            for replica in &observed.replicas {
                let mut r = tablet_locations_pb::ReplicaPB::new();
                r.role = Some(EnumOrUnknown::new(replica.role));
                r.member_type = Some(EnumOrUnknown::new(replica.member_type));
                let mut ts_info = TSInfoPB::new();
                ts_info.set_permanent_uuid(replica.ts_uuid.to_vec());
                if let Some(d) = self.ts_manager.get(&replica.ts_uuid) {
                    let reg = &d.pb.registration;
                    ts_info.private_rpc_addresses = reg.private_rpc_addresses.clone();
                    ts_info.broadcast_addresses = reg.broadcast_addresses.clone();
                    ts_info.cloud_info = reg.cloud_info.clone();
                    if reg.has_placement_uuid() {
                        ts_info.set_placement_uuid(reg.placement_uuid().to_vec());
                    }
                }
                r.ts_info = Some(ts_info).into();
                locations.replicas.push(r);
            }
        } else if let Some(cstate) = system_cstate.or(pb.committed_consensus_state.as_ref()) {
            // ConsensusStateToTabletLocations: a system tablet's masters, or no reports yet in
            // this term.
            for peer in &cstate.config.peers {
                let mut r = tablet_locations_pb::ReplicaPB::new();
                r.role = Some(EnumOrUnknown::new(consensus_role(
                    cstate,
                    peer.permanent_uuid(),
                )));
                r.member_type = Some(
                    peer.member_type
                        .unwrap_or(EnumOrUnknown::new(PeerMemberType::UNKNOWN_MEMBER_TYPE)),
                );
                let mut ts_info = TSInfoPB::new();
                ts_info.set_permanent_uuid(peer.permanent_uuid().to_vec());
                ts_info.private_rpc_addresses = peer.last_known_private_addr.clone();
                ts_info.broadcast_addresses = peer.last_known_broadcast_addr.clone();
                ts_info.cloud_info = peer.cloud_info.clone();
                r.ts_info = Some(ts_info).into();
                locations.replicas.push(r);
            }
        }
        Ok(locations)
    }
}

/// `system_tablets_`: the sys catalog tablet, and the tablets of the YCQL virtual tables, whose
/// replicas are the masters.
fn is_system_tablet(snapshot: &CatalogSnapshot, tablet: &Entity) -> bool {
    if &*tablet.key.id == master_table_lifecycle::lookup::SYS_CATALOG_TABLET_ID {
        return true;
    }
    let Some(hot) = tablet.tablet() else {
        return false;
    };
    snapshot
        .get(SysRowEntryType::TABLE, &hot.table_id)
        .and_then(|t| t.table())
        .is_some_and(master_table_lifecycle::lookup::is_ycql_virtual_table)
}

/// A tablet lookup failure, with the split children C++ attaches to a hidden or deleted tablet
/// (`SplitChildTabletIdsData`): a client holding a parent finds its children through them.
pub(crate) struct LocationError {
    status: Status,
    split_children: Option<Vec<Vec<u8>>>,
}

impl From<Status> for LocationError {
    fn from(status: Status) -> LocationError {
        LocationError {
            status,
            split_children: None,
        }
    }
}

/// `SplitChildTabletIdsTag::kCategory`.
const SPLIT_CHILD_TABLET_IDS_CATEGORY: u8 = 14;

impl LocationError {
    fn app_status(&self) -> AppStatusPB {
        let mut status = app_status(&self.status);
        if let Some(children) = &self.split_children {
            status.set_errors(encode_split_children(children));
        }
        status
    }
}

/// `Status` error codes as `StatusToPB` serializes them: one `StringVectorBackedErrorTag` entry
/// (its total size, then each id's size and bytes, all sizes `uint64` little endian), then the
/// terminating 0.
fn encode_split_children(children: &[Vec<u8>]) -> Vec<u8> {
    let size: usize = 8 + children.iter().map(|c| 8 + c.len()).sum::<usize>();
    let mut errors = Vec::with_capacity(size + 2);
    errors.push(SPLIT_CHILD_TABLET_IDS_CATEGORY);
    errors.extend_from_slice(&(size as u64).to_le_bytes());
    for child in children {
        errors.extend_from_slice(&(child.len() as u64).to_le_bytes());
        errors.extend_from_slice(child);
    }
    errors.push(0);
    errors
}

fn primary_placement(snapshot: &CatalogSnapshot) -> Result<Vec<u8>, Status> {
    let config = LeaderServices::cluster_config(snapshot)?;
    Ok(config
        .replication_info
        .live_replicas
        .placement_uuid()
        .to_vec())
}

fn app_status(status: &Status) -> AppStatusPB {
    let error = master_error(
        master_error_pb::Code::UNKNOWN_ERROR,
        status.code,
        &status.message,
    );
    error.status.unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn split_children_encode_as_cpp_status_error_codes() {
        let bytes = encode_split_children(&[b"ab".to_vec(), b"c".to_vec()]);
        let mut expected = vec![SPLIT_CHILD_TABLET_IDS_CATEGORY];
        // 8 (total) + 8 + 2 + 8 + 1.
        expected.extend_from_slice(&27u64.to_le_bytes());
        expected.extend_from_slice(&2u64.to_le_bytes());
        expected.extend_from_slice(b"ab");
        expected.extend_from_slice(&1u64.to_le_bytes());
        expected.extend_from_slice(b"c");
        expected.push(0);
        assert_eq!(bytes, expected);
    }
}
