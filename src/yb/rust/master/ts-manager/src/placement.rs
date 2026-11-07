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

//! Placement rules, as `CatalogManagerUtil` and common_net.cc apply them. An unset cloud, region or
//! zone is a wildcard.

use std::collections::HashSet;

use yb_host_api::{Status, StatusCode};

use crate::TsDescriptor;
use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;
use yb_proto::pb::common_net::{CloudInfoPB, HostPortPB, PlacementInfoPB, ReplicationInfoPB};
use yb_proto::pb::consensus_metadata::{PeerMemberType, RaftPeerPB};
use yb_proto::pb::wire_protocol::ServerRegistrationPB;

/// `TSDescriptor::generate_placement_id`.
pub fn placement_id(ci: &CloudInfoPB) -> String {
    format!(
        "{}:{}:{}",
        ci.placement_cloud(),
        ci.placement_region(),
        ci.placement_zone()
    )
}

fn short_debug(message: &impl protobuf::MessageFull) -> String {
    protobuf::text_format::print_to_string(message)
}

/// `CloudInfoContainsCloudInfo`: `lhs`, with its wildcards, covers `rhs`.
pub fn cloud_info_contains(lhs: &CloudInfoPB, rhs: &CloudInfoPB) -> bool {
    if !lhs.has_placement_cloud() {
        return true;
    }
    if lhs.placement_cloud() != rhs.placement_cloud() {
        return false;
    }
    if !lhs.has_placement_region() {
        return true;
    }
    if lhs.placement_region() != rhs.placement_region() {
        return false;
    }
    if !lhs.has_placement_zone() {
        return true;
    }
    lhs.placement_zone() == rhs.placement_zone()
}

/// `PlacementInfoContainsCloudInfo`.
pub fn placement_contains(placement: &PlacementInfoPB, ci: &CloudInfoPB) -> bool {
    placement
        .placement_blocks
        .iter()
        .any(|b| cloud_info_contains(&b.cloud_info, ci))
}

/// `CloudInfoMatchesPlacementInfo`: some block covers `ci`, or `ci` covers some block.
pub fn cloud_info_matches_placement(ci: &CloudInfoPB, placement: &PlacementInfoPB) -> bool {
    placement
        .placement_blocks
        .iter()
        .any(|b| cloud_info_contains(ci, &b.cloud_info) || cloud_info_contains(&b.cloud_info, ci))
}

/// `CatalogManagerUtil::IsCloudInfoPrefix` (`ComputeCloudInfoSimilarity(..) == ZONE_MATCH`): no
/// field set in both differs. `TSDescriptor::MatchesCloudInfo`, which replica selection uses.
pub fn is_cloud_info_prefix(a: &CloudInfoPB, b: &CloudInfoPB) -> bool {
    let differs =
        |x: Option<&String>, y: Option<&String>| matches!((x, y), (Some(x), Some(y)) if x != y);
    !differs(a.placement_cloud.as_ref(), b.placement_cloud.as_ref())
        && !differs(a.placement_region.as_ref(), b.placement_region.as_ref())
        && !differs(a.placement_zone.as_ref(), b.placement_zone.as_ref())
}

fn is_wildcard(ci: &CloudInfoPB) -> bool {
    !ci.has_placement_cloud() || !ci.has_placement_region() || !ci.has_placement_zone()
}

fn illegal(message: String) -> Status {
    Status::new(StatusCode::IllegalState, message)
}

