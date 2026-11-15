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

//! Stopping and starting persistence of databases (`XClusterStopPersisting`,
//! `XClusterStartPersisting`): the `PERSISTENCE_BLOCK_MAP` row says up to which hybrid time each
//! database's writes are blocked, and every tserver that may hold an xCluster-guarded lease gets
//! the new map before the call returns (`PropagateXClusterGuardedInfo`).

use std::time::Duration;

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use protobuf::Message;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb::State as NsState, SysNamespaceEntryPB, SysPersistenceBlockMapEntryPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_replication::{
    XClusterGetPersistenceBlockMapResponsePB, XClusterStartPersistingRequestPB,
    XClusterStopPersistingRequestPB,
};
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};
use yb_proto::pb::tserver_admin::{
    ApplyXClusterGuardedInfoIfNewerRequestPB, ApplyXClusterGuardedInfoIfNewerResponsePB,
};

use crate::{corrupt, status, unknown, XCluster};

/// `kSystemDatabaseNames` of `GetYsqlUserDatabaseOids`.
const SYSTEM_DATABASES: [&str; 5] = [
    "template0",
    "template1",
    "system_platform",
    "system_postgres",
    "yb_system",
];

impl XCluster {
    /// `GetYsqlUserDatabaseOids`: the oids of running user YSQL databases.
    fn user_database_oids(&self, namespace_ids: &[String]) -> Result<Vec<u32>, Status> {
        let snapshot = self.catalog.snapshot();
        let mut oids = Vec::new();
        for ns in namespace_ids {
            let pb: SysNamespaceEntryPB = snapshot
                .get(SysRowEntryType::NAMESPACE, ns.as_bytes())
                .ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Keyspace identifier not found: {ns}"),
                    )
                })?
                .parse()
                .map_err(corrupt)?;
            let name = String::from_utf8_lossy(pb.name()).into_owned();
            if pb.database_type() != YQLDatabase::YQL_DATABASE_PGSQL {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!("Namespace {name} [id={ns}] is not a YSQL database"),
                ));
            }
            if SYSTEM_DATABASES.contains(&name.as_str()) {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!("Database {name} [id={ns}] is a system database"),
                ));
            }
            if pb.state() != NsState::RUNNING {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!(
                        "Database {name} [id={ns}] is not running; its state is: {:?}",
                        pb.state()
                    ),
                ));
            }
            oids.push(
                master_table_lifecycle::pg_database_oid(ns.as_bytes()).ok_or_else(|| {
                    status(
                        StatusCode::InvalidArgument,
                        format!("Invalid namespace id {ns}"),
                    )
                })?,
            );
        }
        Ok(oids)
    }

    /// `PersistenceBlockMap::SetBlockedThrough`.
    async fn set_blocked_through(
        &self,
        token: &LeaderToken,
        oids: &[u32],
        ht: u64,
    ) -> Result<(), Status> {
        self.writer
            .mutate(token, |snapshot| {
                let entity = snapshot
                    .get(SysRowEntryType::PERSISTENCE_BLOCK_MAP, b"")
                    .ok_or_else(|| {
                        status(
                            StatusCode::IllegalState,
                            "Persistence block map is not initialized",
                        )
                    })?;
                let mut pb: SysPersistenceBlockMapEntryPB = entity.parse().map_err(corrupt)?;
                let map = &mut pb
                    .persistence_block_map
                    .mut_or_insert_default()
                    .blocked_through_ht;
                for oid in oids {
                    map.insert(*oid, ht);
                }
                pb.set_version(pb.version() + 1);
                Ok((
                    Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::PERSISTENCE_BLOCK_MAP,
                        b"",
                        &pb,
                    )?,
                    (),
                ))
            })
            .await
            .map(|_| ())
            .map_err(|s| {
                status(
                    s.code,
                    format!(
                        "updating persistence block map in sys-catalog: {}",
                        s.message
                    ),
                )
            })
    }

    /// `PropagateXClusterGuardedInfo`: each tserver that may hold a lease applies a copy of the
    /// guarded info made now, retried until `deadline`. A tserver that lost its lease meanwhile
    /// needs none: any lease it gets later comes with newer info.
    async fn propagate_guarded_info(
        &self,
        token: &LeaderToken,
        deadline: Instant,
    ) -> Result<(), Status> {
        if !(self.options.enforce_guarded_lease)() {
            return Ok(());
        }
        if !(self.options.persist_tserver_registry)() {
            return Err(status(
                StatusCode::IllegalState,
                "PropagateXClusterGuardedInfo requires the TServer registry to be persisted",
            ));
        }
        let snapshot = self.catalog.snapshot();
        let config = crate::setup::cluster_config(&snapshot)?;
        let info = self
            .ts_manager
            .xcluster_guarded_info(&snapshot, token.term(), &config)?;
        let mut req = ApplyXClusterGuardedInfoIfNewerRequestPB::new();
        req.xcluster_guarded_info = protobuf::MessageField::some(info);
        let bytes = req
            .write_to_bytes()
            .map_err(|e| status(StatusCode::InternalError, e.to_string()))?;
        let may_hold = |uuid: &[u8]| {
            self.ts_manager.get(uuid).is_some_and(|d| {
                d.pb.xcluster_guarded_lease_state()
                    != master_ts_manager::XClusterGuardedLeaseState::DEFINITELY_NO_LEASE
            })
        };
        let targets: Vec<Vec<u8>> = self
            .ts_manager
            .all()
            .into_iter()
            .map(|d| d.uuid.to_vec())
            .filter(|u| may_hold(u))
            .collect();
        let rpc = self.rpc()?;
        let mut tasks = tokio::task::JoinSet::new();
        for uuid in targets {
            let address = self.ts_manager.rpc_address(&uuid);
            let (rpc, bytes) = (rpc.clone(), bytes.clone());
            tasks.spawn(async move {
                let Some((host, port)) = address else {
                    return (
                        uuid,
                        Err(status(StatusCode::NotFound, "No address for the tserver")),
                    );
                };
                let mut last = status(StatusCode::TimedOut, "Propagation task never reported");
                while Instant::now() < deadline {
                    let left = deadline.saturating_duration_since(Instant::now());
                    match rpc
                        .call(
                            &host,
                            port,
                            "yb.tserver.TabletServerAdminService",
                            "ApplyXClusterGuardedInfoIfNewer",
                            &bytes,
                            left,
                        )
                        .await
                    {
                        Ok(out) => {
                            match ApplyXClusterGuardedInfoIfNewerResponsePB::parse_from_bytes(&out)
                            {
                                Ok(r) if r.error.is_none() => return (uuid, Ok(())),
                                Ok(r) => {
                                    last = status(
                                        StatusCode::RemoteError,
                                        r.error.status.message().to_string(),
                                    )
                                }
                                Err(e) => last = corrupt(e),
                            }
                        }
                        Err(s) => last = s,
                    }
                    tokio::time::sleep(Duration::from_millis(100).min(left)).await;
                }
                (uuid, Err(last))
            });
        }
        while let Some(joined) = tasks.join_next().await {
            let Ok((uuid, result)) = joined else {
                continue;
            };
            if let Err(s) = result {
                if !may_hold(&uuid) {
                    tracing::info!(
                        "Ignoring failure to propagate xCluster-guarded info to TServer {} \
                         because it no longer holds a lease: {s}",
                        String::from_utf8_lossy(&uuid)
                    );
                    continue;
                }
                return Err(status(
                    s.code,
                    format!(
                        "Failed to propagate xCluster-guarded info to TServer {}: {}",
                        String::from_utf8_lossy(&uuid),
                        s.message
                    ),
                ));
            }
        }
        Ok(())
    }

    /// `XClusterStopPersisting`: the databases' writes are blocked from now on.
    pub async fn stop_persisting(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &XClusterStopPersistingRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterStopPersisting: {:?}", req);
        let _serial = self.persisting.lock().await;
        let oids = self
            .user_database_oids(&req.namespace_ids)
            .map_err(unknown)?;
        self.set_blocked_through(token, &oids, u64::MAX)
            .await
            .map_err(unknown)?;
        tracing::info!("Stopped persisting for databases {:?}", req.namespace_ids);
        self.propagate_guarded_info(token, persisting_deadline(deadline))
            .await
            .map_err(unknown)
    }

    /// `XClusterStartPersisting`: writes that originated before a time past every clock now stay
    /// blocked; the call returns once the master's clock passed that time plus the clock skew, so
    /// work starting after it is not blocked.
    pub async fn start_persisting(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &XClusterStartPersistingRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("XClusterStartPersisting: {:?}", req);
        let _serial = self.persisting.lock().await;
        let oids = self
            .user_database_oids(&req.namespace_ids)
            .map_err(unknown)?;
        let skew = (self.options.max_clock_skew)().as_micros() as u64;
        let now = self.xrepl.host_now().map_err(unknown)?;
        let blocked_through = now + ((skew + 1) << 12);
        self.set_blocked_through(token, &oids, blocked_through)
            .await
            .map_err(unknown)?;
        tracing::info!(
            "Started persisting for databases {:?}, blocking origination times through \
             {blocked_through}",
            req.namespace_ids
        );
        let deadline = persisting_deadline(deadline);
        self.propagate_guarded_info(token, deadline)
            .await
            .map_err(unknown)?;
        let target = blocked_through + ((skew + 1) << 12);
        loop {
            let now = self.xrepl.host_now().map_err(unknown)?;
            if now >= target {
                return Ok(());
            }
            if Instant::now() >= deadline {
                return Err(unknown(status(
                    StatusCode::TimedOut,
                    "Timed out waiting for the clock to pass the blocked time",
                )));
            }
            let wait_micros = (target - now) >> 12;
            tokio::time::sleep(Duration::from_micros(wait_micros.max(1))).await;
        }
    }

    /// `XClusterGetPersistenceBlockMap`.
    pub fn get_persistence_block_map(
        &self,
        resp: &mut XClusterGetPersistenceBlockMapResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let pb: SysPersistenceBlockMapEntryPB = self
            .catalog
            .snapshot()
            .get(SysRowEntryType::PERSISTENCE_BLOCK_MAP, b"")
            .ok_or_else(|| {
                unknown(status(
                    StatusCode::IllegalState,
                    "Persistence block map is not initialized",
                ))
            })?
            .parse()
            .map_err(|e| unknown(corrupt(e)))?;
        resp.persistence_block_map = protobuf::MessageField::some(pb);
        Ok(())
    }
}

/// `ComputePersistingRpcDeadline`: a little before the caller's, so it gets the master's error
/// rather than its own timeout.
fn persisting_deadline(deadline: Instant) -> Instant {
    deadline
        .checked_sub(Duration::from_secs(2))
        .unwrap_or(deadline)
}
