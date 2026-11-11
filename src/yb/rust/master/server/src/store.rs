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

//! The host-facing traits of the core, over the bridge: the sys-catalog store, the apply sink, the
//! Raft leader state and the heartbeat parts the host owns.

use std::sync::{Arc, RwLock, RwLockReadGuard};
use std::time::Duration;

use cxx::UniquePtr;
use master_catalog_state::defaults::DefaultsHost;
use master_leadership::{LeaderStateSource, LeaderStatus, Leadership, RaftLeaderState};
use master_sys_catalog::{
    AppliedBatch, ApplyEvent, ApplySender, HybridTime, OpId, Row, RowChange, SysCatalogStore,
    WriteFuture,
};
use master_table_lifecycle::SchemaHost;
use master_ts_manager::{ContributorContext, HeartbeatContributor};
use protobuf::{Enum, Message};
use tokio::sync::oneshot;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::ffi::bridge::{MasterInfoHandle, RowChangeFfi, ScannedRow};

fn host_error(e: cxx::Exception) -> Status {
    Status::new(StatusCode::IllegalState, e.what())
}

/// Closed before the host is destroyed. Calls through the handles below hold it open, so one
/// still running on a thread the runtime's shutdown does not wait for (a blocking scan, the
/// self-check pool) finishes first, and later calls fail instead of reaching a freed host.
pub(crate) struct HostGate(RwLock<bool>);

impl HostGate {
    pub(crate) fn open() -> Arc<HostGate> {
        Arc::new(HostGate(RwLock::new(true)))
    }

    fn enter(&self) -> Option<RwLockReadGuard<'_, bool>> {
        let guard = self.0.read().unwrap_or_else(|e| e.into_inner());
        (*guard).then_some(guard)
    }

    /// Waits for calls in progress, then fails every later one.
    pub(crate) fn close(&self) {
        *self.0.write().unwrap_or_else(|e| e.into_inner()) = false;
    }
}

fn shut_down() -> Status {
    Status::new(StatusCode::Aborted, "The master host is shut down")
}

/// What the host's YCQL virtual tables show, from the leader's catalog (`VirtualTables` in
/// master_host.h).
pub(crate) struct VirtualTables {
    pub(crate) leader: Arc<master_rpc_handlers::LeaderServices>,
    pub(crate) info: Arc<dyn master_rpc_handlers::MasterInfo>,
}

impl VirtualTables {
    /// The serialized `YcqlVirtualTableInputPB` of the virtual table whose tablet is `tablet_id`,
    /// or nothing when it is not one. Fails when this master is not a ready leader.
    pub(crate) fn input(&self, tablet_id: &[u8]) -> Result<Vec<u8>, String> {
        let masters = self.info.committed_consensus_state().ok();
        match self
            .leader
            .ycql_virtual_table_input(tablet_id, masters.as_ref())
        {
            Ok(Some(input)) => input.write_to_bytes().map_err(|e| e.to_string()),
            Ok(None) => Ok(Vec::new()),
            Err(status) => Err(format!("{:?}: {}", status.code, status.message)),
        }
    }
}

/// The bridge's apply sink: forwards the tap's events to the apply stream.
pub(crate) struct ApplySink {
    pub(crate) sender: ApplySender,
    pub(crate) leadership: Arc<Leadership>,
}

impl ApplySink {
    pub(crate) fn on_applied_batch(
        &self,
        op_term: i64,
        op_index: i64,
        hybrid_time: u64,
        rows: Vec<RowChangeFfi>,
    ) {
        let changes = rows
            .into_iter()
            .map(|row| {
                let entry_type =
                    SysRowEntryType::from_i32(row.entry_type).unwrap_or(SysRowEntryType::UNKNOWN);
                if row.deleted {
                    RowChange::Delete {
                        entry_type,
                        id: row.id,
                    }
                } else {
                    RowChange::Upsert {
                        entry_type,
                        id: row.id,
                        data: row.data,
                    }
                }
            })
            .collect();
        self.sender.send(ApplyEvent::Rows(AppliedBatch {
            op_id: OpId {
                term: op_term,
                index: op_index,
            },
            hybrid_time: HybridTime(hybrid_time),
            changes,
        }));
    }

