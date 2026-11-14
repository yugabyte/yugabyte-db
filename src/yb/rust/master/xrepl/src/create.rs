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

//! `CreateCDCStream` of a CDCSDK stream (`CreateNewCDCStreamForNamespace`,
//! `CreateNewCdcsdkStream`): the stream row, `cdc_state` rows for its tablets, and retention
//! barriers on them, set by `AlterSchema` (`ChangeMetadata`) to every tablet's leader. With a
//! consistent snapshot option, each tablet answers with the op id the snapshot is safe from, and
//! the stream gets one snapshot time for all of them.

use std::collections::HashSet;
use std::sync::Arc;
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use master_table_lifecycle::TableLifecycle;
use master_task_runtime::{Attempt, BoxFuture};
use protobuf::{EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, sys_tables_entry_pb, SysCDCStreamEntryPB,
    SysTablesEntryPB,
};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::common_types::{
    CDCSDKDynamicTablesOption, CDCSDKSnapshotOption, PgReplicaIdentity,
};
use yb_proto::pb::master_ddl::{CreateTableRequestPB, CreateTableResponsePB};
use yb_proto::pb::master_replication::{CreateCDCStreamRequestPB, CreateCDCStreamResponsePB};
use yb_proto::pb::master_rust_peer::{
    CdcStateCreateTableRequestRequestPB, CdcStateCreateTableRequestResponsePB, CdcStateEntryPB,
    CdcStateReadRequestPB, CdcStateReadResponsePB, CdcStateUpsertRequestPB,
    CdcStateUpsertResponsePB, SetCatalogRetentionBarrierRequestPB,
    SetCatalogRetentionBarrierResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::operations::ChangeMetadataRequestPB;
use yb_proto::pb::opid::OpIdPB;
use yb_proto::pb::tserver_admin::ChangeMetadataResponsePB;

use crate::streams::{stream_rows, StreamRow};
use crate::{
    error, Xrepl, ID_TYPE, NAMESPACE_ID, NOTIFICATIONS_SLOT_PREFIX, RECORD_TYPE, SOURCE_TYPE,
    YB_SYSTEM_DB_NAME,
};

/// What creates `system.cdc_state`: the leader's table lifecycle.
pub trait CreatesTables: Send + Sync {
    /// `CreateTable`.
    fn create<'a>(
        &'a self,
        token: &'a LeaderToken,
        deadline: Instant,
        req: &'a CreateTableRequestPB,
        resp: &'a mut CreateTableResponsePB,
    ) -> BoxFuture<'a, Result<(), MasterErrorPB>>;
}

impl CreatesTables for TableLifecycle {
    fn create<'a>(
        &'a self,
        token: &'a LeaderToken,
        deadline: Instant,
        req: &'a CreateTableRequestPB,
        resp: &'a mut CreateTableResponsePB,
    ) -> BoxFuture<'a, Result<(), MasterErrorPB>> {
        Box::pin(self.create_table(token, deadline, req, resp))
    }
}

/// `kYbGrpcStreamIndicator`: the plugin name of a gRPC stream's slot.
const GRPC_STREAM_INDICATOR: &str = "yb_grpc";
/// `kCDCSDKSlotEntryTabletId`: the `cdc_state` row of a replication slot.
const SLOT_ENTRY_TABLET_ID: &str = "dummy_id_for_replication_slot";
/// `kPgYbNotificationsTableName`.
const PG_YB_NOTIFICATIONS_TABLE: &str = "pg_yb_notifications";
/// `kSysCatalogTabletId`.
const SYS_CATALOG_TABLET_ID: &[u8] = b"00000000000000000000000000000000";
/// `kSystemNamespaceId`.
const SYSTEM_NAMESPACE_ID: &[u8] = b"00000000000000000000000000000001";
/// `kPgSequencesDataTableId`.
const SEQUENCES_DATA_TABLE_ID: &[u8] = b"0000ffff00003000800000000000ffff";
/// `PgSystemAttrNum::kYBRowId`: the hidden key column of a table without a primary key.
const YB_ROW_ID_ATTNUM: i32 = -100;
// PG catalog tables a logical replication stream polls (`IsCatalogTableEligibleForCDC`).
const PG_CLASS_OID: u32 = 1259;
const PG_ATTRIBUTE_OID: u32 = 1249;
const PG_PUBLICATION_REL_OID: u32 = 6106;
const PG_REPLICATION_ORIGIN_OID: u32 = 6000;
const PG_PUBLICATION_OID: u32 = 6104;
const TEMPLATE1_OID: u32 = 1;

/// `GetPgsqlTableId`: a catalog table (OID below `kPgFirstNormalObjectId`) carries the current PG
/// version in byte 9 (`kPgCurrentUuidVersion`, 1 for PG15).
fn pgsql_table_id(database_oid: u32, table_oid: u32) -> Vec<u8> {
    let version = if table_oid < 16384 { 1 } else { 0 };
    format!("{database_oid:08x}00003000800{version}0000{table_oid:08x}").into_bytes()
}

/// The option values a request names, with C++'s defaults.
struct RequestOptions {
    id_type: String,
    source_type: String,
    record_type: String,
}

fn request_options(req: &CreateCDCStreamRequestPB) -> RequestOptions {
    let mut out = RequestOptions {
        id_type: crate::TABLE_ID.to_string(),
        source_type: "XCLUSTER".to_string(),
        record_type: String::new(),
    };
    for o in &req.options {
        let value = String::from_utf8_lossy(o.value()).into_owned();
        match o.key() {
            ID_TYPE => out.id_type = value,
            SOURCE_TYPE => out.source_type = value,
            RECORD_TYPE => out.record_type = value,
            _ => {}
        }
    }
    out
}

fn invalid_request(message: impl Into<String>) -> MasterErrorPB {
    error(Code::INVALID_REQUEST, StatusCode::InvalidArgument, message)
}

/// `IsNotificationSlotCreationRequest` and `IsLogicalReplicationStreamCreationRequest`.
fn is_logical_replication_request(req: &CreateCDCStreamRequestPB, namespace_name: &str) -> bool {
    let notification = namespace_name == YB_SYSTEM_DB_NAME
        && req
            .cdcsdk_ysql_replication_slot_name()
            .starts_with(NOTIFICATIONS_SLOT_PREFIX);
    notification
        || (req.has_cdcsdk_ysql_replication_slot_plugin_name()
            && !req.cdcsdk_ysql_replication_slot_plugin_name().is_empty()
            && req.cdcsdk_ysql_replication_slot_plugin_name() != GRPC_STREAM_INDICATOR)
}

