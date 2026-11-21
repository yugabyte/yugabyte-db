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

//! The safe time computation, with the cases of the C++ `xcluster_safe_time_service-test`.

use std::collections::HashMap;

use yb_proto::pb::catalog_entity_info::sys_universe_replication_entry_pb::State as UniverseState;
use yb_proto::pb::catalog_entity_info::SysUniverseReplicationEntryPB;
use yb_proto::pb::cdc_consumer::{ProducerEntryPB, ProducerTabletListPB, StreamEntryPB};

use super::*;

const DB1: &str = "db1";
const DB2: &str = "db2";
/// `HybridTime::kInvalid`.
const INVALID: u64 = u64::MAX - 1;
/// What the fake host's `HybridTimeNow` answers: the "leader safe time" a namespace with only
/// `ddl_queue` tablets gets under the DDL_QUEUE filter.
const NOW: u64 = 100_000_000_000 << 12;
const HT1: u64 = NOW - 1;
const HT2: u64 = NOW;
const HT3: u64 = NOW + 1;

/// Records the tables the service asks to create.
#[derive(Default)]
struct RecordingTables {
    created: Mutex<Vec<String>>,
}

impl CreatesTables for RecordingTables {
    fn create<'a>(
        &'a self,
        _token: &'a LeaderToken,
        _deadline: Instant,
        req: &'a CreateTableRequestPB,
        _resp: &'a mut CreateTableResponsePB,
    ) -> BoxFuture<'a, Result<(), MasterErrorPB>> {
        self.created.lock().unwrap().push(req.name().to_string());
        Box::pin(async { Ok(()) })
    }
}

/// A producer tablet replicated into a namespace, and whether it belongs to `ddl_queue`.
#[derive(Clone, Copy)]
struct Tablet {
    id: &'static str,
    ns: &'static str,
    ddl_queue: bool,
}

const fn t(id: &'static str, ns: &'static str) -> Tablet {
    Tablet {
        id,
        ns,
        ddl_queue: false,
    }
}

const fn ddl(id: &'static str, ns: &'static str) -> Tablet {
    Tablet {
        id,
        ns,
        ddl_queue: true,
    }
}

/// The C++ test's default consumer registry: t1 in db1, t2 and t3 in db2.
const DEFAULT: [Tablet; 3] = [t("t1", DB1), t("t2", DB2), t("t3", DB2)];

fn table_row(id: &str, ns: &str, name: &str) -> RowChange {
    let mut table = ysql_table(name);
    table.set_namespace_id(ns.as_bytes().to_vec());
    RowChange::Upsert {
        entry_type: SysRowEntryType::TABLE,
        id: id.as_bytes().to_vec(),
        data: table.write_to_bytes().unwrap(),
    }
}

/// A transactional inbound group `GROUP`, the consumer tables of db1 and db2, and (unless
/// `with_table` is false) a running `system.xcluster_safe_time`.
async fn setup(h: &Harness, with_table: bool) {
    let mut group = SysUniverseReplicationEntryPB::new();
    group.set_replication_group_id(GROUP.into());
    group.state = Some(EnumOrUnknown::new(UniverseState::ACTIVE));
    group.set_transactional(true);
    let mut rows = vec![
        RowChange::Upsert {
            entry_type: SysRowEntryType::UNIVERSE_REPLICATION,
            id: GROUP.as_bytes().to_vec(),
            data: group.write_to_bytes().unwrap(),
        },
        table_row("ct-db1", DB1, "t"),
        table_row("ct-db2", DB2, "t"),
    ];
    if with_table {
        rows.push(table_row(
            "safe-time-table",
            "00000000000000000000000000000001",
            "xcluster_safe_time",
        ));
    }
    h.fake.apply(rows);
    h.catalog.barrier().await;
}

