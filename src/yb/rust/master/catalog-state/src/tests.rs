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

use std::collections::BTreeMap;
use std::sync::Arc;
use std::time::Duration;

use master_catalog_entity::HotFields;
use master_leadership::ready_for_tests;
use master_sys_catalog::{FakeSysCatalog, HybridTime, RowChange};
use proptest::prelude::*;
use protobuf::Message;
use yb_proto::pb::catalog_entity_info::{
    sys_tablets_entry_pb, SysClusterConfigEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};

use super::*;

fn table_row(name: &str, version: u32) -> Vec<u8> {
    let mut table = SysTablesEntryPB::new();
    table.set_name(name.as_bytes().to_vec());
    table.set_namespace_id(b"ns".to_vec());
    table.set_version(version);
    table.schema = Some(Default::default()).into();
    table.write_to_bytes().unwrap()
}

fn tablet_row(table_id: &str, start: &str, state: sys_tablets_entry_pb::State) -> Vec<u8> {
    let mut tablet = SysTabletsEntryPB::new();
    tablet.set_table_id(table_id.as_bytes().to_vec());
    tablet.set_state(state);
    tablet
        .partition
        .mut_or_insert_default()
        .set_partition_key_start(start.as_bytes().to_vec());
    tablet.write_to_bytes().unwrap()
}

fn config_row(version: i32) -> Vec<u8> {
    let mut config = SysClusterConfigEntryPB::new();
    config.set_version(version);
    config.write_to_bytes().unwrap()
}

fn upsert(entry_type: SysRowEntryType, id: &str, data: Vec<u8>) -> RowChange {
    RowChange::Upsert {
        entry_type,
        id: id.as_bytes().to_vec(),
        data,
    }
}

struct Setup {
    fake: Arc<FakeSysCatalog>,
    catalog: Arc<Catalog>,
}

fn start(fake: Arc<FakeSysCatalog>, events: master_sys_catalog::ApplyEvents) -> Setup {
    let catalog = Catalog::new(fake.sender().clone());
    tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
    Setup { fake, catalog }
}

fn snapshot_rows(snapshot: &CatalogSnapshot) -> BTreeMap<(i32, Vec<u8>), Vec<u8>> {
    snapshot
        .rows()
        .map(|(t, id, data)| ((t, id.to_vec()), data.to_vec()))
        .collect()
}

#[tokio::test]
async fn a_load_keeps_ops_applied_after_its_scan_below_its_read_time() {
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "t1",
        table_row("a", 1),
    )]);
    let s = start(fake, events);
    s.catalog.barrier().await;
    let as_of = s.catalog.snapshot().as_of;
    // A snapshot op applied after the scan, at a hybrid time the scan already covered.
    s.fake.apply_at(
        as_of,
        vec![upsert(SysRowEntryType::TABLE, "t2", table_row("b", 1))],
    );
    s.catalog.barrier().await;
    let snapshot = s.catalog.snapshot();
    assert_eq!(snapshot.count(SysRowEntryType::TABLE), 2);
    assert_eq!(snapshot_rows(&snapshot), s.fake.rows_at(s.fake.now()));
}

#[tokio::test]
async fn a_requested_reload_fences_the_writer_until_it_is_published() {
    let (fake, _events) = FakeSysCatalog::new();
    let catalog = Catalog::new(fake.sender().clone());
    assert!(!catalog.is_reloading());
    catalog.request_reload("test");
    assert!(catalog.is_reloading());
    catalog.finish_reloads(1);
    assert!(!catalog.is_reloading());
}

#[tokio::test]
async fn rows_cpp_loads_do_not_stop_the_materializer() {
    let (fake, events) = FakeSysCatalog::new();
    let s = start(fake, events);
    s.catalog.barrier().await;
    s.fake.apply(vec![
        upsert(SysRowEntryType::UNKNOWN, "new-type", vec![1, 2, 3]),
        // A TABLE row missing its required fields.
        upsert(SysRowEntryType::TABLE, "broken", vec![]),
        upsert(SysRowEntryType::TABLE, "t1", table_row("a", 1)),
    ]);
    s.catalog.barrier().await;
    let snapshot = s.catalog.snapshot();
    assert_eq!(snapshot.count(SysRowEntryType::UNKNOWN), 0);
    assert_eq!(snapshot.count(SysRowEntryType::TABLE), 2);
    let broken = snapshot.get(SysRowEntryType::TABLE, b"broken").unwrap();
    assert!(matches!(broken.hot, HotFields::Other));
    assert!(snapshot.table_by_name(b"ns", "a").is_some());
}