/// `IsCdcLogicalReplicationStream`.
fn is_logical_replication_stream(snapshot: &CatalogSnapshot, row: &StreamRow) -> bool {
    if row.pb.namespace_id().is_empty() {
        return false;
    }
    let notification = row
        .pb
        .cdcsdk_ysql_replication_slot_name()
        .starts_with(NOTIFICATIONS_SLOT_PREFIX)
        && snapshot
            .get(SysRowEntryType::NAMESPACE, row.pb.namespace_id().as_bytes())
            .and_then(|n| n.namespace().map(|h| h.name == YB_SYSTEM_DB_NAME))
            .unwrap_or(false);
    notification
        || (!row.pb.cdcsdk_ysql_replication_slot_plugin_name().is_empty()
            && row.pb.cdcsdk_ysql_replication_slot_plugin_name() != GRPC_STREAM_INDICATOR)
}

/// A table row with what eligibility asks of it.
struct TableView<'a> {
    entity: &'a Arc<Entity>,
    pb: SysTablesEntryPB,
    namespace_name: String,
}

impl TableView<'_> {
    fn id(&self) -> &[u8] {
        &self.entity.key.id
    }

    /// `TableInfo::is_system`: the PG catalog and the YCQL system tables.
    fn is_system(&self, snapshot: &CatalogSnapshot) -> bool {
        master_table_lifecycle::lookup::is_system_table(snapshot, self.entity)
    }

    /// `TableInfo::IsUserCreated`.
    fn is_user_created(&self, snapshot: &CatalogSnapshot) -> bool {
        matches!(
            self.pb.table_type(),
            TableType::PGSQL_TABLE_TYPE | TableType::YQL_TABLE_TYPE
        ) && !self.is_system(snapshot)
            && self.id() != SEQUENCES_DATA_TABLE_ID
            && self.pb.namespace_id() != SYSTEM_NAMESPACE_ID
            && !master_table_lifecycle::lookup::is_colocation_parent_table_id(self.id())
            && self.namespace_name != YB_SYSTEM_DB_NAME
    }

    fn is_user_table(&self, snapshot: &CatalogSnapshot) -> bool {
        self.is_user_created(snapshot) && self.pb.indexed_table_id().is_empty()
    }

    fn is_user_index(&self, snapshot: &CatalogSnapshot) -> bool {
        self.is_user_created(snapshot) && !self.pb.indexed_table_id().is_empty()
    }

    /// `HasUserSpecifiedPrimaryKey`: no ybrowid column.
    fn has_user_specified_primary_key(&self) -> bool {
        !self
            .pb
            .schema
            .columns
            .iter()
            .any(|c| c.order() == YB_ROW_ID_ATTNUM)
    }

    /// `IsXClusterDDLReplicationTable`.
    fn is_xcluster_ddl_replication_table(&self) -> bool {
        self.pb.schema.DEPRECATED_pgschema_name() == "yb_xcluster_ddl_replication"
            && matches!(self.pb.name(), b"ddl_queue" | b"replicated_ddls")
    }
}

fn table_view<'a>(snapshot: &CatalogSnapshot, entity: &'a Arc<Entity>) -> Option<TableView<'a>> {
    let pb: SysTablesEntryPB = entity.parse().ok()?;
    let namespace_name = snapshot
        .get(SysRowEntryType::NAMESPACE, pb.namespace_id())
        .and_then(|n| n.namespace().map(|h| h.name.clone()))
        .unwrap_or_default();
    Some(TableView {
        entity,
        pb,
        namespace_name,
    })
}

impl Xrepl {
    /// `IsTableEligibleForCDCSDKStream`.
    fn eligible_for_cdcsdk(
        &self,
        snapshot: &CatalogSnapshot,
        table: &TableView<'_>,
        check_schema: bool,
        allow_tables_without_primary_key: bool,
        allow_catalog_tables: bool,
    ) -> bool {
        if check_schema {
            if !allow_tables_without_primary_key && !table.has_user_specified_primary_key() {
                return false;
            }
            if (self.options.add_indexes_to_stream)() && table.is_user_index(snapshot) {
                return true;
            }
        }
        if table.pb.is_matview() || table.is_xcluster_ddl_replication_table() {
            return false;
        }
        if allow_catalog_tables && !table.is_user_table(snapshot) {
            let eligible_catalog = master_table_lifecycle::pg_table_oid(table.id())
                .is_some_and(|oid| self.is_catalog_table_eligible(oid));
            if eligible_catalog {
                return true;
            }
        }
        table.is_user_table(snapshot)
            || (table.namespace_name == YB_SYSTEM_DB_NAME
                && table.pb.name() == PG_YB_NOTIFICATIONS_TABLE.as_bytes())
    }

    /// `IsTableEligibleForCDCSDKStream` of a table row, with its schema checked.
    pub(crate) fn eligible_table(
        &self,
        snapshot: &CatalogSnapshot,
        entity: &Arc<Entity>,
        allow_tables_without_primary_key: bool,
        allow_catalog_tables: bool,
    ) -> bool {
        table_view(snapshot, entity).is_some_and(|t| {
            self.eligible_for_cdcsdk(
                snapshot,
                &t,
                true,
                allow_tables_without_primary_key,
                allow_catalog_tables,
            )
        })
    }

    fn is_catalog_table_eligible(&self, oid: u32) -> bool {
        matches!(
            oid,
            PG_CLASS_OID | PG_PUBLICATION_REL_OID | PG_REPLICATION_ORIGIN_OID | PG_PUBLICATION_OID
        ) || ((self.options.replication_slot_transactional_ddl)() && oid == PG_ATTRIBUTE_OID)
    }