/// `CatalogManagerUtil::IsPlacementInfoValid`.
pub fn validate_placement(placement: &PlacementInfoPB) -> Result<(), Status> {
    let blocks: Vec<&CloudInfoPB> = placement
        .placement_blocks
        .iter()
        .filter_map(|b| b.cloud_info.as_ref())
        .collect();
    let mut seen = HashSet::new();
    for ci in &blocks {
        if !seen.insert(placement_id(ci)) {
            return Err(illegal(format!(
                "Placement information specified should not contain duplicates. Given placement \
                 block: {} is a duplicate",
                short_debug(*ci)
            )));
        }
    }
    for ci in &blocks {
        let (cloud, region, zone) = (
            ci.has_placement_cloud(),
            ci.has_placement_region(),
            ci.has_placement_zone(),
        );
        let prefix = matches!(
            (cloud, region, zone),
            (false, false, false) | (true, false, false) | (true, true, false) | (true, true, true)
        );
        if !prefix {
            return Err(illegal(format!(
                "Placement information specified should be prefixes.Given placement block: {} \
                 isn't a prefix",
                short_debug(*ci)
            )));
        }
    }
    for (i, a) in blocks.iter().enumerate() {
        for (j, b) in blocks.iter().enumerate() {
            if i != j && is_cloud_info_prefix(a, b) {
                return Err(illegal(format!(
                    "Placement information specified should not overlap. {} and {} overlap. For \
                     instance, c1.r1.z1,c1.r1 is invalid while c1.r1.z1,c1.r1.z2 is valid. Also \
                     note that c1.r1,c1.r1 is valid.",
                    short_debug(*a),
                    short_debug(*b)
                )));
            }
        }
    }
    let total_min: i64 = placement
        .placement_blocks
        .iter()
        .map(|b| i64::from(b.min_num_replicas()))
        .sum();
    if total_min > i64::from(placement.num_replicas()) {
        return Err(illegal(format!(
            "num_replicas ({}) should be greater than or equal to the total of replica counts \
             specified in placement_info ({total_min}).",
            placement.num_replicas()
        )));
    }
    validate_max_num_replicas(placement)
}

/// `CatalogManagerUtil::ValidateMaxNumReplicasFields`.
pub fn validate_max_num_replicas(placement: &PlacementInfoPB) -> Result<(), Status> {
    if !placement
        .placement_blocks
        .iter()
        .any(|b| b.has_max_num_replicas())
    {
        return Ok(());
    }
    let mut total_max = 0i64;
    let mut ids = HashSet::new();
    for block in &placement.placement_blocks {
        if is_wildcard(&block.cloud_info) {
            return Err(illegal(format!(
                "max_num_replicas is not supported in combination with wildcard placement \
                 blocks: {}",
                short_debug(block)
            )));
        }
        if !ids.insert(placement_id(&block.cloud_info)) {
            return Err(illegal(format!(
                "max_num_replicas is not supported in combination with duplicate placement \
                 blocks: {}",
                short_debug(block)
            )));
        }
        if block.has_max_num_replicas() {
            if block.max_num_replicas() < 1 {
                return Err(illegal(format!(
                    "max_num_replicas ({}) must be greater than or equal to 1",
                    block.max_num_replicas()
                )));
            }
            if block.max_num_replicas() < block.min_num_replicas() {
                return Err(illegal(format!(
                    "max_num_replicas ({}) must be greater than or equal to min_num_replicas ({})",
                    block.max_num_replicas(),
                    block.min_num_replicas()
                )));
            }
        }
        total_max += i64::from(if block.has_max_num_replicas() {
            block.max_num_replicas()
        } else {
            placement.num_replicas()
        });
    }
    if total_max < i64::from(placement.num_replicas()) {
        return Err(illegal(format!(
            "num_replicas ({}) should be less than or equal to the total maximum replica count \
             ({total_max}).",
            placement.num_replicas()
        )));
    }
    Ok(())
}

/// `CatalogManagerUtil::GetPlacementUuidFromRaftPeer`.
pub fn placement_uuid_of_peer(
    replication_info: &ReplicationInfoPB,
    peer: &RaftPeerPB,
) -> Result<Vec<u8>, Status> {
    match peer.member_type() {
        PeerMemberType::PRE_VOTER | PeerMemberType::VOTER => {
            Ok(replication_info.live_replicas.placement_uuid().to_vec())
        }
        PeerMemberType::PRE_OBSERVER | PeerMemberType::OBSERVER => {
            let matches: Vec<&[u8]> = replication_info
                .read_replicas
                .iter()
                .filter(|p| placement_contains(p, &peer.cloud_info))
                .map(|p| p.placement_uuid())
                .collect();
            match matches.as_slice() {
                [uuid] => Ok(uuid.to_vec()),
                _ => Err(illegal(format!(
                    "Expect 1 placement match for peer {}, found {}",
                    short_debug(peer),
                    matches.len()
                ))),
            }
        }
        _ => Err(illegal(format!(
            "Member type unknown for peer {}",
            short_debug(peer)
        ))),
    }
}

