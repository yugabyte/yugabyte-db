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

//! The clone state machine, with the cases of the C++ `clone_state_manager-test`.

use std::sync::Mutex;

use master_table_lifecycle::{LifecycleOptions, SchemaHost};
use protobuf::{Enum, EnumOrUnknown};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_snapshot_entry_pb::State as SnapshotState,
};
use yb_proto::pb::catalog_entity_info::{sys_tablets_entry_pb, SysTabletsEntryPB};
use yb_proto::pb::common::PartitionPB;
use yb_proto::pb::master_backup::{
    snapshot_schedule_filter_pb, CreateSnapshotScheduleRequestPB, CreateSnapshotScheduleResponsePB,
    SysRestorationEntryPB as RestorationPB, TableIdentifiersPB, TabletRestorationPB,
};
use yb_proto::pb::master_rust_peer::{
    ClosestLiveTserverResponsePB, CountPgYbMigrationRowsResponsePB,
};
use yb_proto::pb::master_types::TableIdentifierPB;
use yb_proto::pb::tserver::ClearMetacacheResponsePB;
use yb_proto::pb::tserver_admin::EnableDbConnsResponsePB;
use yb_proto::pb::tserver_types::TabletServerErrorPB;
use yb_proto::pb::wire_protocol::app_status_pb::ErrorCode;

use super::*;
use crate::tests::Harness;

const TARGET: &str = "target_namespace_name";
/// Before the harness's snapshots, so the snapshot of `create_keyspace_snapshot` covers it.
const RESTORE_TIME: u64 = 12345 << 12;

fn request(database_type: YQLDatabase) -> CloneNamespaceRequestPB {
    let mut req = CloneNamespaceRequestPB::new();
    req.set_target_namespace_name(TARGET.into());
    req.set_restore_ht(RESTORE_TIME);
    let source = req.source_namespace.mut_or_insert_default();
    source.set_name("ks".into());
    source.set_database_type(database_type);
    req
}

async fn create(h: &Harness, database_type: YQLDatabase) -> Result<(Vec<u8>, u32), MasterErrorPB> {
    h.coordinator
        .create_clone_state(
            &h.token,
            h.deadline(),
            b"ns1",
            database_type,
            &request(database_type),
        )
        .await
}

fn clone_row(h: &Harness, id: &[u8]) -> SysCloneStatePB {
    h.catalog
        .snapshot()
        .get(SysRowEntryType::CLONE_STATE, id)
        .unwrap()
        .parse()
        .unwrap()
}

fn clone_state(h: &Harness, id: &[u8]) -> CloneState {
    clone_row(h, id).aggregate_state()
}

async fn set_state(h: &Harness, id: &[u8], state: CloneState) {
    h.coordinator
        .update_clone(&h.token, id, |pb| pb.set_aggregate_state(state))
        .await
        .unwrap();
    h.catalog.barrier().await;
}

/// What a clone started this term would have recorded for its later steps.
fn set_run(h: &Harness, id: &[u8], f: impl FnOnce(&mut CloneRun)) {
    let mut state = h.coordinator.state(h.token.term());
    f(state.clones.entry(id.to_vec()).or_default());
}

fn run(h: &Harness, id: &[u8]) -> CloneRun {
    h.coordinator.state(h.token.term()).clones[id].clone()
}

async fn upsert(h: &Harness, entry_type: SysRowEntryType, id: &[u8], data: Vec<u8>) {
    h.writer
        .submit(&h.token, Mutation::new().upsert(entry_type, id, data))
        .await
        .unwrap();
    h.catalog.barrier().await;
}

async fn put_tablet(h: &Harness, id: &[u8], state: sys_tablets_entry_pb::State) {
    let mut t = SysTabletsEntryPB::new();
    t.set_table_id(b"t1".to_vec());
    t.partition = MessageField::some(PartitionPB::new());
    t.state = Some(EnumOrUnknown::new(state));
    upsert(h, SysRowEntryType::TABLET, id, t.write_to_bytes().unwrap()).await;
}