#[tokio::test]
async fn a_tablet_mapped_by_parent_id_hosts_the_child_tables() {
    let (fake, events) = FakeSysCatalog::new();
    let mut child = SysTablesEntryPB::new();
    child.set_name(b"child".to_vec());
    child.set_version(0);
    child.schema = Some(Default::default()).into();
    child.set_parent_table_id(b"parent".to_vec());
    let mut tablet = SysTabletsEntryPB::new();
    tablet.set_table_id(b"parent".to_vec());
    tablet.set_hosted_tables_mapped_by_parent_id(true);
    fake.apply(vec![
        upsert(SysRowEntryType::TABLE, "parent", table_row("p", 0)),
        upsert(
            SysRowEntryType::TABLE,
            "child",
            child.write_to_bytes().unwrap(),
        ),
        upsert(
            SysRowEntryType::TABLET,
            "tablet",
            tablet.write_to_bytes().unwrap(),
        ),
    ]);
    let s = start(fake, events);
    s.catalog.barrier().await;
    let snapshot = s.catalog.snapshot();
    let tablet = snapshot.get(SysRowEntryType::TABLET, b"tablet").unwrap();
    assert_eq!(
        snapshot.hosted_table_ids(tablet),
        vec![b"child".to_vec(), b"parent".to_vec()]
    );
    // Deleted tables leave the tablet.
    child.set_state(yb_proto::pb::catalog_entity_info::sys_tables_entry_pb::State::DELETED);
    s.fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "child",
        child.write_to_bytes().unwrap(),
    )]);
    s.catalog.barrier().await;
    let snapshot = s.catalog.snapshot();
    let tablet = snapshot.get(SysRowEntryType::TABLET, b"tablet").unwrap();
    assert_eq!(snapshot.hosted_table_ids(tablet), vec![b"parent".to_vec()]);
}

#[tokio::test]
async fn loads_then_follows_the_apply_stream() {
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "t1",
        table_row("a", 1),
    )]);
    let s = start(fake, events);
    s.catalog.barrier().await;
    let snapshot = s.catalog.snapshot();
    assert!(snapshot.loaded);
    assert_eq!(snapshot.count(SysRowEntryType::TABLE), 1);
    assert_eq!(
        snapshot.table_by_name(b"ns", "a").unwrap().key.id.as_ref(),
        b"t1"
    );

    s.fake.apply(vec![
        upsert(SysRowEntryType::TABLE, "t1", table_row("b", 2)),
        upsert(
            SysRowEntryType::TABLET,
            "tab2",
            tablet_row("t1", "m", sys_tablets_entry_pb::State::RUNNING),
        ),
        upsert(
            SysRowEntryType::TABLET,
            "tab1",
            tablet_row("t1", "", sys_tablets_entry_pb::State::RUNNING),
        ),
        upsert(
            SysRowEntryType::TABLET,
            "tab0",
            tablet_row("t1", "", sys_tablets_entry_pb::State::REPLACED),
        ),
    ]);
    s.catalog.barrier().await;
    let snapshot = s.catalog.snapshot();
    assert!(snapshot.table_by_name(b"ns", "a").is_none());
    assert_eq!(snapshot.table_by_name(b"ns", "b").unwrap().version, 2);
    let active: Vec<_> = snapshot
        .active_tablets_of(b"t1")
        .iter()
        .map(|e| e.key.id.to_vec())
        .collect();
    assert_eq!(active, vec![b"tab1".to_vec(), b"tab2".to_vec()]);
    assert_eq!(snapshot_rows(&snapshot), s.fake.rows_at(s.fake.now()));

    s.fake.apply(vec![RowChange::Delete {
        entry_type: SysRowEntryType::TABLET,
        id: b"tab2".to_vec(),
    }]);
    s.catalog.barrier().await;
    assert_eq!(s.catalog.snapshot().tablets_of(b"t1").len(), 2);
}

#[tokio::test]
async fn closed_tablet_is_an_unloaded_catalog() {
    let (fake, events) = FakeSysCatalog::new();
    fake.set_open(false);
    let s = start(fake, events);
    s.catalog.barrier().await;
    assert!(!s.catalog.snapshot().loaded);
    s.fake.set_open(true);
    s.catalog.barrier().await;
    assert!(s.catalog.snapshot().loaded);
}