    pub(crate) fn on_reload(&self, reason: String, min_hybrid_time: u64) {
        self.sender.send(ApplyEvent::Reload {
            reason,
            min_time: HybridTime(min_hybrid_time),
        });
    }

    pub(crate) fn on_leadership_change(&self) {
        self.leadership.nudge();
    }
}

/// Carries a write's result back from the C++ completion callback.
pub(crate) struct WriteCompletion(oneshot::Sender<Result<(), Status>>);

// The Box is the bridge's ownership transfer from C++, so clippy's boxed_local does not apply.
#[allow(clippy::boxed_local)]
pub(crate) fn complete_write(completion: Box<WriteCompletion>, status_code: i32, message: String) {
    let result = if status_code == 0 {
        Ok(())
    } else {
        Err(Status::new(StatusCode::from_i32(status_code), message))
    };
    // The receiver is gone if the writer stopped waiting.
    let _ = completion.0.send(result);
}

/// [`SysCatalogStore`] over the host's sys-catalog tablet.
pub(crate) struct HostStore(
    pub(crate) UniquePtr<MasterInfoHandle>,
    pub(crate) Arc<HostGate>,
);

impl SysCatalogStore for HostStore {
    fn is_open(&self) -> bool {
        self.1.enter().is_some() && self.0.sys_catalog_is_open()
    }

    fn safe_time(&self, min_time: HybridTime, timeout: Duration) -> Result<HybridTime, Status> {
        let _open = self.1.enter().ok_or_else(shut_down)?;
        self.0
            .sys_catalog_safe_time(min_time.0, timeout.as_millis() as i64)
            .map(HybridTime)
            .map_err(host_error)
    }

    fn scan(&self, entry_type: SysRowEntryType, read_time: HybridTime) -> Result<Vec<Row>, Status> {
        let _open = self.1.enter().ok_or_else(shut_down)?;
        Ok(self
            .0
            .scan_sys_catalog(entry_type as i32, read_time.0)
            .map_err(host_error)?
            .into_iter()
            .map(|row| Row {
                id: row.id,
                data: row.data,
            })
            .collect())
    }

    fn write_rows(&self, term: i64, rows: Vec<RowChange>) -> WriteFuture {
        let rows: Vec<RowChangeFfi> = rows
            .into_iter()
            .map(|change| match change {
                RowChange::Upsert {
                    entry_type,
                    id,
                    data,
                } => RowChangeFfi {
                    entry_type: entry_type as i32,
                    id,
                    data,
                    deleted: false,
                },
                RowChange::Delete { entry_type, id } => RowChangeFfi {
                    entry_type: entry_type as i32,
                    id,
                    data: Vec::new(),
                    deleted: true,
                },
            })
            .collect();
        let Some(_open) = self.1.enter() else {
            return Box::pin(async { Err(shut_down()) });
        };
        let (sender, receiver) = oneshot::channel();
        self.0
            .write_sys_catalog_rows(term, &rows, Box::new(WriteCompletion(sender)));
        Box::pin(async move {
            receiver.await.unwrap_or_else(|_| {
                Err(Status::new(
                    StatusCode::Aborted,
                    "sys catalog write dropped without completing",
                ))
            })
        })
    }
}

/// [`LeaderStateSource`] over the host's consensus.
pub(crate) struct HostLeaderState(
    pub(crate) UniquePtr<MasterInfoHandle>,
    pub(crate) Arc<HostGate>,
);

impl LeaderStateSource for HostLeaderState {
    fn leader_state(&self) -> RaftLeaderState {
        let Some(_open) = self.1.enter() else {
            return RaftLeaderState {
                role: PeerRole::NON_PARTICIPANT,
                term: -1,
                status: LeaderStatus::NotLeader,
            };
        };
        let state = self.0.sys_catalog_leader_state();
        RaftLeaderState {
            role: PeerRole::from_i32(state.role).unwrap_or(PeerRole::UNKNOWN_ROLE),
            term: state.term,
            status: LeaderStatus::from_i32(state.leader_status),
        }
    }
}

