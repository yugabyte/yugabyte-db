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

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use yb_proto::pb::catalog_entity_info::{sys_clone_state_pb::State as CloneState, SysCloneStatePB};
use yb_proto::pb::master_backup::{ListClonesRequestPB, ListClonesResponsePB};

use master_catalog_state::{run_materializer, Catalog, CatalogWriter, Mutation, WriterOptions};
use master_leadership::{FakeLeaderState, LeaderToken, Leadership, Phase};
use master_sys_catalog::{FakeSysCatalog, HybridTime, RowChange, SysCatalogStore};
use master_tablet_locations::Replica;
use master_task_runtime::{BoxFuture, TaskRegistry, TermScope, TsRpcExecutor};
use master_ts_manager::{TsManager, TsManagerOptions};
use protobuf::{EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::rpc::FakeRpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::backup::tablet_snapshot_op_request_pb::Operation;
use yb_proto::pb::backup::{TabletSnapshotOpRequestPB, TabletSnapshotOpResponsePB};
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::{State, TabletSnapshotPB};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB,
    SysNamespaceEntryPB, SysRowEntries, SysSnapshotEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common::PartitionPB;
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::{PeerRole, TableType, YQLDatabase};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_backup::{
    snapshot_schedule_filter_pb, CreateSnapshotRequestPB, CreateSnapshotResponsePB,
    CreateSnapshotScheduleRequestPB, CreateSnapshotScheduleResponsePB, DeleteSnapshotRequestPB,
    DeleteSnapshotScheduleRequestPB, EditSnapshotScheduleRequestPB, EditSnapshotScheduleResponsePB,
    ListSnapshotRestorationsRequestPB, ListSnapshotRestorationsResponsePB,
    ListSnapshotSchedulesRequestPB, ListSnapshotSchedulesResponsePB, ListSnapshotsRequestPB,
    ListSnapshotsResponsePB, RestoreSnapshotRequestPB, RestoreSnapshotResponsePB,
    SnapshotScheduleOptionsPB, SysRestorationEntryPB, TabletRestorationPB,
};
use yb_proto::pb::master_heartbeat::TSInformationPB;
use yb_proto::pb::master_types::{master_error_pb::Code, SysRowEntryType, TableIdentifierPB};
use yb_proto::pb::tserver_types::tablet_server_error_pb::Code as TsCode;
use yb_proto::pb::wire_protocol::app_status_pb::ErrorCode;

use crate::state::{aggregated_state, Outcome};
use crate::tablet_ops::{error_codes, outcome, OpError};
use crate::{ids, SnapshotCoordinator, SnapshotHost, SnapshotOptions};

/// Applies snapshot operations as the C++ coordinator's apply half writes their rows.
struct FakeHost {
    fake: Arc<FakeSysCatalog>,
    now: AtomicU64,
    submitted: Mutex<Vec<TabletSnapshotOpRequestPB>>,
}

impl FakeHost {
    fn row<M: Message>(&self, entry_type: SysRowEntryType, id: &[u8]) -> Option<M> {
        let at = self.fake.now();
        self.fake
            .rows_at(at)
            .get(&(entry_type as i32, id.to_vec()))
            .map(|d| M::parse_from_bytes(d).unwrap())
    }
}

impl SnapshotHost for FakeHost {
    fn submit(
        &self,
        _term: i64,
        req: TabletSnapshotOpRequestPB,
    ) -> BoxFuture<'static, Result<(), Status>> {
        self.submitted.lock().unwrap().push(req.clone());
        match req.operation.enum_value_or_default() {
            Operation::CREATE_ON_MASTER => {
                let mut pb = SysSnapshotEntryPB::new();
                pb.set_state(State::CREATING);
                for t in &req.tablet_id {
                    let mut ts = TabletSnapshotPB::new();
                    ts.set_id(t.clone());
                    ts.set_state(if req.imported {
                        State::COMPLETE
                    } else {
                        State::CREATING
                    });
                    pb.tablet_snapshots.push(ts);
                }
                let entries =
                    SysRowEntries::parse_from_bytes(&req.extra_data.value).unwrap_or_default();
                pb.entries = entries.entries;
                pb.set_snapshot_hybrid_time(req.snapshot_hybrid_time);
                pb.set_version(2);
                if !req.schedule_id.is_empty() {
                    pb.set_schedule_id(req.schedule_id.clone());
                }
                if req.previous_snapshot_hybrid_time != 0 {
                    pb.set_previous_snapshot_hybrid_time(req.previous_snapshot_hybrid_time);
                }
                if req.retention_duration_hours != 0 {
                    pb.set_retention_duration_hours(req.retention_duration_hours);
                }
                pb.set_imported(req.imported);
                self.fake.apply(vec![RowChange::Upsert {
                    entry_type: SysRowEntryType::SNAPSHOT,
                    id: req.snapshot_id.clone(),
                    data: pb.write_to_bytes().unwrap(),
                }]);
            }
            Operation::DELETE_ON_MASTER => {
                let mut pb: SysSnapshotEntryPB = self
                    .row(SysRowEntryType::SNAPSHOT, &req.snapshot_id)
                    .unwrap();
                pb.set_state(State::DELETING);
                for t in pb.tablet_snapshots.iter_mut() {
                    t.set_state(State::DELETING);
                }
                pb.set_version(pb.version() + 1);
                self.fake.apply(vec![RowChange::Upsert {
                    entry_type: SysRowEntryType::SNAPSHOT,
                    id: req.snapshot_id.clone(),
                    data: pb.write_to_bytes().unwrap(),
                }]);
            }
            Operation::RESTORE_SYS_CATALOG => {
                let snapshot: SysSnapshotEntryPB = self
                    .row(SysRowEntryType::SNAPSHOT, &req.snapshot_id)
                    .unwrap();
                let mut pb = SysRestorationEntryPB::new();
                pb.set_state(State::RESTORING);
                for t in &snapshot.tablet_snapshots {
                    let mut r = TabletRestorationPB::new();
                    r.set_id(t.id().to_vec());
                    r.set_state(State::RESTORING);
                    pb.tablet_restorations.push(r);
                }
                pb.set_snapshot_id(req.snapshot_id.clone());
                pb.set_schedule_id(snapshot.schedule_id().to_vec());
                pb.set_is_sys_catalog_restored(true);
                pb.set_restore_at_ht(req.snapshot_hybrid_time);
                pb.set_version(2);
                self.fake.apply(vec![RowChange::Upsert {
                    entry_type: SysRowEntryType::SNAPSHOT_RESTORATION,
                    id: req.restoration_id.clone(),
                    data: pb.write_to_bytes().unwrap(),
                }]);
            }
            other => panic!("unexpected {other:?}"),
        }
        Box::pin(async { Ok(()) })
    }

    fn max_global_now(&self) -> Result<u64, Status> {
        Ok(self.now.fetch_add(1 << 12, Ordering::SeqCst))
    }

    fn now(&self) -> Result<u64, Status> {
        Ok(self.now.fetch_add(1 << 12, Ordering::SeqCst))
    }

    fn scan_at(
        &self,
        entry_type: SysRowEntryType,
        read_time: u64,
        _timeout: Duration,
    ) -> Result<Vec<(Vec<u8>, Vec<u8>)>, Status> {
        let _ = read_time;
        Ok(self
            .fake
            .scan(entry_type, HybridTime(u64::MAX))?
            .into_iter()
            .map(|r| (r.id, r.data))
            .collect())
    }

    fn call(&self, method: &'static str, _: &[u8]) -> Result<Vec<u8>, Status> {
        Err(Status::new(StatusCode::NotSupported, method))
    }
}

