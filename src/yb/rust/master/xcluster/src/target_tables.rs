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

//! New tables and xCluster, before they run (`GetPostTabletCreateTasks`): a table in a source
//! namespace gets its streams (see `checkpoint`), and a table in a DB-scoped target namespace
//! joins its inbound group (`AddTableToXClusterTargetTask`). The work runs as a task per table;
//! the assigner asks each pass whether it is done.

use std::collections::HashMap;
use std::time::Duration;

use master_leadership::LeaderToken;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_universe_replication_entry_pb::State as UniverseState, SysNamespaceEntryPB,
    SysTablesEntryPB, SysUniverseReplicationEntryPB,
};
use yb_proto::pb::master_replication::{
    get_xcluster_streams_request_pb::TableInfo as StreamsTableInfo, GetXClusterStreamsRequestPB,
    GetXClusterStreamsResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::outbound::table_pb;
use crate::setup::{cluster_config, universes, SetupData, ALTER_SUFFIX};
use crate::{is_eligible, status, XCluster};

/// Each new table's task: the term it runs in and, once done, its outcome.
pub(crate) type PostCreateTasks = HashMap<Vec<u8>, (i64, Option<Result<(), Status>>)>;

const POLL: Duration = Duration::from_millis(200);

impl XCluster {
    /// Starts the table's task once per term and reports its outcome when done.
    pub(crate) fn post_tablet_create_task(&self, table_id: Vec<u8>) -> Result<bool, Status> {
        if (self.options.disable_new_table_processing)() {
            return Ok(true);
        }
        let token = self.leadership.token()?;
        let mut tasks = self.post_create.lock().unwrap();
        match tasks.get(&table_id) {
            Some((term, Some(result))) if *term == token.term() => {
                let result = result.clone();
                tasks.remove(&table_id);
                return result.map(|()| true);
            }
            Some((term, None)) if *term == token.term() => return Ok(false),
            _ => {}
        }
        let Some(this) = self.me.upgrade() else {
            return Err(status(
                StatusCode::ShutdownInProgress,
                "xCluster is shutting down",
            ));
        };
        tasks.insert(table_id.clone(), (token.term(), None));
        tokio::spawn(async move {
            let mut result = this.add_new_table(&table_id).await;
            if result.is_ok() {
                result = this.add_table_to_target(&token, &table_id).await;
            }
            if let Some(entry) = this.post_create.lock().unwrap().get_mut(&table_id) {
                if entry.0 == token.term() {
                    entry.1 = Some(result);
                }
            }
        });
        Ok(false)
    }

    /// `ShouldAddTableToReplicationGroup`: an eligible table of one of a DB-scoped group's
    /// namespaces that the group does not have yet.
    pub(crate) fn should_add_to_group(
        &self,
        universe: &SysUniverseReplicationEntryPB,
        table_id: &[u8],
    ) -> Result<bool, Status> {
        let snapshot = self.catalog.snapshot();
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Ok(false);
        };
        let pb: SysTablesEntryPB = table.parse().map_err(crate::corrupt)?;
        if !is_eligible(
            &snapshot,
            table,
            &pb,
            universe.db_scoped_info.automatic_ddl_mode(),
        ) {
            return Ok(false);
        }
        if matches!(
            universe.state(),
            UniverseState::DELETED | UniverseState::DELETED_ERROR | UniverseState::FAILED
        ) {
            return Ok(false);
        }
        let ns = String::from_utf8_lossy(pb.namespace_id()).into_owned();
        if !universe.db_scoped_info.namespace_infos.is_empty() {
            if !crate::setup::includes_consumer_namespace(universe, &ns) {
                return Ok(false);
            }
        } else {
            // A transactional table-level group takes in the indexes of its tables, so reads at
            // the safe time see them.
            let indexed = pb.index_info.indexed_table_id();
            if !universe.transactional() || indexed.is_empty() {
                return Ok(false);
            }
            let indexed = String::from_utf8_lossy(indexed);
            let consumer = cluster_config(&snapshot)?
                .consumer_registry
                .producer_map
                .values()
                .flat_map(|p| p.stream_map.values())
                .any(|s| s.consumer_table_id == indexed);
            if !consumer {
                return Ok(false);
            }
        }
        if master_table_lifecycle::lookup::is_colocation_parent_table_id(table_id) {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Colocated parent tables can only be added during the initial xCluster \
                     replication setup: {}",
                    String::from_utf8_lossy(table_id)
                ),
            ));
        }
        let config = cluster_config(&snapshot)?;
        let Some(entry) = config
            .consumer_registry
            .producer_map
            .get(universe.replication_group_id())
        else {
            return Ok(false);
        };
        if entry.disable_stream {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Table belongs to xCluster replication group {} which is currently disabled",
                    universe.replication_group_id()
                ),
            ));
        }
        let id = String::from_utf8_lossy(table_id);
        Ok(!entry.stream_map.values().any(|s| s.consumer_table_id == id))
    }

    /// `AddTableToXClusterTargetTask`: the source's stream of the table (by its source table id
    /// in automatic mode, else by name) is added to the group as an alter, then the safe time
    /// must pass where it was, so it covers the new table.
    pub(crate) async fn add_table_to_target(
        &self,
        token: &LeaderToken,
        table_id: &[u8],
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        for universe in universes(&snapshot) {
            if !self.should_add_to_group(&universe, table_id)? {
                continue;
            }
            let group = universe.replication_group_id().to_string();
            let pb = table_pb(&snapshot, table_id)
                .ok_or_else(|| status(StatusCode::NotFound, "Table not found"))?;
            let ns = String::from_utf8_lossy(pb.namespace_id()).into_owned();
            if universe.db_scoped_info.namespace_infos.is_empty() {
                self.add_index_to_table_group(token, &universe, table_id, &pb, &ns)
                    .await?;
                continue;
            }
            let producer_ns = universe
                .db_scoped_info
                .namespace_infos
                .iter()
                .find(|i| i.consumer_namespace_id() == ns)
                .map(|i| i.producer_namespace_id().to_string())
                .ok_or_else(|| {
                    status(
                        StatusCode::NotFound,
                        format!("Namespace {ns} not found in replication group {group}"),
                    )
                })?;
            let automatic = universe.db_scoped_info.automatic_ddl_mode();
            let mut req = GetXClusterStreamsRequestPB::new();
            req.set_replication_group_id(group.clone());
            req.set_namespace_id(producer_ns);
            let source_table_id = pb.xcluster_table_info.xcluster_source_table_id();
            if !source_table_id.is_empty() {
                req.source_table_ids.push(source_table_id.to_vec());
                req.set_create_stream_if_missing(true);
            } else if automatic {
                return Err(status(
                    StatusCode::IllegalState,
                    format!(
                        "Table being added to xCluster automatic-mode replication lacks source \
                         table ID; it has pg_schema: {} tablename: {}",
                        pb.schema.DEPRECATED_pgschema_name(),
                        String::from_utf8_lossy(pb.name())
                    ),
                ));
            } else {
                let mut t = StreamsTableInfo::new();
                t.set_table_name(String::from_utf8_lossy(pb.name()).into_owned());
                t.set_pg_schema_name(pb.schema.DEPRECATED_pgschema_name().to_string());
                req.table_infos.push(t);
            }
            let data = SetupData {
                group_id: format!("{group}{ALTER_SUFFIX}"),
                source_masters: universe.producer_master_addresses.clone(),
                transactional: universe.transactional(),
                automatic_ddl_mode: automatic,
                ..Default::default()
            };
            let bootstrap = |s: Status| {
                status(
                    StatusCode::InvalidArgument,
                    format!(
                        "Failed to bootstrap table on the source universe of xCluster replication \
                         group {group}: {s}"
                    ),
                )
            };
            let resp: GetXClusterStreamsResponsePB = self
                .source_call(
                    &data,
                    "GetXClusterStreams",
                    &req,
                    Instant::now() + (self.options.remote_timeout)(),
                )
                .await
                .and_then(crate::remote::check)
                .map_err(bootstrap)?;
            if resp.not_ready() {
                return Err(bootstrap(status(
                    StatusCode::IllegalState,
                    "XCluster stream is not ready",
                )));
            }
            let info = resp.table_infos.first().ok_or_else(|| {
                bootstrap(status(
                    StatusCode::NotFound,
                    "No stream returned for the table",
                ))
            })?;
            let producer_table = String::from_utf8_lossy(info.table_id()).into_owned();
            let stream_id = String::from_utf8_lossy(info.xrepl_stream_id()).into_owned();
            tracing::info!(
                "Adding table {} to xcluster universe replication {group} with bootstrap_id: \
                 {stream_id} and producer_table_id: {producer_table}",
                String::from_utf8_lossy(table_id)
            );
            let Some(this) = self.me.upgrade() else {
                return Err(status(
                    StatusCode::ShutdownInProgress,
                    "xCluster is shutting down",
                ));
            };
            let add = crate::alter::AddTables {
                group_id: group.clone(),
                namespace: None,
                source_table_ids: vec![producer_table],
                stream_ids: vec![stream_id],
                target_table_id: automatic.then(|| String::from_utf8_lossy(table_id).into_owned()),
            };
            this.add_tables_to_replication(
                token,
                Instant::now() + (self.options.alter_setup_timeout)(),
                add,
            )
            .await
            .map_err(|e| crate::status_of(&e))?;
            loop {
                match self.setup_done(&data.group_id, true)? {
                    Some(result) => {
                        result?;
                        break;
                    }
                    None => tokio::time::sleep(POLL).await,
                }
            }
            self.wait_for_safe_time_to_cover(token, &ns).await?;
            tracing::info!(
                "Table {} successfully added to xCluster universe replication",
                String::from_utf8_lossy(table_id)
            );
        }
        Ok(())
    }

    /// The table-level path of `AddTableToXClusterTargetTask`: the source bootstraps a stream for
    /// the index by name (`BootstrapProducer`); the target's backfill fills in what was written
    /// before its bootstrap time, so the safe time must pass that time first.
    async fn add_index_to_table_group(
        &self,
        token: &LeaderToken,
        universe: &SysUniverseReplicationEntryPB,
        table_id: &[u8],
        pb: &SysTablesEntryPB,
        ns: &str,
    ) -> Result<(), Status> {
        use yb_proto::pb::master_replication::{
            BootstrapProducerRequestPB, BootstrapProducerResponsePB,
        };
        let group = universe.replication_group_id().to_string();
        let snapshot = self.catalog.snapshot();
        let ns_name = snapshot
            .get(SysRowEntryType::NAMESPACE, ns.as_bytes())
            .and_then(|n| n.parse::<SysNamespaceEntryPB>().ok())
            .map(|n| String::from_utf8_lossy(n.name()).into_owned())
            .ok_or_else(|| status(StatusCode::NotFound, format!("Namespace {ns} not found")))?;
        let mut req = BootstrapProducerRequestPB::new();
        req.db_type = Some(protobuf::EnumOrUnknown::new(
            yb_proto::pb::common_types::YQLDatabase::YQL_DATABASE_PGSQL,
        ));
        req.set_namespace_name(ns_name);
        req.pg_schema_name
            .push(self.target_schema_name(table_id, pb).await);
        req.table_name
            .push(String::from_utf8_lossy(pb.name()).into_owned());
        let data = SetupData {
            group_id: format!("{group}{ALTER_SUFFIX}"),
            source_masters: universe.producer_master_addresses.clone(),
            transactional: universe.transactional(),
            ..Default::default()
        };
        let bootstrap = |s: Status| {
            status(
                StatusCode::InvalidArgument,
                format!(
                    "Failed to bootstrap table on the source universe of xCluster replication \
                     group {group}: {s}"
                ),
            )
        };
        let resp: BootstrapProducerResponsePB = self
            .source_call(
                &data,
                "BootstrapProducer",
                &req,
                Instant::now() + (self.options.remote_timeout)(),
            )
            .await
            .and_then(crate::remote::check)
            .map_err(bootstrap)?;
        if resp.table_ids.len() != 1 || resp.bootstrap_ids.len() != 1 {
            return Err(bootstrap(status(
                StatusCode::IllegalState,
                format!(
                    "Expected one bootstrapped table, got {} tables and {} streams",
                    resp.table_ids.len(),
                    resp.bootstrap_ids.len()
                ),
            )));
        }
        let bootstrap_time = resp.bootstrap_time();
        if !crate::safe_time::is_valid(bootstrap_time) {
            return Err(status(
                StatusCode::IllegalState,
                format!("xCluster Bootstrap time is not valid {bootstrap_time}"),
            ));
        }
        let producer_table = resp.table_ids[0].clone();
        let stream_id = String::from_utf8_lossy(&resp.bootstrap_ids[0]).into_owned();
        tracing::info!(
            "Adding table to xcluster universe replication {group} with bootstrap_id:{stream_id}, \
             bootstrap_time:{bootstrap_time} and producer_table_id:{producer_table}"
        );
        let Some(this) = self.me.upgrade() else {
            return Err(status(
                StatusCode::ShutdownInProgress,
                "xCluster is shutting down",
            ));
        };
        let add = crate::alter::AddTables {
            group_id: group.clone(),
            namespace: None,
            source_table_ids: vec![producer_table],
            stream_ids: vec![stream_id],
            target_table_id: None,
        };
        this.add_tables_to_replication(
            token,
            Instant::now() + (self.options.alter_setup_timeout)(),
            add,
        )
        .await
        .map_err(|e| crate::status_of(&e))?;
        loop {
            match self.setup_done(&data.group_id, true)? {
                Some(result) => {
                    result?;
                    break;
                }
                None => tokio::time::sleep(POLL).await,
            }
        }
        self.wait_for_safe_time_past(token, ns, bootstrap_time)
            .await?;
        tracing::info!(
            "Table {} successfully added to xCluster universe replication",
            String::from_utf8_lossy(table_id)
        );
        Ok(())
    }

    /// `RefreshAndGetXClusterSafeTime` and `WaitForXClusterSafeTimeCaughtUp`: the safe time
    /// without `ddl_queue` moves past its value once the new table's tablets count.
    async fn wait_for_safe_time_to_cover(
        &self,
        token: &LeaderToken,
        ns: &str,
    ) -> Result<(), Status> {
        self.wait_for_safe_time_past(token, ns, 0).await
    }

    /// Waits until the namespace's safe time without `ddl_queue` passes both where it is now and
    /// `at_least`.
    async fn wait_for_safe_time_past(
        &self,
        token: &LeaderToken,
        ns: &str,
        at_least: u64,
    ) -> Result<(), Status> {
        self.compute_safe_time(token).await?;
        let get = || {
            self.safe_time
                .lock()
                .unwrap()
                .without_ddl_queue
                .get(ns)
                .copied()
        };
        let Some(initial) = get() else {
            return Ok(());
        };
        // Invalid before the namespace's first table; only progress matters.
        let initial = if crate::safe_time::is_valid(initial) {
            initial.max(at_least)
        } else {
            at_least
        };
        loop {
            tokio::time::sleep(POLL).await;
            self.compute_safe_time(token).await?;
            match get() {
                None => return Ok(()),
                Some(ht) if crate::safe_time::is_valid(ht) && ht > initial => return Ok(()),
                Some(ht) => {
                    tracing::debug!(
                        "Waiting for xCluster safe time {ht} to advance beyond {initial}"
                    );
                }
            }
        }
    }
}