    /// `FindAllTablesForCDCSDK`.
    fn tables_for_cdcsdk(
        &self,
        snapshot: &CatalogSnapshot,
        namespace_id: &[u8],
        allow_tables_without_primary_key: bool,
    ) -> Vec<Vec<u8>> {
        snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| master_table_lifecycle::lookup::is_visible_to_client(t))
            .filter_map(|t| table_view(snapshot, t))
            .filter(|t| t.pb.namespace_id() == namespace_id)
            .filter(|t| {
                self.eligible_for_cdcsdk(snapshot, t, true, allow_tables_without_primary_key, false)
            })
            .map(|t| t.id().to_vec())
            .collect()
    }

    /// `ValidateCDCSDKRequestProperties`.
    fn validate_cdcsdk_request(
        &self,
        req: &CreateCDCStreamRequestPB,
        options: &RequestOptions,
    ) -> Result<(), MasterErrorPB> {
        if options.source_type != "CDCSDK" {
            return Err(invalid_request(
                "Namespace CDC stream is only supported for CDCSDK",
            ));
        }
        if options.id_type != NAMESPACE_ID {
            return Err(invalid_request(
                "Invalid id_type in options. Expected to be NAMESPACEID for all CDCSDK streams",
            ));
        }
        if !(self.options.replication_commands)() && req.has_cdcsdk_ysql_replication_slot_name() {
            return Err(invalid_request(
                "Creation of CDCSDK stream with a replication slot name is disallowed",
            ));
        }
        let create = req.cdcsdk_stream_create_options.as_ref();
        if create.is_some_and(|c| c.bound_table_ids.is_some())
            && req.has_cdcsdk_ysql_replication_slot_name()
        {
            return Err(invalid_request(
                "CDC streams bound to specific tables cannot be created with a replication slot",
            ));
        }
        if !(self.options.allow_lsn_types)() && create.is_some_and(|c| c.has_lsn_type()) {
            return Err(invalid_request(
                "Creation of CDCSDK stream with a replication slot having LSN type is disallowed \
                 because the flag ysql_yb_allow_replication_slot_lsn_types is disabled",
            ));
        }
        if !(self.options.allow_ordering_modes)() && create.is_some_and(|c| c.has_ordering_mode()) {
            return Err(invalid_request(
                "Creation of CDCSDK stream with a replication slot having ordering mode is \
                 disallowed because the flag ysql_yb_allow_replication_slot_ordering_modes is \
                 disabled",
            ));
        }
        if !(self.options.pg_create_grpc_stream)()
            && req.cdcsdk_ysql_replication_slot_plugin_name() == GRPC_STREAM_INDICATOR
        {
            return Err(invalid_request(
                "Upgrade is not yet finalised on a build that supports the yb_grpc plugin; \
                 complete upgrade finalization and retry",
            ));
        }
        if (self.options.replica_identity)() && req.has_cdcsdk_ysql_replication_slot_name() {
            return Ok(());
        }
        match options.record_type.as_str() {
            "PG_FULL" | "PG_CHANGE_OLD_NEW" | "PG_DEFAULT" | "PG_NOTHING" => {
                if !(self.options.postgres_replica_identity)() {
                    return Err(error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::InvalidArgument,
                        "Using new record types is disallowed in the middle of an upgrade. \
                         Finalize the upgrade and try again.",
                    ));
                }
                Ok(())
            }
            "ALL" | "FULL_ROW_NEW_IMAGE" | "MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES" | "CHANGE" => {
                Ok(())
            }
            other => Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                format!("Invalid CDCRecordType value: {other}"),
            )),
        }
    }

    /// `CreateCDCStream`.
    pub async fn create_cdc_stream(
        &self,
        tables: &dyn CreatesTables,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateCDCStreamRequestPB,
        resp: &mut CreateCDCStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("CreateCDCStream: {:?}", req);
        if !req.has_table_id() && !req.has_namespace_id() {
            return Err(invalid_request(
                "One of table_id or namespace_id must be provided",
            ));
        }
        let options = request_options(req);
        if options.source_type == "XCLUSTER"
            || (req.has_table_id() && options.id_type != NAMESPACE_ID)
        {
            return Err(master_ts_manager::master_error_tagged(
                Code::INVALID_REQUEST,
                StatusCode::NotSupported,
                "Creating an xCluster stream is not served by a Rust master leader yet",
            ));
        }
        self.validate_cdcsdk_request(req, &options)?;
        self.create_namespace_stream(tables, token, deadline, req, resp)
            .await
    }

    /// `CreateNewCDCStreamForNamespace`.
    async fn create_namespace_stream(
        &self,
        tables: &dyn CreatesTables,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateCDCStreamRequestPB,
        resp: &mut CreateCDCStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        // Without replication commands the namespace is in table_id, as before them.
        let namespace_id = if (self.options.replication_commands)() {
            req.namespace_id().to_string()
        } else {
            req.table_id().to_string()
        };
        let snapshot = self.catalog.snapshot();
        let namespace = snapshot
            .get(SysRowEntryType::NAMESPACE, namespace_id.as_bytes())
            .and_then(|n| n.namespace().cloned())
            .ok_or_else(|| {
                error(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Keyspace identifier not found: {namespace_id}"),
                )
            })?;
        if namespace.name == YB_SYSTEM_DB_NAME
            && !req
                .cdcsdk_ysql_replication_slot_name()
                .starts_with(NOTIFICATIONS_SLOT_PREFIX)
        {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "CDC streams are not supported for yb_system database",
            ));
        }
        let logical = is_logical_replication_request(req, &namespace.name);
        let allow_without_pk = (self.options.tables_without_primary_key)() && logical;
        let mut table_ids = match req
            .cdcsdk_stream_create_options
            .as_ref()
            .and_then(|c| c.bound_table_ids.as_ref())
        {
            Some(bound) => {
                let mut ids = Vec::new();
                for tid in &bound.table_ids {
                    let entity = snapshot
                        .get(SysRowEntryType::TABLE, tid.as_bytes())
                        .ok_or_else(|| {
                            error(
                                Code::UNKNOWN_ERROR,
                                StatusCode::NotFound,
                                format!(
                                    "Bound table id {tid} not found in namespace {namespace_id}"
                                ),
                            )
                        })?;
                    if !master_table_lifecycle::lookup::is_visible_to_client(entity) {
                        return Err(error(
                            Code::UNKNOWN_ERROR,
                            StatusCode::NotFound,
                            format!("Bound table id {tid} is not visible to the client"),
                        ));
                    }
                    let view = table_view(&snapshot, entity).ok_or_else(|| {
                        error(Code::UNKNOWN_ERROR, StatusCode::Corruption, "bad table row")
                    })?;
                    if view.pb.namespace_id() != namespace_id.as_bytes() {
                        return Err(error(
                            Code::UNKNOWN_ERROR,
                            StatusCode::InvalidArgument,
                            format!(
                                "Bound table id {tid} does not belong to namespace {namespace_id}"
                            ),
                        ));
                    }
                    if !self.eligible_for_cdcsdk(&snapshot, &view, true, allow_without_pk, false) {
                        return Err(error(
                            Code::UNKNOWN_ERROR,
                            StatusCode::InvalidArgument,
                            format!("Bound table id {tid} is not eligible for CDC"),
                        ));
                    }
                    ids.push(tid.as_bytes().to_vec());
                }
                if ids.is_empty() {
                    return Err(error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::InvalidArgument,
                        "No eligible tables found in the list provided to create a CDC stream",
                    ));
                }
                ids
            }
            None => self.tables_for_cdcsdk(&snapshot, namespace_id.as_bytes(), allow_without_pk),
        };
        // A logical replication stream polls these catalog tables for publication, replication
        // origin and schema changes.
        if (self.options.implicit_dynamic_tables)()
            && (self.options.dynamic_schema_changes)()
            && logical
        {
            let db_oid = master_table_lifecycle::pg_database_oid(namespace_id.as_bytes())
                .ok_or_else(|| invalid_request(format!("Not a YSQL database: {namespace_id}")))?;
            table_ids.push(pgsql_table_id(db_oid, PG_CLASS_OID));
            if (self.options.replication_slot_transactional_ddl)() {
                table_ids.push(pgsql_table_id(db_oid, PG_ATTRIBUTE_OID));
            }
            table_ids.push(pgsql_table_id(db_oid, PG_PUBLICATION_REL_OID));
            table_ids.push(pgsql_table_id(TEMPLATE1_OID, PG_REPLICATION_ORIGIN_OID));
            table_ids.push(pgsql_table_id(db_oid, PG_PUBLICATION_OID));
        }
        tracing::debug!("Creating CDCSDK stream for {} tables", table_ids.len());
        self.create_cdcsdk_stream(
            tables,
            token,
            deadline,
            req,
            &table_ids,
            &namespace_id,
            &namespace.name,
            resp,
        )
        .await
    }

    /// `CreateNewCdcsdkStream`. A failure after the row is written marks the stream DELETING,
    /// which removes whatever `cdc_state` rows it got (`RollbackFailedCreateCDCSDKStream`).
    #[allow(clippy::too_many_arguments)]
    async fn create_cdcsdk_stream(
        &self,
        tables: &dyn CreatesTables,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateCDCStreamRequestPB,
        table_ids: &[Vec<u8>],
        namespace_id: &str,
        namespace_name: &str,
        resp: &mut CreateCDCStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let start = std::time::Instant::now();
        self.create_cdc_state_table_if_not_found(tables, token, deadline)
            .await?;
        let logical = is_logical_replication_request(req, namespace_name);
        let snapshot = self.catalog.snapshot();
        let rows = stream_rows(&snapshot);
        let mut slot_name = req.cdcsdk_ysql_replication_slot_name().to_string();
        if req.has_cdcsdk_ysql_replication_slot_name() {
            let live_slots: HashSet<&str> = rows
                .iter()
                .filter(|r| {
                    !r.is_deleting() && !r.pb.cdcsdk_ysql_replication_slot_name().is_empty()
                })
                .map(|r| r.pb.cdcsdk_ysql_replication_slot_name())
                .collect();
            if live_slots.contains(slot_name.as_str()) {
                return Err(master_ts_manager::master_error_tagged(
                    Code::OBJECT_ALREADY_PRESENT,
                    StatusCode::AlreadyPresent,
                    "CDC stream with the given replication slot name already exists",
                ));
            }
            if live_slots.len() as u64 >= u64::from((self.options.max_replication_slots)()) {
                return Err(master_ts_manager::master_error_tagged(
                    Code::REPLICATION_SLOT_LIMIT_REACHED,
                    StatusCode::ReplicationSlotLimitReached,
                    "Replication slot limit reached",
                ));
            }
        }
        // A namespace has logical replication streams or gRPC streams, not both.
        for row in rows.iter().filter(|r| r.pb.namespace_id() == namespace_id) {
            let existing_logical = is_logical_replication_stream(&snapshot, row);
            if logical != existing_logical {
                return Err(error(
                    Code::INVALID_REQUEST,
                    StatusCode::IllegalState,
                    format!(
                        "namespace {namespace_id} already has a {} stream on it",
                        if existing_logical {
                            "logical_replication"
                        } else {
                            "gRPC"
                        }
                    ),
                ));
            }
        }
        let consistent_option = req.has_cdcsdk_consistent_snapshot_option()
            && (self.options.consistent_snapshot_streams)();
        let consistent_use = req.has_cdcsdk_consistent_snapshot_option()
            && matches!(
                req.cdcsdk_consistent_snapshot_option(),
                CDCSDKSnapshotOption::USE_SNAPSHOT | CDCSDKSnapshotOption::EXPORT_SNAPSHOT
            );
        let create = req.cdcsdk_stream_create_options.as_ref();
        let disable_dynamic_tables = create.is_some_and(|c| {
            c.bound_table_ids.is_some()
                || (c.has_cdcsdk_dynamic_tables_option()
                    && c.cdcsdk_dynamic_tables_option()
                        == CDCSDKDynamicTablesOption::DYNAMIC_TABLES_DISABLED)
        });
        let stream_id = crate::new_stream_id(&rows);
        let grpc_slots = (self.options.pg_create_grpc_stream)();
        let require_slot_name = grpc_slots || req.has_cdcsdk_ysql_replication_slot_name();
        if grpc_slots && !req.has_cdcsdk_ysql_replication_slot_name() {
            slot_name = format!("grpc_{stream_id}");
        }
        let require_plugin_name = grpc_slots || req.has_cdcsdk_ysql_replication_slot_plugin_name();
        let require_slot_entry = (logical && consistent_option) || (!logical && grpc_slots);
        let require_replica_identity_map = req.has_cdcsdk_ysql_replication_slot_plugin_name();
        let use_replica_identity =
            (self.options.replica_identity)() && require_replica_identity_map;

        let mut pb = SysCDCStreamEntryPB::new();
        pb.set_namespace_id(namespace_id.to_string());
        let mut history_for_replica_identity = false;
        for table_id in table_ids {
            pb.table_id
                .push(String::from_utf8_lossy(table_id).into_owned());
            if use_replica_identity {
                let entity = snapshot
                    .get(SysRowEntryType::TABLE, table_id)
                    .ok_or_else(|| table_not_found(table_id))?;
                let view =
                    table_view(&snapshot, entity).ok_or_else(|| table_not_found(table_id))?;
                // Catalog tables use CHANGE.
                let identity = if view.is_system(&snapshot) {
                    PgReplicaIdentity::CHANGE
                } else {
                    view.pb.schema.table_properties.ysql_replica_identity()
                };
                history_for_replica_identity |= !matches!(
                    identity,
                    PgReplicaIdentity::CHANGE | PgReplicaIdentity::NOTHING
                );
                pb.replica_identity_map.insert(
                    String::from_utf8_lossy(table_id).into_owned(),
                    EnumOrUnknown::new(identity),
                );
            }
        }
        pb.set_transactional(req.transactional());
        for option in &req.options {
            if use_replica_identity && option.key() == RECORD_TYPE {
                continue;
            }
            pb.options.push(option.clone());
        }
        let state = if req.has_initial_state() {
            req.initial_state()
        } else if consistent_option {
            StreamState::INITIATED
        } else {
            StreamState::ACTIVE
        };
        pb.state = Some(EnumOrUnknown::new(state));
        if require_slot_name {
            pb.set_cdcsdk_ysql_replication_slot_name(slot_name.clone());
        }
        if require_plugin_name {
            pb.set_cdcsdk_ysql_replication_slot_plugin_name(
                if req.has_cdcsdk_ysql_replication_slot_plugin_name() {
                    req.cdcsdk_ysql_replication_slot_plugin_name().to_string()
                } else {
                    GRPC_STREAM_INDICATOR.to_string()
                },
            );
        }
        if logical {
            pb.set_allow_tables_without_primary_key((self.options.tables_without_primary_key)());
            pb.set_detect_publication_changes_implicitly(
                (self.options.implicit_dynamic_tables)() && (self.options.dynamic_schema_changes)(),
            );
        }
        pb.set_cdcsdk_disable_dynamic_table_addition(disable_dynamic_tables);
        if let Some(c) = create.filter(|_| req.has_cdcsdk_ysql_replication_slot_name()) {
            if (self.options.allow_lsn_types)() {
                pb.cdcsdk_ysql_replication_slot_lsn_type = c.lsn_type;
            }
            if (self.options.allow_ordering_modes)() {
                pb.cdcsdk_ysql_replication_slot_ordering_mode = c.ordering_mode;
            }
        }
        // The row, unless a concurrent create took the slot.
        let written = self
            .writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    if require_slot_name
                        && stream_rows(snapshot).iter().any(|r| {
                            !r.is_deleting()
                                && r.pb.cdcsdk_ysql_replication_slot_name() == slot_name
                        })
                    {
                        return Err(Status::new(
                            StatusCode::AlreadyPresent,
                            "CDC stream with the given replication slot name already exists",
                        ));
                    }
                    let mutation = Mutation::new()
                        .read_absent(SysRowEntryType::CDC_STREAM, stream_id.as_bytes())
                        .upsert_message(SysRowEntryType::CDC_STREAM, stream_id.as_bytes(), &pb)?;
                    Ok((mutation, ()))
                },
            )
            .await;
        if let Err(s) = written {
            return Err(if s.code == StatusCode::AlreadyPresent {
                master_ts_manager::master_error_tagged(
                    Code::OBJECT_ALREADY_PRESENT,
                    s.code,
                    &s.message,
                )
            } else {
                error(Code::UNKNOWN_ERROR, s.code, s.message)
            });
        }
        resp.set_stream_id(stream_id.clone().into_bytes());
        tracing::info!("Created CDC stream {stream_id}");
        // A bootstrap (initial state other than ACTIVE) leaves cdc_state to BootstrapProducer.
        if (self.options.disable_cdc_state_insert_on_setup)()
            || (req.has_initial_state() && req.initial_state() != StreamState::ACTIVE)
        {
            return Ok(());
        }
        let history_for_record_type = !use_replica_identity
            && req
                .options
                .iter()
                .filter(|o| o.key() == RECORD_TYPE)
                .next_back()
                .is_some_and(|o| !matches!(o.value(), b"CHANGE" | b"PG_NOTHING"));
        let finished = self
            .finish_cdcsdk_stream(
                token,
                deadline,
                req,
                &stream_id,
                table_ids,
                consistent_option,
                consistent_use,
                consistent_use || history_for_record_type || history_for_replica_identity,
                require_slot_entry,
                resp,
            )
            .await;
        if let Err(e) = finished {
            tracing::warn!("Rolling back the CDC stream creation for stream {stream_id}: {e:?}");
            if let Err(s) = self
                .drop_streams(
                    token,
                    deadline,
                    std::slice::from_ref(&stream_id),
                    StreamState::DELETING,
                )
                .await
            {
                tracing::warn!("Failed to cleanup failed CDC stream {stream_id}: {s}");
            }
            return Err(e);
        }
        tracing::info!(
            "Stream {stream_id} creation took {}ms",
            start.elapsed().as_millis()
        );
        Ok(())
    }

    /// Steps 1 to 3 of `CreateNewCdcsdkStream` once the row exists.
    #[allow(clippy::too_many_arguments)]
    async fn finish_cdcsdk_stream(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateCDCStreamRequestPB,
        stream_id: &str,
        table_ids: &[Vec<u8>],
        consistent_option: bool,
        consistent_use: bool,
        require_history_cutoff: bool,
        require_slot_entry: bool,
        resp: &mut CreateCDCStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let e = |s: Status| error(Code::UNKNOWN_ERROR, s.code, s.message);
        // Step 1: invalid checkpoints, which a rollback finds.
        self.populate_cdc_state(stream_id, table_ids, false, false, 0, 0, false)
            .await
            .map_err(e)?;
        // Step 2: retention barriers; with the consistent snapshot option they also write each
        // tablet's snapshot safe op id.
        self.set_retention_barriers(
            token,
            deadline,
            stream_id,
            table_ids,
            consistent_option,
            require_history_cutoff,
        )
        .await
        .map_err(e)?;
        // Step 3: one consistent snapshot time for every tablet.
        let creation_time = now_micros();
        let mut snapshot_time = 0;
        if consistent_option {
            snapshot_time = self.max_global_now().await.map_err(e)?;
            tracing::info!("Consistent Snapshot Time for stream {stream_id} is: {snapshot_time}");
            resp.set_cdcsdk_consistent_snapshot_time(snapshot_time);
            let option = req.cdcsdk_consistent_snapshot_option;
            let id = stream_id.to_string();
            self.writer
                .mutate_with(
                    token,
                    WriteOptions {
                        deadline: Some(deadline),
                        ..Default::default()
                    },
                    |snapshot| {
                        let entity = snapshot
                            .get(SysRowEntryType::CDC_STREAM, id.as_bytes())
                            .ok_or_else(|| Status::new(StatusCode::NotFound, "stream is gone"))?;
                        let mut pb: SysCDCStreamEntryPB = entity
                            .parse()
                            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                        let meta = pb.cdcsdk_stream_metadata.mut_or_insert_default();
                        meta.set_snapshot_time(snapshot_time);
                        meta.consistent_snapshot_option = option;
                        pb.set_stream_creation_time(creation_time);
                        pb.state = Some(EnumOrUnknown::new(StreamState::ACTIVE));
                        Ok((
                            Mutation::new().read(entity).upsert_message(
                                SysRowEntryType::CDC_STREAM,
                                id.as_bytes(),
                                &pb,
                            )?,
                            (),
                        ))
                    },
                )
                .await
                .map_err(e)?;
        }
        self.populate_cdc_state(
            stream_id,
            table_ids,
            consistent_option,
            consistent_use,
            snapshot_time,
            creation_time,
            require_slot_entry,
        )
        .await
        .map_err(e)
    }

    /// `CreateCdcStateTableIfNotFound`: `system.cdc_state`, created by the first stream.
    async fn create_cdc_state_table_if_not_found(
        &self,
        tables: &dyn CreatesTables,
        token: &LeaderToken,
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let exists = snapshot.iter(SysRowEntryType::TABLE).any(|t| {
            t.table().is_some_and(|h| {
                h.namespace_id.as_slice() == SYSTEM_NAMESPACE_ID && h.name == "cdc_state"
            }) && master_table_lifecycle::lookup::is_visible_to_client(t)
        });
        if exists {
            return Ok(());
        }
        let resp: CdcStateCreateTableRequestResponsePB = self
            .host_call(
                "CdcStateCreateTableRequest",
                CdcStateCreateTableRequestRequestPB::new(),
            )
            .await
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        let req: CreateTableRequestPB = resp.request.unwrap_or_default();
        let mut create = CreateTableResponsePB::new();
        match tables.create(token, deadline, &req, &mut create).await {
            Ok(()) => Ok(()),
            Err(e) if e.code() == Code::OBJECT_ALREADY_PRESENT => Ok(()),
            Err(e) => Err(e),
        }
    }

    /// `PopulateCDCStateTable`.
    #[allow(clippy::too_many_arguments)]
    async fn populate_cdc_state(
        &self,
        stream_id: &str,
        table_ids: &[Vec<u8>],
        consistent_option: bool,
        consistent_use: bool,
        snapshot_time: u64,
        creation_time: u64,
        create_slot_entry: bool,
    ) -> Result<(), Status> {
        // Every tablet's retention barrier must have written its checkpoint; a tablet missing
        // here was split since, and the client retries the creation.
        let mut seen = HashSet::new();
        if consistent_option {
            let mut read = CdcStateReadRequestPB::new();
            read.set_stream_id(stream_id.as_bytes().to_vec());
            let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
            for row in rows.entries {
                if !row.checkpoint.is_some() || is_invalid(&row.checkpoint) {
                    return Err(Status::new(
                        StatusCode::IllegalState,
                        format!(
                            "Checkpoint for tablet id {} unexpectedly found Invalid for stream \
                             id {stream_id}",
                            String::from_utf8_lossy(row.tablet_id())
                        ),
                    ));
                }
                seen.insert(row.tablet_id().to_vec());
            }
        }
        let snapshot = self.catalog.snapshot();
        let mut entries = Vec::new();
        for table_id in table_ids {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                return Err(Status::new(
                    StatusCode::NotFound,
                    format!(
                        "Table with identifier {} not found",
                        String::from_utf8_lossy(table_id)
                    ),
                ));
            };
            let colocated = table
                .parse::<SysTablesEntryPB>()
                .is_ok_and(|t| t.colocated());
            for tablet in snapshot.hosting_tablets_of(table_id) {
                let tablet_id = tablet.key.id.to_vec();
                let mut entry = CdcStateEntryPB::new();
                entry.set_tablet_id(tablet_id.clone());
                entry.set_stream_id(stream_id.as_bytes().to_vec());
                if consistent_option {
                    if !seen.contains(&tablet_id) {
                        return Err(Status::new(
                            StatusCode::IllegalState,
                            format!(
                                "CDC State Table entry unexpectedly not found for tablet id {}",
                                String::from_utf8_lossy(&tablet_id)
                            ),
                        ));
                    }
                    if consistent_use {
                        entry.set_snapshot_key(Vec::new());
                    }
                    entry.set_active_time(creation_time);
                    entry.set_cdc_sdk_safe_time(snapshot_time);
                } else {
                    entry.checkpoint = MessageField::some(invalid_op_id());
                    entry.set_active_time(0);
                    entry.set_cdc_sdk_safe_time(0);
                }
                entries.push(entry);
                if consistent_option && colocated {
                    let mut col = CdcStateEntryPB::new();
                    col.set_tablet_id(tablet_id);
                    col.set_stream_id(stream_id.as_bytes().to_vec());
                    col.set_colocated_table_id(table_id.clone());
                    if consistent_use {
                        col.set_snapshot_key(Vec::new());
                    }
                    col.set_active_time(now_micros());
                    col.set_cdc_sdk_safe_time(snapshot_time);
                    entries.push(col);
                }
            }
        }
        if (self.options.replication_slot_consumption)() && create_slot_entry {
            // Streaming starts from the snapshot time, or from now without a snapshot.
            let commit_time = if consistent_option {
                snapshot_time
            } else {
                self.max_global_now().await?
            };
            entries.push(slot_entry(stream_id, commit_time));
        }
        let mut upsert = CdcStateUpsertRequestPB::new();
        upsert.entries = entries;
        let _: CdcStateUpsertResponsePB = self.host_call("CdcStateUpsert", upsert).await?;
        Ok(())
    }

    /// `SetAllCDCSDKRetentionBarriers`: the tables' WAL retention, and each tablet's leader asked
    /// to keep history for the stream; catalog tables, on the sys catalog tablet, through the
    /// host.
    async fn set_retention_barriers(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        stream_id: &str,
        table_ids: &[Vec<u8>],
        consistent_option: bool,
        require_history_cutoff: bool,
    ) -> Result<(), Status> {
        let retention = (self.options.wal_retention_secs)();
        let ids: Vec<Vec<u8>> = table_ids.to_vec();
        // `AlterTable` with only wal_retention_secs: the row, without a new schema version.
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let mut mutation = Mutation::new();
                    for id in &ids {
                        let table = snapshot.get(SysRowEntryType::TABLE, id).ok_or_else(|| {
                            Status::new(
                                StatusCode::NotFound,
                                format!("Table does not exist: {}", String::from_utf8_lossy(id)),
                            )
                        })?;
                        let mut pb: SysTablesEntryPB = table
                            .parse()
                            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                        if matches!(
                            pb.state(),
                            sys_tables_entry_pb::State::DELETING
                                | sys_tables_entry_pb::State::DELETED
                        ) {
                            return Err(Status::new(
                                StatusCode::NotFound,
                                format!("Table does not exist: {}", String::from_utf8_lossy(id)),
                            ));
                        }
                        if pb.wal_retention_secs() != retention {
                            pb.set_wal_retention_secs(retention);
                            mutation = mutation.read(table).upsert_message(
                                SysRowEntryType::TABLE,
                                id,
                                &pb,
                            )?;
                        }
                    }
                    Ok((mutation, ()))
                },
            )
            .await?;
        let snapshot = self.catalog.snapshot();
        let mut sends = Vec::new();
        for table_id in table_ids {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                continue;
            };
            let pb: SysTablesEntryPB = table
                .parse()
                .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
            let tablets = snapshot.hosting_tablets_of(table_id);
            let on_sys_catalog = tablets.len() == 1 && &*tablets[0].key.id == SYS_CATALOG_TABLET_ID;
            if on_sys_catalog {
                // `SetAllInitialCDCSDKRetentionBarriersOnCatalogTable`, only in this context.
                if consistent_option
                    && (self.options.implicit_dynamic_tables)()
                    && (self.options.dynamic_schema_changes)()
                {
                    let mut req = SetCatalogRetentionBarrierRequestPB::new();
                    req.set_table_id(table_id.clone());
                    req.set_stream_id(stream_id.as_bytes().to_vec());
                    req.set_schema_version(pb.version());
                    req.schema = pb.schema.clone();
                    if pb.has_wal_retention_secs() {
                        req.set_wal_retention_secs(pb.wal_retention_secs());
                    }
                    let _: SetCatalogRetentionBarrierResponsePB =
                        self.host_call("SetCatalogRetentionBarrier", req).await?;
                }
                continue;
            }
            for tablet in tablets {
                sends.push(self.alter_tablet(
                    *token,
                    deadline,
                    table_id.clone(),
                    pb.clone(),
                    tablet.key.id.to_vec(),
                    consistent_option.then(|| stream_id.to_string()),
                    require_history_cutoff,
                ));
            }
        }
        let mut first_error = None;
        for result in join_all(sends).await {
            if let Err(s) = result {
                first_error.get_or_insert(s);
            }
        }
        if let Some(s) = first_error {
            return Err(Status::new(
                StatusCode::InternalError,
                format!(
                    "Unable to set retention barriers for table, error: {}",
                    s.message
                ),
            ));
        }
        if consistent_option {
            self.wait_for_snapshot_safe_op_ids(stream_id, table_ids, deadline)
                .await?;
        }
        Ok(())
    }

    /// One `AsyncAlterTable` of a create-stream context: on the response, the tablet's snapshot
    /// safe op id becomes its `cdc_state` row (`PopulateCDCStateTableWithCDCSDKSnapshotSafeOpId
    /// Details`).
    #[allow(clippy::too_many_arguments)]
    async fn alter_tablet(
        &self,
        token: LeaderToken,
        deadline: Instant,
        table_id: Vec<u8>,
        table: SysTablesEntryPB,
        tablet_id: Vec<u8>,
        stream_id: Option<String>,
        require_history_cutoff: bool,
    ) -> Result<(), Status> {
        let mut req = ChangeMetadataRequestPB::new();
        req.set_schema_version(table.version());
        req.set_tablet_id(tablet_id.clone());
        req.set_alter_table_id(table_id.clone());
        if table.has_wal_retention_secs() {
            req.set_wal_retention_secs(table.wal_retention_secs());
        }
        if let Some(id) = &stream_id {
            req.set_retention_requester_id(id.as_bytes().to_vec());
            req.set_cdc_sdk_require_history_cutoff(require_history_cutoff);
        }
        req.schema = table.schema.clone();
        req.set_new_table_name(String::from_utf8_lossy(table.name()).into_owned());
        req.indexes = table.indexes.clone();
        req.set_first_provisional_column_id(table.next_column_id());
        let rpc = self.rpc()?;
        let rpc_timeout = (self.options.rpc_timeout)();
        let response = std::sync::Mutex::new(None::<ChangeMetadataResponsePB>);
        let leader_of = |tablet: &[u8]| {
            self.ts_manager
                .locations
                .get(tablet)
                .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
        };
        let lane = leader_of(&tablet_id).unwrap_or_default();
        self.executor
            .run(&lane, "AlterSchema", &self.retry, |_| {
                let current = self.leadership.check(&token);
                let leader = leader_of(&tablet_id);
                let address = leader.as_ref().and_then(|l| self.ts_manager.rpc_address(l));
                let now = self.host_now();
                let (rpc, mut req, response) = (rpc.clone(), req.clone(), &response);
                async move {
                    current.map_err(Attempt::Fail)?;
                    if Instant::now() >= deadline {
                        return Err(Attempt::Fail(Status::new(
                            StatusCode::TimedOut,
                            "AlterSchema passed its deadline",
                        )));
                    }
                    let (Some(leader), Some((host, port))) = (leader, address) else {
                        return Err(Attempt::Retry(Status::new(
                            StatusCode::ServiceUnavailable,
                            "No leader for the tablet",
                        )));
                    };
                    req.set_dest_uuid(leader);
                    if let Ok(now) = now {
                        req.set_propagated_hybrid_time(now);
                    }
                    let bytes = req.write_to_bytes().map_err(|e| {
                        Attempt::Fail(Status::new(StatusCode::InternalError, e.to_string()))
                    })?;
                    let out = rpc
                        .call(
                            &host,
                            port,
                            "yb.tserver.TabletServerAdminService",
                            "AlterSchema",
                            &bytes,
                            rpc_timeout,
                        )
                        .await
                        .map_err(Attempt::Retry)?;
                    let resp = ChangeMetadataResponsePB::parse_from_bytes(&out).map_err(|e| {
                        Attempt::Retry(Status::new(StatusCode::Corruption, e.to_string()))
                    })?;
                    if let Some(e) = resp.error.as_ref() {
                        use yb_proto::pb::tserver_types::tablet_server_error_pb::Code as TsCode;
                        // `AsyncAlterTable::HandleResponse`: these end the task.
                        if matches!(
                            e.code(),
                            TsCode::TABLET_NOT_FOUND
                                | TsCode::MISMATCHED_SCHEMA
                                | TsCode::TABLET_HAS_A_NEWER_SCHEMA
                        ) {
                            return Ok(());
                        }
                        return Err(Attempt::Retry(Status::new(
                            StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                            e.status.message().to_string(),
                        )));
                    }
                    *response.lock().unwrap() = Some(resp);
                    Ok(())
                }
            })
            .await?;
        let Some(stream_id) = stream_id else {
            return Ok(());
        };
        let resp = response.into_inner().unwrap();
        let Some(resp) = resp
            .filter(|r| r.cdc_sdk_snapshot_safe_op_id.is_some() && r.has_propagated_hybrid_time())
        else {
            tracing::warn!(
                "Response not as expected. Not inserting any rows into cdc_state table for \
                 stream_id: {stream_id} and tablet id: {}",
                String::from_utf8_lossy(&tablet_id)
            );
            return Ok(());
        };
        let proposed = resp.propagated_hybrid_time();
        let safe = resp.cdc_sdk_snapshot_safe_op_id.clone().unwrap_or_default();
        let physical_micros = proposed >> 12;
        let entry = |colocated: Option<&[u8]>| {
            let mut e = CdcStateEntryPB::new();
            e.set_tablet_id(tablet_id.clone());
            e.set_stream_id(stream_id.as_bytes().to_vec());
            if let Some(t) = colocated {
                e.set_colocated_table_id(t.to_vec());
            }
            e.checkpoint = MessageField::some(safe.clone());
            e.set_cdc_sdk_safe_time(proposed);
            if require_history_cutoff {
                e.set_snapshot_key(Vec::new());
            }
            e.set_active_time(now_micros());
            e.set_last_replication_time(physical_micros);
            e
        };
        let mut upsert = CdcStateUpsertRequestPB::new();
        upsert.entries.push(entry(None));
        if table.colocated() {
            upsert.entries.push(entry(Some(&table_id)));
        }
        if let Err(s) = self
            .host_call::<_, CdcStateUpsertResponsePB>("CdcStateUpsert", upsert)
            .await
        {
            tracing::warn!(
                "Populating the cdc_state table for stream {stream_id} and tablet {} failed: {s}",
                String::from_utf8_lossy(&tablet_id)
            );
        }
        Ok(())
    }

    /// `WaitForSnapshotSafeOpIdToBePopulated`: every tablet's row has its checkpoint (the catalog
    /// tables' one tablet counted once).
    async fn wait_for_snapshot_safe_op_ids(
        &self,
        stream_id: &str,
        table_ids: &[Vec<u8>],
        deadline: Instant,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let mut expected = 0;
        let mut sys_catalog_seen = false;
        for table_id in table_ids {
            let tablets = snapshot.hosting_tablets_of(table_id);
            let on_sys_catalog = tablets.len() == 1 && &*tablets[0].key.id == SYS_CATALOG_TABLET_ID;
            if on_sys_catalog {
                if !sys_catalog_seen {
                    expected += 1;
                    sys_catalog_seen = true;
                }
            } else {
                expected += tablets.len();
            }
        }
        loop {
            let mut read = CdcStateReadRequestPB::new();
            read.set_stream_id(stream_id.as_bytes().to_vec());
            let rows: CdcStateReadResponsePB = self.host_call("CdcStateRead", read).await?;
            let all_valid = rows
                .entries
                .iter()
                .all(|r| r.checkpoint.is_some() && !is_invalid(&r.checkpoint));
            if all_valid && rows.entries.len() >= expected {
                return Ok(());
            }
            if Instant::now() + Duration::from_millis(500) >= deadline {
                return Err(Status::new(
                    StatusCode::TimedOut,
                    format!(
                        "Waiting for snapshot safe opids to be populated for stream_id: \
                         {stream_id}"
                    ),
                ));
            }
            tokio::time::sleep(Duration::from_millis(500)).await;
        }
    }
}

