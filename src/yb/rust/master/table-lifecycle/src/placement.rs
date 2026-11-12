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

//! Where a table's replicas may go: the live tservers of its placement, `CheckValidPlacementInfo`,
//! and `SelectReplicasForTablet`'s least-loaded choice.

use std::collections::HashMap;

use master_catalog_state::CatalogSnapshot;
use master_ts_manager::placement::{
    blacklist_addresses, is_blacklisted, is_cloud_info_prefix, validate_max_num_replicas,
};
use master_ts_manager::{TsDescriptor, TsManager};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;
use yb_proto::pb::common_net::{PlacementBlockPB, PlacementInfoPB, ReplicationInfoPB};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB};

use crate::error;

/// The cluster config, or an empty one.
pub(crate) fn cluster_config(snapshot: &CatalogSnapshot) -> SysClusterConfigEntryPB {
    snapshot
        .cluster_config()
        .and_then(|c| c.parse().ok())
        .unwrap_or_default()
}

/// `GetTableReplicationInfo` for a YCQL table: its own replication info when set, else the
/// cluster's.
pub fn table_replication_info(
    table: Option<&ReplicationInfoPB>,
    config: &SysClusterConfigEntryPB,
) -> ReplicationInfoPB {
    match table {
        Some(info) if replication_info_set(info) => info.clone(),
        _ => config.replication_info.clone().unwrap_or_default(),
    }
}

/// `IsReplicationInfoSet`.
pub fn replication_info_set(info: &ReplicationInfoPB) -> bool {
    info.live_replicas.as_ref().is_some_and(|l| {
        l.num_replicas() > 0 || !l.placement_blocks.is_empty() || !l.placement_uuid().is_empty()
    }) || !info.read_replicas.is_empty()
        || !info.affinitized_leaders.is_empty()
        || !info.multi_affinitized_leaders.is_empty()
}

/// `GetNumReplicasOrGlobalReplicationFactor`.
pub(crate) fn num_replicas(placement: &PlacementInfoPB, replication_factor: i32) -> usize {
    if placement.num_replicas() > 0 {
        placement.num_replicas() as usize
    } else {
        replication_factor.max(0) as usize
    }
}

/// The live, non-blacklisted tservers of the placement uuid (empty: the live cluster's), in uuid
/// order: `GetAllLiveDescriptors` with the blacklist, then `GetTsDescsFromPlacementInfo`.
pub(crate) fn live_tservers(
    ts_manager: &TsManager,
    config: &SysClusterConfigEntryPB,
    placement_uuid: &[u8],
) -> Vec<TsDescriptor> {
    let blacklist = blacklist_addresses(config);
    let placement_uuid = if placement_uuid.is_empty() {
        config
            .replication_info
            .live_replicas
            .placement_uuid()
            .to_vec()
    } else {
        placement_uuid.to_vec()
    };
    ts_manager
        .live()
        .into_iter()
        .filter(|d| !is_blacklisted(d, &blacklist))
        .filter(|d| d.placement_uuid() == placement_uuid.as_slice())
        .collect()
}

/// `FindTServersForPlacementInfo`: how many of `tservers` are in one of the placement's blocks.
pub(crate) fn in_placement(tservers: &[TsDescriptor], placement: &PlacementInfoPB) -> usize {
    tservers
        .iter()
        .filter(|d| placement.placement_blocks.iter().any(|b| in_block(d, b)))
        .count()
}

/// `TSDescriptor::MatchesCloudInfo(block.cloud_info())`, as `PlacementCandidates` are built.
fn in_block(descriptor: &TsDescriptor, block: &PlacementBlockPB) -> bool {
    is_cloud_info_prefix(
        &block.cloud_info.clone().unwrap_or_default(),
        &descriptor
            .pb
            .registration
            .cloud_info
            .clone()
            .unwrap_or_default(),
    )
}

fn effective_max(block: &PlacementBlockPB, num_replicas: usize) -> usize {
    if block.has_max_num_replicas() {
        block.max_num_replicas().max(0) as usize
    } else {
        num_replicas
    }
}

