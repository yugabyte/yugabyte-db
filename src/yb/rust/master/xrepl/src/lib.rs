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

//! CDC streams on a Rust leader (`xrepl_catalog_manager.cc`): the `CDC_STREAM` rows xCluster and
//! CDCSDK share. C++ keeps maps of streams by table and by replication slot; here they are
//! derived from the catalog snapshot when asked.

mod cdcsdk;
mod controller;
mod create;
mod delete;
mod metadata;
mod streams;
#[cfg(test)]
mod tests;

pub use controller::XreplController;
pub use create::CreatesTables;
pub use streams::{stream_rows, StreamRow};

use std::sync::{Arc, OnceLock};
use std::time::Duration;

use master_catalog_state::{Catalog, CatalogWriter};
use master_leadership::Leadership;
use master_table_lifecycle::SchemaHost;
use master_task_runtime::{RetryPolicy, TsRpcExecutor};
use master_ts_manager::TsManager;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB};

// The stream options of `cdc_types.h`.
/// The option naming what the stream's ids are.
pub const ID_TYPE: &str = "id_type";
/// `id_type` of a stream of tables (xCluster).
pub const TABLE_ID: &str = "TABLEID";
/// `id_type` of a stream of a namespace (CDCSDK).
pub const NAMESPACE_ID: &str = "NAMESPACEID";
/// The option of a CDCSDK stream's record type.
pub const RECORD_TYPE: &str = "record_type";
/// The option of the stream's client (xCluster or CDCSDK).
pub const SOURCE_TYPE: &str = "source_type";
/// The pseudo-option a stream's state is listed as.
pub const STREAM_STATE: &str = "state";
/// `kYbNotificationsSlotPrefix`: the slots of the internal LISTEN/NOTIFY streams.
pub const NOTIFICATIONS_SLOT_PREFIX: &str = "yb_notifications_";
/// `kYbSystemDbName`.
pub const YB_SYSTEM_DB_NAME: &str = "yb_system";

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct XreplOptions {
    /// `cdcsdk_enable_dynamic_tables_disable_option`.
    pub dynamic_tables_disable_option: fn() -> bool,
    /// `ysql_yb_allow_replication_slot_lsn_types`.
    pub allow_lsn_types: fn() -> bool,
    /// `ysql_yb_allow_replication_slot_ordering_modes`.
    pub allow_ordering_modes: fn() -> bool,
    /// `TEST_xcluster_fail_setup_stream_update`.
    pub fail_setup_stream_update: fn() -> bool,
    /// `catalog_manager_bg_task_wait_ms`.
    pub bg_task_interval: fn() -> Duration,
    /// `TEST_cdcsdk_disable_deleted_stream_cleanup`.
    pub disable_deleted_stream_cleanup: fn() -> bool,
    /// `TEST_cdcsdk_disable_drop_table_cleanup`.
    pub disable_drop_table_cleanup: fn() -> bool,
    /// `ysql_yb_enable_replication_commands`.
    pub replication_commands: fn() -> bool,
    /// `ysql_yb_cdcsdk_stream_tables_without_primary_key`.
    pub tables_without_primary_key: fn() -> bool,
    /// `ysql_yb_enable_implicit_dynamic_tables_logical_replication`.
    pub implicit_dynamic_tables: fn() -> bool,
    /// `cdc_enable_dynamic_schema_changes`.
    pub dynamic_schema_changes: fn() -> bool,
    /// `TEST_ysql_yb_enable_replication_slot_transactional_ddl`.
    pub replication_slot_transactional_ddl: fn() -> bool,
    /// `max_replication_slots`.
    pub max_replication_slots: fn() -> u32,
    /// `yb_enable_cdc_consistent_snapshot_streams`.
    pub consistent_snapshot_streams: fn() -> bool,
    /// The `cdc_pg_create_grpc_stream` AutoFlag.
    pub pg_create_grpc_stream: fn() -> bool,
    /// `ysql_yb_enable_replica_identity`.
    pub replica_identity: fn() -> bool,
    /// The `cdc_enable_postgres_replica_identity` AutoFlag.
    pub postgres_replica_identity: fn() -> bool,
    /// `cdc_wal_retention_time_secs`.
    pub wal_retention_secs: fn() -> u32,
    /// `ysql_yb_enable_replication_slot_consumption`.
    pub replication_slot_consumption: fn() -> bool,
    /// `TEST_disable_cdc_state_insert_on_setup`.
    pub disable_cdc_state_insert_on_setup: fn() -> bool,
    /// `TEST_cdcsdk_add_indexes_to_stream`.
    pub add_indexes_to_stream: fn() -> bool,
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
}