type TabletHandler = dyn Fn(&TabletSnapshotOpRequestPB) -> TabletSnapshotOpResponsePB + Send + Sync;

struct Harness {
    coordinator: Arc<SnapshotCoordinator>,
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    host: Arc<FakeHost>,
    token: LeaderToken,
    scope: TermScope,
    /// TabletSnapshotOp requests, by operation.
    calls: Arc<Mutex<Vec<TabletSnapshotOpRequestPB>>>,
    handler: Arc<Mutex<Arc<TabletHandler>>>,
}

fn tserver(uuid: &str) -> (TSInformationPB, bool) {
    let mut info = TSInformationPB::new();
    info.tserver_instance
        .mut_or_insert_default()
        .set_permanent_uuid(uuid.as_bytes().to_vec());
    let mut hp = HostPortPB::new();
    hp.set_host(uuid.to_string());
    hp.set_port(9100);
    info.registration
        .mut_or_insert_default()
        .common
        .mut_or_insert_default()
        .private_rpc_addresses
        .push(hp);
    (info, true)
}

fn table(namespace_id: &[u8], name: &str, table_type: TableType) -> Vec<u8> {
    let mut t = SysTablesEntryPB::new();
    t.set_name(name.into());
    t.set_namespace_id(namespace_id.to_vec());
    t.table_type = Some(EnumOrUnknown::new(table_type));
    t.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    t.set_version(1);
    t.schema = protobuf::MessageField::some(Default::default());
    t.partition_schema = protobuf::MessageField::some(Default::default());
    t.write_to_bytes().unwrap()
}