fn stream(consumer_table: &str, producer_table: &str, tablet: &str) -> StreamEntryPB {
    let mut stream = StreamEntryPB::new();
    stream.consumer_table_id = consumer_table.into();
    stream.producer_table_id = producer_table.into();
    let mut list = ProducerTabletListPB::new();
    list.tablets.push(tablet.into());
    stream
        .consumer_producer_tablet_map
        .insert(format!("consumer-{tablet}"), list);
    stream
}

async fn set_registry_streams(h: &Harness, streams: Vec<StreamEntryPB>) {
    let mut entry = ProducerEntryPB::new();
    for (i, s) in streams.into_iter().enumerate() {
        entry.stream_map.insert(format!("stream{i}"), s);
    }
    let mut config = SysClusterConfigEntryPB::new();
    if !entry.stream_map.is_empty() {
        config
            .consumer_registry
            .mut_or_insert_default()
            .producer_map
            .insert(GROUP.into(), entry);
    }
    h.fake.apply(vec![RowChange::Upsert {
        entry_type: SysRowEntryType::CLUSTER_CONFIG,
        id: Vec::new(),
        data: config.write_to_bytes().unwrap(),
    }]);
    h.catalog.barrier().await;
}

/// The consumer registry: one stream per producer tablet.
async fn set_registry(h: &Harness, tablets: &[Tablet]) {
    let streams = tablets
        .iter()
        .map(|tablet| {
            let mut s = stream(&format!("ct-{}", tablet.ns), "producer-table", tablet.id);
            s.is_ddl_queue_table = tablet.ddl_queue;
            s
        })
        .collect();
    set_registry_streams(h, streams).await;
}

fn row(tablet_column: &str, ht: u64) -> XClusterSafeTimeRowPB {
    let mut r = XClusterSafeTimeRowPB::new();
    r.set_replication_group_id(GROUP.into());
    r.set_tablet_id(tablet_column.into());
    r.set_safe_time(ht as i64);
    r
}

/// Sets the tablet's row in `system.xcluster_safe_time`, as its tserver's poller does.
fn put(h: &Harness, tablet: &str, ht: u64) {
    let mut rows = h.host.safe_time_rows.lock().unwrap();
    rows.retain(|r| r.tablet_id() != tablet);
    rows.push(row(tablet, ht));
}

fn remove(h: &Harness, tablet: &str) {
    h.host
        .safe_time_rows
        .lock()
        .unwrap()
        .retain(|r| r.tablet_id() != tablet);
}

/// The tablet columns of the rows deleted so far, sorted.
fn deleted(h: &Harness) -> Vec<String> {
    let mut out: Vec<String> = h
        .host
        .safe_time_deletes
        .lock()
        .unwrap()
        .iter()
        .map(|r| r.tablet_id().to_string())
        .collect();
    out.sort();
    out
}

async fn compute(h: &Harness) {
    h.xcluster.compute_safe_time(&h.token).await.unwrap();
    h.catalog.barrier().await;
}

/// The persisted `XCLUSTER_SAFE_TIME` map.
fn safe_times(h: &Harness) -> HashMap<String, u64> {
    crate::safe_time::safe_time_map(&h.catalog.snapshot())
}

fn safe_time_for(h: &Harness, ns: &str, filter: XClusterSafeTimeFilter) -> u64 {
    let mut req = GetXClusterSafeTimeForNamespaceRequestPB::new();
    req.set_namespace_id(ns.into());
    req.set_filter(filter);
    let mut resp = GetXClusterSafeTimeForNamespaceResponsePB::new();
    h.xcluster
        .get_xcluster_safe_time_for_namespace(&req, &mut resp)
        .unwrap();
    resp.safe_time_ht()
}

/// (no filter, DDL_QUEUE filter) for the namespace.
fn both(h: &Harness, ns: &str) -> (u64, u64) {
    (
        safe_time_for(h, ns, XClusterSafeTimeFilter::NONE),
        safe_time_for(h, ns, XClusterSafeTimeFilter::DDL_QUEUE),
    )
}

