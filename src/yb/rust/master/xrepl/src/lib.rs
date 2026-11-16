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
mod dynamic;
mod metadata;
mod removal;
mod split;
mod streams;
#[cfg(test)]
mod tests;
mod xcluster;

pub use controller::XreplController;
pub use create::CreatesTables;
pub use streams::{parse_stream_id, stream_rows, strip_sequences_data_alias, StreamRow};
pub use xcluster::xcluster_stream_options;

use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::sync::{Arc, Mutex, OnceLock};
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
    /// `cdc_create_stream_alter_table_dispatch_batch_size`: tables per batch of retention barrier
    /// alters; 0 or less sends one batch.
    pub alter_batch_size: fn() -> i32,
    /// `cdc_create_stream_alter_table_dispatch_delay_ms`: the pause after each batch.
    pub alter_batch_delay: fn() -> Duration,
    /// `TEST_running_test`: test sync points are consulted on the host.
    pub sync_points: fn() -> bool,
    /// The `cdcsdk_enable_dynamic_table_addition_with_table_cleanup` AutoFlag.
    pub table_cleanup: fn() -> bool,
    /// `cdcsdk_table_processing_limit_per_run`.
    pub table_processing_limit: fn() -> i32,
    /// `TEST_cdcsdk_skip_updating_cdc_state_entries_on_table_removal`.
    pub skip_state_entries_on_table_removal: fn() -> bool,
    /// `TEST_cdcsdk_skip_table_removal_from_qualified_list`.
    pub skip_table_removal_from_qualified_list: fn() -> bool,
    /// `TEST_cdcsdk_skip_processing_unqualified_tables`.
    pub skip_processing_unqualified_tables: fn() -> bool,
    /// `cdc_intent_retention_ms`: how long a tablet's barriers outlive its last activity.
    pub intent_retention: fn() -> Duration,
    /// `enable_table_rewrite_for_cdcsdk_table`: dropped tables of logical replication streams are
    /// kept hidden until the slots are past them.
    pub table_rewrite_retention: fn() -> bool,
    /// `TEST_cdc_add_dynamic_index_to_state_table`: every new tablet gets barriers and rows for
    /// every CDCSDK stream, eligible or not.
    pub add_dynamic_index_to_state_table: fn() -> bool,
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
            alter_batch_size: || 0,
            alter_batch_delay: || Duration::ZERO,
            sync_points: || false,
            table_cleanup: || true,
            table_processing_limit: || 2,
            skip_state_entries_on_table_removal: || false,
            skip_table_removal_from_qualified_list: || false,
            skip_processing_unqualified_tables: || false,
            intent_retention: || Duration::from_secs(8 * 3600),
            table_rewrite_retention: || true,
            add_dynamic_index_to_state_table: || false,
        }
    }
}

/// What master-xcluster adds to the hooks DDL calls through [`Xrepl`].
pub trait XreplExtension: Send + Sync {
    /// Work before a new table runs (`GetPostTabletCreateTasks`); false while it runs.
    fn post_tablet_create(
        &self,
        table_id: Vec<u8>,
    ) -> master_task_runtime::BoxFuture<'_, Result<bool, Status>>;

    /// The xCluster part of a new YSQL table's row.
    fn xcluster_create_table(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        req: &yb_proto::pb::master_ddl::CreateTableRequestPB,
        table: &mut yb_proto::pb::catalog_entity_info::SysTablesEntryPB,
    ) -> Result<(), Status>;

    /// A YSQL DDL on the table committed (`ClearXClusterFieldsAfterYsqlDDL`).
    fn xcluster_after_ddl(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        table_id: &[u8],
        table: &mut yb_proto::pb::catalog_entity_info::SysTablesEntryPB,
        mutation: master_catalog_state::Mutation,
    ) -> Result<master_catalog_state::Mutation, Status>;

    /// `IsNamespaceInAutomaticDDLMode`.
    fn is_automatic_ddl_mode(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        namespace_id: &[u8],
    ) -> bool;

    /// Whether a dropped table is hidden, not deleted, for xCluster streams.
    fn retains_dropped_table(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        table_id: &[u8],
    ) -> bool;