fn tablet(table_id: &[u8], start: &[u8]) -> Vec<u8> {
    let mut t = SysTabletsEntryPB::new();
    t.set_table_id(table_id.to_vec());
    let mut p = PartitionPB::new();
    p.set_partition_key_start(start.to_vec());
    t.partition = protobuf::MessageField::some(p);
    t.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
    t.write_to_bytes().unwrap()
}

fn ok_response(_: &TabletSnapshotOpRequestPB) -> TabletSnapshotOpResponsePB {
    TabletSnapshotOpResponsePB::new()
}

impl Harness {
    async fn new() -> Harness {
        let (fake, events) = FakeSysCatalog::new();
        let mut namespace = SysNamespaceEntryPB::new();
        namespace.set_name("ks".into());
        namespace.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_CQL));
        namespace.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
        fake.apply(vec![
            RowChange::Upsert {
                entry_type: SysRowEntryType::CLUSTER_CONFIG,
                id: Vec::new(),
                data: SysClusterConfigEntryPB::new().write_to_bytes().unwrap(),
            },
            RowChange::Upsert {
                entry_type: SysRowEntryType::NAMESPACE,
                id: b"ns1".to_vec(),
                data: namespace.write_to_bytes().unwrap(),
            },
            RowChange::Upsert {
                entry_type: SysRowEntryType::TABLE,
                id: b"t1".to_vec(),
                data: table(b"ns1", "t", TableType::YQL_TABLE_TYPE),
            },
            RowChange::Upsert {
                entry_type: SysRowEntryType::TABLET,
                id: b"tab1".to_vec(),
                data: tablet(b"t1", b""),
            },
            RowChange::Upsert {
                entry_type: SysRowEntryType::TABLET,
                id: b"tab2".to_vec(),
                data: tablet(b"t1", b"\x80"),
            },
        ]);
        fake.set_term(3);
        let catalog = Catalog::new(fake.sender().clone());
        tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
        catalog.barrier().await;
        let source = FakeLeaderState::new();
        let leadership = Leadership::new(source.clone(), || true, Arc::default());
        source.lead(3);
        leadership.step();
        let mut phases = leadership.subscribe();
        while !matches!(*phases.borrow_and_update(), Phase::Ready { .. }) {
            phases.changed().await.unwrap();
        }
        let token = leadership.token().unwrap();
        let writer = CatalogWriter::start(
            catalog.clone(),
            fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        let ts_manager = TsManager::new(Arc::default(), TsManagerOptions::default());
        ts_manager.apply_peer_tservers(&[tserver("ts0"), tserver("ts1")], true);
        for (tablet, leader) in [(b"tab1", "ts0"), (b"tab2", "ts1")] {
            ts_manager.locations.update_replica(
                tablet,
                Replica::new(
                    leader.as_bytes().into(),
                    PeerRole::LEADER,
                    PeerMemberType::VOTER,
                ),
            );
        }
        let host = Arc::new(FakeHost {
            fake: fake.clone(),
            now: AtomicU64::new(1_000_000 << 12),
            submitted: Mutex::default(),
        });
        let coordinator = SnapshotCoordinator::new(
            catalog.clone(),
            writer.clone(),
            leadership,
            ts_manager,
            host.clone(),
            TsRpcExecutor::new(|_| usize::MAX),
            master_task_runtime::RetryPolicy {
                max_attempts: 3,
                ..Default::default()
            },
            SnapshotOptions {
                cleanup_delay: || Duration::ZERO,
                ..Default::default()
            },
        );
        let calls = Arc::new(Mutex::new(Vec::new()));
        let handler: Arc<Mutex<Arc<TabletHandler>>> = Arc::new(Mutex::new(Arc::new(ok_response)));
        let (record, answer) = (calls.clone(), handler.clone());
        coordinator.set_rpc(Arc::new(FakeRpcClient::new(
            move |_, _, service, method, req| {
                assert_eq!(service, "yb.tserver.TabletServerBackupService");
                assert_eq!(method, "TabletSnapshotOp");
                let req = TabletSnapshotOpRequestPB::parse_from_bytes(req).unwrap();
                record.lock().unwrap().push(req.clone());
                let handler = answer.lock().unwrap().clone();
                Ok(handler(&req).write_to_bytes().unwrap())
            },
        )));
        Harness {
            coordinator,
            catalog,
            writer,
            host,
            token,
            scope: TermScope::new(3, Arc::new(TaskRegistry::default())),
            calls,
            handler,
        }
    }

    fn deadline(&self) -> Instant {
        Instant::now() + Duration::from_secs(30)
    }

    /// Runs passes until `done` holds.
    async fn settle(&self, done: impl Fn(&Harness) -> bool) {
        for _ in 0..200 {
            self.coordinator.poll(&self.scope);
            tokio::time::sleep(Duration::from_millis(10)).await;
            self.catalog.barrier().await;
            if done(self) {
                return;
            }
        }
        panic!("did not settle");
    }

    fn snapshot_row(&self, id: &[u8]) -> Option<SysSnapshotEntryPB> {
        self.catalog
            .snapshot()
            .get(SysRowEntryType::SNAPSHOT, id)
            .map(|e| e.parse().unwrap())
    }

    fn ops(&self, operation: Operation) -> Vec<TabletSnapshotOpRequestPB> {
        self.calls
            .lock()
            .unwrap()
            .iter()
            .filter(|r| r.operation.enum_value_or_default() == operation)
            .cloned()
            .collect()
    }

    async fn create_keyspace_snapshot(&self) -> Vec<u8> {
        let mut req = CreateSnapshotRequestPB::new();
        let mut ident = TableIdentifierPB::new();
        ident
            .namespace
            .mut_or_insert_default()
            .set_id(b"ns1".to_vec());
        req.tables.push(ident);
        let mut resp = CreateSnapshotResponsePB::new();
        self.coordinator
            .create_snapshot(&self.token, self.deadline(), &req, &mut resp)
            .await
            .unwrap();
        resp.snapshot_id().to_vec()
    }

    fn list(&self, id: &[u8]) -> ListSnapshotsResponsePB {
        let mut req = ListSnapshotsRequestPB::new();
        req.set_snapshot_id(id.to_vec());
        let mut resp = ListSnapshotsResponsePB::new();
        futures_block(
            self.coordinator
                .list_snapshots(&self.token, &req, &mut resp),
        )
        .unwrap();
        resp
    }
}