/// `CheckValidPlacementInfo` as `CreateTable` runs it. Its codes are set on a response C++ then
/// drops, so the client gets `UNKNOWN_ERROR`.
pub(crate) fn check_placement(
    placement: &PlacementInfoPB,
    tservers: &[TsDescriptor],
    replication_factor: i32,
    check_ts_count: bool,
) -> Result<(), MasterErrorPB> {
    let invalid =
        |message: String| error(Code::UNKNOWN_ERROR, StatusCode::InvalidArgument, message);
    let num_replicas = num_replicas(placement, replication_factor);
    let quorum = num_replicas / 2 + 1;
    if check_ts_count && quorum > tservers.len() {
        return Err(invalid(format!(
            "Not enough live tablet servers to create table with replication factor {}. Need at \
             least {} tablet servers whereas {} are alive.",
            num_replicas,
            quorum,
            tservers.len()
        )));
    }
    if placement.placement_blocks.is_empty() {
        return Ok(());
    }
    let mut minimum_sum = 0;
    for block in &placement.placement_blocks {
        minimum_sum += block.min_num_replicas().max(0) as usize;
        if block.cloud_info.is_none() {
            return Err(invalid(format!(
                "Got placement info without cloud info set: {}",
                protobuf::text_format::print_to_string(block)
            )));
        }
    }
    if minimum_sum > num_replicas {
        return Err(invalid(format!(
            "Sum of minimum replicas per placement ({minimum_sum}) is greater than num_replicas  \
             ({num_replicas})"
        )));
    }
    let maximum_sum: usize = placement
        .placement_blocks
        .iter()
        .map(|b| effective_max(b, num_replicas))
        .sum();
    if maximum_sum < num_replicas {
        return Err(invalid(format!(
            "Sum of maximum replicas per placement ({maximum_sum}) is less than num_replicas \
             ({num_replicas})"
        )));
    }
    let allowed = tservers
        .iter()
        .filter(|d| placement.placement_blocks.iter().any(|b| in_block(d, b)))
        .count();
    if allowed < quorum {
        return Err(invalid(format!(
            "Not enough tablet servers in the requested placements. Need at least {quorum}, have \
             {allowed}"
        )));
    }
    // Whether a quorum fits within the blocks' minimums and their effective maximums: first what
    // the minimums can take, then the slack, spread over the tservers each block has room for.
    let mut feasible = 0;
    let mut extra_servers = 0;
    for block in &placement.placement_blocks {
        let in_this = tservers.iter().filter(|d| in_block(d, block)).count();
        let min = block.min_num_replicas().max(0) as usize;
        feasible += in_this.min(min);
        extra_servers += in_this
            .min(effective_max(block, num_replicas))
            .saturating_sub(min);
    }
    feasible += (num_replicas - minimum_sum).min(extra_servers);
    if feasible < quorum {
        return Err(invalid(format!(
            "Not enough tablet servers in the requested placements. Can only find {feasible} \
             tablet servers for the replicas but need at least {quorum}."
        )));
    }
    Ok(())
}

/// Replica counts per tserver: of the table being assigned, and over all tables. Picks add to
/// both, as `CatalogManagerUtil::AddReplica` does, so one round spreads its tablets.
#[derive(Default)]
pub(crate) struct Load {
    pub(crate) table: HashMap<Vec<u8>, usize>,
    pub(crate) global: HashMap<Vec<u8>, usize>,
}

impl Load {
    fn key(&self, uuid: &[u8]) -> (usize, usize, Vec<u8>) {
        (
            self.table.get(uuid).copied().unwrap_or(0),
            self.global.get(uuid).copied().unwrap_or(0),
            uuid.to_vec(),
        )
    }

    fn add(&mut self, uuid: &[u8]) {
        *self.table.entry(uuid.to_vec()).or_default() += 1;
        *self.global.entry(uuid.to_vec()).or_default() += 1;
    }
}

