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

//! Setting up an inbound replication group (`XClusterInboundReplicationGroupSetupTask`): each
//! source table is matched to a target table, gets a stream on the source and a tablet mapping,
//! and the group's row and consumer registry entry are written in one write. An alter group
//! (`<id>.ALTER`) merges its tables into the original group instead.

use std::collections::{BTreeMap, BTreeSet};

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_xrepl::strip_sequences_data_alias;
use protobuf::{EnumOrUnknown, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState,
    sys_universe_replication_entry_pb::{
        dbscope_info::NamespaceInfo as DbNamespaceInfo, State as UniverseState,
    },
    SysCDCStreamEntryPB, SysClusterConfigEntryPB, SysNamespaceEntryPB, SysTablesEntryPB,
    SysUniverseReplicationEntryPB,
};
use yb_proto::pb::cdc_consumer::{ProducerEntryPB, StreamEntryPB};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_client::{GetTableLocationsRequestPB, GetTableLocationsResponsePB};
use yb_proto::pb::master_ddl::{
    GetTableSchemaRequestPB, GetTableSchemaResponsePB, GetTablegroupSchemaRequestPB,
    GetTablegroupSchemaResponsePB,
};
use yb_proto::pb::master_replication::{
    update_cdcstream_request_pb::StreamUpdateInfo, CreateCDCStreamRequestPB,
    CreateCDCStreamResponsePB, GetCDCStreamRequestPB, GetCDCStreamResponsePB,
    IsBootstrapRequiredRequestPB, IsBootstrapRequiredResponsePB, UpdateCDCStreamRequestPB,
    UpdateCDCStreamResponsePB, XClusterEnsureSequenceUpdatesAreInWalRequestPB,
    XClusterEnsureSequenceUpdatesAreInWalResponsePB,
};
use yb_proto::pb::master_rust_peer::{
    xcluster_tablet_mapping_request_pb::ConsumerTabletPB, XClusterSchemasEquivalentRequestPB,
    XClusterSchemasEquivalentResponsePB, XClusterTabletMappingRequestPB,
    XClusterTabletMappingResponsePB, XClusterValidateAutoFlagsRequestPB,
    XClusterValidateAutoFlagsResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::master_types::SysRowEntryType::{CLUSTER_CONFIG, UNIVERSE_REPLICATION};

use crate::remote::{call_leader, check, MASTER_SERVICE};
use crate::{corrupt, eligible_tables, is_ddl_queue_table, sequences_data_alias, status, XCluster};

/// `YQL_DATABASE_PGSQL`, as target namespaces are looked up.
pub(crate) const PGSQL: i32 = yb_proto::pb::common_types::YQLDatabase::YQL_DATABASE_PGSQL as i32;

/// The suffix of an alter group's id (`kAlterReplicationGroupSuffix`).
pub(crate) const ALTER_SUFFIX: &str = ".ALTER";

/// `GetOriginalReplicationGroupId`.
pub(crate) fn original_group_id(id: &str) -> &str {
    id.strip_suffix(ALTER_SUFFIX).unwrap_or(id)
}

/// What a setup or alter is given (`XClusterSetupUniverseReplicationData`).
#[derive(Clone, Debug, Default)]
pub(crate) struct SetupData {
    pub group_id: String,
    pub source_masters: Vec<HostPortPB>,
    pub transactional: bool,
    pub automatic_ddl_mode: bool,
    pub source_table_ids: Vec<String>,
    pub target_table_ids: Vec<String>,
    pub stream_ids: Vec<String>,
    pub source_namespace_ids: Vec<String>,
    pub target_namespace_ids: Vec<String>,
}

impl SetupData {
    pub(crate) fn is_alter(&self) -> bool {
        self.group_id.ends_with(ALTER_SUFFIX)
    }
}

/// What a table's setup found (`XClusterTableSetupInfo`).
#[derive(Clone, Debug, Default)]
struct TableSetup {
    target_table_id: String,
    stream_id: String,
    stream_entry: StreamEntryPB,
    stream_options: Vec<(String, Vec<u8>)>,
}

/// `IsTablegroupParentTableId`: a tablegroup's or a colocated database's parent table.
fn is_tablegroup_parent(id: &str) -> bool {
    id.ends_with(".tablegroup.parent.uuid") || id.ends_with(".colocation.parent.uuid")
}

/// `GetTablegroupIdFromParentTableId`.
fn tablegroup_of_parent(id: &str) -> &str {
    &id[..id.len().min(32)]
}

/// The universe replication row of `id`, unless missing or DELETED.
pub(crate) fn universe(
    snapshot: &CatalogSnapshot,
    id: &str,
) -> Option<(
    std::sync::Arc<master_catalog_entity::Entity>,
    SysUniverseReplicationEntryPB,
)> {
    let entity = snapshot.get(SysRowEntryType::UNIVERSE_REPLICATION, id.as_bytes())?;
    let pb: SysUniverseReplicationEntryPB = entity.parse().ok()?;
    (pb.state() != UniverseState::DELETED).then(|| (entity.clone(), pb))
}

/// Every universe replication row not DELETED.
pub(crate) fn universes(snapshot: &CatalogSnapshot) -> Vec<SysUniverseReplicationEntryPB> {
    snapshot
        .iter(SysRowEntryType::UNIVERSE_REPLICATION)
        .filter_map(|e| e.parse::<SysUniverseReplicationEntryPB>().ok())
        .filter(|pb| pb.state() != UniverseState::DELETED)
        .collect()
}

pub(crate) fn cluster_config(
    snapshot: &CatalogSnapshot,
) -> Result<SysClusterConfigEntryPB, Status> {
    snapshot
        .cluster_config()
        .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?
        .parse()
        .map_err(corrupt)
}

/// `IncludesConsumerNamespace`.
pub(crate) fn includes_consumer_namespace(pb: &SysUniverseReplicationEntryPB, ns: &str) -> bool {
    pb.db_scoped_info
        .namespace_infos
        .iter()
        .any(|i| i.consumer_namespace_id() == ns)
}

impl XCluster {
    /// `ValidateInputArguments`.
    pub(crate) fn validate_setup(
        &self,
        data: &SetupData,
        local_masters: &[HostPortPB],
    ) -> Result<bool, Status> {
        if data.group_id.is_empty() {
            return Err(status(
                StatusCode::InvalidArgument,
                "Invalid Replication Group Id",
            ));
        }
        let snapshot = self.catalog.snapshot();
        let existing = universe(&snapshot, original_group_id(&data.group_id));
        let db_scoped = if data.is_alter() {
            let (_, pb) = existing.ok_or_else(|| {
                status(
                    StatusCode::NotFound,
                    format!("Replication group {} not found", data.group_id),
                )
            })?;
            if data.automatic_ddl_mode != pb.db_scoped_info.automatic_ddl_mode() {
                return Err(status(
                    StatusCode::InvalidArgument,
                    "Automatic DDL mode setting passed differs from existing replication group's",
                ));
            }
            !pb.db_scoped_info.namespace_infos.is_empty()
        } else {
            if existing.is_some() {
                return Err(status(
                    StatusCode::AlreadyPresent,
                    format!("Replication group {} already present", data.group_id),
                ));
            }
            !data.source_namespace_ids.is_empty()
        };
        if data.automatic_ddl_mode && !db_scoped {
            return Err(status(
                StatusCode::InvalidArgument,
                "Automatic DDL mode is only valid for DB-scoped replication groups",
            ));
        }
        if db_scoped && !data.transactional {
            return Err(status(
                StatusCode::InvalidArgument,
                "Transactional flag must be set for DB-scoped replication groups",
            ));
        }
        if data.source_table_ids.is_empty() {
            return Err(status(StatusCode::InvalidArgument, "No tables provided"));
        }
        let mut seen = BTreeSet::new();
        for t in &data.source_table_ids {
            if t.is_empty() {
                return Err(status(StatusCode::InvalidArgument, "Invalid Table Id"));
            }
            if t.ends_with(".colocated.parent.uuid") {
                return Err(status(
                    StatusCode::NotSupported,
                    format!(
                        "Pre GA colocated databases are not supported with xCluster replication: \
                         {t}"
                    ),
                ));
            }
            if !db_scoped && strip_sequences_data_alias(t) == crate::SEQUENCES_DATA_TABLE_ID {
                return Err(status(
                    StatusCode::NotSupported,
                    format!("Replication of the sequences_data table is not supported: {t}"),
                ));
            }
            if !seen.insert(t.clone()) {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!(
                        "Duplicate table source table id: {:?}",
                        data.source_table_ids
                    ),
                ));
            }
        }
        if !data.target_table_ids.is_empty()
            && data.target_table_ids.len() != data.source_table_ids.len()
        {
            return Err(status(
                StatusCode::InvalidArgument,
                "Number of target tables must be equal to number of source tables",
            ));
        }
        if !data.stream_ids.is_empty() && data.stream_ids.len() != data.source_table_ids.len() {
            return Err(status(
                StatusCode::InvalidArgument,
                "Number of bootstrap ids must be equal to number of tables",
            ));
        }
        if cluster_config(&snapshot)?.cluster_uuid() == data.group_id {
            return Err(status(
                StatusCode::InvalidArgument,
                "Replication group Id cannot be the same as the cluster UUID",
            ));
        }
        for hp in &data.source_masters {
            if local_masters
                .iter()
                .any(|l| l.host() == hp.host() && l.port() == hp.port())
            {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!(
                        "Master address {}:{} belongs to the target universe",
                        hp.host(),
                        hp.port()
                    ),
                ));
            }
        }
        if data.source_namespace_ids.len() != data.target_namespace_ids.len() {
            return Err(status(
                StatusCode::InvalidArgument,
                "Source and target namespace ids must be of the same size",
            ));
        }
        self.validate_namespaces(&snapshot, data)?;
        Ok(db_scoped)
    }

    /// `ValidateNamespaceListForDbScoped`: a target namespace is in one group only.
    fn validate_namespaces(
        &self,
        snapshot: &CatalogSnapshot,
        data: &SetupData,
    ) -> Result<(), Status> {
        for pb in universes(snapshot) {
            for ns in &data.target_namespace_ids {
                if includes_consumer_namespace(&pb, ns) {
                    return Err(status(
                        StatusCode::AlreadyPresent,
                        format!(
                            "Namespace {ns} already included in replication group {}",
                            pb.replication_group_id()
                        ),
                    ));
                }
            }
        }
        Ok(())
    }

    /// The task's steps; the outcome becomes the group's `IsSetupUniverseReplicationDone`.
    pub(crate) async fn run_setup(
        &self,
        token: &LeaderToken,
        data: &SetupData,
    ) -> Result<(), Status> {
        let mut auto_flags_version = None;
        if (self.options.auto_flag_validation)() && !data.is_alter() {
            auto_flags_version = Some(self.compatible_auto_flags_version(data).await?);
        }
        if data.automatic_ddl_mode && !data.is_alter() {
            let _: yb_proto::pb::master_rust_peer::CreateSequencesDataTableResponsePB = self
                .host_call(
                    "CreateSequencesDataTable",
                    yb_proto::pb::master_rust_peer::CreateSequencesDataTableRequestPB::new(),
                )
                .await?;
        }
        let mut bootstrap_sequences = false;
        if data.automatic_ddl_mode {
            for ns in &data.target_namespace_ids {
                let switchover = crate::outbound::groups(&self.catalog.snapshot())
                    .iter()
                    .any(|(_, g)| g.automatic_ddl_mode() && g.namespace_infos.contains_key(ns));
                tracing::info!(
                    "xCluster InboundReplicationGroup [{}]: Setting up DDL replication extension \
                     for namespace {ns}{}",
                    data.group_id,
                    if switchover {
                        " as part of a switchover"
                    } else {
                        ""
                    }
                );
                self.setup_target_ddl_extension(token, ns)
                    .await
                    .map_err(|s| {
                        status(
                            s.code,
                            format!(
                                "Failed to setup xCluster DDL replication extension: {}",
                                s.message
                            ),
                        )
                    })?;
                if !switchover {
                    bootstrap_sequences = true;
                }
            }
        }
        if bootstrap_sequences {
            let mut req = XClusterEnsureSequenceUpdatesAreInWalRequestPB::new();
            req.set_replication_group_id(data.group_id.clone());
            req.namespace_ids = data.source_namespace_ids.clone();
            let deadline = Instant::now() + (self.options.ensure_sequence_updates_timeout)();
            let _: XClusterEnsureSequenceUpdatesAreInWalResponsePB = check(
                self.source_call(
                    data,
                    "XClusterEnsureSequenceUpdatesAreInWal",
                    &req,
                    deadline,
                )
                .await?,
            )?;
        }
        tracing::info!(
            "xCluster InboundReplicationGroup [{}]: Started schema validation for {} table(s)",
            data.group_id,
            data.source_table_ids.len()
        );
        let mut tables = BTreeMap::new();
        for (i, source) in data.source_table_ids.iter().enumerate() {
            let target = data.target_table_ids.get(i).cloned();
            let stream = data.stream_ids.get(i).cloned();
            let info = self
                .setup_table(data, source, target, stream)
                .await
                .map_err(|s| {
                    status(
                        s.code,
                        format!("Error processing source table {source}: {}", s.message),
                    )
                })?;
            tables.insert(source.clone(), info);
        }
        if !data.stream_ids.is_empty() {
            self.update_source_stream_options(data, &tables)
                .await
                .map_err(|s| {
                    status(
                        s.code,
                        format!(
                            "Failed to update xCluster stream options on source universe: {}",
                            s.message
                        ),
                    )
                })?;
        }
        tracing::info!(
            "xCluster InboundReplicationGroup [{}]: Table validation and stream setup completed \
             successfully for {} table(s)",
            data.group_id,
            tables.len()
        );
        self.write_replication_group(token, data, &tables, auto_flags_version)
            .await
    }

    /// `GetAutoFlagConfigVersionIfCompatible`.
    async fn compatible_auto_flags_version(&self, data: &SetupData) -> Result<u32, Status> {
        let mut req = XClusterValidateAutoFlagsRequestPB::new();
        req.set_replication_group_id(data.group_id.clone());
        req.master_addresses = data.source_masters.clone();
        let resp: XClusterValidateAutoFlagsResponsePB =
            self.host_call("XClusterValidateAutoFlags", req).await?;
        if !resp.has_valid() {
            return Ok(0);
        }
        if !resp.valid() {
            return Err(status(
                StatusCode::IllegalState,
                "AutoFlags between the universes are not compatible. Upgrade the target universe \
                 to a version higher than or equal to the source universe",
            ));
        }
        Ok(resp.source_version())
    }

    /// `SetupDDLReplicationExtension` as a target: the extension, and the role that stops writes.
    pub(crate) async fn setup_target_ddl_extension(
        &self,
        token: &LeaderToken,
        ns: &str,
    ) -> Result<(), Status> {
        let name = XCluster::namespace_name(&self.catalog.snapshot(), ns)?;
        self.execute_pgsql(
            &name,
            &[format!(
                "CREATE EXTENSION IF NOT EXISTS {}",
                crate::DDL_QUEUE_PG_SCHEMA_NAME
            )],
        )
        .await?;
        self.set_role(
            token,
            ns,
            yb_proto::pb::common_types::xcluster_namespace_info_pb::XClusterRole::AUTOMATIC_TARGET,
        )
        .await
    }

    pub(crate) async fn source_call<Req: protobuf::Message, Resp: protobuf::MessageFull>(
        &self,
        data: &SetupData,
        method: &'static str,
        req: &Req,
        deadline: Instant,
    ) -> Result<Resp, Status> {
        call_leader(
            &*self.remote,
            &data.group_id,
            &data.source_masters,
            MASTER_SERVICE,
            method,
            req,
            deadline,
        )
        .await
    }

    fn remote_deadline(&self) -> Instant {
        Instant::now() + (self.options.remote_timeout)()
    }

    /// `XClusterTableSetupTask`: the target table, the source stream and the tablet mapping.
    async fn setup_table(
        &self,
        data: &SetupData,
        source: &str,
        target: Option<String>,
        stream_id: Option<String>,
    ) -> Result<TableSetup, Status> {
        let mut info = TableSetup {
            stream_id: stream_id.unwrap_or_default(),
            ..Default::default()
        };
        let target_table_id = if is_tablegroup_parent(source) {
            self.setup_tablegroup(data, source, &mut info).await?
        } else if let Some(target) = target {
            if target.is_empty() {
                return Err(status(
                    StatusCode::InvalidArgument,
                    "Expected non-empty target table id.",
                ));
            }
            // Automatic DDL replication brings the schemas in step; versions map from 0.
            let versions = info.stream_entry.schema_versions.mut_or_insert_default();
            versions.current_producer_schema_version = 0;
            versions.current_consumer_schema_version = 0;
            target
        } else {
            let mut req = GetTableSchemaRequestPB::new();
            req.table
                .mut_or_insert_default()
                .set_table_id(strip_sequences_data_alias(source).as_bytes().to_vec());
            let resp: GetTableSchemaResponsePB = check(
                call_leader(
                    &*self.remote,
                    &data.group_id,
                    &data.source_masters,
                    MASTER_SERVICE,
                    "GetTableSchema",
                    &req,
                    self.remote_deadline(),
                )
                .await
                .map_err(|s| {
                    status(s.code, format!("Error from source universe: {}", s.message))
                })?,
            )
            .map_err(|s| status(s.code, format!("Error from source universe: {}", s.message)))?;
            if resp.identifier.namespace.name() == "system" {
                return Err(status(
                    StatusCode::NotSupported,
                    "Cannot replicate system tables",
                ));
            }
            let (target_id, target_version) = self.target_for_source(data, source, &resp).await?;
            let versions = info.stream_entry.schema_versions.mut_or_insert_default();
            versions.current_producer_schema_version = resp.version();
            versions.current_consumer_schema_version = target_version;
            target_id
        };
        self.populate_stream_entry(data, source, &target_table_id, &mut info)?;
        if (self.options.check_bootstrap_required)() {
            let mut req = IsBootstrapRequiredRequestPB::new();
            req.table_ids.push(source.to_string());
            if !info.stream_id.is_empty() {
                req.stream_ids.push(info.stream_id.clone());
            }
            let resp: IsBootstrapRequiredResponsePB = check(
                self.source_call(data, "IsBootstrapRequired", &req, self.remote_deadline())
                    .await?,
            )?;
            if resp.results.iter().any(|r| r.bootstrap_required()) {
                return Err(status(
                    StatusCode::IllegalState,
                    format!("Bootstrap is required for Table {source}"),
                ));
            }
        }
        if info.stream_id.is_empty() {
            let mut req = CreateCDCStreamRequestPB::new();
            req.set_table_id(source.to_string());
            req.options = master_xrepl::xcluster_stream_options();
            req.initial_state = Some(EnumOrUnknown::new(StreamState::ACTIVE));
            req.set_transactional(data.transactional);
            let resp: CreateCDCStreamResponsePB = check(
                self.source_call(data, "CreateCDCStream", &req, self.remote_deadline())
                    .await?,
            )
            .map_err(|s| {
                status(
                    s.code,
                    format!(
                        "Error creating xCluster Stream on source universe: {}",
                        s.message
                    ),
                )
            })?;
            info.stream_id = String::from_utf8_lossy(resp.stream_id()).into_owned();
        } else {
            let mut req = GetCDCStreamRequestPB::new();
            req.set_stream_id(info.stream_id.clone().into_bytes());
            let resp: GetCDCStreamResponsePB = check(
                self.source_call(data, "GetCDCStream", &req, self.remote_deadline())
                    .await?,
            )
            .map_err(|s| status(s.code, format!("Error from source universe: {}", s.message)))?;
            let received = resp
                .stream
                .table_id
                .first()
                .map(|t| String::from_utf8_lossy(t).into_owned())
                .unwrap_or_default();
            if received != source {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!(
                        "Invalid xCluster Stream id for table {source}. Stream id {} belongs to \
                         table {received}",
                        info.stream_id
                    ),
                ));
            }
            info.stream_options = resp
                .stream
                .options
                .iter()
                .map(|o| (o.key().to_string(), o.value().to_vec()))
                .collect();
            info.stream_entry.xcluster_use_target_applied_filter =
                resp.stream.xcluster_use_target_applied_filter();
        }
        self.populate_tablet_mapping(data, source, &mut info)
            .await?;
        Ok(info)
    }

    /// `ValidateSourceSchemaAndGetTargetSchema`: the target table with the source's name, schema
    /// name and an equivalent schema, and its schema version.
    async fn target_for_source(
        &self,
        data: &SetupData,
        source: &str,
        resp: &GetTableSchemaResponsePB,
    ) -> Result<(String, u32), Status> {
        let ysql = resp.table_type() == TableType::PGSQL_TABLE_TYPE;
        let table_name = resp.identifier.table_name();
        let ns_name = resp.identifier.namespace.name();
        if data.transactional && !(self.options.allow_ycql_transactional)() && !ysql {
            return Err(status(
                StatusCode::NotSupported,
                format!(
                    "Transactional replication is not supported for non-YSQL tables: \
                     {ns_name}.{table_name}"
                ),
            ));
        }
        let source_schema_name = resp.schema.DEPRECATED_pgschema_name().to_string();
        let snapshot = self.catalog.snapshot();
        let mut found: Option<(Vec<u8>, SysTablesEntryPB)> = None;
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else {
                continue;
            };
            if hot.name != table_name
                || !master_table_lifecycle::lookup::is_visible_to_client(table)
            {
                continue;
            }
            let ns_ok = snapshot
                .get(SysRowEntryType::NAMESPACE, &hot.namespace_id)
                .and_then(|n| n.parse::<SysNamespaceEntryPB>().ok())
                .is_some_and(|n| n.name() == ns_name.as_bytes());
            if !ns_ok {
                continue;
            }
            let pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            if ysql && pb.schema.DEPRECATED_pgschema_name() != source_schema_name {
                continue;
            }
            if !data.automatic_ddl_mode && ysql && pb.is_matview() {
                return Err(status(
                    StatusCode::NotSupported,
                    format!(
                        "Replication is not supported for materialized view: \
                         {ns_name}.{table_name}"
                    ),
                ));
            }
            let mut req = XClusterSchemasEquivalentRequestPB::new();
            req.source = resp.schema.clone();
            req.target = pb.schema.clone();
            let eq: XClusterSchemasEquivalentResponsePB =
                self.host_call("XClusterSchemasEquivalent", req).await?;
            if !eq.equivalent() {
                return Err(status(
                    StatusCode::IllegalState,
                    format!(
                        "Source and target schemas don't match: Source: {source}, Target: {}, \
                         Source schema: {:?}, Target schema: {:?}",
                        String::from_utf8_lossy(&table.key.id),
                        resp.schema,
                        pb.schema
                    ),
                ));
            }
            found = Some((table.key.id.to_vec(), pb));
            break;
        }
        let (target_id, pb) = found.ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!(
                    "Could not find matching table for {ns_name}.{table_name}{}",
                    if ysql {
                        format!(" pgschema_name: {source_schema_name}")
                    } else {
                        String::new()
                    }
                ),
            )
        })?;
        if resp.colocated() {
            let source_clc = &resp.schema.colocated_table_id;
            let target_clc = &pb.schema.colocated_table_id;
            if !source_clc.has_colocation_id() {
                return Err(status(
                    StatusCode::NotFound,
                    format!("Missing colocation ID for source table {source}"),
                ));
            }
            if !target_clc.has_colocation_id() {
                return Err(status(
                    StatusCode::NotFound,
                    format!(
                        "Missing colocation ID for target table {}",
                        String::from_utf8_lossy(&target_id)
                    ),
                ));
            }
            if source_clc.colocation_id() != target_clc.colocation_id() {
                return Err(status(
                    StatusCode::IllegalState,
                    format!(
                        "Source and target colocation IDs don't match for colocated table: \
                         Source: {source}, Target: {}, Source colocation ID: {}, Target \
                         colocation ID: {}",
                        String::from_utf8_lossy(&target_id),
                        source_clc.colocation_id(),
                        target_clc.colocation_id()
                    ),
                ));
            }
        }
        let mut target_id = String::from_utf8_lossy(&target_id).into_owned();
        if target_id == crate::SEQUENCES_DATA_TABLE_ID {
            let source_ns = source
                .split(".sequences_data_for.")
                .nth(1)
                .ok_or_else(|| status(StatusCode::InvalidArgument, "Not a sequences_data alias"))?;
            let i = data
                .source_namespace_ids
                .iter()
                .position(|n| n == source_ns)
                .ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!(
                            "Couldn't find source namespace {source_ns} in replication group's \
                             namespace mapping"
                        ),
                    )
                })?;
            target_id = sequences_data_alias(&data.target_namespace_ids[i]);
        }
        Ok((target_id, pb.version()))
    }

    /// `ProcessTablegroup`: every table of the source tablegroup matches one of the target's, and
    /// the target's parent table is the stream's consumer.
    async fn setup_tablegroup(
        &self,
        data: &SetupData,
        source: &str,
        info: &mut TableSetup,
    ) -> Result<String, Status> {
        let source_tablegroup = tablegroup_of_parent(source);
        let mut req = GetTablegroupSchemaRequestPB::new();
        req.tablegroup
            .mut_or_insert_default()
            .set_id(source_tablegroup.as_bytes().to_vec());
        let resp: GetTablegroupSchemaResponsePB = check(
            call_leader(
                &*self.remote,
                &data.group_id,
                &data.source_masters,
                MASTER_SERVICE,
                "GetTablegroupSchema",
                &req,
                self.remote_deadline(),
            )
            .await?,
        )
        .map_err(|s| status(s.code, format!("Error from source universe: {}", s.message)))?;
        if resp.get_table_schema_response_pbs.is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                "Received empty list of tables to validate from source universe",
            ));
        }
        let mut validated = BTreeSet::new();
        for table in &resp.get_table_schema_response_pbs {
            if !table.colocated() {
                return Err(status(
                    StatusCode::InvalidArgument,
                    format!(
                        "Received non-colocated table {} from source universe",
                        String::from_utf8_lossy(table.identifier.table_id())
                    ),
                ));
            }
            let (target_id, version) = self
                .target_for_source(
                    data,
                    &String::from_utf8_lossy(table.identifier.table_id()),
                    table,
                )
                .await?;
            info.stream_entry.colocated_schema_versions.insert(
                table.schema.colocated_table_id.colocation_id(),
                yb_proto::pb::cdc_consumer::SchemaVersionsPB {
                    current_producer_schema_version: table.version(),
                    current_consumer_schema_version: version,
                    ..Default::default()
                },
            );
            validated.insert(target_id);
        }
        let snapshot = self.catalog.snapshot();
        let first = validated.iter().next().expect("validated a table");
        let tablet = snapshot
            .hosting_tablets_of(first.as_bytes())
            .first()
            .map(|t| t.key.id.to_vec())
            .ok_or_else(|| status(StatusCode::NotFound, "No tablet hosts the target tables"))?;
        let tablet_entity = snapshot
            .get(SysRowEntryType::TABLET, &tablet)
            .ok_or_else(|| status(StatusCode::NotFound, "Target tablet not found"))?;
        let hosted: Vec<String> = snapshot
            .hosted_table_ids(tablet_entity)
            .into_iter()
            .map(|t| String::from_utf8_lossy(&t).into_owned())
            .collect();
        let parent = hosted
            .iter()
            .find(|t| master_table_lifecycle::lookup::is_colocation_parent_table_id(t.as_bytes()))
            .cloned()
            .ok_or_else(|| status(StatusCode::NotFound, "Target tablegroup parent not found"))?;
        let in_target: BTreeSet<String> = hosted
            .into_iter()
            .filter(|t| t != &parent)
            .filter(|t| {
                snapshot
                    .get(SysRowEntryType::TABLE, t.as_bytes())
                    .is_some_and(|e| master_table_lifecycle::lookup::is_visible_to_client(e))
            })
            .collect();
        if in_target != validated {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Mismatch between tables associated with source tablegroup \
                     {source_tablegroup} and tables in target tablegroup {}: ({:?}) vs ({:?}).",
                    tablegroup_of_parent(&parent),
                    validated,
                    in_target
                ),
            ));
        }
        Ok(parent)
    }

    /// `PopulateTableStreamEntry`.
    fn populate_stream_entry(
        &self,
        data: &SetupData,
        source: &str,
        target: &str,
        info: &mut TableSetup,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let config = cluster_config(&snapshot)?;
        let consumer = config
            .consumer_registry
            .producer_map
            .values()
            .flat_map(|p| p.stream_map.values())
            .any(|s| s.consumer_table_id == target);
        if consumer {
            return Err(status(
                StatusCode::IllegalState,
                "N:1 replication topology not supported",
            ));
        }
        info.target_table_id = target.to_string();
        info.stream_entry.consumer_table_id = target.to_string();
        info.stream_entry.producer_table_id = source.to_string();
        if data.automatic_ddl_mode {
            let stripped = strip_sequences_data_alias(target);
            let pb =
                crate::outbound::table_pb(&snapshot, stripped.as_bytes()).ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Table unexpectedly missing during replication: {stripped}"),
                    )
                })?;
            info.stream_entry.is_ddl_queue_table = is_ddl_queue_table(&pb);
        }
        Ok(())
    }

    /// `PopulateTabletMapping`: the source's tablet locations against the target's tablets.
    async fn populate_tablet_mapping(
        &self,
        data: &SetupData,
        source: &str,
        info: &mut TableSetup,
    ) -> Result<(), Status> {
        let mut req = GetTableLocationsRequestPB::new();
        req.table
            .mut_or_insert_default()
            .set_table_id(strip_sequences_data_alias(source).as_bytes().to_vec());
        req.set_max_returned_locations(i32::MAX as u32);
        req.set_require_tablets_running(true);
        req.set_partitions_only(true);
        let locations: GetTableLocationsResponsePB = check(
            call_leader(
                &*self.remote,
                &data.group_id,
                &data.source_masters,
                MASTER_SERVICE,
                "GetTableLocations",
                &req,
                self.remote_deadline(),
            )
            .await?,
        )
        .map_err(|s| status(s.code, format!("Error from source universe: {}", s.message)))?;
        let mut req = XClusterTabletMappingRequestPB::new();
        req.set_producer_table_id(source.to_string());
        req.set_consumer_table_id(info.target_table_id.clone());
        req.consumer_tablets = self.consumer_tablets(&info.target_table_id)?;
        req.producer_locations = locations.tablet_locations;
        req.stream_entry = MessageField::some(info.stream_entry.clone());
        let resp: XClusterTabletMappingResponsePB =
            self.host_call("XClusterTabletMapping", req).await?;
        info.stream_entry = resp.stream_entry.unwrap_or_default();
        Ok(())
    }

    /// `GetTableKeyRanges`: the target table's active tablets and their partitions.
    pub(crate) fn consumer_tablets(&self, target: &str) -> Result<Vec<ConsumerTabletPB>, Status> {
        let snapshot = self.catalog.snapshot();
        let stripped = strip_sequences_data_alias(target);
        let mut out = Vec::new();
        for tablet in snapshot.active_tablets_of(stripped.as_bytes()) {
            let pb: yb_proto::pb::catalog_entity_info::SysTabletsEntryPB =
                tablet.parse().map_err(corrupt)?;
            let mut t = ConsumerTabletPB::new();
            t.set_tablet_id(tablet.key.id.to_vec());
            t.partition = pb.partition.clone();
            out.push(t);
        }
        if out.is_empty() {
            return Err(status(
                StatusCode::NotFound,
                format!("No tablets for target table {stripped}"),
            ));
        }
        Ok(out)
    }

    /// `UpdateSourceStreamOptions`: the given streams become ACTIVE on the source, with the
    /// options they had.
    async fn update_source_stream_options(
        &self,
        data: &SetupData,
        tables: &BTreeMap<String, TableSetup>,
    ) -> Result<(), Status> {
        let mut req = UpdateCDCStreamRequestPB::new();
        for (source, info) in tables {
            let mut entry = SysCDCStreamEntryPB::new();
            entry.table_id.push(source.clone());
            for (key, value) in &info.stream_options {
                if key == "state" {
                    continue;
                }
                let mut o = yb_proto::pb::catalog_entity_info::CDCStreamOptionsPB::new();
                o.set_key(key.clone());
                o.set_value(value.clone());
                entry.options.push(o);
            }
            entry.state = Some(EnumOrUnknown::new(StreamState::ACTIVE));
            entry.set_transactional(data.transactional);
            let mut update = StreamUpdateInfo::new();
            update.set_stream_id(info.stream_id.clone().into_bytes());
            update.entry = MessageField::some(entry);
            req.streams.push(update);
        }
        let _: UpdateCDCStreamResponsePB = check(
            self.source_call(data, "UpdateCDCStream", &req, self.remote_deadline())
                .await?,
        )?;
        Ok(())
    }

    /// `ValidateTableListForDbScoped`: the group has every eligible table of its namespaces and
    /// no other.
    fn validate_db_scoped_tables(
        &self,
        snapshot: &CatalogSnapshot,
        data: &SetupData,
        tables: &BTreeMap<String, TableSetup>,
    ) -> Result<(), Status> {
        if data.source_namespace_ids.is_empty() {
            return Ok(());
        }
        let targets: BTreeSet<String> =
            tables.values().map(|t| t.target_table_id.clone()).collect();
        let mut validated = BTreeSet::new();
        for ns in &data.target_namespace_ids {
            let mut missing = Vec::new();
            for d in eligible_tables(snapshot, ns, data.automatic_ddl_mode)? {
                if targets.contains(&d.id) {
                    validated.insert(d.id);
                } else {
                    missing.push(d.id);
                }
            }
            if !missing.is_empty() {
                return Err(status(
                    StatusCode::IllegalState,
                    format!(
                        "Namespace {ns} has additional tables that were not added to xCluster DB \
                         Scoped replication group {}: {missing:?}",
                        data.group_id
                    ),
                ));
            }
        }
        let diff: Vec<&String> = targets.symmetric_difference(&validated).collect();
        if !diff.is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "xCluster DB Scoped replication group {} contains tables {diff:?} that do not \
                     belong to replicated namespaces {:?}",
                    data.group_id, data.target_namespace_ids
                ),
            ));
        }
        Ok(())
    }

    /// `SetupReplicationGroup`: the group's row (a new one, or the original's for an alter) and
    /// its consumer registry entry, in one write.
    async fn write_replication_group(
        &self,
        token: &LeaderToken,
        data: &SetupData,
        tables: &BTreeMap<String, TableSetup>,
        auto_flags_version: Option<u32>,
    ) -> Result<(), Status> {
        let pause = (self.options.pause_at_start_of_setup)();
        if !pause.is_zero() {
            tokio::time::sleep(pause).await;
        }
        let original = original_group_id(&data.group_id).to_string();
        self.writer
            .mutate(token, |snapshot| {
                if !data.is_alter() {
                    self.validate_namespaces(snapshot, data)?;
                }
                self.validate_db_scoped_tables(snapshot, data, tables)?;
                let existing = universe(snapshot, &original);
                let (mut m, mut pb) = if data.is_alter() {
                    let (entity, pb) = existing.ok_or_else(|| {
                        status(
                            StatusCode::NotFound,
                            format!("Replication group {original} not found"),
                        )
                    })?;
                    (Mutation::new().read(&entity), pb)
                } else {
                    if existing.is_some() {
                        return Err(status(
                            StatusCode::AlreadyPresent,
                            format!("Replication group {} already present", data.group_id),
                        ));
                    }
                    let mut pb = SysUniverseReplicationEntryPB::new();
                    pb.set_replication_group_id(data.group_id.clone());
                    pb.producer_master_addresses = data.source_masters.clone();
                    pb.state = Some(EnumOrUnknown::new(UniverseState::ACTIVE));
                    pb.set_transactional(data.transactional);
                    if !data.source_namespace_ids.is_empty() {
                        pb.db_scoped_info
                            .mut_or_insert_default()
                            .set_automatic_ddl_mode(data.automatic_ddl_mode);
                    }
                    if let Some(v) = auto_flags_version {
                        pb.set_validated_local_auto_flags_config_version(v);
                    }
                    (
                        Mutation::new().read_absent(UNIVERSE_REPLICATION, original.as_bytes()),
                        pb,
                    )
                };
                for (s, t) in data
                    .source_namespace_ids
                    .iter()
                    .zip(&data.target_namespace_ids)
                {
                    let mut i = DbNamespaceInfo::new();
                    i.set_producer_namespace_id(s.clone());
                    i.set_consumer_namespace_id(t.clone());
                    pb.db_scoped_info
                        .mut_or_insert_default()
                        .namespace_infos
                        .push(i);
                }
                // In the order given, which tests rely on.
                for source in &data.source_table_ids {
                    let info = &tables[source];
                    pb.tables.push(source.clone());
                    pb.table_streams
                        .insert(source.clone(), info.stream_id.clone());
                    pb.validated_tables
                        .insert(source.clone(), info.target_table_id.clone());
                }
                let config_entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut config: SysClusterConfigEntryPB = config_entity.parse().map_err(corrupt)?;
                let registry = config.consumer_registry.mut_or_insert_default();
                let producer_map = &mut registry.producer_map;
                if data.is_alter() {
                    if !producer_map.contains_key(&original) {
                        return Err(status(
                            StatusCode::InvalidArgument,
                            format!(
                                "ClusterConfig producer_map missing for ReplicationGroup \
                                 {original}"
                            ),
                        ));
                    }
                    if (self.options.fail_universe_replication_merge)() {
                        return Err(status(
                            StatusCode::IllegalState,
                            "TEST_fail_universe_replication_merge",
                        ));
                    }
                    tracing::info!(
                        "Merging xCluster ReplicationGroup: {} into {original}",
                        data.group_id
                    );
                } else {
                    if producer_map.contains_key(&original) {
                        return Err(status(
                            StatusCode::InvalidArgument,
                            format!(
                                "ReplicationGroup {original} already exists in ClusterConfig \
                                 producer_map"
                            ),
                        ));
                    }
                    let mut entry = ProducerEntryPB::new();
                    entry.master_addrs = data.source_masters.clone();
                    entry.automatic_ddl_mode = data.automatic_ddl_mode;
                    if let Some(v) = auto_flags_version {
                        entry.compatible_auto_flag_config_version = v;
                        entry.validated_auto_flags_config_version = v;
                    }
                    producer_map.insert(original.clone(), entry);
                }
                let entry = producer_map.get_mut(&original).expect("inserted");
                for info in tables.values() {
                    entry
                        .stream_map
                        .insert(info.stream_id.clone(), info.stream_entry.clone());
                }
                config.set_version(config.version() + 1);
                m = m
                    .upsert_message(UNIVERSE_REPLICATION, original.as_bytes(), &pb)?
                    .read(config_entity)
                    .upsert_message(CLUSTER_CONFIG, &config_entity.key.id, &config)?;
                Ok((m, ()))
            })
            .await
            .map(|_| ())
            .map_err(|s| {
                status(
                    s.code,
                    format!("Failed to write to sys-catalog: {}", s.message),
                )
            })?;
        tracing::info!(
            "xCluster InboundReplicationGroup [{}]: Replication group set up with {} table(s)",
            data.group_id,
            tables.len()
        );
        Ok(())
    }
}