fn map(entries: &[(&str, u64)]) -> HashMap<String, u64> {
    entries.iter().map(|(k, v)| (k.to_string(), *v)).collect()
}

#[tokio::test]
async fn safe_time_with_no_replication_creates_nothing() {
    let tables = Arc::new(RecordingTables::default());
    let h = Harness::with_tables(XClusterOptions::default(), tables.clone()).await;
    setup(&h, false).await;
    compute(&h).await;
    assert!(safe_times(&h).is_empty());
    assert!(deleted(&h).is_empty());
    assert!(tables.created.lock().unwrap().is_empty());
}

#[tokio::test]
async fn safe_time_table_is_created_once_tablets_are_replicated() {
    let tables = Arc::new(RecordingTables::default());
    let h = Harness::with_tables(XClusterOptions::default(), tables.clone()).await;
    setup(&h, false).await;
    set_registry(&h, &DEFAULT).await;
    compute(&h).await;
    assert_eq!(*tables.created.lock().unwrap(), vec![String::new()]);

    // The table exists but no tserver has written to it: every namespace is invalid.
    setup(&h, true).await;
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, INVALID), (DB2, INVALID)]));
    assert!(deleted(&h).is_empty());
    assert_eq!(tables.created.lock().unwrap().len(), 1);
}

#[tokio::test]
async fn safe_time_is_the_minimum_of_caught_up_tablets() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(&h, &DEFAULT).await;
    for tablet in ["t1", "t2", "t3"] {
        put(&h, tablet, HT1);
    }
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1), (DB2, HT1)]));
    assert!(deleted(&h).is_empty());
}

#[tokio::test]
async fn a_missing_tablet_invalidates_its_namespace_and_safe_time_never_goes_back() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(&h, &DEFAULT).await;
    put(&h, "t1", HT2);
    put(&h, "t2", HT2);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT2), (DB2, INVALID)]));
    assert!(deleted(&h).is_empty());

    // The tablet catches up.
    put(&h, "t3", HT2);
    compute(&h).await;
    assert_eq!(safe_times(&h)[DB2], HT2);

    // A stale YCQL write moves the tablet's time back; the namespace's does not move.
    put(&h, "t3", HT1);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT2), (DB2, HT2)]));
    assert!(deleted(&h).is_empty());
}

#[tokio::test]
async fn a_lagging_tablet_holds_its_namespace_back() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(&h, &DEFAULT).await;
    put(&h, "t1", HT2);
    put(&h, "t2", HT2);
    put(&h, "t3", HT1);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT2), (DB2, HT1)]));
    assert!(deleted(&h).is_empty());
}

#[tokio::test]
async fn rows_of_tablets_no_longer_replicated_are_deleted() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(&h, &DEFAULT).await;
    for tablet in ["t1", "t2", "t3"] {
        put(&h, tablet, HT2);
    }
    put(&h, "t4", HT3);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT2), (DB2, HT2)]));
    assert_eq!(deleted(&h), vec!["t4"]);
}

#[tokio::test]
async fn stopping_replication_deletes_every_row_and_the_namespaces() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(&h, &DEFAULT).await;
    for tablet in ["t1", "t2", "t3"] {
        put(&h, tablet, HT1);
    }
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1), (DB2, HT1)]));

    set_registry(&h, &[]).await;
    compute(&h).await;
    assert!(safe_times(&h).is_empty());
    assert_eq!(deleted(&h), vec!["t1", "t2", "t3"]);

    // Replication again: the rows are gone until tservers write them.
    set_registry(&h, &DEFAULT).await;
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, INVALID), (DB2, INVALID)]));
}