/// Runs a future that does not wait on anything.
fn futures_block<T>(f: impl std::future::Future<Output = T>) -> T {
    let waker = std::task::Waker::noop();
    let mut cx = std::task::Context::from_waker(waker);
    let mut f = std::pin::pin!(f);
    match f.as_mut().poll(&mut cx) {
        std::task::Poll::Ready(v) => v,
        std::task::Poll::Pending => panic!("pending"),
    }
}

#[tokio::test]
async fn a_snapshot_is_created_on_every_tablet_and_written_once() {
    let h = Harness::new().await;
    let id = h.create_keyspace_snapshot().await;
    let submitted = h.host.submitted.lock().unwrap().clone();
    assert_eq!(submitted.len(), 1);
    assert_eq!(
        submitted[0].tablet_id,
        vec![b"tab1".to_vec(), b"tab2".to_vec()]
    );
    let entries = SysRowEntries::parse_from_bytes(&submitted[0].extra_data.value).unwrap();
    let types: Vec<SysRowEntryType> = entries.entries.iter().map(|e| e.type_()).collect();
    assert_eq!(
        types,
        vec![
            SysRowEntryType::NAMESPACE,
            SysRowEntryType::TABLE,
            SysRowEntryType::TABLET,
            SysRowEntryType::TABLET
        ]
    );
    assert_eq!(submitted[0].retention_duration_hours, 24);
    assert_eq!(
        h.list(&id).snapshots[0].entry.state(),
        State::CREATING,
        "before any tablet answered"
    );
    h.settle(|h| {
        h.snapshot_row(&id).is_some_and(|pb| {
            pb.tablet_snapshots
                .iter()
                .all(|t| t.state() == State::COMPLETE)
        })
    })
    .await;
    let creates = h.ops(Operation::CREATE_ON_TABLET);
    assert_eq!(creates.len(), 2);
    assert!(creates.iter().all(|r| r.snapshot_id == id));
    let row = h.snapshot_row(&id).unwrap();
    // The initial state is kept; the tablets say it is complete.
    assert_eq!(row.state(), State::CREATING);
    assert_eq!(row.version(), 3);
    assert_eq!(h.list(&id).snapshots[0].entry.state(), State::COMPLETE);
}

#[tokio::test]
async fn a_failure_with_no_terminal_state_is_sent_again() {
    let h = Harness::new().await;
    let failures = Arc::new(AtomicU64::new(0));
    let count = failures.clone();
    *h.handler.lock().unwrap() = Arc::new(move |req| {
        let mut resp = TabletSnapshotOpResponsePB::new();
        if req.tablet_id[0] == b"tab1" && count.fetch_add(1, Ordering::SeqCst) < 4 {
            let e = resp.error.mut_or_insert_default();
            e.set_code(TsCode::UNKNOWN_ERROR);
            e.status
                .mut_or_insert_default()
                .set_code(ErrorCode::TIMED_OUT);
        }
        resp
    });
    let id = h.create_keyspace_snapshot().await;
    h.settle(|h| {
        h.snapshot_row(&id).is_some_and(|pb| {
            pb.tablet_snapshots
                .iter()
                .all(|t| t.state() == State::COMPLETE)
        })
    })
    .await;
    assert!(failures.load(Ordering::SeqCst) >= 4);
}

