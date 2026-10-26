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

//! One fake tablet server: what it tells the master in heartbeats, following the protocol of
//! `HeartbeatPoller::TryHeartbeat` (src/yb/tserver/heartbeater.cc).

use std::collections::{BTreeMap, BTreeSet};

use yb_proto::pb::common_net::{CloudInfoPB, HostPortPB};
use yb_proto::pb::consensus_metadata::{ConsensusStatePB, RaftConfigPB};
use yb_proto::pb::master_heartbeat::{
    ReportedTabletPB, TSHeartbeatRequestPB, TSHeartbeatResponsePB,
};
use yb_proto::pb::master_types::TSRegistrationPB;
use yb_proto::pb::tablet_types::{RaftGroupStatePB, TabletDataState};
use yb_proto::pb::tserver_admin::CreateTabletRequestPB;
use yb_proto::pb::wire_protocol::ServerRegistrationPB;

/// Port the fake tservers advertise for their (nonexistent) web servers.
const HTTP_PORT: u32 = 9000;

struct Tablet {
    config: RaftConfigPB,
    leader: Vec<u8>,
    schema_version: u32,
}

/// The tablets a heartbeat reported, so the response can acknowledge them.
#[derive(Debug, Default)]
pub struct SentReport {
    has_registration: bool,
    full: bool,
    updated: Vec<Vec<u8>>,
    removed: Vec<Vec<u8>>,
}

/// One fake tablet server.
pub struct SimTserver {
    uuid: Vec<u8>,
    instance_seqno: i64,
    host: String,
    port: u16,
    tablets: BTreeMap<Vec<u8>, Tablet>,
    /// Tablets created or changed since the master last acknowledged them.
    dirty: BTreeSet<Vec<u8>>,
    /// Tablets deleted since the master last acknowledged them.
    removed: BTreeSet<Vec<u8>>,
    needs_reregister: bool,
    needs_full_report: bool,
    sequence_number: i32,
    universe_uuid: String,
}

impl SimTserver {
    /// A tserver with a deterministic UUID listening at `host:port`.
    pub fn new(index: usize, host: String, port: u16, instance_seqno: i64) -> SimTserver {
        SimTserver {
            uuid: format!("{:032x}", 0x5100_0000_0000_0000_u64 + index as u64).into_bytes(),
            instance_seqno,
            host,
            port,
            tablets: BTreeMap::new(),
            dirty: BTreeSet::new(),
            removed: BTreeSet::new(),
            needs_reregister: true,
            needs_full_report: false,
            sequence_number: 0,
            universe_uuid: String::new(),
        }
    }

    /// The permanent UUID.
    pub fn uuid(&self) -> &[u8] {
        &self.uuid
    }

    /// Registered, with its full tablet report acknowledged.
    pub fn is_registered(&self) -> bool {
        !self.needs_reregister && !self.needs_full_report && self.sequence_number > 0
    }

    /// Number of tablet replicas hosted.
    pub fn num_tablets(&self) -> usize {
        self.tablets.len()
    }

    fn registration(&self) -> TSRegistrationPB {
        let mut common = ServerRegistrationPB::new();
        let mut rpc = HostPortPB::new();
        rpc.set_host(self.host.clone());
        rpc.set_port(self.port.into());
        common.private_rpc_addresses.push(rpc);
        let mut http = HostPortPB::new();
        http.set_host(self.host.clone());
        http.set_port(HTTP_PORT);
        common.http_addresses.push(http);
        let mut cloud = CloudInfoPB::new();
        cloud.set_placement_cloud("cloud1".into());
        cloud.set_placement_region("datacenter1".into());
        cloud.set_placement_zone("rack1".into());
        common.cloud_info = Some(cloud).into();
        let mut registration = TSRegistrationPB::new();
        registration.common = Some(common).into();
        registration
    }

    fn reported_tablet(&self, tablet_id: &[u8], tablet: &Tablet) -> ReportedTabletPB {
        let mut state = ConsensusStatePB::new();
        state.set_current_term(1);
        state.set_leader_uuid(String::from_utf8_lossy(&tablet.leader).into_owned());
        state.config = Some(tablet.config.clone()).into();
        let mut reported = ReportedTabletPB::new();
        reported.set_tablet_id(tablet_id.to_vec());
        reported.set_state(RaftGroupStatePB::RUNNING);
        reported.set_tablet_data_state(TabletDataState::TABLET_DATA_READY);
        reported.committed_consensus_state = Some(state).into();
        reported.set_schema_version(tablet.schema_version);
        reported
    }