/// A restoration row the controller leaves alone (its sys catalog step not done), in `state`.
async fn put_restoration(h: &Harness, id: &[u8], state: SnapshotState) {
    let mut pb = RestorationPB::new();
    pb.set_state(state);
    pb.set_snapshot_id(ids::random_id());
    let mut t = TabletRestorationPB::new();
    t.set_id(b"tab1".to_vec());
    t.set_state(state);
    pb.tablet_restorations.push(t);
    upsert(
        h,
        SysRowEntryType::SNAPSHOT_RESTORATION,
        id,
        pb.write_to_bytes().unwrap(),
    )
    .await;
}

/// Passes of the controller, for a state that must not change.
async fn passes(h: &Harness) {
    for _ in 0..10 {
        h.coordinator.poll(&h.scope);
        tokio::time::sleep(Duration::from_millis(10)).await;
        h.catalog.barrier().await;
    }
}

async fn settle_state(h: &Harness, id: &[u8], states: &[CloneState]) {
    h.settle(|h| states.contains(&clone_state(h, id))).await;
}

fn assert_aborted(h: &Harness, id: &[u8]) -> String {
    let pb = clone_row(h, id);
    assert_eq!(pb.aggregate_state(), CloneState::ABORTED);
    assert!(!pb.abort_message().is_empty());
    pb.abort_message().to_string()
}

/// `CreateCloneState`.
#[tokio::test]
async fn a_clone_state_records_the_request_with_the_next_seq_no() {
    let h = Harness::new().await;
    let (id, seq_no) = create(&h, YQLDatabase::YQL_DATABASE_CQL).await.unwrap();
    assert_eq!(seq_no, 1);
    let mut expected = SysCloneStatePB::new();
    expected.set_aggregate_state(CloneState::CLONE_SCHEMA_STARTED);
    expected.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
    expected.set_source_namespace_id("ns1".into());
    expected.set_source_namespace_name("ks".into());
    expected.set_clone_request_seq_no(1);
    expected.set_target_namespace_name(TARGET.into());
    expected.set_restore_time(RESTORE_TIME);
    assert_eq!(clone_row(&h, &id), expected);
    let ns: SysNamespaceEntryPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::NAMESPACE, b"ns1")
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(ns.clone_request_seq_no(), 1);
    // Known to this term, so the controller does not abort it.
    assert!(h.coordinator.state(h.token.term()).clones.contains_key(&id));
}

/// `CreateSecondCloneState`: a new clone of a namespace only once its last clone is done.
#[tokio::test]
async fn a_second_clone_is_refused_until_the_last_one_is_done() {
    let h = Harness::new().await;
    let (mut latest, mut seq_no) = create(&h, YQLDatabase::YQL_DATABASE_CQL).await.unwrap();
    for state in CloneState::VALUES {
        set_state(&h, &latest, *state).await;
        let created = create(&h, YQLDatabase::YQL_DATABASE_CQL).await;
        if matches!(state, CloneState::ABORTED | CloneState::COMPLETE) {
            let (id, n) = created.unwrap();
            assert_eq!(n, seq_no + 1, "{state:?}");
            (latest, seq_no) = (id, n);
        } else {
            let e = created.unwrap_err();
            assert_eq!(e.status.code(), ErrorCode::ALREADY_PRESENT, "{state:?}");
        }
    }
}