#[tokio::test]
async fn a_snapshot_of_a_dropped_table_fails() {
    let h = Harness::new().await;
    let id = h.create_keyspace_snapshot().await;
    // The table is dropped before the tablets are asked.
    h.writer
        .submit(
            &h.token,
            Mutation::new().upsert(SysRowEntryType::TABLE, b"t1", {
                let mut t = SysTablesEntryPB::parse_from_bytes(&table(
                    b"ns1",
                    "t",
                    TableType::YQL_TABLE_TYPE,
                ))
                .unwrap();
                t.set_state(sys_tables_entry_pb::State::DELETING);
                t.write_to_bytes().unwrap()
            }),
        )
        .await
        .unwrap();
    h.settle(|h| {
        h.snapshot_row(&id).is_some_and(|pb| {
            pb.tablet_snapshots
                .iter()
                .all(|t| t.state() == State::FAILED)
        })
    })
    .await;
    assert_eq!(h.list(&id).snapshots[0].entry.state(), State::FAILED);
}

#[tokio::test]
async fn delete_removes_tablet_snapshots_then_the_row() {
    let h = Harness::new().await;
    let id = h.create_keyspace_snapshot().await;
    h.settle(|h| h.list(&id).snapshots[0].entry.state() == State::COMPLETE)
        .await;
    let mut req = DeleteSnapshotRequestPB::new();
    req.set_snapshot_id(id.clone());
    h.coordinator
        .delete_snapshot(&h.token, h.deadline(), &req)
        .await
        .unwrap();
    // A second delete is refused while the first runs.
    let e = h
        .coordinator
        .delete_snapshot(&h.token, h.deadline(), &req)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::SNAPSHOT_NOT_FOUND);
    assert!(e.status.message().starts_with("The snapshot "), "{e:?}");
    h.settle(|h| h.snapshot_row(&id).is_none()).await;
    assert_eq!(h.ops(Operation::DELETE_ON_TABLET).len(), 2);
}

#[tokio::test]
async fn a_tablet_gone_while_deleting_counts_as_deleted() {
    let h = Harness::new().await;
    let id = h.create_keyspace_snapshot().await;
    h.settle(|h| h.list(&id).snapshots[0].entry.state() == State::COMPLETE)
        .await;
    *h.handler.lock().unwrap() = Arc::new(|_| {
        let mut resp = TabletSnapshotOpResponsePB::new();
        let e = resp.error.mut_or_insert_default();
        e.set_code(TsCode::TABLET_NOT_FOUND);
        e.status
            .mut_or_insert_default()
            .set_code(ErrorCode::NOT_FOUND);
        resp
    });
    let mut req = DeleteSnapshotRequestPB::new();
    req.set_snapshot_id(id.clone());
    h.coordinator
        .delete_snapshot(&h.token, h.deadline(), &req)
        .await
        .unwrap();
    h.settle(|h| h.snapshot_row(&id).is_none()).await;
}

fn schedule_request(interval: u64, retention: u64) -> CreateSnapshotScheduleRequestPB {
    let mut req = CreateSnapshotScheduleRequestPB::new();
    let options = req.options.mut_or_insert_default();
    let mut tables = yb_proto::pb::master_backup::TableIdentifiersPB::new();
    let mut ident = TableIdentifierPB::new();
    let ns = ident.namespace.mut_or_insert_default();
    ns.set_name("ks".into());
    ns.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
    tables.tables.push(ident);
    options.filter.mut_or_insert_default().filter =
        Some(snapshot_schedule_filter_pb::Filter::Tables(tables));
    options.set_interval_sec(interval);
    options.set_retention_duration_sec(retention);
    req
}