#[tokio::test]
async fn the_ddl_queue_filter_leaves_out_ddl_queue_tablets() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(
        &h,
        &[
            ddl("t1", DB1),
            ddl("t2", DB2),
            t("t3", DB2),
            t("t4", DB1),
            t("t5", DB1),
        ],
    )
    .await;
    put(&h, "t1", HT1);
    put(&h, "t4", HT2);
    put(&h, "t5", HT3);
    put(&h, "t2", HT2);
    put(&h, "t3", INVALID);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1), (DB2, INVALID)]));
    assert!(deleted(&h).is_empty());
    assert_eq!(both(&h, DB1), (HT1, HT2));
    assert_eq!(both(&h, DB2), (INVALID, INVALID));

    // Both advance.
    put(&h, "t1", HT3);
    put(&h, "t3", HT3);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT2), (DB2, HT2)]));
    assert_eq!(both(&h, DB1), (HT2, HT2));
    assert_eq!(both(&h, DB2), (HT2, HT3));

    // Tablet times going back move neither.
    put(&h, "t1", HT1);
    put(&h, "t3", HT1);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT2), (DB2, HT2)]));
    assert_eq!(both(&h, DB1), (HT2, HT2));
    assert_eq!(both(&h, DB2), (HT2, HT3));
}

#[tokio::test]
async fn a_namespace_with_only_ddl_queue_tablets_gets_the_leader_time_under_the_filter() {
    let h = Harness::new().await;
    setup(&h, true).await;
    set_registry(&h, &[ddl("t1", DB1)]).await;
    put(&h, "t1", HT1);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1)]));
    assert!(deleted(&h).is_empty());
    assert_eq!(both(&h, DB1), (HT1, NOW));

    // A second tablet.
    set_registry(&h, &[ddl("t1", DB1), t("t2", DB1)]).await;
    put(&h, "t2", HT3);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1)]));
    assert_eq!(both(&h, DB1), (HT1, HT3));

    // Removing it does not move the filtered time back to the leader's.
    set_registry(&h, &[ddl("t1", DB1)]).await;
    remove(&h, "t2");
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1)]));
    assert_eq!(both(&h, DB1), (HT1, HT3));
}

/// `SafeTimeTablePK`: a `sequences_data` alias stream's row is keyed by
/// `sequence.<source namespace>.<tablet>`, so it is told apart from a plain row of the same
/// tablet.
#[tokio::test]
async fn a_sequences_data_row_is_keyed_by_its_namespace_and_tablet() {
    let h = Harness::new().await;
    setup(&h, true).await;
    let alias = |ns: &str| format!("0000ffff00003000800000000000ffff.sequences_data_for.{ns}");
    set_registry_streams(
        &h,
        vec![
            stream("ct-db1", "producer-table", "t1"),
            stream(&alias(DB1), &alias("source-ns"), "seq-tablet"),
        ],
    )
    .await;
    put(&h, "t1", HT2);
    put(&h, "sequence.source-ns.seq-tablet", HT1);
    // The same tablet, not as an alias: no producer tablet has this key.
    put(&h, "seq-tablet", HT3);
    compute(&h).await;
    assert_eq!(safe_times(&h), map(&[(DB1, HT1)]));
    assert_eq!(deleted(&h), vec!["seq-tablet"]);
}

#[tokio::test]
async fn the_safe_time_for_a_namespace_needs_the_table_read_once() {
    let h = Harness::new().await;
    let mut req = GetXClusterSafeTimeForNamespaceRequestPB::new();
    req.set_namespace_id(DB1.into());
    req.set_filter(XClusterSafeTimeFilter::NONE);
    let mut resp = GetXClusterSafeTimeForNamespaceResponsePB::new();
    let e = h
        .xcluster
        .get_xcluster_safe_time_for_namespace(&req, &mut resp)
        .unwrap_err();
    assert_eq!(e.status.message(), "Safe time table is not ready yet.");

    setup(&h, true).await;
    compute(&h).await;
    let e = h
        .xcluster
        .get_xcluster_safe_time_for_namespace(&req, &mut resp)
        .unwrap_err();
    assert_eq!(
        e.status.message(),
        "Could not find safe time entry for namespace db1"
    );
}