/// `HandleCreatingState*` and `HandleRestoringStateRestored` of a YCQL clone: the target snapshot
/// is restored once every target tablet runs, and the clone is COMPLETE once restored.
#[tokio::test]
async fn a_creating_clone_restores_once_every_target_tablet_runs() {
    let h = Harness::new().await;
    let target_snapshot = h.create_keyspace_snapshot().await;
    h.settle(|h| h.list(&target_snapshot).snapshots[0].entry.state() == SnapshotState::COMPLETE)
        .await;
    let (id, _) = create(&h, YQLDatabase::YQL_DATABASE_CQL).await.unwrap();
    put_tablet(&h, b"target1", sys_tablets_entry_pb::State::CREATING).await;
    put_tablet(&h, b"target2", sys_tablets_entry_pb::State::CREATING).await;
    set_run(&h, &id, |run| {
        run.tablets = vec![
            (b"tab1".to_vec(), b"target1".to_vec()),
            (b"tab2".to_vec(), b"target2".to_vec()),
        ];
        run.target_snapshot_id = target_snapshot.clone();
    });
    set_state(&h, &id, CloneState::CREATING).await;

    // No target tablet runs.
    passes(&h).await;
    assert_eq!(clone_state(&h, &id), CloneState::CREATING);

    // Some do.
    put_tablet(&h, b"target1", sys_tablets_entry_pb::State::RUNNING).await;
    passes(&h).await;
    assert_eq!(clone_state(&h, &id), CloneState::CREATING);
    assert!(run(&h, &id).restoration_id.is_empty());

    put_tablet(&h, b"target2", sys_tablets_entry_pb::State::RUNNING).await;
    settle_state(&h, &id, &[CloneState::RESTORING, CloneState::COMPLETE]).await;
    let restoration_id = run(&h, &id).restoration_id;
    assert!(!restoration_id.is_empty());
    let restoration: RestorationPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::SNAPSHOT_RESTORATION, &restoration_id)
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(restoration.snapshot_id(), target_snapshot.as_slice());
    settle_state(&h, &id, &[CloneState::COMPLETE]).await;
}

/// `HandleRestoringStateIncomplete` and `HandleRestoringStateRestored`.
#[tokio::test]
async fn a_restoring_clone_waits_for_its_restoration() {
    let h = Harness::new().await;
    let (id, _) = create(&h, YQLDatabase::YQL_DATABASE_CQL).await.unwrap();
    let restoration_id = ids::random_id();
    put_restoration(&h, &restoration_id, SnapshotState::RESTORING).await;
    set_run(&h, &id, |run| run.restoration_id = restoration_id.clone());
    set_state(&h, &id, CloneState::RESTORING).await;
    passes(&h).await;
    assert_eq!(clone_state(&h, &id), CloneState::RESTORING);

    put_restoration(&h, &restoration_id, SnapshotState::RESTORED).await;
    settle_state(&h, &id, &[CloneState::COMPLETE]).await;
}

/// `AbortInRestoringState`: the restoration cannot be listed.
#[tokio::test]
async fn a_restoring_clone_whose_restoration_is_gone_is_aborted() {
    let h = Harness::new().await;
    let (id, _) = create(&h, YQLDatabase::YQL_DATABASE_CQL).await.unwrap();
    set_run(&h, &id, |run| run.restoration_id = ids::random_id());
    set_state(&h, &id, CloneState::RESTORING).await;
    settle_state(&h, &id, &[CloneState::ABORTED]).await;
    assert_aborted(&h, &id);
}

/// `AbortInCreatingState`, with the restore failing (C++ also aborts when a target tablet
/// cannot be found; the Rust clone waits for it).
#[tokio::test]
async fn a_creating_clone_whose_restore_fails_is_aborted() {
    let h = Harness::new().await;
    let (id, _) = create(&h, YQLDatabase::YQL_DATABASE_CQL).await.unwrap();
    set_run(&h, &id, |run| run.target_snapshot_id = ids::random_id());
    set_state(&h, &id, CloneState::CREATING).await;
    settle_state(&h, &id, &[CloneState::ABORTED]).await;
    assert_aborted(&h, &id);
}