impl Default for XreplOptions {
    fn default() -> Self {
        XreplOptions {
            dynamic_tables_disable_option: || true,
            allow_lsn_types: || true,
            allow_ordering_modes: || true,
            fail_setup_stream_update: || false,
            bg_task_interval: || Duration::from_secs(1),
            disable_deleted_stream_cleanup: || false,
            disable_drop_table_cleanup: || false,
            replication_commands: || true,
            tables_without_primary_key: || false,
            implicit_dynamic_tables: || false,
            dynamic_schema_changes: || true,
            replication_slot_transactional_ddl: || false,
            max_replication_slots: || 10,
            consistent_snapshot_streams: || true,
            pg_create_grpc_stream: || false,
            replica_identity: || true,
            postgres_replica_identity: || true,
            wal_retention_secs: || 4 * 3600,
            replication_slot_consumption: || true,
            disable_cdc_state_insert_on_setup: || false,
            add_indexes_to_stream: || false,
            rpc_timeout: || Duration::from_secs(30),
        }
    }
}

/// The CDC stream service of the leader.
pub struct Xrepl {
    pub(crate) catalog: Arc<Catalog>,
    pub(crate) writer: Arc<CatalogWriter>,
    pub(crate) leadership: Arc<Leadership>,
    pub(crate) ts_manager: Arc<TsManager>,
    /// The host's C++ steps (the `cdc_state` table, the clock).
    pub(crate) host: Arc<dyn SchemaHost>,
    pub(crate) rpc: OnceLock<Arc<dyn RpcClient>>,
    pub(crate) executor: Arc<TsRpcExecutor>,
    pub(crate) retry: RetryPolicy,
    pub(crate) options: XreplOptions,
}

impl Xrepl {
    /// The service of this master.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        host: Arc<dyn SchemaHost>,
        executor: Arc<TsRpcExecutor>,
        retry: RetryPolicy,
        options: XreplOptions,
    ) -> Arc<Xrepl> {
        Arc::new(Xrepl {
            catalog,
            writer,
            leadership,
            ts_manager,
            host,
            rpc: OnceLock::new(),
            executor,
            retry,
            options,
        })
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    pub(crate) fn rpc(&self) -> Result<Arc<dyn RpcClient>, Status> {
        self.rpc
            .get()
            .cloned()
            .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No RPC client yet"))
    }

    /// `Clock::Now` of the host.
    pub(crate) fn host_now(&self) -> Result<u64, Status> {
        self.hybrid_time_now(false)
    }

    /// `Clock::MaxGlobalNow` of the host.
    pub(crate) async fn max_global_now(&self) -> Result<u64, Status> {
        let host = self.host.clone();
        tokio::task::spawn_blocking(move || hybrid_time_now(&*host, true))
            .await
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?
    }

    fn hybrid_time_now(&self, max_global: bool) -> Result<u64, Status> {
        hybrid_time_now(&*self.host, max_global)
    }
}

fn hybrid_time_now(host: &dyn SchemaHost, max_global: bool) -> Result<u64, Status> {
    let mut req = yb_proto::pb::master_rust_peer::HybridTimeNowRequestPB::new();
    req.set_max_global(max_global);
    let bytes = protobuf::Message::write_to_bytes(&req)
        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
    let out = host.call("HybridTimeNow", &bytes)?;
    let resp: yb_proto::pb::master_rust_peer::HybridTimeNowResponsePB =
        protobuf::Message::parse_from_bytes(&out)
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
    Ok(resp.hybrid_time())
}

/// `GenerateNewXreplStreamId`: a random id no stream row has, as `Uuid::ToHexString` writes it.
pub(crate) fn new_stream_id(rows: &[StreamRow]) -> String {
    loop {
        let mut b: [u8; 16] = rand::random();
        // Version 4, variant 1, as boost's random_generator.
        b[6] = (b[6] & 0x0f) | 0x40;
        b[8] = (b[8] & 0x3f) | 0x80;
        let id: String = b.iter().map(|x| format!("{x:02x}")).collect();
        if !rows.iter().any(|r| r.id == id) {
            return id;
        }
    }
}

pub(crate) fn error(code: Code, status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    master_ts_manager::master_error_coded(code, status, &message.into())
}

/// The `error` of a host response, which `HandleRequest` sets from the C++ step's status.
pub(crate) fn host_error(resp: &dyn protobuf::MessageDyn) -> Option<MasterErrorPB> {
    let field = resp.descriptor_dyn().field_by_name("error")?;
    let value = field.get_singular(resp)?;
    let message = value.to_message()?;
    let message: &dyn protobuf::MessageDyn = &*message;
    message.downcast_ref::<MasterErrorPB>().cloned()
}