#[tokio::test]
async fn writer_acks_after_apply_and_skips_unchanged_rows() {
    let (fake, events) = FakeSysCatalog::new();
    fake.set_term(3);
    let s = start(fake, events);
    let (leadership, _source) = ready_for_tests(3).await;
    let writer = CatalogWriter::start(
        s.catalog.clone(),
        s.fake.clone(),
        leadership.clone(),
        WriterOptions::default(),
    );
    s.catalog.barrier().await;
    let token = leadership.token().unwrap();
    let snapshot = writer
        .submit(
            &token,
            Mutation::new()
                .read_absent(SysRowEntryType::TABLE, b"t1")
                .upsert(SysRowEntryType::TABLE, b"t1", table_row("a", 1)),
        )
        .await
        .unwrap();
    assert!(snapshot.get(SysRowEntryType::TABLE, b"t1").is_some());
    assert_eq!(s.fake.writes(), 1);

    // The same bytes again: no Raft write.
    writer
        .submit(
            &token,
            Mutation::new().upsert(SysRowEntryType::TABLE, b"t1", table_row("a", 1)),
        )
        .await
        .unwrap();
    assert_eq!(s.fake.writes(), 1);

    // A stale read conflicts.
    let stale = Mutation::new().read_absent(SysRowEntryType::TABLE, b"t1");
    assert_eq!(
        writer
            .submit(
                &token,
                stale.upsert(SysRowEntryType::TABLE, b"t1", table_row("b", 1))
            )
            .await
            .unwrap_err(),
        MutationError::Conflict
    );
}

#[tokio::test]
async fn stale_token_and_failed_write() {
    let (fake, events) = FakeSysCatalog::new();
    let s = start(fake, events);
    let (leadership, source) = ready_for_tests(1).await;
    let writer = CatalogWriter::start(
        s.catalog.clone(),
        s.fake.clone(),
        leadership.clone(),
        WriterOptions::default(),
    );
    s.catalog.barrier().await;
    let token = leadership.token().unwrap();
    s.fake
        .fail_next_write(Status::new(StatusCode::IllegalState, "term changed"));
    let result = writer
        .submit(
            &token,
            Mutation::new().upsert(SysRowEntryType::TABLE, b"t1", table_row("a", 1)),
        )
        .await;
    assert!(matches!(result, Err(MutationError::Write(_))));

    source.set(
        yb_proto::pb::common_types::PeerRole::FOLLOWER,
        2,
        master_leadership::LeaderStatus::NotLeader,
    );
    let result = writer
        .submit(
            &token,
            Mutation::new().upsert(SysRowEntryType::TABLE, b"t1", table_row("a", 1)),
        )
        .await;
    assert!(matches!(result, Err(MutationError::NotLeader(_))));
    assert_eq!(s.fake.writes(), 0);
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn concurrent_read_modify_writes_do_not_lose_updates() {
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(vec![upsert(
        SysRowEntryType::CLUSTER_CONFIG,
        "",
        config_row(0),
    )]);
    let s = start(fake, events);
    let (leadership, _source) = ready_for_tests(1).await;
    let writer = CatalogWriter::start(
        s.catalog.clone(),
        s.fake.clone(),
        leadership.clone(),
        WriterOptions::default(),
    );
    s.catalog.barrier().await;
    let token = leadership.token().unwrap();
    let mut tasks = Vec::new();
    for _ in 0..8 {
        let writer = writer.clone();
        tasks.push(tokio::spawn(async move {
            for _ in 0..5 {
                writer
                    .mutate(&token, |snapshot| {
                        let config = snapshot.cluster_config().unwrap();
                        let HotFields::ClusterConfig { version } = config.hot else {
                            unreachable!()
                        };
                        let mutation = Mutation::new().read(config).upsert(
                            SysRowEntryType::CLUSTER_CONFIG,
                            b"",
                            config_row(version + 1),
                        );
                        Ok((mutation, ()))
                    })
                    .await
                    .unwrap();
            }
        }));
    }
    for task in tasks {
        task.await.unwrap();
    }
    let snapshot = s.catalog.snapshot();
    let HotFields::ClusterConfig { version } = snapshot.cluster_config().unwrap().hot else {
        unreachable!()
    };
    assert_eq!(version, 40);
}

#[tokio::test]
async fn restore_reloads_and_invalidates_earlier_reads() {
    let (fake, events) = FakeSysCatalog::new();
    let t0 = fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "t1",
        table_row("a", 1),
    )]);
    let s = start(fake, events);
    let (leadership, _source) = ready_for_tests(1).await;
    let writer = CatalogWriter::start(
        s.catalog.clone(),
        s.fake.clone(),
        leadership.clone(),
        WriterOptions::default(),
    );
    s.catalog.barrier().await;
    let before = s.catalog.snapshot();
    let table = before.get(SysRowEntryType::TABLE, b"t1").unwrap().clone();
    s.fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "t2",
        table_row("b", 1),
    )]);
    s.fake.restore(t0);
    s.catalog.barrier().await;
    let after = s.catalog.snapshot();
    assert_eq!(after.generation, before.generation + 1);
    assert!(after.get(SysRowEntryType::TABLE, b"t2").is_none());
    assert_eq!(snapshot_rows(&after), s.fake.rows_at(s.fake.now()));
    // Same bytes, new version: a decision made before the restore must be rebuilt.
    let token = leadership.token().unwrap();
    let result = writer
        .submit(
            &token,
            Mutation::new()
                .read(&table)
                .upsert(SysRowEntryType::TABLE, b"t1", table_row("c", 2)),
        )
        .await;
    assert_eq!(result.unwrap_err(), MutationError::Conflict);
}