#[tokio::test]
async fn schedules_take_snapshots_and_are_one_per_namespace() {
    let h = Harness::new().await;
    let mut resp = CreateSnapshotScheduleResponsePB::new();
    h.coordinator
        .create_schedule(
            &h.token,
            h.deadline(),
            &schedule_request(60, 600),
            &mut resp,
        )
        .await
        .unwrap();
    let schedule_id = resp.snapshot_schedule_id().to_vec();
    let e = h
        .coordinator
        .create_schedule(
            &h.token,
            h.deadline(),
            &schedule_request(60, 600),
            &mut CreateSnapshotScheduleResponsePB::new(),
        )
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::OBJECT_ALREADY_PRESENT);
    // The filter is stored by namespace id.
    let options: SnapshotScheduleOptionsPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::SNAPSHOT_SCHEDULE, &schedule_id)
        .unwrap()
        .parse()
        .unwrap();
    let Some(snapshot_schedule_filter_pb::Filter::Tables(t)) =
        options.filter.as_ref().and_then(|f| f.filter.clone())
    else {
        panic!("no filter");
    };
    assert_eq!(t.tables[0].namespace.id(), b"ns1");
    // The first pass takes the schedule's first snapshot.
    h.settle(|h| {
        let mut resp = ListSnapshotSchedulesResponsePB::new();
        h.coordinator
            .list_schedules(&h.token, &ListSnapshotSchedulesRequestPB::new(), &mut resp)
            .unwrap();
        resp.schedules[0]
            .snapshots
            .first()
            .is_some_and(|s| s.entry.state() == State::COMPLETE)
    })
    .await;
    let mut resp = ListSnapshotSchedulesResponsePB::new();
    h.coordinator
        .list_schedules(&h.token, &ListSnapshotSchedulesRequestPB::new(), &mut resp)
        .unwrap();
    assert_eq!(resp.schedules[0].snapshots.len(), 1, "one per interval");
    assert_eq!(
        resp.schedules[0].options.filter.tables().tables[0]
            .namespace
            .name(),
        "ks"
    );
    // Editing validates the result.
    let mut edit = EditSnapshotScheduleRequestPB::new();
    edit.set_snapshot_schedule_id(schedule_id.clone());
    edit.set_interval_sec(600);
    let e = h
        .coordinator
        .edit_schedule(
            &h.token,
            h.deadline(),
            &edit,
            &mut EditSnapshotScheduleResponsePB::new(),
        )
        .await
        .unwrap_err();
    assert_eq!(
        e.status.message(),
        "Interval must be strictly less than retention"
    );
    // A deleted schedule's snapshots go, then the schedule.
    let mut delete = DeleteSnapshotScheduleRequestPB::new();
    delete.set_snapshot_schedule_id(schedule_id.clone());
    h.coordinator
        .delete_schedule(&h.token, h.deadline(), &delete)
        .await
        .unwrap();
    h.settle(|h| {
        let snapshot = h.catalog.snapshot();
        snapshot
            .get(SysRowEntryType::SNAPSHOT_SCHEDULE, &schedule_id)
            .is_none()
            && snapshot.count(SysRowEntryType::SNAPSHOT) == 0
    })
    .await;
}