/// `AbortIncompleteCloneOnLoad`: a clone of an earlier term not done is aborted; a done one is
/// left as it is.
#[tokio::test]
async fn clones_an_earlier_term_left_unfinished_are_aborted() {
    let h = Harness::new().await;
    let mut ids = Vec::new();
    for (i, state) in CloneState::VALUES.iter().enumerate() {
        let mut pb = SysCloneStatePB::new();
        pb.set_source_namespace_id(format!("ns-{i}"));
        pb.set_clone_request_seq_no(1);
        pb.set_aggregate_state(*state);
        if *state == CloneState::ABORTED {
            pb.set_abort_message("Test abort message".into());
        }
        let id = format!("clone-{i}").into_bytes();
        upsert(
            &h,
            SysRowEntryType::CLONE_STATE,
            &id,
            pb.write_to_bytes().unwrap(),
        )
        .await;
        ids.push((id, *state));
    }
    h.settle(|h| ids.iter().all(|(id, _)| is_done(&clone_row(h, id))))
        .await;
    for (id, state) in &ids {
        let pb = clone_row(&h, id);
        match state {
            CloneState::ABORTED => assert_eq!(pb.abort_message(), "Test abort message"),
            CloneState::COMPLETE => {
                assert_eq!(pb.aggregate_state(), CloneState::COMPLETE);
                assert!(!pb.has_abort_message());
            }
            _ => assert_eq!(assert_aborted(&h, id), "aborted by master failover"),
        }
    }
}

/// `Load`: every clone of a namespace is listed, in sequence number order.
#[tokio::test]
async fn clones_are_listed_in_seq_no_order() {
    let h = Harness::new().await;
    let mut first = SysCloneStatePB::new();
    first.set_aggregate_state(CloneState::COMPLETE);
    first.set_source_namespace_id("ns1".into());
    first.set_source_namespace_name("ks".into());
    first.set_target_namespace_name(TARGET.into());
    first.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    first.set_restore_time(RESTORE_TIME);
    first.set_clone_request_seq_no(100);
    let mut second = SysCloneStatePB::new();
    second.set_aggregate_state(CloneState::ABORTED);
    second.set_source_namespace_id("ns1".into());
    second.set_abort_message("Test abort message".into());
    second.set_target_namespace_name(TARGET.into());
    second.set_restore_time(RESTORE_TIME + 1);
    second.set_clone_request_seq_no(101);
    // Ids in the other order.
    upsert(
        &h,
        SysRowEntryType::CLONE_STATE,
        b"a",
        second.write_to_bytes().unwrap(),
    )
    .await;
    upsert(
        &h,
        SysRowEntryType::CLONE_STATE,
        b"b",
        first.write_to_bytes().unwrap(),
    )
    .await;
    let mut req = ListClonesRequestPB::new();
    req.set_source_namespace_id("ns1".into());
    let mut resp = ListClonesResponsePB::new();
    h.coordinator.list_clones(&req, &mut resp);
    assert_eq!(resp.entries, vec![first, second]);
}

/// A YSQL clone's RESTORED step: the target namespace and what its tservers were asked.
struct PgClone {
    h: Harness,
    id: Vec<u8>,
    calls: Arc<Mutex<Vec<TserverCall>>>,
}

/// (tserver host, method, request).
type TserverCall = (String, String, Vec<u8>);

const TARGET_ID: &str = "000033c6000030008000000000000000";

/// A YSQL clone whose restoration is done. `closest` answers `ClosestLiveTserver`, `answer` the
/// tserver calls.
async fn pg_clone(
    closest: Result<&'static str, Status>,
    answer: impl Fn(&str, &str) -> Vec<u8> + Send + Sync + 'static,
) -> PgClone {
    let h = Harness::new().await;
    let mut ns = SysNamespaceEntryPB::new();
    ns.set_name(TARGET.into());
    ns.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_PGSQL));
    ns.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
    upsert(
        &h,
        SysRowEntryType::NAMESPACE,
        TARGET_ID.as_bytes(),
        ns.write_to_bytes().unwrap(),
    )
    .await;
    *h.host.calls.lock().unwrap() = Some(Box::new(move |method, _| {
        assert_eq!(method, "ClosestLiveTserver");
        Some(closest.clone().map(|uuid| {
            let mut resp = ClosestLiveTserverResponsePB::new();
            resp.set_permanent_uuid(uuid.as_bytes().to_vec());
            resp.write_to_bytes().unwrap()
        }))
    }));
    let calls: Arc<Mutex<Vec<TserverCall>>> = Arc::default();
    let record = calls.clone();
    *h.tserver_handler.lock().unwrap() = Some(Arc::new(move |host, method, req| {
        record
            .lock()
            .unwrap()
            .push((host.to_string(), method.to_string(), req.to_vec()));
        Ok(answer(host, method))
    }));
    let (id, _) = create(&h, YQLDatabase::YQL_DATABASE_PGSQL).await.unwrap();
    let restoration_id = ids::random_id();
    put_restoration(&h, &restoration_id, SnapshotState::RESTORED).await;
    set_run(&h, &id, |run| run.restoration_id = restoration_id);
    set_state(&h, &id, CloneState::RESTORING).await;
    PgClone { h, id, calls }
}