#[tokio::test]
async fn self_check_catches_rows_the_tap_missed() {
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "t1",
        table_row("a", 1),
    )]);
    let s = start(fake, events);
    s.catalog.barrier().await;
    let store = s.fake.clone();
    let catalog = s.catalog.clone();
    assert!(
        tokio::task::spawn_blocking(move || self_check(&catalog, &*store))
            .await
            .unwrap()
            .unwrap()
    );

    s.fake.apply_silently(vec![upsert(
        SysRowEntryType::TABLE,
        "t9",
        table_row("x", 1),
    )]);
    // The self-check reads at the snapshot's point; move it past the silent write.
    s.fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "t2",
        table_row("b", 1),
    )]);
    s.catalog.barrier().await;
    let store = s.fake.clone();
    let catalog = s.catalog.clone();
    assert!(
        !tokio::task::spawn_blocking(move || self_check(&catalog, &*store))
            .await
            .unwrap()
            .unwrap()
    );
    s.catalog.barrier().await;
    assert_eq!(
        s.catalog
            .stats
            .self_check_mismatches
            .load(std::sync::atomic::Ordering::Relaxed),
        1
    );
    let snapshot = s.catalog.snapshot();
    assert!(snapshot.get(SysRowEntryType::TABLE, b"t9").is_some());
}

/// One step of a randomized workload.
#[derive(Clone, Debug)]
enum Op {
    /// Another leader replicates a table row.
    ReplicatedTable(u8, u8),
    /// Another leader replicates a delete.
    ReplicatedDelete(u8),
    /// This master's writer bumps a table's version.
    Bump(u8),
    /// A PITR restore to the state `n` steps back.
    Restore(u8),
}