#[tokio::test]
async fn a_restore_restores_every_tablet_and_completes() {
    let h = Harness::new().await;
    let id = h.create_keyspace_snapshot().await;
    // Persisted: a restore is refused while only the in-memory progress is complete.
    h.settle(|h| {
        h.snapshot_row(&id).is_some_and(|pb| {
            pb.tablet_snapshots
                .iter()
                .all(|t| t.state() == State::COMPLETE)
        })
    })
    .await;
    let mut req = RestoreSnapshotRequestPB::new();
    req.set_snapshot_id(id.clone());
    let mut resp = RestoreSnapshotResponsePB::new();
    h.coordinator
        .restore_snapshot(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    let restoration_id = resp.restoration_id().to_vec();
    h.settle(|h| {
        h.catalog
            .snapshot()
            .get(SysRowEntryType::SNAPSHOT_RESTORATION, &restoration_id)
            .and_then(|e| e.parse::<SysRestorationEntryPB>().ok())
            .is_some_and(|pb| pb.complete_time_ht() != 0)
    })
    .await;
    let restores = h.ops(Operation::RESTORE_ON_TABLET);
    assert_eq!(restores.len(), 2);
    assert!(restores.iter().all(|r| r.snapshot_id == id));
    assert_eq!(h.ops(Operation::RESTORE_FINISHED).len(), 2);
    let mut req = ListSnapshotRestorationsRequestPB::new();
    req.set_restoration_id(restoration_id.clone());
    let mut resp = ListSnapshotRestorationsResponsePB::new();
    h.coordinator
        .list_restorations(&h.token, &req, &mut resp)
        .unwrap();
    assert_eq!(resp.restorations[0].entry.state(), State::RESTORED);
}

#[tokio::test]
async fn a_new_term_resends_what_the_last_one_did_not_write() {
    let h = Harness::new().await;
    // Tablets answer, but the leader changes before the row is written.
    let id = h.create_keyspace_snapshot().await;
    {
        let mut state = h.coordinator.state(3);
        let mut outcomes = HashMap::new();
        outcomes.insert(
            b"tab1".to_vec(),
            Outcome {
                from: State::CREATING,
                state: State::COMPLETE,
                error: None,
            },
        );
        state.outcomes.insert(id.clone(), outcomes);
    }
    assert_eq!(h.coordinator.state(4).outcomes.len(), 0);
    let row = h.snapshot_row(&id).unwrap();
    assert!(row
        .tablet_snapshots
        .iter()
        .all(|t| t.state() == State::CREATING));
}

#[test]
fn outcome_rules() {
    let err = |code: StatusCode| OpError {
        status: Status::new(code, "x"),
        ts_code: None,
        master_code: None,
        snapshot_too_old: false,
    };
    assert_eq!(
        outcome(State::CREATING, Ok(()), false).unwrap().state,
        State::COMPLETE
    );
    assert_eq!(
        outcome(State::CREATING, Ok(()), true).unwrap().state,
        State::FAILED
    );
    assert_eq!(
        outcome(State::CREATING, Err(err(StatusCode::Expired)), false)
            .unwrap()
            .state,
        State::FAILED
    );
    assert!(outcome(State::CREATING, Err(err(StatusCode::TimedOut)), false).is_none());
    let mut gone = err(StatusCode::NotFound);
    gone.ts_code = Some(TsCode::TABLET_NOT_FOUND);
    assert_eq!(
        outcome(State::DELETING, Err(gone.clone()), false)
            .unwrap()
            .state,
        State::DELETED
    );
    assert!(outcome(State::CREATING, Err(gone), false).is_none());
    let mut not_running = err(StatusCode::Expired);
    not_running.master_code = Some(Code::TABLE_NOT_RUNNING);
    assert_eq!(
        outcome(State::DELETING, Err(not_running.clone()), false)
            .unwrap()
            .state,
        State::DELETED
    );
    assert_eq!(
        outcome(State::RESTORING, Err(not_running), false)
            .unwrap()
            .state,
        State::FAILED
    );
    assert_eq!(
        outcome(State::RESTORING, Err(err(StatusCode::Aborted)), false)
            .unwrap()
            .state,
        State::FAILED
    );
}

#[test]
fn aggregation() {
    let t = |s: State| (b"t".to_vec(), s);
    assert_eq!(
        aggregated_state(State::CREATING, &[], false).unwrap(),
        State::COMPLETE
    );
    assert_eq!(
        aggregated_state(
            State::CREATING,
            &[t(State::COMPLETE), t(State::CREATING)],
            false
        )
        .unwrap(),
        State::CREATING
    );
    assert_eq!(
        aggregated_state(
            State::CREATING,
            &[t(State::COMPLETE), t(State::FAILED)],
            false
        )
        .unwrap(),
        State::FAILED
    );
    assert!(aggregated_state(
        State::DELETING,
        &[t(State::DELETED), t(State::COMPLETE)],
        false
    )
    .is_err());
}

#[test]
fn status_error_codes() {
    // A transaction error (7) of kSnapshotTooOld, then a master error (9), then the end.
    let mut bytes = vec![7];
    bytes.extend(4i32.to_le_bytes());
    bytes.push(9);
    bytes.extend(42i32.to_le_bytes());
    bytes.push(0);
    assert_eq!(error_codes(&bytes), vec![(7, 4), (9, 42)]);
    // A category of unknown shape stops decoding.
    assert_eq!(error_codes(&[14, 1, 2, 3]), vec![]);
    let _ = ids::random_id();
}

#[tokio::test]
async fn hidden_tablets_go_once_no_schedule_can_restore_before_they_were_hidden() {
    let h = Harness::new().await;
    let schedule_id = ids::random_id();
    let mut options = SnapshotScheduleOptionsPB::new();
    options.set_interval_sec(60);
    options.set_retention_duration_sec(600);
    // A snapshot of the schedule restores to times from 100.
    let mut snap = SysSnapshotEntryPB::new();
    snap.set_state(State::CREATING);
    snap.set_schedule_id(schedule_id.clone());
    snap.set_snapshot_hybrid_time(200 << 12);
    snap.set_previous_snapshot_hybrid_time(100 << 12);
    // The table and its tablets were hidden at 150, retained by the schedule.
    let mut table_pb =
        SysTablesEntryPB::parse_from_bytes(&table(b"ns1", "t", TableType::YQL_TABLE_TYPE)).unwrap();
    table_pb.set_hide_state(sys_tables_entry_pb::HideState::HIDDEN);
    table_pb.set_hide_hybrid_time(150 << 12);
    let mut mutation = Mutation::new()
        .upsert(
            SysRowEntryType::SNAPSHOT_SCHEDULE,
            &schedule_id,
            options.write_to_bytes().unwrap(),
        )
        .upsert(
            SysRowEntryType::SNAPSHOT,
            &ids::random_id(),
            snap.write_to_bytes().unwrap(),
        )
        .upsert(
            SysRowEntryType::TABLE,
            b"t1",
            table_pb.write_to_bytes().unwrap(),
        );
    for (id, start) in [(b"tab1", b"".as_slice()), (b"tab2", b"\x80".as_slice())] {
        let mut pb = SysTabletsEntryPB::parse_from_bytes(&tablet(b"t1", start)).unwrap();
        pb.set_hide_hybrid_time(150 << 12);
        pb.retained_by_snapshot_schedules.push(schedule_id.clone());
        mutation = mutation.upsert(SysRowEntryType::TABLET, id, pb.write_to_bytes().unwrap());
    }
    h.writer.submit(&h.token, mutation).await.unwrap();
    let tablet_state = |h: &Harness, id: &[u8]| {
        h.catalog
            .snapshot()
            .get(SysRowEntryType::TABLET, id)
            .unwrap()
            .parse::<SysTabletsEntryPB>()
            .unwrap()
            .state()
    };
    for _ in 0..5 {
        h.coordinator.poll(&h.scope);
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    assert_eq!(
        tablet_state(&h, b"tab1"),
        sys_tablets_entry_pb::State::RUNNING
    );
    // Once the schedule is gone, nothing retains them.
    options.set_delete_time(1);
    h.writer
        .submit(
            &h.token,
            Mutation::new().upsert(
                SysRowEntryType::SNAPSHOT_SCHEDULE,
                &schedule_id,
                options.write_to_bytes().unwrap(),
            ),
        )
        .await
        .unwrap();
    h.settle(|h| {
        h.catalog
            .snapshot()
            .get(SysRowEntryType::TABLE, b"t1")
            .unwrap()
            .parse::<SysTablesEntryPB>()
            .unwrap()
            .state()
            == sys_tables_entry_pb::State::DELETED
    })
    .await;
    assert_eq!(
        tablet_state(&h, b"tab2"),
        sys_tablets_entry_pb::State::DELETED
    );
}

#[tokio::test]
async fn clones_of_an_earlier_term_are_aborted_and_listed_by_namespace_and_seq_no() {
    let h = Harness::new().await;
    let clone_row = |seq_no: u32, state: CloneState| {
        let mut pb = SysCloneStatePB::new();
        pb.set_source_namespace_id("ns1".into());
        pb.set_clone_request_seq_no(seq_no);
        pb.set_aggregate_state(state);
        pb.write_to_bytes().unwrap()
    };
    // A finished clone and one a previous leader left creating.
    let mutation = Mutation::new()
        .upsert(
            SysRowEntryType::CLONE_STATE,
            b"c1",
            clone_row(1, CloneState::COMPLETE),
        )
        .upsert(
            SysRowEntryType::CLONE_STATE,
            b"c2",
            clone_row(2, CloneState::CREATING),
        );
    h.writer.submit(&h.token, mutation).await.unwrap();
    h.catalog.barrier().await;
    let list = |source: Option<&str>, seq_no: Option<u32>| {
        let mut req = ListClonesRequestPB::new();
        if let Some(s) = source {
            req.set_source_namespace_id(s.into());
        }
        if let Some(n) = seq_no {
            req.set_seq_no(n);
        }
        let mut resp = ListClonesResponsePB::new();
        h.coordinator.list_clones(&req, &mut resp);
        resp.entries
    };
    h.settle(|h| {
        let mut req = ListClonesRequestPB::new();
        req.set_source_namespace_id("ns1".into());
        req.set_seq_no(2);
        let mut resp = ListClonesResponsePB::new();
        h.coordinator.list_clones(&req, &mut resp);
        resp.entries
            .first()
            .is_some_and(|c| c.aggregate_state() == CloneState::ABORTED)
    })
    .await;
    let aborted = &list(Some("ns1"), Some(2))[0];
    assert_eq!(aborted.abort_message(), "aborted by master failover");
    assert_eq!(list(Some("ns1"), None).len(), 2);
    assert_eq!(list(None, None).len(), 2);
    assert!(list(Some("other"), None).is_empty());
    // A finished clone is left alone.
    assert_eq!(
        list(Some("ns1"), Some(1))[0].aggregate_state(),
        CloneState::COMPLETE
    );
}

#[tokio::test]
async fn a_task_of_an_earlier_term_does_not_reset_the_current_terms_state() {
    let h = Harness::new().await;
    h.coordinator.state(4).writing.insert(b"row".to_vec());
    // A term-3 task finishing late.
    h.coordinator.state(3).writing.remove(b"row".as_slice());
    assert!(h.coordinator.state(4).writing.contains(b"row".as_slice()));
}