fn ok_answer(_: &str, method: &str) -> Vec<u8> {
    match method {
        "ClearMetacache" => ClearMetacacheResponsePB::new().write_to_bytes().unwrap(),
        "EnableDbConns" => EnableDbConnsResponsePB::new().write_to_bytes().unwrap(),
        other => panic!("unexpected {other}"),
    }
}

fn ts_error(message: &str) -> MessageField<TabletServerErrorPB> {
    let mut e = TabletServerErrorPB::new();
    e.set_code(yb_proto::pb::tserver_types::tablet_server_error_pb::Code::UNKNOWN_ERROR);
    e.status
        .mut_or_insert_default()
        .set_code(ErrorCode::ILLEGAL_STATE);
    e.status.mut_or_insert_default().set_message(message.into());
    MessageField::some(e)
}

impl PgClone {
    fn methods(&self) -> Vec<(String, String)> {
        self.calls
            .lock()
            .unwrap()
            .iter()
            .map(|(host, method, _)| (host.clone(), method.clone()))
            .collect()
    }
}

/// `ClearMetacacheCallbacksElectOneEnableDbConnsTask`: every live tserver clears its metacache
/// before one tserver enables connections, once.
#[tokio::test]
async fn a_restored_ysql_clone_clears_every_metacache_then_enables_connections_once() {
    let c = pg_clone(Ok("ts1"), ok_answer).await;
    settle_state(&c.h, &c.id, &[CloneState::COMPLETE]).await;
    let mut methods = c.methods();
    let enable = methods.pop().unwrap();
    assert_eq!(enable, ("ts1".to_string(), "EnableDbConns".to_string()));
    methods.sort();
    assert_eq!(
        methods,
        vec![
            ("ts0".to_string(), "ClearMetacache".to_string()),
            ("ts1".to_string(), "ClearMetacache".to_string()),
        ]
    );
    for (_, method, req) in c.calls.lock().unwrap().iter() {
        if method == "ClearMetacache" {
            let req = ClearMetacacheRequestPB::parse_from_bytes(req).unwrap();
            assert_eq!(req.namespace_id(), TARGET_ID);
        } else {
            let req = EnableDbConnsRequestPB::parse_from_bytes(req).unwrap();
            assert_eq!(req.target_db_name(), TARGET.as_bytes());
        }
    }
}

/// `AbortIfClearMetacacheFails`.
#[tokio::test]
async fn a_ysql_clone_is_aborted_when_a_metacache_is_not_cleared() {
    let c = pg_clone(Ok("ts0"), |host, method| {
        if host == "ts1" && method == "ClearMetacache" {
            let mut resp = ClearMetacacheResponsePB::new();
            resp.error = ts_error("Fail ClearMetacache for test");
            return resp.write_to_bytes().unwrap();
        }
        ok_answer(host, method)
    })
    .await;
    settle_state(&c.h, &c.id, &[CloneState::ABORTED]).await;
    assert_aborted(&c.h, &c.id);
    assert!(!c.methods().iter().any(|(_, m)| m == "EnableDbConns"));
}

/// `AbortIfEnableDbConnsFails`.
#[tokio::test]
async fn a_ysql_clone_is_aborted_when_connections_are_not_enabled() {
    let c = pg_clone(Ok("ts0"), |host, method| {
        if method == "EnableDbConns" {
            let mut resp = EnableDbConnsResponsePB::new();
            resp.error = ts_error("Fail EnableDbConns for test");
            return resp.write_to_bytes().unwrap();
        }
        ok_answer(host, method)
    })
    .await;
    settle_state(&c.h, &c.id, &[CloneState::ABORTED]).await;
    assert!(assert_aborted(&c.h, &c.id).contains("Fail EnableDbConns for test"));
}

