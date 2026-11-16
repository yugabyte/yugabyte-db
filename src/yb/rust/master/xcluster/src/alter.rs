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

//! `AlterUniverseReplication` (`AlterUniverseReplicationHelper`): one change per request. Tables
//! are added by setting up the group's alter group (`<id>.ALTER`), which merges into it; tables
//! and namespaces are removed, and the source's addresses changed, in one write each.

use std::collections::{BTreeSet, HashMap};
use std::sync::Arc;

use master_catalog_state::Mutation;
use master_leadership::LeaderToken;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{SysClusterConfigEntryPB, SysUniverseReplicationEntryPB};
use yb_proto::pb::master_replication::{
    AlterUniverseReplicationRequestPB, DeleteCDCStreamRequestPB, DeleteCDCStreamResponsePB,
};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, NamespaceIdentifierPB, SysRowEntryType,
};

use crate::remote::{call_leader, MASTER_SERVICE};
use crate::setup::{universe, SetupData, ALTER_SUFFIX, PGSQL};
use crate::{corrupt, eligible_tables, status, unknown, XCluster};

/// What `AddTablesToReplication` is given.
#[derive(Default)]
pub(crate) struct AddTables {
    pub group_id: String,
    pub namespace: Option<NamespaceIdentifierPB>,
    pub source_table_ids: Vec<String>,
    pub stream_ids: Vec<String>,
    pub target_table_id: Option<String>,
}

fn coded(code: Code, s: Status) -> MasterErrorPB {
    master_ts_manager::master_error_coded(code, s.code, &s.message)
}

impl XCluster {
    /// `ValidateUniverseUUID`: a request naming a universe must name this one.
    pub(crate) fn validate_universe_uuid(&self, uuid: &str) -> Result<(), Status> {
        if uuid.is_empty() {
            return Ok(());
        }
        let config = crate::setup::cluster_config(&self.catalog.snapshot())?;
        if config.universe_uuid() != uuid {
            let expected = if config.universe_uuid().is_empty() {
                "empty"
            } else {
                config.universe_uuid()
            };
            return Err(status(
                StatusCode::InvalidArgument,
                format!("Invalid Universe UUID {uuid}. Expected {expected}"),
            ));
        }
        Ok(())
    }