fn op() -> impl Strategy<Value = Op> {
    prop_oneof![
        4 => (0u8..8, 0u8..4).prop_map(|(t, v)| Op::ReplicatedTable(t, v)),
        2 => (0u8..8).prop_map(Op::ReplicatedDelete),
        3 => (0u8..8).prop_map(Op::Bump),
        1 => (1u8..4).prop_map(Op::Restore),
    ]
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 48, ..ProptestConfig::default() })]

    /// Whatever mix of replicated writes, local writes and restores, the materialized catalog ends
    /// equal to the store, row for row.
    #[test]
    fn catalog_matches_store_under_random_workload(ops in prop::collection::vec(op(), 1..40)) {
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()
            .unwrap();
        runtime.block_on(async move {
            let (fake, events) = FakeSysCatalog::new();
            let s = start(fake, events);
            let (leadership, _source) = ready_for_tests(1).await;
            let writer = CatalogWriter::start(
                s.catalog.clone(),
                s.fake.clone(),
                leadership.clone(),
                WriterOptions::default(),
            );
            let token = leadership.token().unwrap();
            let mut history: Vec<HybridTime> = vec![s.fake.now()];
            for op in ops {
                match op {
                    Op::ReplicatedTable(t, v) => {
                        let id = format!("t{t}");
                        history.push(s.fake.apply(vec![upsert(
                            SysRowEntryType::TABLE,
                            &id,
                            table_row(&id, v as u32),
                        )]));
                    }
                    Op::ReplicatedDelete(t) => {
                        history.push(s.fake.apply(vec![RowChange::Delete {
                            entry_type: SysRowEntryType::TABLE,
                            id: format!("t{t}").into_bytes(),
                        }]));
                    }
                    Op::Bump(t) => {
                        let id = format!("t{t}");
                        writer
                            .mutate(&token, |snapshot| {
                                let current = snapshot.get(SysRowEntryType::TABLE, id.as_bytes());
                                let version = current
                                    .and_then(|e| e.table().map(|t| t.schema_version))
                                    .unwrap_or(0);
                                let base = match current {
                                    Some(e) => Mutation::new().read(e),
                                    None => Mutation::new()
                                        .read_absent(SysRowEntryType::TABLE, id.as_bytes()),
                                };
                                Ok((
                                    base.upsert(
                                        SysRowEntryType::TABLE,
                                        id.as_bytes(),
                                        table_row(&id, version + 1),
                                    ),
                                    (),
                                ))
                            })
                            .await
                            .unwrap();
                        history.push(s.fake.now());
                    }
                    Op::Restore(back) => {
                        let at = history[history.len().saturating_sub(back as usize + 1)];
                        s.fake.restore(at);
                        history.push(s.fake.now());
                    }
                }
            }
            s.catalog.barrier().await;
            let snapshot = s.catalog.snapshot();
            assert_eq!(snapshot_rows(&snapshot), s.fake.rows_at(s.fake.now()));
            let digest = rows_digest(
                s.fake
                    .rows_at(s.fake.now())
                    .iter()
                    .map(|((t, id), data)| (*t, &id[..], &data[..])),
            );
            assert_eq!(snapshot.digest(), digest);
        });
    }
}

fn one_at_a_time(metrics: Arc<yb_host_api::metrics::InMemoryMetrics>) -> WriterOptions {
    WriterOptions {
        max_batch_rows: || 1,
        max_inflight: || 1,
        metrics,
        ..Default::default()
    }
}

#[tokio::test]
async fn expired_mutation_is_dropped_unwritten() {
    let (fake, events) = FakeSysCatalog::new();
    let s = start(fake, events);
    let (leadership, _source) = ready_for_tests(1).await;
    let metrics = yb_host_api::metrics::InMemoryMetrics::new();
    let writer = CatalogWriter::start(
        s.catalog.clone(),
        s.fake.clone(),
        leadership.clone(),
        one_at_a_time(metrics.clone()),
    );
    s.catalog.barrier().await;
    let token = leadership.token().unwrap();

    // The first write holds the only in-flight slot, so the second waits in the queue past its
    // deadline.
    s.fake.pause_writes();
    let first = {
        let (writer, token) = (writer.clone(), token);
        tokio::spawn(async move {
            writer
                .submit(
                    &token,
                    Mutation::new().upsert(SysRowEntryType::TABLE, b"t1", table_row("a", 1)),
                )
                .await
        })
    };
    tokio::time::sleep(Duration::from_millis(20)).await;
    let second = {
        let (writer, token) = (writer.clone(), token);
        tokio::spawn(async move {
            writer
                .submit_with(
                    &token,
                    Mutation::new().upsert(SysRowEntryType::TABLE, b"t2", table_row("b", 1)),
                    WriteOptions::with_timeout(Duration::from_millis(10)),
                )
                .await
        })
    };
    tokio::time::sleep(Duration::from_millis(50)).await;
    s.fake.resume_writes();
    first.await.unwrap().unwrap();
    assert_eq!(second.await.unwrap().unwrap_err(), MutationError::Expired);
    assert!(s
        .catalog
        .snapshot()
        .get(SysRowEntryType::TABLE, b"t2")
        .is_none());
    assert_eq!(s.fake.writes(), 1);
    assert_eq!(
        metrics.counter_value("rust_master_catalog_writer_expired"),
        1
    );
    assert_eq!(
        metrics.histogram_values("rust_master_catalog_writer_batch_rows"),
        vec![1]
    );
    assert_eq!(
        metrics
            .histogram_values("rust_master_catalog_writer_commit_latency_us")
            .len(),
        1
    );

    // mutate_with reports the deadline as TimedOut.
    let status = writer
        .mutate_with(
            &token,
            WriteOptions {
                deadline: Some(tokio::time::Instant::now()),
                ..Default::default()
            },
            |_| {
                Ok((
                    Mutation::new().upsert(SysRowEntryType::TABLE, b"t3", table_row("c", 1)),
                    (),
                ))
            },
        )
        .await
        .unwrap_err();
    assert_eq!(status.code, StatusCode::TimedOut);
}

