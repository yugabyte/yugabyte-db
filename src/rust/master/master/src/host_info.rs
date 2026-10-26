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

//! [`MasterInfo`] backed by the C++ host.

use cxx::UniquePtr;
use master_rpc_handlers::MasterInfo;
use protobuf::{Enum, Message};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::ConsensusStatePB;
use yb_proto::pb::wire_protocol::{NodeInstancePB, ServerRegistrationPB};

use crate::ffi::bridge::MasterInfoHandle;

pub(crate) struct HostInfo(UniquePtr<MasterInfoHandle>);

impl HostInfo {
    pub(crate) fn new(handle: UniquePtr<MasterInfoHandle>) -> HostInfo {
        HostInfo(handle)
    }
}

fn parse<M: Message>(bytes: &[u8]) -> Result<M, Status> {
    M::parse_from_bytes(bytes).map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))
}

fn host_error(e: cxx::Exception) -> Status {
    Status::new(StatusCode::IllegalState, e.what())
}

impl MasterInfo for HostInfo {
    fn instance(&self) -> NodeInstancePB {
        parse(&self.0.instance_pb()).expect("the host serializes a valid NodeInstancePB")
    }

    fn registration(&self) -> Result<ServerRegistrationPB, Status> {
        parse(&self.0.registration_pb().map_err(host_error)?)
    }

    fn raft_role(&self) -> PeerRole {
        PeerRole::from_i32(self.0.raft_role()).unwrap_or(PeerRole::UNKNOWN_ROLE)
    }

    fn committed_consensus_state(&self) -> Result<ConsensusStatePB, Status> {
        parse(&self.0.committed_consensus_state_pb().map_err(host_error)?)
    }

    fn is_initialized(&self) -> bool {
        self.0.is_sys_catalog_initialized()
    }

    fn is_shell_mode(&self) -> bool {
        self.0.is_in_shell_mode()
    }

    fn flush_sys_catalog(&self) -> Vec<u8> {
        self.0.flush_sys_catalog_response()
    }

    fn compact_sys_catalog(&self) -> Vec<u8> {
        self.0.compact_sys_catalog_response()
    }
}
