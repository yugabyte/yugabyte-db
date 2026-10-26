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

//! The tserver services the master calls. All fake tservers share one RPC server, so each call is
//! routed by its request's `dest_uuid`.

use std::collections::{BTreeMap, HashMap};
use std::sync::{Arc, Mutex};

use protobuf::{Message, MessageDyn};
use yb_host_api::rpc::{InboundCall, RpcHandler};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::operations::ChangeMetadataRequestPB;
use yb_proto::pb::tserver_admin::{
    ChangeMetadataResponsePB, CreateTabletRequestPB, CreateTabletResponsePB, DeleteTabletRequestPB,
    DeleteTabletResponsePB,
};
use yb_proto::ServiceDesc;

use crate::tserver::SimTserver;

/// The fake tservers of one simulator process, and what the master asked of them.
#[derive(Default)]
pub struct SimCluster {
    tservers: HashMap<Vec<u8>, Arc<Mutex<SimTserver>>>,
    /// Calls the simulator answers without modelling them, by `Service.Method`.
    unmodeled_calls: Mutex<BTreeMap<String, u64>>,
}

impl SimCluster {
    /// A cluster of `tservers`.
    pub fn new(tservers: Vec<SimTserver>) -> SimCluster {
        SimCluster {
            tservers: tservers
                .into_iter()
                .map(|ts| (ts.uuid().to_vec(), Arc::new(Mutex::new(ts))))
                .collect(),
            unmodeled_calls: Mutex::default(),
        }
    }

    /// Every tserver.
    pub fn tservers(&self) -> Vec<Arc<Mutex<SimTserver>>> {
        let mut all: Vec<_> = self.tservers.iter().collect();
        all.sort_by(|a, b| a.0.cmp(b.0));
        all.into_iter().map(|(_, ts)| ts.clone()).collect()
    }

    /// Calls answered without modelling them, by `Service.Method`.
    pub fn unmodeled_calls(&self) -> BTreeMap<String, u64> {
        self.unmodeled_calls.lock().unwrap().clone()
    }

    fn tserver(&self, uuid: &[u8]) -> Result<&Arc<Mutex<SimTserver>>, Status> {
        self.tservers.get(uuid).ok_or_else(|| {
            Status::new(
                StatusCode::InvalidArgument,
                format!("no simulated tserver {}", String::from_utf8_lossy(uuid)),
            )
        })
    }
}

/// Serves one tserver service for every fake tserver.
pub struct SimService {
    service: &'static ServiceDesc,
    cluster: Arc<SimCluster>,
}

impl SimService {
    /// Serves `service`.
    pub fn new(service: &'static ServiceDesc, cluster: Arc<SimCluster>) -> SimService {
        SimService { service, cluster }
    }

    fn handle_admin(&self, method: &str, request: &[u8]) -> Option<Result<Vec<u8>, Status>> {
        let result = match method {
            "CreateTablet" => parse::<CreateTabletRequestPB>(request).and_then(|req| {
                self.cluster
                    .tserver(req.dest_uuid())?
                    .lock()
                    .unwrap()
                    .create_tablet(&req);
                serialize(&CreateTabletResponsePB::new())
            }),
            "DeleteTablet" => parse::<DeleteTabletRequestPB>(request).and_then(|req| {
                self.cluster
                    .tserver(req.dest_uuid())?
                    .lock()
                    .unwrap()
                    .delete_tablet(req.tablet_id());
                serialize(&DeleteTabletResponsePB::new())
            }),
            "AlterSchema" => parse::<ChangeMetadataRequestPB>(request).and_then(|req| {
                let ts = self.cluster.tserver(req.dest_uuid())?;
                if !ts
                    .lock()
                    .unwrap()
                    .alter_schema(req.tablet_id(), req.schema_version())
                {
                    return Err(Status::new(
                        StatusCode::NotFound,
                        format!("tablet {}", String::from_utf8_lossy(req.tablet_id())),
                    ));
                }
                serialize(&ChangeMetadataResponsePB::new())
            }),
            _ => return None,
        };
        Some(result)
    }
}

fn parse<M: Message>(bytes: &[u8]) -> Result<M, Status> {
    M::parse_from_bytes(bytes).map_err(|e| Status::new(StatusCode::InvalidArgument, e.to_string()))
}

fn serialize(message: &dyn MessageDyn) -> Result<Vec<u8>, Status> {
    message
        .write_to_bytes_dyn()
        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))
}

impl RpcHandler for SimService {
    fn handle(&self, call: Box<dyn InboundCall>) {
        let Some(method) = self.service.methods.get(call.method_index()) else {
            return call.respond_error(Status::new(StatusCode::NotSupported, "unknown method"));
        };
        let modeled = if self.service.proto_name == "yb.tserver.TabletServerAdminService" {
            self.handle_admin(method.name, call.request())
        } else {
            None
        };
        match modeled {
            Some(Ok(response)) => call.respond(&response),
            Some(Err(status)) => call.respond_error(status),
            None => {
                *self
                    .cluster
                    .unmodeled_calls
                    .lock()
                    .unwrap()
                    .entry(format!("{}.{}", self.service.proto_name, method.name))
                    .or_default() += 1;
                // An empty response is a success with nothing to report, which is what the master
                // gets from an idle tserver for the calls it makes in passing. A response with
                // required fields cannot be empty.
                if method.response_has_required_fields {
                    call.respond_error(Status::new(
                        StatusCode::NotSupported,
                        format!("the tserver simulator does not model {}", method.name),
                    ))
                } else {
                    call.respond(&[])
                }
            }
        }
    }
}