fn table_not_found(table_id: &[u8]) -> MasterErrorPB {
    error(
        Code::OBJECT_NOT_FOUND,
        StatusCode::NotFound,
        format!(
            "Table with identifier {} not found",
            String::from_utf8_lossy(table_id)
        ),
    )
}

/// `OpId::Invalid()`.
fn invalid_op_id() -> OpIdPB {
    let mut op = OpIdPB::new();
    op.set_term(-1);
    op.set_index(-1);
    op
}

fn is_invalid(op: &MessageField<OpIdPB>) -> bool {
    op.as_ref()
        .is_some_and(|o| o.term() == -1 && o.index() == -1)
}

/// `MakeSlotEntry`: the slot's consistent point; the first streamed record gets LSN and txn id 2.
fn slot_entry(stream_id: &str, commit_time: u64) -> CdcStateEntryPB {
    let mut e = CdcStateEntryPB::new();
    e.set_tablet_id(SLOT_ENTRY_TABLET_ID.as_bytes().to_vec());
    e.set_stream_id(stream_id.as_bytes().to_vec());
    e.set_confirmed_flush_lsn(2);
    e.set_restart_lsn(1);
    e.set_xmin(1);
    e.set_record_id_commit_time(commit_time);
    e.set_cdc_sdk_safe_time(commit_time);
    e.set_last_pub_refresh_time(commit_time);
    e.set_pub_refresh_times(Vec::new());
    e.set_last_decided_pub_refresh_time(format!("{commit_time}F").into_bytes());
    e
}

fn now_micros() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_micros() as u64)
}

async fn join_all<F: std::future::Future>(futures: Vec<F>) -> Vec<F::Output> {
    let mut pinned: Vec<_> = futures.into_iter().map(Box::pin).collect();
    let mut out: Vec<Option<F::Output>> = pinned.iter().map(|_| None).collect();
    std::future::poll_fn(|cx| {
        let mut pending = false;
        for (i, f) in pinned.iter_mut().enumerate() {
            if out[i].is_none() {
                match f.as_mut().poll(cx) {
                    std::task::Poll::Ready(v) => out[i] = Some(v),
                    std::task::Poll::Pending => pending = true,
                }
            }
        }
        if pending {
            std::task::Poll::Pending
        } else {
            std::task::Poll::Ready(())
        }
    })
    .await;
    out.into_iter().map(|o| o.expect("ready")).collect()
}