    /// A table's tablet split was registered (`HandleTabletSplit` of a consumer table).
    fn split_registered(
        &self,
        table_id: Vec<u8>,
        parent: Vec<u8>,
        children: Vec<Vec<u8>>,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>>;

    /// Whether a hidden tablet is still kept for xCluster streams.
    fn retains_hidden_tablet(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        tablet_id: &[u8],
    ) -> bool;
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
    /// `cdcsdk_ineligible_tables_to_streams_`: tables whose `cdc_state` rows of the streams are
    /// to be deleted.
    pub(crate) ineligible_tables: Mutex<BTreeMap<Vec<u8>, BTreeSet<String>>>,
    /// `namespace_to_cdcsdk_non_eligible_table_map_`: the tables to take out of streams that may
    /// not have them, found in the first pass of the term.
    #[allow(clippy::type_complexity)]
    pub(crate) non_eligible_tables: Mutex<(Option<i64>, BTreeMap<Vec<u8>, BTreeSet<String>>)>,
    /// Hidden split parents no stream needs any more, for the hidden-objects cleanup.
    pub(crate) released_split_parents: Mutex<HashSet<Vec<u8>>>,
    /// The host's CDC service serves GetChanges on the sys catalog tablet; enabled once, as C++
    /// never disables it.
    pub(crate) cdc_service_enabled: std::sync::atomic::AtomicBool,
    /// master-xcluster's part of the hooks.
    pub(crate) extension: OnceLock<Arc<dyn XreplExtension>>,
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
            ineligible_tables: Mutex::default(),
            released_split_parents: Mutex::default(),
            non_eligible_tables: Mutex::default(),
            cdc_service_enabled: Default::default(),
            extension: OnceLock::new(),
        })
    }

    /// master-xcluster's part of the hooks; set once, before any leader term.
    pub fn set_extension(&self, extension: Arc<dyn XreplExtension>) {
        let _ = self.extension.set(extension);
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
    pub fn host_now(&self) -> Result<u64, Status> {
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

    /// `TEST_CDCSDKFailCreateStreamRequestIfNeeded`: Aborted when a test's callback for the sync
    /// point asks for a failure.
    pub async fn fail_at(&self, sync_point: &'static str) -> Result<(), Status> {
        if !(self.options.sync_points)() {
            return Ok(());
        }
        let mut req = yb_proto::pb::master_rust_peer::TestSyncPointCallbackRequestPB::new();
        req.set_name(sync_point.to_string());
        let resp: yb_proto::pb::master_rust_peer::TestSyncPointCallbackResponsePB =
            self.host_call("TestSyncPointCallback", req).await?;
        if resp.fail() {
            return Err(Status::new(
                StatusCode::Aborted,
                format!("Test failure for sync point {sync_point}."),
            ));
        }
        Ok(())
    }

    /// `SetCDCServiceEnabled`: the master's CDC service, which logical replication polls the
    /// PG catalog tables through, serves once a CDCSDK stream exists.
    pub(crate) async fn enable_cdc_service(&self) {
        use std::sync::atomic::Ordering;
        if self.cdc_service_enabled.load(Ordering::Acquire) {
            return;
        }
        let req = yb_proto::pb::master_rust_peer::EnableCdcServiceRequestPB::new();
        match self
            .host_call::<_, yb_proto::pb::master_rust_peer::EnableCdcServiceResponsePB>(
                "EnableCDCService",
                req,
            )
            .await
        {
            Ok(_) => self.cdc_service_enabled.store(true, Ordering::Release),
            Err(s) => tracing::warn!("Enabling the master's CDC service failed: {s}"),
        }
    }

    /// A `TEST_SYNC_POINT_CALLBACK` whose test callback fails the step, as C++'s
    /// `SCHECK(!TEST_fail)` does.
    pub(crate) async fn fail_for_testing(&self, sync_point: &'static str) -> Result<(), Status> {
        match self.fail_at(sync_point).await {
            Err(s) if s.code == StatusCode::Aborted => {
                tracing::info!("Failing at {sync_point} for testing");
                Err(Status::new(StatusCode::IllegalState, "Failing for TESTING"))
            }
            other => other,
        }
    }

    /// A test sync point; a test's `LoadDependency` may hold the caller here.
    pub async fn sync_point(&self, sync_point: &'static str) {
        if let Err(s) = self.fail_at(sync_point).await {
            tracing::warn!("Sync point {sync_point}: {s}");
        }
    }

    /// A test sync point whose callback takes a `std::string*`, such as a tablet id.
    pub(crate) async fn sync_point_with(&self, sync_point: &'static str, arg: &[u8]) {
        if !(self.options.sync_points)() {
            return;
        }
        let mut req = yb_proto::pb::master_rust_peer::TestSyncPointCallbackRequestPB::new();
        req.set_name(sync_point.to_string());
        req.set_string_arg(arg.to_vec());
        if let Err(s) = self
            .host_call::<_, yb_proto::pb::master_rust_peer::TestSyncPointCallbackResponsePB>(
                "TestSyncPointCallback",
                req,
            )
            .await
        {
            tracing::warn!("Sync point {sync_point} failed: {s}");
        }
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
pub fn new_stream_id(rows: &[StreamRow]) -> String {
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