/// The heartbeat fields the host's C++ objects own (L7): universe key registry, snapshots info,
/// the AutoFlags config, and the YSQL catalog versions and invalidation messages.
pub(crate) struct HostHeartbeatParts(
    pub(crate) UniquePtr<MasterInfoHandle>,
    pub(crate) Arc<HostGate>,
);

impl HeartbeatContributor for HostHeartbeatParts {
    fn contribute(
        &self,
        req: &TSHeartbeatRequestPB,
        ctx: &ContributorContext<'_>,
        resp: &mut TSHeartbeatResponsePB,
    ) -> Result<(), Status> {
        let encryption_info = match ctx.cluster_config.encryption_info.as_ref() {
            Some(info) => info
                .write_to_bytes()
                .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?,
            None => Vec::new(),
        };
        let auto_flags_version = if req.has_auto_flags_config_version() {
            req.auto_flags_config_version() as i64
        } else {
            -1
        };
        let _open = self.1.enter().ok_or_else(shut_down)?;
        let bytes = self
            .0
            .heartbeat_host_parts(
                &encryption_info,
                ctx.send_universe_key_registry,
                auto_flags_version,
                req.has_ysql_db_catalog_versions_fingerprint(),
                req.ysql_db_catalog_versions_fingerprint(),
            )
            .map_err(host_error)?;
        let parts = TSHeartbeatResponsePB::parse_from_bytes(&bytes)
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        if parts.universe_key_registry.is_some() {
            resp.universe_key_registry = parts.universe_key_registry;
        }
        resp.snapshots_info = parts.snapshots_info;
        if parts.auto_flags_config.is_some() {
            resp.auto_flags_config = parts.auto_flags_config;
        }
        if parts.db_catalog_version_data.is_some() {
            resp.db_catalog_version_data = parts.db_catalog_version_data;
        }
        if parts.db_catalog_inval_messages_data.is_some() {
            resp.db_catalog_inval_messages_data = parts.db_catalog_inval_messages_data;
        }
        Ok(())
    }
}

/// The host's C++ steps of table DDL (W7): schema normalization, partitions, alter steps.
pub(crate) struct HostSchema(
    pub(crate) UniquePtr<MasterInfoHandle>,
    pub(crate) Arc<HostGate>,
);

impl SchemaHost for HostSchema {
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status> {
        let _open = self.1.enter().ok_or_else(shut_down)?;
        self.0.handle_on_host(method, request).map_err(host_error)
    }
}

/// What only the host can build of a catalog's default rows (K10).
pub(crate) struct HostDefaults(
    pub(crate) UniquePtr<MasterInfoHandle>,
    pub(crate) Arc<HostGate>,
);

impl DefaultsHost for HostDefaults {
    fn system_table_rows(
        &self,
        tables: &[(Vec<u8>, Vec<u8>)],
        tables_with_tablets: &[String],
    ) -> Result<Vec<RowChange>, Status> {
        let tables: Vec<ScannedRow> = tables
            .iter()
            .map(|(id, data)| ScannedRow {
                id: id.clone(),
                data: data.clone(),
            })
            .collect();
        let _open = self.1.enter().ok_or_else(shut_down)?;
        let rows = self
            .0
            .system_table_rows(&tables, &tables_with_tablets.to_vec())
            .map_err(host_error)?;
        rows.into_iter()
            .map(|row| {
                let entry_type = SysRowEntryType::from_i32(row.entry_type).ok_or_else(|| {
                    Status::new(StatusCode::Corruption, "unknown entry type from the host")
                })?;
                Ok(if row.deleted {
                    RowChange::Delete {
                        entry_type,
                        id: row.id,
                    }
                } else {
                    RowChange::Upsert {
                        entry_type,
                        id: row.id,
                        data: row.data,
                    }
                })
            })
            .collect()
    }

    fn default_ycql_password_hash(&self) -> Result<Vec<u8>, Status> {
        let _open = self.1.enter().ok_or_else(shut_down)?;
        self.0.default_ycql_password_hash().map_err(host_error)
    }

    fn ysql_major_version(&self) -> u32 {
        self.0.ysql_major_version()
    }
}