/// The addresses in the cluster config's server blacklist.
pub fn blacklist_addresses(config: &SysClusterConfigEntryPB) -> HashSet<(String, u32)> {
    config
        .server_blacklist
        .hosts
        .iter()
        .map(|h| (h.host().to_string(), h.port()))
        .collect()
}

/// The leader blacklist's addresses, for [`is_blacklisted`].
pub fn leader_blacklist_addresses(config: &SysClusterConfigEntryPB) -> HashSet<(String, u32)> {
    config
        .leader_blacklist
        .hosts
        .iter()
        .map(|h| (h.host().to_string(), h.port()))
        .collect()
}

/// Whether any of the tserver's addresses is blacklisted.
pub fn is_blacklisted(descriptor: &TsDescriptor, blacklist: &HashSet<(String, u32)>) -> bool {
    let reg = &descriptor.pb.registration;
    reg.private_rpc_addresses
        .iter()
        .chain(reg.broadcast_addresses.iter())
        .any(|hp: &HostPortPB| blacklist.contains(&(hp.host().to_string(), hp.port())))
}

/// `UsePublicIp`: whether a server at `to` is called at its broadcast address from `from`, under
/// `use_private_ip` (`never`, `cloud`, `region` or `zone`).
pub fn use_public_ip(to: &CloudInfoPB, from: &CloudInfoPB, mode: &str) -> bool {
    if mode == "never" || to.placement_cloud() != from.placement_cloud() {
        return true;
    }
    if mode == "cloud" {
        return false;
    }
    if to.placement_region() != from.placement_region() {
        return true;
    }
    if mode == "region" {
        return false;
    }
    to.placement_zone() != from.placement_zone()
}

/// `DesiredHostPort`: the first broadcast address when the public address may be used, else the
/// first private one.
pub fn desired_host_port<'a>(
    registration: &'a ServerRegistrationPB,
    from: &CloudInfoPB,
    mode: &str,
) -> Option<&'a HostPortPB> {
    let public = use_public_ip(&registration.cloud_info, from, mode);
    match registration.broadcast_addresses.first() {
        Some(hp) if public => Some(hp),
        _ => registration.private_rpc_addresses.first(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use yb_proto::pb::common_net::PlacementBlockPB;

    fn ci(cloud: Option<&str>, region: Option<&str>, zone: Option<&str>) -> CloudInfoPB {
        let mut ci = CloudInfoPB::new();
        ci.placement_cloud = cloud.map(String::from);
        ci.placement_region = region.map(String::from);
        ci.placement_zone = zone.map(String::from);
        ci
    }

    fn placement(num_replicas: i32, blocks: &[(CloudInfoPB, i32)]) -> PlacementInfoPB {
        let mut placement = PlacementInfoPB::new();
        placement.set_num_replicas(num_replicas);
        for (ci, min) in blocks {
            let mut block = PlacementBlockPB::new();
            block.cloud_info = Some(ci.clone()).into();
            block.set_min_num_replicas(*min);
            placement.placement_blocks.push(block);
        }
        placement
    }

    #[test]
    fn min_replicas_over_num_replicas_is_invalid() {
        let z1 = ci(Some("aws"), Some("r1"), Some("z1"));
        let z2 = ci(Some("aws"), Some("r1"), Some("z2"));
        assert!(validate_placement(&placement(3, &[(z1.clone(), 2), (z2.clone(), 2)])).is_err());
        assert!(validate_placement(&placement(4, &[(z1, 2), (z2, 2)])).is_ok());
    }

    #[test]
    fn overlapping_and_non_prefix_blocks_are_invalid() {
        let region = ci(Some("aws"), Some("r1"), None);
        let zone = ci(Some("aws"), Some("r1"), Some("z1"));
        assert!(validate_placement(&placement(3, &[(region, 1), (zone, 1)])).is_err());
        let gap = ci(Some("aws"), None, Some("z1"));
        assert!(validate_placement(&placement(3, &[(gap, 1)])).is_err());
    }

    #[test]
    fn unset_fields_are_wildcards() {
        let block = placement(3, &[(ci(Some("aws"), Some("r1"), None), 1)]);
        assert!(cloud_info_matches_placement(
            &ci(Some("aws"), Some("r1"), Some("z1")),
            &block
        ));
        assert!(!cloud_info_matches_placement(
            &ci(Some("aws"), Some("r2"), Some("z1")),
            &block
        ));
    }
}