#[tokio::test]
async fn priority_lane_goes_first() {
    let (fake, events) = FakeSysCatalog::new();
    let s = start(fake, events);
    let (leadership, _source) = ready_for_tests(1).await;
    let writer = CatalogWriter::start(
        s.catalog.clone(),
        s.fake.clone(),
        leadership.clone(),
        one_at_a_time(yb_host_api::metrics::InMemoryMetrics::new()),
    );
    s.catalog.barrier().await;
    let token = leadership.token().unwrap();
    let (order_tx, mut order) = tokio::sync::mpsc::unbounded_channel();
    let submit = |id: &'static str, lane: Lane| {
        let (writer, order_tx) = (writer.clone(), order_tx.clone());
        tokio::spawn(async move {
            writer
                .submit_with(
                    &token,
                    Mutation::new().upsert(SysRowEntryType::TABLE, id.as_bytes(), table_row(id, 1)),
                    WriteOptions {
                        lane,
                        ..Default::default()
                    },
                )
                .await
                .unwrap();
            order_tx.send(id).unwrap();
        })
    };

    s.fake.pause_writes();
    let blocker = submit("blocker", Lane::Normal);
    tokio::time::sleep(Duration::from_millis(20)).await;
    let normal = submit("normal", Lane::Normal);
    tokio::time::sleep(Duration::from_millis(20)).await;
    let urgent = submit("urgent", Lane::Priority);
    tokio::time::sleep(Duration::from_millis(20)).await;
    s.fake.resume_writes();
    for task in [blocker, normal, urgent] {
        task.await.unwrap();
    }
    let mut seen = Vec::new();
    while let Ok(id) = order.try_recv() {
        seen.push(id);
    }
    assert_eq!(seen, vec!["blocker", "urgent", "normal"]);
}

mod default_rows {
    use std::sync::Mutex;

    use super::*;
    use crate::defaults::{
        default_rows, write_default_rows, DefaultsHost, DefaultsOptions, DEFAULT_YCQL_ROLE,
        SECURITY_CONFIG_ID,
    };
    use yb_proto::pb::catalog_entity_info::SysConfigEntryPB;

    /// Table ids, and ids with tablets.
    type SystemTables = (Vec<Vec<u8>>, Vec<String>);

    /// A host with one system table, "peers" in the system namespace.
    #[derive(Default)]
    struct FakeHost {
        /// What the last call was given.
        seen: Mutex<Option<SystemTables>>,
    }

    impl DefaultsHost for FakeHost {
        fn system_table_rows(
            &self,
            tables: &[(Vec<u8>, Vec<u8>)],
            tables_with_tablets: &[String],
        ) -> Result<Vec<RowChange>, Status> {
            *self.seen.lock().unwrap() = Some((
                tables.iter().map(|(id, _)| id.clone()).collect(),
                tables_with_tablets.to_vec(),
            ));
            if !tables.is_empty() {
                return Ok(vec![]);
            }
            let mut table = SysTablesEntryPB::parse_from_bytes(&table_row("peers", 0)).unwrap();
            table.set_namespace_id(b"00000000000000000000000000000001".to_vec());
            Ok(vec![
                upsert(
                    SysRowEntryType::TABLE,
                    "peers-id",
                    table.write_to_bytes().unwrap(),
                ),
                upsert(
                    SysRowEntryType::TABLET,
                    "peers-tablet",
                    tablet_row("peers-id", "", sys_tablets_entry_pb::State::RUNNING),
                ),
            ])
        }

        fn default_ycql_password_hash(&self) -> Result<Vec<u8>, Status> {
            Ok(b"hash".to_vec())
        }

        fn ysql_major_version(&self) -> u32 {
            15
        }
    }

    const OPTIONS: DefaultsOptions = DefaultsOptions {
        cluster_uuid: String::new,
        universe_uuid_check: || true,
        allow_ycql_role_drop: || true,
    };