    /// The next heartbeat, and what it reports.
    pub fn heartbeat_request(&mut self) -> (TSHeartbeatRequestPB, SentReport) {
        let mut req = TSHeartbeatRequestPB::new();
        let instance = req
            .common
            .mut_or_insert_default()
            .ts_instance
            .mut_or_insert_default();
        instance.set_permanent_uuid(self.uuid.clone());
        instance.set_instance_seqno(self.instance_seqno);

        let mut sent = SentReport {
            has_registration: self.needs_reregister,
            full: self.needs_full_report,
            ..Default::default()
        };
        if self.needs_reregister {
            req.registration = Some(self.registration()).into();
        }

        let report = req.tablet_report.mut_or_insert_default();
        report.set_is_incremental(!sent.full);
        report.set_sequence_number(self.sequence_number);
        self.sequence_number += 1;
        let ids: Vec<Vec<u8>> = if sent.full {
            self.tablets.keys().cloned().collect()
        } else {
            self.dirty.iter().cloned().collect()
        };
        for id in ids {
            if let Some(tablet) = self.tablets.get(&id) {
                report
                    .updated_tablets
                    .push(self.reported_tablet(&id, tablet));
                sent.updated.push(id);
            }
        }
        if !sent.full {
            for id in &self.removed {
                report.removed_tablet_ids.push(id.clone());
                sent.removed.push(id.clone());
            }
        }
        if sent.full {
            report.set_full_report_seq_no(report.sequence_number());
        }

        let leaders = self
            .tablets
            .values()
            .filter(|t| t.leader == self.uuid)
            .count();
        req.set_num_live_tablets(self.tablets.len() as i32);
        req.set_leader_count(leaders as i32);
        if !self.universe_uuid.is_empty() {
            req.set_universe_uuid(self.universe_uuid.clone());
        }
        (req, sent)
    }

    /// Applies the master's answer to a heartbeat built by [`Self::heartbeat_request`]. As in
    /// C++, the universe UUID is taken even from an error response: a master answers a tserver
    /// that has none with an error that carries it.
    pub fn handle_heartbeat_response(&mut self, sent: &SentReport, resp: &TSHeartbeatResponsePB) {
        if !resp.universe_uuid().is_empty() {
            self.universe_uuid = resp.universe_uuid().to_string();
        }
        if resp.error.is_some() {
            return;
        }
        if resp.needs_reregister() {
            self.needs_reregister = true;
            return;
        }
        if sent.has_registration {
            self.needs_reregister = false;
        }
        if resp.needs_full_tablet_report() {
            self.needs_full_report = true;
            return;
        }
        if resp.tablet_report.processing_truncated() {
            // The master stopped part way; everything sent is reported again.
            return;
        }
        if sent.full {
            self.needs_full_report = false;
        }
        for id in &sent.updated {
            self.dirty.remove(id);
        }
        for id in &sent.removed {
            self.removed.remove(id);
        }
    }

    /// `CreateTablet`: the replica starts RUNNING at once. Its leader is the peer with the lowest
    /// UUID, which every replica agrees on without an election.
    pub fn create_tablet(&mut self, req: &CreateTabletRequestPB) {
        let leader = req
            .config
            .peers
            .iter()
            .map(|p| p.permanent_uuid().to_vec())
            .min()
            .unwrap_or_default();
        let id = req.tablet_id().to_vec();
        self.tablets.insert(
            id.clone(),
            Tablet {
                config: req.config.clone().unwrap_or_default(),
                leader,
                schema_version: 0,
            },
        );
        self.removed.remove(&id);
        self.dirty.insert(id);
    }

    /// `DeleteTablet`.
    pub fn delete_tablet(&mut self, tablet_id: &[u8]) {
        if self.tablets.remove(tablet_id).is_some() {
            self.dirty.remove(tablet_id);
            self.removed.insert(tablet_id.to_vec());
        }
    }

    /// `AlterSchema`. Returns false for an unknown tablet.
    pub fn alter_schema(&mut self, tablet_id: &[u8], schema_version: u32) -> bool {
        match self.tablets.get_mut(tablet_id) {
            Some(tablet) => {
                tablet.schema_version = schema_version;
                self.dirty.insert(tablet_id.to_vec());
                true
            }
            None => false,
        }
    }
}