impl XCluster {
    /// `RemoveDroppedTablesOnConsumer`: a consumer table that is gone or hidden leaves its group,
    /// and its stream is deleted on the source. C++ does this when the table is dropped and once
    /// after loading the catalog; reconciling each pass covers both.
    pub(crate) async fn remove_dropped_target_tables(
        &self,
        token: &LeaderToken,
    ) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let config = cluster_config(&snapshot)?;
        let mut by_group: HashMap<String, Vec<String>> = HashMap::new();
        for (group, entry) in &config.consumer_registry.producer_map {
            for stream in entry.stream_map.values() {
                let stripped = master_xrepl::strip_sequences_data_alias(&stream.consumer_table_id);
                let gone = snapshot
                    .get(SysRowEntryType::TABLE, stripped.as_bytes())
                    .is_none_or(|t| !master_table_lifecycle::lookup::is_visible_to_client(t));
                if gone {
                    by_group
                        .entry(group.clone())
                        .or_default()
                        .push(stream.producer_table_id.clone());
                }
            }
        }
        for (group, tables) in by_group {
            if crate::setup::universe(&snapshot, &group).is_none() {
                continue;
            }
            tracing::info!("Removing dropped tables {tables:?} from xcluster replication {group}");
            self.remove_tables_from_group(
                token,
                Instant::now() + (self.options.remote_timeout)(),
                &group,
                &tables,
                true,
                None,
            )
            .await?;
        }
        Ok(())
    }
}