    /// `AlterUniverseReplication`.
    pub async fn alter_universe_replication(
        self: &Arc<Self>,
        token: &LeaderToken,
        deadline: Instant,
        req: &AlterUniverseReplicationRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("AlterUniverseReplication: {:?}", req);
        let id = req.replication_group_id();
        if id.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        self.validate_universe_uuid(req.universe_uuid())
            .map_err(unknown)?;
        if req.has_DEPRECATED_new_replication_group_id() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Use of field deprecated_new_replication_group_id is not supported",
            )));
        }
        let snapshot = self.catalog.snapshot();
        let (_, pb) = universe(&snapshot, id).ok_or_else(|| {
            coded(
                Code::OBJECT_NOT_FOUND,
                status(
                    StatusCode::NotFound,
                    format!("Could not find xCluster replication group {id}"),
                ),
            )
        })?;
        let count = [
            !req.producer_master_addresses.is_empty(),
            !req.producer_table_ids_to_remove.is_empty(),
            !req.producer_table_ids_to_add.is_empty(),
            !req.producer_namespace_id_to_remove().is_empty(),
        ]
        .iter()
        .filter(|b| **b)
        .count();
        if count != 1 {
            return Err(coded(
                Code::INVALID_REQUEST,
                status(
                    StatusCode::InvalidArgument,
                    format!("Only 1 Alter operation per request currently supported: {req:?}"),
                ),
            ));
        }
        if !req.producer_master_addresses.is_empty() {
            return self
                .update_producer_addresses(token, id, req)
                .await
                .map_err(|s| {
                    if s.code == StatusCode::NotFound {
                        coded(Code::OBJECT_NOT_FOUND, s)
                    } else {
                        unknown(s)
                    }
                });
        }
        if !req.producer_namespace_id_to_remove().is_empty() {
            return self
                .remove_namespace_from_group(
                    token,
                    deadline,
                    &pb,
                    req.producer_namespace_id_to_remove(),
                )
                .await
                .map_err(unknown);
        }
        if !req.producer_table_ids_to_remove.is_empty() {
            return self
                .remove_tables_from_group(
                    token,
                    deadline,
                    id,
                    &req.producer_table_ids_to_remove,
                    true,
                    None,
                )
                .await
                .map_err(unknown);
        }
        let mut streams = Vec::new();
        for s in &req.producer_bootstrap_ids_to_add {
            streams.push(master_xrepl::parse_stream_id(s.as_bytes())?);
        }
        let add = AddTables {
            group_id: id.to_string(),
            namespace: req.producer_namespace_to_add.clone().into_option(),
            source_table_ids: req.producer_table_ids_to_add.clone(),
            stream_ids: streams,
            target_table_id: None,
        };
        self.add_tables_to_replication(token, deadline, add).await
    }

    /// `UpdateProducerAddress`.
    async fn update_producer_addresses(
        &self,
        token: &LeaderToken,
        id: &str,
        req: &AlterUniverseReplicationRequestPB,
    ) -> Result<(), Status> {
        self.writer
            .mutate(token, |snapshot| {
                let (entity, mut pb) = universe(snapshot, id).ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Could not find xCluster replication group {id}"),
                    )
                })?;
                pb.producer_master_addresses = req.producer_master_addresses.clone();
                let config_entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut config: SysClusterConfigEntryPB = config_entity.parse().map_err(corrupt)?;
                let entry = config
                    .consumer_registry
                    .mut_or_insert_default()
                    .producer_map
                    .get_mut(id)
                    .ok_or_else(|| {
                        status(
                            StatusCode::NotFound,
                            format!("Could not find xCluster ReplicationGroup {req:?}"),
                        )
                    })?;
                entry.master_addrs = req.producer_master_addresses.clone();
                config.set_version(config.version() + 1);
                Ok((
                    Mutation::new()
                        .read(&entity)
                        .upsert_message(SysRowEntryType::UNIVERSE_REPLICATION, id.as_bytes(), &pb)?
                        .read(config_entity)
                        .upsert_message(
                            SysRowEntryType::CLUSTER_CONFIG,
                            &config_entity.key.id,
                            &config,
                        )?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// `AddTablesToReplication`: the alter group is set up with the tables the group does not
    /// have yet, retried while another alter of the group runs.
    pub(crate) async fn add_tables_to_replication(
        self: &Arc<Self>,
        token: &LeaderToken,
        deadline: Instant,
        add: AddTables,
    ) -> Result<(), MasterErrorPB> {
        let invalid = |m: String| {
            coded(
                Code::INVALID_REQUEST,
                status(StatusCode::InvalidArgument, m),
            )
        };
        if add.source_table_ids.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "No tables specified",
            )));
        }
        let snapshot = self.catalog.snapshot();
        let (_, pb) = universe(&snapshot, &add.group_id).ok_or_else(|| {
            unknown(status(
                StatusCode::NotFound,
                format!("Could not find xCluster replication group {}", add.group_id),
            ))
        })?;
        let db_scoped = !pb.db_scoped_info.namespace_infos.is_empty();
        if db_scoped {
            if let Some(ns) = &add.namespace {
                if ns.id().is_empty() {
                    return Err(unknown(status(
                        StatusCode::InvalidArgument,
                        "Invalid Namespace Id",
                    )));
                }
                if ns.name().is_empty() {
                    return Err(unknown(status(
                        StatusCode::InvalidArgument,
                        "Invalid Namespace name",
                    )));
                }
                if ns.database_type() != yb_proto::pb::common_types::YQLDatabase::YQL_DATABASE_PGSQL
                {
                    return Err(unknown(status(
                        StatusCode::InvalidArgument,
                        "Invalid Namespace database_type",
                    )));
                }
            } else if add.source_table_ids.len() != 1 {
                return Err(unknown(status(
                    StatusCode::InvalidArgument,
                    "When adding more than table to a DB scoped replication the namespace info \
                     must also be provided",
                )));
            }
        } else if add.namespace.is_some() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Cannot add namespaces to non DB scoped replication",
            )));
        }
        if !add.stream_ids.is_empty() && add.stream_ids.len() != add.source_table_ids.len() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Number of bootstrap ids must be equal to number of tables",
            )));
        }
        if add.target_table_id.is_some()
            && (add.source_table_ids.len() != 1 || add.namespace.is_some())
        {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "Target table id can only be specified when adding a single table",
            )));
        }
        let alter_id = format!("{}{ALTER_SUFFIX}", add.group_id);
        if universe(&snapshot, &alter_id).is_some() {
            return Err(invalid(
                "Alter for xCluster ReplicationGroup is already running".to_string(),
            ));
        }
        let mut stream_of = HashMap::new();
        if !add.stream_ids.is_empty() {
            for (t, s) in add.source_table_ids.iter().zip(&add.stream_ids) {
                stream_of.insert(t.clone(), s.clone());
            }
            if stream_of.len() != add.source_table_ids.len() {
                return Err(invalid(
                    "When providing bootstrap ids, the list of tables must be unique".to_string(),
                ));
            }
        }
        let existing: BTreeSet<&String> = pb.tables.iter().collect();
        let new_tables: Vec<String> = add
            .source_table_ids
            .iter()
            .filter(|t| !existing.contains(t))
            .cloned()
            .collect();
        if new_tables.is_empty() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                "xCluster ReplicationGroup already contains all requested tables",
            )));
        }
        let mut data = SetupData {
            group_id: alter_id.clone(),
            source_masters: pb.producer_master_addresses.clone(),
            transactional: pb.transactional(),
            automatic_ddl_mode: pb.db_scoped_info.automatic_ddl_mode(),
            ..Default::default()
        };
        if let Some(ns) = &add.namespace {
            data.source_namespace_ids
                .push(String::from_utf8_lossy(ns.id()).into_owned());
            let target =
                master_table_lifecycle::lookup::find_namespace_by_name(&snapshot, ns.name(), PGSQL)
                    .ok_or_else(|| {
                        coded(
                            Code::NAMESPACE_NOT_FOUND,
                            status(
                                StatusCode::NotFound,
                                format!("Keyspace name not found: {}", ns.name()),
                            ),
                        )
                    })?;
            data.target_namespace_ids
                .push(String::from_utf8_lossy(&target.key.id).into_owned());
        }
        for t in new_tables {
            if let Some(s) = stream_of.get(&t) {
                data.stream_ids.push(s.clone());
            }
            if let Some(target) = &add.target_table_id {
                data.target_table_ids.push(target.clone());
            }
            data.source_table_ids.push(t);
        }
        let timeout_at = Instant::now() + (self.options.alter_setup_timeout)();
        loop {
            match self.start_setup(token, data.clone(), &[]) {
                Ok(()) => return Ok(()),
                Err(s) if s.code == StatusCode::TryAgain => {
                    let _ = deadline;
                    if Instant::now() >= timeout_at {
                        return Err(unknown(status(
                            StatusCode::TimedOut,
                            format!("Waiting for setup replication of {alter_id}"),
                        )));
                    }
                    tokio::time::sleep(std::time::Duration::from_millis(100)).await;
                }
                Err(s) => return Err(unknown(s)),
            }
        }
    }

    /// `RemoveTablesFromReplicationGroupInternal`: the tables' streams leave the consumer
    /// registry and the tables the group's row, in one write; with `cleanup_source_streams` the
    /// streams are deleted on the source first. `edit` changes the row in the same write.
    pub(crate) async fn remove_tables_from_group(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        id: &str,
        tables: &[String],
        cleanup_source_streams: bool,
        edit: Option<&(dyn Fn(&mut SysUniverseReplicationEntryPB) + Sync)>,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let (_, pb) = universe(&snapshot, id).ok_or_else(|| {
            status(
                StatusCode::NotFound,
                format!("Could not find xCluster replication group {id}"),
            )
        })?;
        let existing: BTreeSet<&String> = pb.tables.iter().collect();
        let remove: BTreeSet<String> = tables
            .iter()
            .filter(|t| existing.contains(t))
            .cloned()
            .collect();
        tracing::info!("Removing tables {remove:?} from inbound xCluster replication group {id}");
        if remove.is_empty() && edit.is_none() {
            return Ok(());
        }
        let config = crate::setup::cluster_config(&snapshot)?;
        let streams: Vec<String> = config
            .consumer_registry
            .producer_map
            .get(id)
            .map(|e| {
                e.stream_map
                    .iter()
                    .filter(|(_, s)| remove.contains(&s.producer_table_id))
                    .map(|(k, _)| k.clone())
                    .collect()
            })
            .unwrap_or_default();
        if !streams.is_empty() && cleanup_source_streams {
            let mut req = DeleteCDCStreamRequestPB::new();
            req.stream_id = streams.iter().map(|s| s.clone().into_bytes()).collect();
            req.set_force_delete(true);
            req.set_ignore_errors(true);
            let _: DeleteCDCStreamResponsePB = call_leader(
                &*self.remote,
                id,
                &pb.producer_master_addresses,
                MASTER_SERVICE,
                "DeleteCDCStream",
                &req,
                self.remote_call_deadline(deadline),
            )
            .await
            .and_then(crate::remote::check)
            .map_err(|s| {
                status(
                    s.code,
                    format!("Unable to delete xCluster streams on source: {}", s.message),
                )
            })?;
        }
        self.writer
            .mutate(token, |snapshot| {
                let (entity, mut pb) = universe(snapshot, id).ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Could not find xCluster replication group {id}"),
                    )
                })?;
                if let Some(edit) = edit {
                    edit(&mut pb);
                }
                for t in &remove {
                    pb.table_streams.remove(t);
                    pb.validated_tables.remove(t);
                    pb.tables.retain(|x| x != t);
                }
                let mut m = Mutation::new().read(&entity).upsert_message(
                    SysRowEntryType::UNIVERSE_REPLICATION,
                    id.as_bytes(),
                    &pb,
                )?;
                let config_entity = snapshot
                    .cluster_config()
                    .ok_or_else(|| status(StatusCode::IllegalState, "No cluster config"))?;
                let mut config: SysClusterConfigEntryPB = config_entity.parse().map_err(corrupt)?;
                if let Some(entry) = config
                    .consumer_registry
                    .mut_or_insert_default()
                    .producer_map
                    .get_mut(id)
                {
                    for s in &streams {
                        entry.stream_map.remove(s);
                    }
                    if entry.stream_map.is_empty() && !remove.is_empty() {
                        tracing::warn!(
                            "All tables in xCluster replication group {id} have been removed"
                        );
                    }
                    config.set_version(config.version() + 1);
                    m = m.read(config_entity).upsert_message(
                        SysRowEntryType::CLUSTER_CONFIG,
                        &config_entity.key.id,
                        &config,
                    )?;
                }
                Ok((m, ()))
            })
            .await
            .map(|_| ())
    }

    /// `RemoveNamespaceFromReplicationGroup`: the namespace and its tables leave the group; the
    /// source's outbound group owns the streams.
    async fn remove_namespace_from_group(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        pb: &SysUniverseReplicationEntryPB,
        producer_ns: &str,
    ) -> Result<(), Status> {
        let id = pb.replication_group_id();
        if pb.db_scoped_info.namespace_infos.is_empty() {
            return Err(status(
                StatusCode::IllegalState,
                format!("Replication group {id} is not DB Scoped"),
            ));
        }
        let Some(consumer_ns) = pb
            .db_scoped_info
            .namespace_infos
            .iter()
            .find(|i| i.producer_namespace_id() == producer_ns)
            .map(|i| i.consumer_namespace_id().to_string())
        else {
            tracing::info!(
                "Producer Namespace {producer_ns} not found in xCluster replication group {id}"
            );
            return Ok(());
        };
        let automatic = pb.db_scoped_info.automatic_ddl_mode();
        let consumer_tables: BTreeSet<String> =
            eligible_tables(&self.catalog.snapshot(), &consumer_ns, automatic)?
                .into_iter()
                .map(|d| d.id)
                .collect();
        let producer_tables: Vec<String> = pb
            .validated_tables
            .iter()
            .filter(|(_, c)| consumer_tables.contains(*c))
            .map(|(p, _)| p.clone())
            .collect();
        let producer_ns = producer_ns.to_string();
        let edit = move |pb: &mut SysUniverseReplicationEntryPB| {
            pb.db_scoped_info
                .mut_or_insert_default()
                .namespace_infos
                .retain(|i| i.producer_namespace_id() != producer_ns);
        };
        self.remove_tables_from_group(token, deadline, id, &producer_tables, false, Some(&edit))
            .await?;
        if automatic {
            self.handle_extension_on_drop_replication(token, &consumer_ns)
                .await?;
        }
        Ok(())
    }
}