    #[tokio::test]
    async fn a_new_universe_gets_every_default_row_once() {
        let (fake, events) = FakeSysCatalog::new();
        let s = start(fake, events);
        let (leadership, _source) = ready_for_tests(1).await;
        let writer = CatalogWriter::start(
            s.catalog.clone(),
            s.fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        s.catalog.barrier().await;
        let host = FakeHost::default();
        let token = leadership.token().unwrap();
        write_default_rows(&writer, &token, &host, &OPTIONS)
            .await
            .unwrap();
        assert_eq!(s.fake.writes(), 1, "one Raft write");

        let snapshot = s.catalog.snapshot();
        let types: Vec<i32> = snapshot.rows().map(|(t, _, _)| t).collect();
        for (kind, count) in [
            (SysRowEntryType::SYS_CONFIG, 3),
            (SysRowEntryType::NAMESPACE, 3),
            (SysRowEntryType::TABLE, 1),
            (SysRowEntryType::TABLET, 1),
            (SysRowEntryType::ROLE, 1),
            (SysRowEntryType::CLUSTER_CONFIG, 1),
            (SysRowEntryType::XCLUSTER_CONFIG, 1),
            (SysRowEntryType::PERSISTENCE_BLOCK_MAP, 1),
        ] {
            assert_eq!(
                types.iter().filter(|t| **t == kind as i32).count(),
                count,
                "{kind:?}"
            );
        }
        let config: SysClusterConfigEntryPB = snapshot.cluster_config().unwrap().parse().unwrap();
        assert_eq!(config.version(), 0);
        assert_eq!(config.cluster_uuid().len(), 36);
        assert_eq!(config.universe_uuid().len(), 36);
        let security: SysConfigEntryPB = snapshot
            .get(SysRowEntryType::SYS_CONFIG, SECURITY_CONFIG_ID)
            .unwrap()
            .parse()
            .unwrap();
        assert!(security.security_config().cassandra_user_created());
        let ysql: SysConfigEntryPB = snapshot
            .get(SysRowEntryType::SYS_CONFIG, b"ysql-catalog-configuration")
            .unwrap()
            .parse()
            .unwrap();
        assert!(ysql
            .ysql_catalog_config()
            .transactional_sys_catalog_enabled());
        assert_eq!(
            ysql.ysql_catalog_config()
                .ysql_major_catalog_upgrade_info
                .catalog_version(),
            15
        );

        // The next term finds nothing missing; the host sees the system table and its tablet.
        assert!(default_rows(&snapshot, &host, &OPTIONS).unwrap().is_empty());
        assert_eq!(
            host.seen.lock().unwrap().clone().unwrap(),
            (vec![b"peers-id".to_vec()], vec!["peers-id".to_string()])
        );

        // A dropped default role stays dropped: its creation is recorded.
        s.fake.apply(vec![RowChange::Delete {
            entry_type: SysRowEntryType::ROLE,
            id: DEFAULT_YCQL_ROLE.to_vec(),
        }]);
        s.catalog.barrier().await;
        assert!(default_rows(&s.catalog.snapshot(), &host, &OPTIONS)
            .unwrap()
            .is_empty());
    }

    #[tokio::test]
    async fn an_existing_universe_gets_only_what_it_lacks() {
        let (fake, events) = FakeSysCatalog::new();
        let mut config = SysClusterConfigEntryPB::new();
        config.set_version(7);
        config.set_cluster_uuid("existing".into());
        fake.apply(vec![upsert(
            SysRowEntryType::CLUSTER_CONFIG,
            "",
            config.write_to_bytes().unwrap(),
        )]);
        let s = start(fake, events);
        s.catalog.barrier().await;
        let mutation = default_rows(&s.catalog.snapshot(), &FakeHost::default(), &OPTIONS).unwrap();
        // The universe uuid is added with a new version; the cluster uuid stays.
        let (leadership, _source) = ready_for_tests(1).await;
        let writer = CatalogWriter::start(
            s.catalog.clone(),
            s.fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        writer
            .submit(&leadership.token().unwrap(), mutation)
            .await
            .unwrap();
        let config: SysClusterConfigEntryPB = s
            .catalog
            .snapshot()
            .cluster_config()
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(config.version(), 8);
        assert_eq!(config.cluster_uuid(), "existing");
        assert!(!config.universe_uuid().is_empty());
    }
}