/// `SelectReplicasForTablet` for the live replicas: each block's minimum first, then the least
/// loaded tservers up to each block's maximum. Deterministic, as in C++, and as in C++ a block or a
/// placement with fewer allowed tservers than replicas gets what it has.
pub(crate) fn select_replicas(
    placement: &PlacementInfoPB,
    tservers: &[TsDescriptor],
    replication_factor: i32,
    load: &mut Load,
) -> Result<Vec<TsDescriptor>, Status> {
    let num_replicas = num_replicas(placement, replication_factor);
    let mut chosen: Vec<TsDescriptor> = Vec::new();
    let pick = |candidates: Vec<&TsDescriptor>, load: &Load| -> Option<TsDescriptor> {
        candidates
            .into_iter()
            .min_by_key(|d| load.key(&d.uuid))
            .cloned()
    };
    let not_chosen =
        |d: &TsDescriptor, chosen: &[TsDescriptor]| !chosen.iter().any(|c| c.uuid == d.uuid);
    if placement.placement_blocks.is_empty() {
        while chosen.len() < num_replicas {
            let candidates: Vec<_> = tservers.iter().filter(|d| not_chosen(d, &chosen)).collect();
            let Some(d) = pick(candidates, load) else {
                break;
            };
            load.add(&d.uuid);
            chosen.push(d);
        }
    } else {
        let mut per_block = vec![0usize; placement.placement_blocks.len()];
        for (i, block) in placement.placement_blocks.iter().enumerate() {
            for _ in 0..block.min_num_replicas().max(0) {
                let candidates: Vec<_> = tservers
                    .iter()
                    .filter(|d| in_block(d, block) && not_chosen(d, &chosen))
                    .collect();
                let Some(d) = pick(candidates, load) else {
                    break;
                };
                load.add(&d.uuid);
                per_block[i] += 1;
                chosen.push(d);
            }
        }
        while chosen.len() < num_replicas {
            let candidates: Vec<_> = tservers
                .iter()
                .filter(|d| not_chosen(d, &chosen))
                .filter(|d| {
                    placement.placement_blocks.iter().enumerate().any(|(i, b)| {
                        in_block(d, b) && per_block[i] < effective_max(b, num_replicas)
                    })
                })
                .collect();
            let Some(d) = pick(candidates, load) else {
                break;
            };
            if let Some(i) = placement
                .placement_blocks
                .iter()
                .position(|b| in_block(&d, b))
            {
                per_block[i] += 1;
            }
            load.add(&d.uuid);
            chosen.push(d);
        }
    }
    // Fewer live tservers than replicas leaves the tablet under-replicated, as in C++ (the
    // placement was checked for a quorum at create); none at all cannot create it.
    if chosen.is_empty() {
        return Err(Status::new(
            StatusCode::IllegalState,
            format!("Not enough tablet servers to place {num_replicas} replicas"),
        ));
    }
    Ok(chosen)
}

/// `ValidateTableReplicationInfo`: only the per-block maximums, not `IsPlacementInfoValid`, which
/// is stricter than table-level placements were ever held to, and the placement uuids.
pub(crate) fn validate_table_replication_info(
    info: &ReplicationInfoPB,
    config: &SysClusterConfigEntryPB,
) -> Result<(), Status> {
    let invalid = |message: &str| Status::new(StatusCode::InvalidArgument, message.to_string());
    if !replication_info_set(info) {
        return Err(invalid("No replication info set."));
    }
    if let Some(live) = info.live_replicas.as_ref() {
        validate_max_num_replicas(live)?;
    }
    for read in &info.read_replicas {
        validate_max_num_replicas(read)?;
    }
    let cluster = &config.replication_info;
    let live_uuid = info.live_replicas.placement_uuid();
    if !live_uuid.is_empty() && live_uuid != cluster.live_replicas.placement_uuid() {
        return Err(invalid(
            "Placement uuid for table level replication info must match that of the cluster's \
             live placement info.",
        ));
    }
    let Some(first) = info.read_replicas.first() else {
        return Ok(());
    };
    if first.placement_uuid().is_empty() {
        let mut uuids = cluster.read_replicas.iter().map(|r| r.placement_uuid());
        if let Some(uuid) = uuids.next() {
            if uuids.any(|u| u != uuid) {
                return Err(invalid(
                    "Cluster has more than one read-replica cluster; the read_replica_placement \
                     option must specify a placement_uuid to disambiguate which one to use.",
                ));
            }
        }
        return Ok(());
    }
    if cluster.read_replicas.is_empty()
        || cluster
            .read_replicas
            .iter()
            .any(|r| r.placement_uuid() == first.placement_uuid())
    {
        return Ok(());
    }
    Err(invalid(&format!(
        "Read-replica placement_uuid '{}' does not match any read-replica cluster's \
         placement_uuid in the cluster config.",
        String::from_utf8_lossy(first.placement_uuid())
    )))
}

/// Bytes of a message, for rows.
pub(crate) fn encode(message: &dyn protobuf::MessageDyn) -> Result<Vec<u8>, MasterErrorPB> {
    message.write_to_bytes_dyn().map_err(|e| {
        error(
            Code::UNKNOWN_ERROR,
            StatusCode::InternalError,
            e.to_string(),
        )
    })
}
