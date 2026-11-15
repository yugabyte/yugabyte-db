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

//! The source's table-level xCluster RPCs that ask its tservers: `BootstrapProducer` (new
//! streams checkpointed at the end of the WAL), `IsBootstrapRequired` and
//! `WaitForReplicationDrain`.

use std::collections::{BTreeMap, HashSet};
use std::time::Duration;

use master_xrepl::strip_sequences_data_alias;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::cdc_service::{
    BootstrapProducerRequestPB as TsBootstrapRequestPB,
    BootstrapProducerResponsePB as TsBootstrapResponsePB, CheckReplicationDrainRequestPB,
    CheckReplicationDrainResponsePB,
    IsBootstrapRequiredRequestPB as TsIsBootstrapRequiredRequestPB,
    IsBootstrapRequiredResponsePB as TsIsBootstrapRequiredResponsePB, StreamTabletDrainInfoPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_replication::{
    is_bootstrap_required_response_pb::TableResult,
    wait_for_replication_drain_response_pb::UndrainedStreamInfoPB, BootstrapProducerRequestPB,
    BootstrapProducerResponsePB, IsBootstrapRequiredRequestPB, IsBootstrapRequiredResponsePB,
    WaitForReplicationDrainRequestPB, WaitForReplicationDrainResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::{eligible_tables, status, unknown, XCluster};

const CDC_SERVICE: &str = "yb.cdc.CDCService";

fn cdc_error(e: &yb_proto::pb::cdc_service::CDCErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
        e.status.message().to_string(),
    )
}

impl XCluster {
    fn tablet_leader(&self, tablet: &[u8]) -> Option<Vec<u8>> {
        self.ts_manager
            .locations
            .get(tablet)
            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
    }

    /// `BootstrapProducer`: new streams of the named tables, which a tserver creates and
    /// checkpoints at the end of the WAL.
    pub async fn bootstrap_producer_rpc(
        &self,
        req: &BootstrapProducerRequestPB,
        resp: &mut BootstrapProducerResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing BootstrapProducer request: {:?}", req);
        let invalid = |m: &str| unknown(status(StatusCode::InvalidArgument, m));
        if req.db_type() != YQLDatabase::YQL_DATABASE_PGSQL {
            return Err(invalid("Invalid database type"));
        }
        if req.namespace_name().is_empty() {
            return Err(invalid(
                "Missing required fields in request: namespace_name",
            ));
        }
        if req.table_name.is_empty() {
            return Err(invalid("No tables specified"));
        }
        if req.pg_schema_name.len() != req.table_name.len() {
            return Err(invalid(
                "Number of tables and number of pg schemas must match",
            ));
        }
        let snapshot = self.catalog.snapshot();
        let ns = master_table_lifecycle::lookup::find_namespace_by_name(
            &snapshot,
            req.namespace_name(),
            crate::setup::PGSQL,
        )
        .ok_or_else(|| {
            master_ts_manager::master_error_coded(
                Code::NAMESPACE_NOT_FOUND,
                StatusCode::NotFound,
                &format!("Keyspace name not found: {}", req.namespace_name()),
            )
        })?;
        let all = eligible_tables(&snapshot, &String::from_utf8_lossy(&ns.key.id), false)
            .map_err(unknown)?;
        let mut ts_req = TsBootstrapRequestPB::new();
        let mut leader = None;
        for (name, schema) in req.table_name.iter().zip(&req.pg_schema_name) {
            let d = all
                .iter()
                .find(|d| &d.name == name && &d.pgschema_name == schema)
                .ok_or_else(|| {
                    unknown(status(
                        StatusCode::NotFound,
                        format!(
                            "Table {}.{}{name} not found",
                            req.namespace_name(),
                            if schema.is_empty() {
                                String::new()
                            } else {
                                format!("{schema}.")
                            }
                        ),
                    ))
                })?;
            ts_req.table_ids.push(d.id.clone());
            resp.table_ids.push(d.id.clone());
            if leader.is_none() {
                let tablet = snapshot
                    .hosting_tablets_of(d.id.as_bytes())
                    .first()
                    .map(|t| t.key.id.to_vec());
                leader = tablet.and_then(|t| self.tablet_leader(&t));
            }
        }
        let leader = leader.ok_or_else(|| {
            unknown(status(
                StatusCode::IllegalState,
                "No valid tserver found to bootstrap from",
            ))
        })?;
        let ts_resp: TsBootstrapResponsePB = self
            .ts_call(&leader, CDC_SERVICE, "BootstrapProducer", &ts_req)
            .await
            .map_err(unknown)?;
        if let Some(e) = ts_resp.error.as_ref() {
            return Err(unknown(cdc_error(e)));
        }
        resp.bootstrap_ids = ts_resp.cdc_bootstrap_ids.clone();
        if ts_resp.has_bootstrap_time() {
            resp.set_bootstrap_time(ts_resp.bootstrap_time());
        }
        Ok(())
    }

    /// `IsBootstrapRequired`: per table, whether any tserver leading one of its tablets says a
    /// stream (the given one, or a new one) would miss data.
    pub async fn is_bootstrap_required_rpc(
        &self,
        req: &IsBootstrapRequiredRequestPB,
        resp: &mut IsBootstrapRequiredResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("IsBootstrapRequired: {:?}", req);
        if req.table_ids.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Table ID required",
            )));
        }
        if !req.stream_ids.is_empty() && req.stream_ids.len() != req.table_ids.len() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Stream ID optional, but must match table IDs if specified",
            )));
        }
        let snapshot = self.catalog.snapshot();
        for (i, table_id) in req.table_ids.iter().enumerate() {
            if snapshot
                .get(SysRowEntryType::TABLE, table_id.as_bytes())
                .is_none()
            {
                return Err(unknown(status(
                    StatusCode::NotFound,
                    format!("Table ID not found: {table_id}"),
                )));
            }
            let mut by_leader: BTreeMap<Vec<u8>, TsIsBootstrapRequiredRequestPB> = BTreeMap::new();
            for tablet in snapshot.tablets_of(table_id.as_bytes()) {
                let leader = self.tablet_leader(&tablet.key.id).ok_or_else(|| {
                    unknown(status(
                        StatusCode::NotFound,
                        format!(
                            "No leader found for tablet {}",
                            String::from_utf8_lossy(&tablet.key.id)
                        ),
                    ))
                })?;
                by_leader
                    .entry(leader)
                    .or_default()
                    .tablet_ids
                    .push(String::from_utf8_lossy(&tablet.key.id).into_owned());
            }
            let mut required = false;
            for (leader, mut ts_req) in by_leader {
                if let Some(stream) = req.stream_ids.get(i) {
                    ts_req.set_stream_id(stream.clone());
                }
                let ts_resp: TsIsBootstrapRequiredResponsePB = self
                    .ts_call(&leader, CDC_SERVICE, "IsBootstrapRequired", &ts_req)
                    .await
                    .map_err(unknown)?;
                if let Some(e) = ts_resp.error.as_ref() {
                    return Err(unknown(cdc_error(e)));
                }
                if ts_resp.bootstrap_required() {
                    required = true;
                    break;
                }
            }
            let mut r = TableResult::new();
            r.set_table_id(table_id.as_bytes().to_vec());
            r.set_bootstrap_required(required);
            resp.results.push(r);
        }
        Ok(())
    }

    /// `WaitForReplicationDrain`: until every tablet of the streams' tables has been read past
    /// `target_time`, or the deadline is near; the tablets still behind are listed.
    pub async fn wait_for_replication_drain(
        &self,
        deadline: Instant,
        req: &WaitForReplicationDrainRequestPB,
        resp: &mut WaitForReplicationDrainResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("WaitForReplicationDrain: {:?}", req);
        let invalid = |m: String| {
            master_ts_manager::master_error_coded(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                &m,
            )
        };
        if req.stream_ids.is_empty() {
            return Err(invalid(format!("No stream ID provided: {req:?}")));
        }
        let target = if req.has_target_time() {
            req.target_time()
        } else {
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_or(0, |d| d.as_micros() as u64)
        };
        let mut wanted = Vec::new();
        for s in &req.stream_ids {
            wanted.push(master_xrepl::parse_stream_id(s)?);
        }
        let rows = self.xrepl.streams();
        let missing: Vec<&String> = wanted
            .iter()
            .filter(|w| !rows.iter().any(|r| &r.id == *w))
            .collect();
        if !missing.is_empty() {
            return Err(invalid(format!(
                "Streams not found: {}",
                missing
                    .iter()
                    .map(|s| s.as_str())
                    .collect::<Vec<_>>()
                    .join(",")
            )));
        }
        let streams: Vec<_> = rows
            .into_iter()
            .filter(|r| wanted.contains(&r.id))
            .collect();
        let interval = (self.options.drain_retry_timeout)();
        let mut drained: HashSet<(String, Vec<u8>)> = HashSet::new();
        loop {
            let snapshot = self.catalog.snapshot();
            let mut undrained: HashSet<(String, Vec<u8>)> = HashSet::new();
            let mut by_leader: BTreeMap<Vec<u8>, CheckReplicationDrainRequestPB> = BTreeMap::new();
            for stream in &streams {
                for table in &stream.pb.table_id {
                    let stripped = strip_sequences_data_alias(table);
                    for tablet in snapshot.tablets_of(stripped.as_bytes()) {
                        let key = (stream.id.clone(), tablet.key.id.to_vec());
                        if drained.contains(&key) {
                            continue;
                        }
                        undrained.insert(key);
                        if let Some(leader) = self.tablet_leader(&tablet.key.id) {
                            let mut info = StreamTabletDrainInfoPB::new();
                            info.set_stream_id(stream.id.as_bytes().to_vec());
                            info.set_tablet_id(tablet.key.id.to_vec());
                            by_leader.entry(leader).or_default().stream_info.push(info);
                        }
                    }
                }
            }
            for (leader, mut ts_req) in by_leader {
                if deadline.saturating_duration_since(Instant::now()) <= interval {
                    break;
                }
                ts_req.set_target_time(target);
                match self
                    .ts_call::<_, CheckReplicationDrainResponsePB>(
                        &leader,
                        CDC_SERVICE,
                        "CheckReplicationDrain",
                        &ts_req,
                    )
                    .await
                {
                    Err(s) => {
                        tracing::warn!("CheckReplicationDrain responded with non-ok status: {s}")
                    }
                    Ok(r) if r.error.is_some() => {
                        tracing::warn!("CheckReplicationDrain responded with error: {:?}", r.error)
                    }
                    Ok(r) => {
                        for info in r.drained_stream_info {
                            let key = (
                                String::from_utf8_lossy(info.stream_id()).into_owned(),
                                info.tablet_id().to_vec(),
                            );
                            undrained.remove(&key);
                            drained.insert(key);
                        }
                    }
                }
            }
            if undrained.is_empty()
                || deadline.saturating_duration_since(Instant::now()) <= interval * 2
            {
                for (stream, tablet) in undrained {
                    let mut info = UndrainedStreamInfoPB::new();
                    info.set_stream_id(stream.into_bytes());
                    info.set_tablet_id(tablet);
                    resp.undrained_stream_info.push(info);
                }
                tracing::info!(
                    "WaitForReplicationDrain finished with {} undrained stream tablets",
                    resp.undrained_stream_info.len()
                );
                return Ok(());
            }
            tokio::time::sleep(interval.min(Duration::from_secs(60))).await;
        }
    }
}
