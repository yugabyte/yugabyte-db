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

//! The target's xCluster safe time (`XClusterSafeTimeService`): tservers write each producer
//! tablet's applied time to `system.xcluster_safe_time`; a namespace's safe time is the minimum
//! over the producer tablets of its transactional groups, never going back. The result is the
//! `XCLUSTER_SAFE_TIME` row, which tservers get by heartbeat.

use std::collections::{BTreeMap, HashMap, HashSet};

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, XClusterSafeTimePB};
use yb_proto::pb::master_ddl::{CreateTableRequestPB, CreateTableResponsePB};
use yb_proto::pb::master_replication::{
    get_xcluster_safe_time_response_pb::NamespaceSafeTimePB,
    GetXClusterSafeTimeForNamespaceRequestPB, GetXClusterSafeTimeForNamespaceResponsePB,
    GetXClusterSafeTimeResponsePB, XClusterSafeTimeFilter,
};
use yb_proto::pb::master_rust_peer::{
    XClusterSafeTimeCreateTableRequestRequestPB, XClusterSafeTimeCreateTableRequestResponsePB,
    XClusterSafeTimeDeleteRequestPB, XClusterSafeTimeDeleteResponsePB,
    XClusterSafeTimeReadRequestPB, XClusterSafeTimeReadResponsePB, XClusterSafeTimeRowPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::setup::{cluster_config, universes};
use crate::{status, unknown, XCluster};

/// `HybridTime::kMin`, `kMax` and `kInvalid`.
const HT_MIN: u64 = 0;
const HT_MAX: u64 = u64::MAX;
const HT_INVALID: u64 = u64::MAX - 1;
/// `kTabletIdColumnSequencePrefix` and `kTabletIdColumnSeparator`.
const SEQUENCE_PREFIX: &str = "sequence.";

/// `HybridTime::is_special` negated.
pub(crate) fn is_valid(ht: u64) -> bool {
    !matches!(ht, HT_MIN | HT_MAX | HT_INVALID)
}

/// The `XCLUSTER_SAFE_TIME` row's map.
pub(crate) fn safe_time_map(snapshot: &CatalogSnapshot) -> HashMap<String, u64> {
    snapshot
        .get(SysRowEntryType::XCLUSTER_SAFE_TIME, b"")
        .and_then(|e| e.parse::<XClusterSafeTimePB>().ok())
        .map(|pb| pb.safe_time_map.into_iter().collect())
        .unwrap_or_default()
}

/// A producer tablet as its safe time row names it (`SafeTimeTablePK`): the group and the
/// tablet column, which for a `sequences_data` alias carries the namespace.
type Key = (String, String);

fn key(group: &str, tablet: &str, producer_table: &str) -> Key {
    let column = match producer_table.split_once(".sequences_data_for.") {
        Some((_, ns)) => format!("{SEQUENCE_PREFIX}{ns}.{tablet}"),
        None => tablet.to_string(),
    };
    (group.to_string(), column)
}

/// What a pass computed, kept for the RPCs.
#[derive(Default)]
pub(crate) struct SafeTimeState {
    /// The safe times with `ddl_queue` tablets left out (`XClusterSafeTimeFilter::DDL_QUEUE`).
    pub without_ddl_queue: HashMap<String, u64>,
    /// The table has been read once.
    pub table_ready: bool,
}

impl XCluster {
    /// `RefreshProducerTabletToNamespaceMap`: each producer tablet of a transactional group, with
    /// its consumer namespace, and the tablets of `ddl_queue` streams.
    fn producer_tablets(
        &self,
        snapshot: &CatalogSnapshot,
    ) -> Result<(HashMap<Key, String>, HashSet<String>), Status> {
        let transactional: HashSet<String> = universes(snapshot)
            .into_iter()
            .filter(|u| u.transactional())
            .map(|u| u.replication_group_id().to_string())
            .collect();
        let mut tablets = HashMap::new();
        let mut ddl_queue = HashSet::new();
        if transactional.is_empty() {
            return Ok((tablets, ddl_queue));
        }
        let config = cluster_config(snapshot)?;
        for (group, entry) in &config.consumer_registry.producer_map {
            if !transactional.contains(group) {
                continue;
            }
            for stream in entry.stream_map.values() {
                let consumer = &stream.consumer_table_id;
                let ns = match consumer.split_once(".sequences_data_for.") {
                    Some((_, ns)) => ns.to_string(),
                    None => {
                        let Some(table) = snapshot.get(SysRowEntryType::TABLE, consumer.as_bytes())
                        else {
                            continue;
                        };
                        String::from_utf8_lossy(
                            &table
                                .table()
                                .map(|t| t.namespace_id.clone())
                                .unwrap_or_default(),
                        )
                        .into_owned()
                    }
                };
                for list in stream.consumer_producer_tablet_map.values() {
                    for tablet in &list.tablets {
                        tablets.insert(key(group, tablet, &stream.producer_table_id), ns.clone());
                        if stream.is_ddl_queue_table {
                            ddl_queue.insert(tablet.clone());
                        }
                    }
                }
            }
        }
        Ok((tablets, ddl_queue))
    }

    fn safe_time_table_exists(snapshot: &CatalogSnapshot) -> bool {
        snapshot.iter(SysRowEntryType::TABLE).any(|t| {
            t.table().is_some_and(|h| {
                h.namespace_id.as_slice() == b"00000000000000000000000000000001"
                    && h.name == "xcluster_safe_time"
            }) && t.table().is_some_and(|h| {
                // `WaitForCreateTableToFinish`: its tablets serve.
                h.state == sys_tables_entry_pb::State::RUNNING as i32
                    || h.state == sys_tables_entry_pb::State::ALTERING as i32
            })
        })
    }

    /// `CreateXClusterSafeTimeTableIfNotFound`.
    async fn create_safe_time_table(&self, token: &LeaderToken) -> Result<(), Status> {
        let resp: XClusterSafeTimeCreateTableRequestResponsePB = self
            .host_call(
                "XClusterSafeTimeCreateTableRequest",
                XClusterSafeTimeCreateTableRequestRequestPB::new(),
            )
            .await?;
        let req: CreateTableRequestPB = resp.request.unwrap_or_default();
        let mut create = CreateTableResponsePB::new();
        match self
            .tables
            .create(
                token,
                Instant::now() + (self.options.rpc_timeout)(),
                &req,
                &mut create,
            )
            .await
        {
            Ok(()) => Ok(()),
            Err(e) if e.code() == Code::OBJECT_ALREADY_PRESENT => Ok(()),
            Err(e) => Err(crate::status_of(&e)),
        }
    }

    /// `ComputeSafeTime`: the new map is written when it changed; rows of tablets no longer
    /// replicated are deleted.
    pub(crate) async fn compute_safe_time(&self, token: &LeaderToken) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let (tablets, ddl_queue) = self.producer_tablets(&snapshot)?;
        let exists = Self::safe_time_table_exists(&snapshot);
        if !exists {
            if tablets.is_empty() {
                return self.write_safe_times(token, BTreeMap::new()).await;
            }
            // Setups create it; this handles groups from before an upgrade too.
            return self.create_safe_time_table(token).await;
        }
        let rows: XClusterSafeTimeReadResponsePB = self
            .host_call("XClusterSafeTimeRead", XClusterSafeTimeReadRequestPB::new())
            .await?;
        self.safe_time.lock().unwrap().table_ready = true;
        let mut table: HashMap<Key, u64> = rows
            .rows
            .iter()
            .map(|r| {
                (
                    (
                        r.replication_group_id().to_string(),
                        r.tablet_id().to_string(),
                    ),
                    r.safe_time() as u64,
                )
            })
            .collect();
        let mut by_ns: BTreeMap<String, u64> = BTreeMap::new();
        let mut without_ddl: BTreeMap<String, u64> = BTreeMap::new();
        for (k, ns) in &tablets {
            by_ns.insert(ns.clone(), HT_MAX);
            without_ddl.insert(ns.clone(), HT_MAX);
            table.entry(k.clone()).or_insert(HT_INVALID);
        }
        let mut stale = Vec::new();
        for (k, ht) in &table {
            let Some(ns) = tablets.get(k) else {
                stale.push(k.clone());
                continue;
            };
            let tablet = k.1.rsplit('.').next().unwrap_or(&k.1);
            let in_ddl_queue = ddl_queue.contains(tablet);
            if !is_valid(*ht) {
                by_ns.insert(ns.clone(), HT_INVALID);
                if !in_ddl_queue {
                    without_ddl.insert(ns.clone(), HT_INVALID);
                }
                continue;
            }
            let entry = by_ns.get_mut(ns).expect("inserted");
            if *entry != HT_INVALID {
                *entry = (*entry).min(*ht);
                if !in_ddl_queue {
                    let e = without_ddl.get_mut(ns).expect("inserted");
                    if *e != HT_INVALID {
                        *e = (*e).min(*ht);
                    }
                }
            }
        }
        let previous = safe_time_map(&snapshot);
        let at_least_previous = |map: &mut BTreeMap<String, u64>,
                                 previous: &HashMap<String, u64>| {
            for (ns, ht) in map.iter_mut() {
                let prev = previous.get(ns).copied().unwrap_or(HT_INVALID);
                if !(is_valid(*ht) && (!is_valid(prev) || *ht > prev)) {
                    *ht = prev;
                }
            }
        };
        at_least_previous(&mut by_ns, &previous);
        let now = self.xrepl.host_now()?;
        for ht in without_ddl.values_mut() {
            if *ht == HT_MAX {
                *ht = now;
            }
        }
        {
            let mut state = self.safe_time.lock().unwrap();
            let prev = state.without_ddl_queue.clone();
            at_least_previous(&mut without_ddl, &prev);
            state.without_ddl_queue = without_ddl.into_iter().collect();
        }
        self.write_safe_times(token, by_ns).await?;
        if !stale.is_empty() {
            let mut req = XClusterSafeTimeDeleteRequestPB::new();
            for (group, tablet) in stale {
                let mut row = XClusterSafeTimeRowPB::new();
                row.set_replication_group_id(group);
                row.set_tablet_id(tablet);
                req.rows.push(row);
            }
            let _: XClusterSafeTimeDeleteResponsePB =
                self.host_call("XClusterSafeTimeDelete", req).await?;
        }
        Ok(())
    }

    /// `SetXClusterNamespaceToSafeTimeMap`, when the map changed.
    async fn write_safe_times(
        &self,
        token: &LeaderToken,
        map: BTreeMap<String, u64>,
    ) -> Result<(), Status> {
        self.writer
            .mutate(token, |snapshot| {
                let current = snapshot.get(SysRowEntryType::XCLUSTER_SAFE_TIME, b"");
                let old: BTreeMap<String, u64> = current
                    .and_then(|e| e.parse::<XClusterSafeTimePB>().ok())
                    .map(|pb| pb.safe_time_map.into_iter().collect())
                    .unwrap_or_default();
                if old == map {
                    return Ok((Mutation::new(), ()));
                }
                let mut pb = XClusterSafeTimePB::new();
                pb.safe_time_map = map.clone().into_iter().collect();
                let m = match current {
                    Some(e) => Mutation::new().read(e),
                    None => Mutation::new().read_absent(SysRowEntryType::XCLUSTER_SAFE_TIME, b""),
                };
                Ok((
                    m.upsert_message(SysRowEntryType::XCLUSTER_SAFE_TIME, b"", &pb)?,
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// `GetXClusterSafeTime`: each namespace's safe time, its lag behind now and its skew (the
    /// most caught up tablet's lead over it).
    pub async fn get_xcluster_safe_time(
        &self,
        token: &LeaderToken,
        resp: &mut GetXClusterSafeTimeResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let internal = |s: Status| {
            master_ts_manager::master_error_coded(Code::INTERNAL_ERROR, s.code, &s.message)
        };
        self.compute_safe_time(token).await.map_err(internal)?;
        let snapshot = self.catalog.snapshot();
        let current = safe_time_map(&snapshot);
        let (tablets, _) = self.producer_tablets(&snapshot).map_err(internal)?;
        let mut max: HashMap<String, u64> = HashMap::new();
        if Self::safe_time_table_exists(&snapshot) {
            let rows: XClusterSafeTimeReadResponsePB = self
                .host_call("XClusterSafeTimeRead", XClusterSafeTimeReadRequestPB::new())
                .await
                .map_err(internal)?;
            for r in rows.rows {
                let k = (
                    r.replication_group_id().to_string(),
                    r.tablet_id().to_string(),
                );
                let Some(ns) = tablets.get(&k) else {
                    continue;
                };
                let ht = r.safe_time() as u64;
                let e = max.entry(ns.clone()).or_insert(ht);
                if !is_valid(ht) {
                    *e = HT_INVALID;
                } else if is_valid(*e) && *e < ht {
                    *e = ht;
                }
            }
        }
        let now_micros = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_or(0, |d| d.as_micros() as u64);
        let mut namespaces: Vec<_> = current.into_iter().collect();
        namespaces.sort();
        for (ns, ht) in namespaces {
            let mut e = NamespaceSafeTimePB::new();
            e.set_namespace_id(ns.clone());
            e.set_safe_time_ht(ht);
            e.set_safe_time_lag(now_micros.saturating_sub(ht >> 12));
            let skew = match max.get(&ns) {
                Some(m) if is_valid(ht) && is_valid(*m) => (m >> 12).saturating_sub(ht >> 12),
                _ => u64::MAX,
            };
            e.set_safe_time_skew(skew);
            let name = XCluster::namespace_name(&snapshot, &ns).map_err(internal)?;
            e.set_namespace_name(name);
            resp.namespace_safe_times.push(e);
        }
        Ok(())
    }

    /// `GetXClusterSafeTimeForNamespace`.
    pub fn get_xcluster_safe_time_for_namespace(
        &self,
        req: &GetXClusterSafeTimeForNamespaceRequestPB,
        resp: &mut GetXClusterSafeTimeForNamespaceResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let ns = req.namespace_id();
        if ns.is_empty() || !req.has_filter() {
            return Err(unknown(status(
                StatusCode::InvalidArgument,
                format!("Missing required fields in request: {req:?}"),
            )));
        }
        let state = self.safe_time.lock().unwrap();
        if !state.table_ready {
            return Err(unknown(status(
                StatusCode::IllegalState,
                "Safe time table is not ready yet.",
            )));
        }
        let ht = match req.filter() {
            XClusterSafeTimeFilter::NONE => {
                safe_time_map(&self.catalog.snapshot()).get(ns).copied()
            }
            XClusterSafeTimeFilter::DDL_QUEUE => state.without_ddl_queue.get(ns).copied(),
        };
        let ht = ht.ok_or_else(|| {
            unknown(status(
                StatusCode::NotFound,
                format!("Could not find safe time entry for namespace {ns}"),
            ))
        })?;
        resp.set_safe_time_ht(ht);
        Ok(())
    }
}