/// `AbortIfEnableDbConnsCannotBeScheduled`.
#[tokio::test]
async fn a_ysql_clone_is_aborted_without_a_live_tserver_to_enable_connections() {
    let c = pg_clone(
        Err(Status::new(
            StatusCode::NotFound,
            "No live tserver for test",
        )),
        ok_answer,
    )
    .await;
    settle_state(&c.h, &c.id, &[CloneState::ABORTED]).await;
    assert!(assert_aborted(&c.h, &c.id).contains("No live tserver for test"));
}

struct NoSchemaHost;

impl SchemaHost for NoSchemaHost {
    fn call(&self, method: &'static str, _: &[u8]) -> Result<Vec<u8>, Status> {
        Err(Status::new(StatusCode::NotSupported, method))
    }
}

fn lifecycle(h: &Harness, ysql_major_version: Option<u32>) -> Arc<TableLifecycle> {
    let c = &h.coordinator;
    TableLifecycle::new(
        c.catalog.clone(),
        c.writer.clone(),
        c.leadership.clone(),
        c.ts_manager.clone(),
        Arc::new(NoSchemaHost),
        LifecycleOptions {
            ysql_major_version,
            ..Default::default()
        },
    )
}

/// A schedule of ns1; returns its id.
async fn schedule(h: &Harness) -> Vec<u8> {
    let mut req = CreateSnapshotScheduleRequestPB::new();
    let options = req.options.mut_or_insert_default();
    let mut tables = TableIdentifiersPB::new();
    let mut ident = TableIdentifierPB::new();
    let ns = ident.namespace.mut_or_insert_default();
    ns.set_name("ks".into());
    ns.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
    tables.tables.push(ident);
    options.filter.mut_or_insert_default().filter =
        Some(snapshot_schedule_filter_pb::Filter::Tables(tables));
    options.set_interval_sec(60);
    options.set_retention_duration_sec(600);
    let mut resp = CreateSnapshotScheduleResponsePB::new();
    h.coordinator
        .create_schedule(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    h.catalog.barrier().await;
    resp.snapshot_schedule_id().to_vec()
}

async fn clone_namespace(
    h: &Harness,
    restore_ht: u64,
) -> (Result<(), MasterErrorPB>, CloneNamespaceResponsePB) {
    let mut req = request(YQLDatabase::YQL_DATABASE_CQL);
    req.set_restore_ht(restore_ht);
    let mut resp = CloneNamespaceResponsePB::new();
    let result = h
        .coordinator
        .clone_namespace(lifecycle(h, None), &h.token, h.deadline(), &req, &mut resp)
        .await;
    (result, resp)
}

/// `AbortInStartTabletsCloning`: a failure after the clone row is written aborts the clone, and
/// the call still answers with its namespace and sequence number.
#[tokio::test]
async fn a_clone_failing_once_its_row_is_written_is_aborted() {
    let h = Harness::new().await;
    schedule(&h).await;
    // A time in the future: no snapshot of the schedule can cover it.
    let (result, resp) = clone_namespace(&h, u64::MAX / 2).await;
    result.unwrap();
    assert_eq!(resp.source_namespace_id(), "ns1");
    assert_eq!(resp.seq_no(), 1);
    let mut req = ListClonesRequestPB::new();
    req.set_source_namespace_id("ns1".into());
    req.set_seq_no(1);
    let mut list = ListClonesResponsePB::new();
    h.coordinator.list_clones(&req, &mut list);
    assert_eq!(list.entries[0].aggregate_state(), CloneState::ABORTED);
    assert!(list.entries[0]
        .abort_message()
        .contains("since it is in the future"));
}

/// `DisallowCloneIntoForwardRestoreGap`: a clone to a time a completed restore of the schedule
/// went back over is refused, with no clone row.
#[tokio::test]
async fn a_clone_into_a_forward_restore_gap_is_refused() {
    let h = Harness::new().await;
    let schedule_id = schedule(&h).await;
    let mut pb = RestorationPB::new();
    pb.set_state(SnapshotState::RESTORED);
    pb.set_snapshot_id(ids::random_id());
    pb.set_schedule_id(schedule_id);
    pb.set_restore_at_ht(RESTORE_TIME - (1 << 12));
    pb.set_complete_time_ht(RESTORE_TIME + (1 << 12));
    upsert(
        &h,
        SysRowEntryType::SNAPSHOT_RESTORATION,
        &ids::random_id(),
        pb.write_to_bytes().unwrap(),
    )
    .await;
    let (result, _) = clone_namespace(&h, RESTORE_TIME).await;
    let e = result.unwrap_err();
    assert_eq!(e.status.code(), ErrorCode::NOT_SUPPORTED);
    assert!(e
        .status
        .message()
        .starts_with("Cannot perform a forward restore."));
    assert_eq!(
        h.catalog
            .snapshot()
            .iter(SysRowEntryType::CLONE_STATE)
            .count(),
        0
    );
}

async fn set_upgrade_info(
    h: &Harness,
    catalog_version: u32,
    state: ysql_major_catalog_upgrade_info_pb::State,
) {
    let mut config = SysConfigEntryPB::new();
    let ysql = config.mut_ysql_catalog_config();
    let info = ysql.ysql_major_catalog_upgrade_info.mut_or_insert_default();
    info.set_catalog_version(catalog_version);
    info.set_state(state);
    upsert(
        h,
        SysRowEntryType::SYS_CONFIG,
        master_catalog_state::defaults::YSQL_CATALOG_CONFIG_ID,
        config.write_to_bytes().unwrap(),
    )
    .await;
}

async fn validate(h: &Harness, ysql_major_version: u32) -> Result<(), MasterErrorPB> {
    *h.host.calls.lock().unwrap() = Some(Box::new(|method, _| {
        assert_eq!(method, "CountPgYbMigrationRows");
        let mut resp = CountPgYbMigrationRowsResponsePB::new();
        resp.set_count(1);
        Some(Ok(resp.write_to_bytes().unwrap()))
    }));
    h.coordinator
        .validate_ysql_upgrade_states(
            &lifecycle(h, Some(ysql_major_version)),
            TARGET_ID.as_bytes(),
            RESTORE_TIME,
        )
        .await
}

/// `CloneBlockedDuringYsqlMajorCatalogUpgrade`.
#[tokio::test]
async fn a_ysql_clone_is_refused_during_a_major_upgrade() {
    use ysql_major_catalog_upgrade_info_pb::State as Upgrade;
    let h = Harness::new().await;
    set_upgrade_info(&h, 15, Upgrade::DONE).await;
    validate(&h, 15).await.unwrap();

    // The catalog is on another major version than this build.
    set_upgrade_info(&h, 11, Upgrade::DONE).await;
    let e = validate(&h, 15).await.unwrap_err();
    assert_eq!(e.status.code(), ErrorCode::NOT_SUPPORTED);
    assert!(e
        .status
        .message()
        .contains("YSQL major catalog upgrade is in progress"));
}

/// `CloneBlockedWhenRestoreTimeDuringUpgrade` (the fake host reads the current row as of any
/// time).
#[tokio::test]
async fn a_ysql_clone_to_a_time_during_a_major_upgrade_is_refused() {
    use ysql_major_catalog_upgrade_info_pb::State as Upgrade;
    let h = Harness::new().await;
    for state in [
        Upgrade::PERFORMING_INIT_DB,
        Upgrade::PERFORMING_PG_UPGRADE,
        Upgrade::MONITORING,
        Upgrade::PERFORMING_ROLLBACK,
    ] {
        set_upgrade_info(&h, 15, state).await;
        let e = validate(&h, 15).await.unwrap_err();
        assert!(
            e.status
                .message()
                .contains("YSQL major catalog upgrade was in state"),
            "{state:?}: {}",
            e.status.message()
        );
    }
}
