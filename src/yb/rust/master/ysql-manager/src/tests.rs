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

use std::sync::{Arc, Mutex};

use master_catalog_state::defaults::YSQL_CATALOG_CONFIG_ID;
use master_catalog_state::{run_materializer, Catalog};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_table_lifecycle::SchemaHost;
use protobuf::{EnumOrUnknown, Message};
use yb_host_api::Status;
use yb_proto::pb::catalog_entity_info::{SysConfigEntryPB, SysNamespaceEntryPB};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_admin::IsInitDbDoneResponsePB;
use yb_proto::pb::master_rust_peer::{
    RestoreInitialSysCatalogSnapshotRequestPB, RestoreInitialSysCatalogSnapshotResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::initdb::{is_initdb_done, restore_initial_snapshot_if_needed};

/// Answers `restored` and records the terms it was called with.
struct Host {
    restored: bool,
    calls: Mutex<Vec<i64>>,
}

impl SchemaHost for Host {
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status> {
        assert_eq!(method, "RestoreInitialSysCatalogSnapshot");
        let req = RestoreInitialSysCatalogSnapshotRequestPB::parse_from_bytes(request).unwrap();
        self.calls.lock().unwrap().push(req.term());
        let mut resp = RestoreInitialSysCatalogSnapshotResponsePB::new();
        resp.set_restored(self.restored);
        Ok(resp.write_to_bytes().unwrap())
    }
}

async fn catalog(rows: Vec<RowChange>) -> Arc<Catalog> {
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(rows);
    let catalog = Catalog::new(fake.sender().clone());
    tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
    catalog.barrier().await;
    catalog
}

fn ysql_config(done: bool, error: &str) -> RowChange {
    let mut config = SysConfigEntryPB::new();
    let ysql = config.mut_ysql_catalog_config();
    ysql.set_version(1);
    ysql.set_initdb_done(done);
    if !error.is_empty() {
        ysql.set_initdb_error(error.into());
    }
    RowChange::Upsert {
        entry_type: SysRowEntryType::SYS_CONFIG,
        id: YSQL_CATALOG_CONFIG_ID.to_vec(),
        data: config.write_to_bytes().unwrap(),
    }
}

fn host(restored: bool) -> Arc<Host> {
    Arc::new(Host {
        restored,
        calls: Mutex::default(),
    })
}

#[tokio::test]
async fn is_initdb_done_reads_the_ysql_catalog_config() {
    let done = |rows| async {
        let catalog = catalog(rows).await;
        let mut resp = IsInitDbDoneResponsePB::new();
        is_initdb_done(&catalog.snapshot(), &mut resp);
        (resp.done(), resp.initdb_error().to_string())
    };
    assert_eq!(done(vec![]).await, (false, String::new()));
    assert_eq!(
        done(vec![ysql_config(false, "")]).await,
        (false, String::new())
    );
    assert_eq!(
        done(vec![ysql_config(true, "")]).await,
        (true, String::new())
    );
    assert_eq!(
        done(vec![ysql_config(true, "it broke")]).await,
        (true, "it broke".to_string())
    );
}

#[tokio::test]
async fn the_snapshot_is_restored_only_while_initdb_is_not_done() {
    // A new universe: the host restores.
    let catalog = catalog(vec![]).await;
    let h = host(true);
    assert!(restore_initial_snapshot_if_needed(&catalog, h.clone(), 7)
        .await
        .unwrap());
    assert_eq!(*h.calls.lock().unwrap(), vec![7]);

    // The host may decline (YSQL off, no snapshot, an old universe).
    let h = host(false);
    assert!(!restore_initial_snapshot_if_needed(&catalog, h.clone(), 7)
        .await
        .unwrap());
    assert_eq!(h.calls.lock().unwrap().len(), 1);

    // initdb done: the host is not asked.
    let catalog_done = self::catalog(vec![ysql_config(true, "")]).await;
    let h = host(true);
    assert!(
        !restore_initial_snapshot_if_needed(&catalog_done, h.clone(), 7)
            .await
            .unwrap()
    );
    assert!(h.calls.lock().unwrap().is_empty());
}

#[tokio::test]
async fn a_universe_with_ycql_data_is_not_restored() {
    let mut keyspace = SysNamespaceEntryPB::new();
    keyspace.set_name(b"ks".to_vec());
    keyspace.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_CQL));
    let catalog = catalog(vec![RowChange::Upsert {
        entry_type: SysRowEntryType::NAMESPACE,
        id: b"ks-id".to_vec(),
        data: keyspace.write_to_bytes().unwrap(),
    }])
    .await;
    let h = host(true);
    assert!(!restore_initial_snapshot_if_needed(&catalog, h.clone(), 1)
        .await
        .unwrap());
    assert!(h.calls.lock().unwrap().is_empty());
}

mod lease {
    use std::time::Duration;

    use master_catalog_state::{CatalogWriter, WriterOptions};
    use master_leadership::{ready_for_tests, LeaderToken};
    use master_ts_manager::{TsManager, TsManagerOptions};
    use yb_proto::pb::catalog_entity_info::{
        sys_object_lock_entry_pb, SysObjectLockEntryPB, SysTabletServerEntryPB,
    };
    use yb_proto::pb::docdb::ObjectLockPB;
    use yb_proto::pb::master_ysql_lease::{
        RefreshYsqlLeaseRequestPB, RelinquishYsqlLeaseRequestPB,
    };
    use yb_proto::pb::tserver::ReleaseObjectLockRequestPB;

    use super::*;
    use crate::lease::{export_for_tserver, LeaseOptions, YsqlLeases};
    use yb_host_api::StatusCode;

    const TXN: &str = "01234567-89ab-cdef-0123-456789abcdef";

    struct Harness {
        catalog: Arc<Catalog>,
        fake: Arc<FakeSysCatalog>,
        leases: Arc<YsqlLeases>,
        token: LeaderToken,
        _leadership: Arc<master_leadership::Leadership>,
    }

    async fn harness(options: LeaseOptions, rows: Vec<RowChange>) -> Harness {
        let mut ts = SysTabletServerEntryPB::new();
        ts.set_instance_seqno(5);
        let mut all = vec![RowChange::Upsert {
            entry_type: SysRowEntryType::TSERVER_REGISTRATION,
            id: b"ts1".to_vec(),
            data: ts.write_to_bytes().unwrap(),
        }];
        all.extend(rows);
        let (fake, events) = FakeSysCatalog::new();
        fake.apply(all);
        let catalog = Catalog::new(fake.sender().clone());
        tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
        catalog.barrier().await;
        let (leadership, _source) = ready_for_tests(1).await;
        let writer = CatalogWriter::start(
            catalog.clone(),
            fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        let ts_manager = TsManager::new(
            Arc::default(),
            TsManagerOptions {
                persist_registry: || true,
                ..TsManagerOptions::default()
            },
        );
        ts_manager.on_leader_ready(&catalog.snapshot());
        let leases = YsqlLeases::new(writer, leadership.clone(), ts_manager, options);
        Harness {
            catalog,
            fake,
            leases,
            token: leadership.token().unwrap(),
            _leadership: leadership,
        }
    }

    fn refresh_req(seqno: i64, epoch: Option<u64>) -> RefreshYsqlLeaseRequestPB {
        let mut req = RefreshYsqlLeaseRequestPB::new();
        let instance = req.instance.mut_or_insert_default();
        instance.set_permanent_uuid(b"ts1".to_vec());
        instance.set_instance_seqno(seqno);
        if let Some(epoch) = epoch {
            req.set_current_lease_epoch(epoch);
        }
        req.set_local_request_send_time_ms(1000);
        req
    }

    fn lease_of(h: &Harness) -> sys_object_lock_entry_pb::LeaseInfoPB {
        h.catalog
            .snapshot()
            .get(SysRowEntryType::OBJECT_LOCK_ENTRY, b"ts1")
            .unwrap()
            .parse::<SysObjectLockEntryPB>()
            .unwrap()
            .lease_info
            .unwrap()
    }

    #[tokio::test]
    async fn a_lease_is_extended_only_for_the_same_process_and_epoch() {
        let h = harness(LeaseOptions::default(), vec![]).await;
        let info = h
            .leases
            .refresh(&h.token, &refresh_req(5, None))
            .await
            .unwrap();
        assert!(info.new_lease());
        assert_eq!(info.lease_epoch(), 1);
        assert_eq!(info.lease_expiry_time_ms(), 1000 + 28_000);
        // The floors include the new lease.
        let floors = &info.ddl_lock_entries.lease_epoch_floors;
        assert_eq!(floors.len(), 1);
        assert_eq!(floors[0].ignore_lease_epochs_before(), 1);
        let lease = lease_of(&h);
        assert!(lease.live_lease());
        assert_eq!((lease.lease_epoch(), lease.instance_seqno()), (1, 5));

        // The tserver reports its epoch: extended, no new lease.
        let generation = h.catalog.snapshot().generation;
        let info = h
            .leases
            .refresh(&h.token, &refresh_req(5, Some(1)))
            .await
            .unwrap();
        assert!(!info.new_lease());
        assert_eq!(info.lease_epoch(), 1);
        assert!(info.ddl_lock_entries.is_none());
        assert_eq!(h.catalog.snapshot().generation, generation);

        // The tserver thinks its lease expired: a new epoch, so it restarts its sessions.
        let info = h
            .leases
            .refresh(&h.token, &refresh_req(5, None))
            .await
            .unwrap();
        assert!(info.new_lease());
        assert_eq!(info.lease_epoch(), 2);

        // Another process of the tserver must register first.
        let status = h
            .leases
            .refresh(&h.token, &refresh_req(6, Some(2)))
            .await
            .unwrap_err();
        assert_eq!(status.code, StatusCode::NotFound, "{status}");
    }

    #[tokio::test]
    async fn a_relinquished_lease_is_not_granted_again_to_the_same_process() {
        let h = harness(LeaseOptions::default(), vec![]).await;
        h.leases
            .refresh(&h.token, &refresh_req(5, None))
            .await
            .unwrap();
        let mut req = RelinquishYsqlLeaseRequestPB::new();
        let instance = req.instance.mut_or_insert_default();
        instance.set_permanent_uuid(b"ts1".to_vec());
        instance.set_instance_seqno(5);
        h.leases.relinquish(&h.token, &req).await.unwrap();
        let lease = lease_of(&h);
        assert!(!lease.live_lease() && lease.lease_relinquished());
        let status = h
            .leases
            .refresh(&h.token, &refresh_req(5, Some(1)))
            .await
            .unwrap_err();
        assert!(status.message.contains("relinquished"), "{status}");
        // An older process cannot relinquish.
        instance_seqno_lower(&h, &mut req).await;
    }

    async fn instance_seqno_lower(h: &Harness, req: &mut RelinquishYsqlLeaseRequestPB) {
        req.instance.mut_or_insert_default().set_instance_seqno(4);
        let status = h.leases.relinquish(&h.token, req).await.unwrap_err();
        assert_eq!(status.code, StatusCode::InvalidArgument, "{status}");
    }

    #[tokio::test]
    async fn a_lease_not_refreshed_expires() {
        let options = LeaseOptions {
            ttl: || Duration::from_millis(100),
            client_buffer: || Duration::ZERO,
            ..LeaseOptions::default()
        };
        let h = harness(options, vec![]).await;
        h.leases
            .refresh(&h.token, &refresh_req(5, None))
            .await
            .unwrap();
        assert!(h.leases.expire(&h.token).await.unwrap().is_empty());
        tokio::time::sleep(Duration::from_millis(150)).await;
        assert_eq!(
            h.leases.expire(&h.token).await.unwrap(),
            vec![b"ts1".to_vec()]
        );
        assert!(!lease_of(&h).live_lease());
        // The next refresh is a new lease.
        let info = h
            .leases
            .refresh(&h.token, &refresh_req(5, Some(1)))
            .await
            .unwrap();
        assert!(info.new_lease());
        assert_eq!(info.lease_epoch(), 2);
    }

    #[tokio::test]
    async fn a_new_lease_gets_the_held_locks_less_releases_in_progress() {
        let mut entry = SysObjectLockEntryPB::new();
        let lease = entry.lease_info.mut_or_insert_default();
        lease.set_live_lease(true);
        lease.set_lease_epoch(3);
        lease.set_instance_seqno(5);
        let txns = entry.lease_epochs.entry(3).or_default();
        let subtxns = txns.transactions.entry(TXN.to_string()).or_default();
        subtxns.set_status_tablet(b"status".to_vec());
        for subtxn in [1, 2] {
            let mut lock = ObjectLockPB::new();
            lock.set_relation_oid(1000 + subtxn);
            subtxns.subtxns.entry(subtxn).or_default().locks.push(lock);
        }
        let mut release = ReleaseObjectLockRequestPB::new();
        release.set_txn_id(super::super::lease::tests_txn_bytes(TXN));
        release.set_subtxn_id(2);
        entry.in_progress_release_request.insert(7, release);
        let h = harness(
            LeaseOptions::default(),
            vec![RowChange::Upsert {
                entry_type: SysRowEntryType::OBJECT_LOCK_ENTRY,
                id: b"ts1".to_vec(),
                data: entry.write_to_bytes().unwrap(),
            }],
        )
        .await;
        let exported = export_for_tserver(&h.catalog.snapshot()).unwrap();
        assert_eq!(exported.lock_entries.len(), 1);
        let lock = &exported.lock_entries[0];
        assert_eq!(lock.subtxn_id(), 1);
        assert_eq!(lock.session_host_uuid(), b"ts1");
        assert_eq!(lock.status_tablet(), b"status");
        assert_eq!(lock.txn_id().len(), 16);
        assert_eq!(lock.object_locks[0].relation_oid(), 1001);
        assert_eq!(
            exported.lease_epoch_floors[0].ignore_lease_epochs_before(),
            3
        );
    }

    /// A refresh of a lease whose expiry is being written waits for the write, as C++'s write
    /// lock on the tserver's entry makes it, and gets a new lease rather than an extension of the
    /// one the master is marking dead.
    #[tokio::test]
    async fn a_refresh_waits_for_an_expiry_of_its_lease() {
        let h = harness(
            LeaseOptions {
                ttl: || Duration::from_millis(100),
                client_buffer: || Duration::ZERO,
                ..LeaseOptions::default()
            },
            vec![],
        )
        .await;
        let info = h
            .leases
            .refresh(&h.token, &refresh_req(5, None))
            .await
            .unwrap();
        assert!(info.new_lease());
        assert_eq!(info.lease_epoch(), 1);
        tokio::time::sleep(Duration::from_millis(150)).await;
        h.fake.pause_writes();
        let writes = h.fake.writes();
        let expire = tokio::spawn({
            let (leases, token) = (h.leases.clone(), h.token);
            async move { leases.expire(&token).await }
        });
        while h.fake.writes() == writes {
            tokio::task::yield_now().await;
        }
        let refresh = tokio::spawn({
            let (leases, token) = (h.leases.clone(), h.token);
            async move { leases.refresh(&token, &refresh_req(5, Some(1))).await }
        });
        for _ in 0..100 {
            tokio::task::yield_now().await;
        }
        assert!(!refresh.is_finished());
        h.fake.resume_writes();
        assert_eq!(expire.await.unwrap().unwrap(), vec![b"ts1".to_vec()]);
        let info = refresh.await.unwrap().unwrap();
        assert!(info.new_lease());
        assert_eq!(info.lease_epoch(), 2);
        assert!(lease_of(&h).live_lease());
    }
}

mod locks {
    use std::time::Duration;

    use master_catalog_state::{CatalogWriter, WriterOptions};
    use master_leadership::{ready_for_tests, LeaderToken};
    use master_task_runtime::{BoxFuture, TsRpcExecutor};
    use master_ts_manager::{TsManager, TsManagerOptions};
    use tokio::time::Instant;
    use yb_host_api::rpc::FakeRpcClient;
    use yb_host_api::StatusCode;
    use yb_proto::pb::catalog_entity_info::{SysObjectLockEntryPB, SysTabletServerEntryPB};
    use yb_proto::pb::common_net::HostPortPB;
    use yb_proto::pb::docdb::ObjectLockPB;
    use yb_proto::pb::master_ddl::{
        AcquireObjectLocksGlobalRequestPB, ReleaseObjectLocksGlobalRequestPB,
    };
    use yb_proto::pb::tserver::{
        AcquireObjectLockRequestPB, AcquireObjectLockResponsePB, DdlLockEntriesPB,
        ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB,
    };

    use super::*;
    use crate::lease::tests_txn_bytes;
    use crate::locks::{export_for_master, LocalLockManager, LockOptions, ObjectLocks};

    const TXN: &str = "01234567-89ab-cdef-0123-456789abcdef";

    /// Records what the master's lock manager was asked.
    #[derive(Default)]
    struct FakeLocal {
        acquired: Mutex<Vec<AcquireObjectLockRequestPB>>,
        released: Mutex<Vec<ReleaseObjectLockRequestPB>>,
        resets: Mutex<Vec<DdlLockEntriesPB>>,
    }

    impl LocalLockManager for FakeLocal {
        fn acquire(
            &self,
            req: &AcquireObjectLockRequestPB,
            _timeout: Duration,
        ) -> BoxFuture<'static, Result<(), Status>> {
            self.acquired.lock().unwrap().push(req.clone());
            Box::pin(async { Ok(()) })
        }
        fn release(
            &self,
            req: &ReleaseObjectLockRequestPB,
            _timeout: Duration,
        ) -> Result<(), Status> {
            self.released.lock().unwrap().push(req.clone());
            Ok(())
        }
        fn reset(&self, entries: &DdlLockEntriesPB) -> Result<(), Status> {
            self.resets.lock().unwrap().push(entries.clone());
            Ok(())
        }
    }

    type Sent = Arc<Mutex<Vec<(String, String, Vec<u8>)>>>;

    struct Harness {
        catalog: Arc<Catalog>,
        locks: Arc<ObjectLocks>,
        local: Arc<FakeLocal>,
        sent: Sent,
        token: LeaderToken,
        _leadership: Arc<master_leadership::Leadership>,
    }

    fn tserver(uuid: &str) -> RowChange {
        let mut ts = SysTabletServerEntryPB::new();
        ts.set_instance_seqno(1);
        let mut hp = HostPortPB::new();
        hp.set_host(format!("{uuid}.host"));
        hp.set_port(9100);
        ts.registration
            .mut_or_insert_default()
            .private_rpc_addresses
            .push(hp);
        RowChange::Upsert {
            entry_type: SysRowEntryType::TSERVER_REGISTRATION,
            id: uuid.as_bytes().to_vec(),
            data: ts.write_to_bytes().unwrap(),
        }
    }

    /// A lock row with a lease (`live`, `epoch`) and the given locks of `TXN` at `epoch`.
    fn lock_row(uuid: &str, live: bool, epoch: u64, held: &[u32]) -> RowChange {
        let mut entry = SysObjectLockEntryPB::new();
        let lease = entry.lease_info.mut_or_insert_default();
        lease.set_live_lease(live);
        lease.set_lease_epoch(epoch);
        lease.set_instance_seqno(1);
        if !held.is_empty() {
            let subtxns = entry
                .lease_epochs
                .entry(epoch)
                .or_default()
                .transactions
                .entry(TXN.to_string())
                .or_default();
            for subtxn in held {
                let mut lock = ObjectLockPB::new();
                lock.set_relation_oid(*subtxn);
                subtxns.subtxns.entry(*subtxn).or_default().locks.push(lock);
            }
        }
        RowChange::Upsert {
            entry_type: SysRowEntryType::OBJECT_LOCK_ENTRY,
            id: uuid.as_bytes().to_vec(),
            data: entry.write_to_bytes().unwrap(),
        }
    }

    async fn harness(rows: Vec<RowChange>) -> Harness {
        let mut all = vec![tserver("ts1"), tserver("ts2")];
        all.extend(rows);
        let (fake, events) = FakeSysCatalog::new();
        fake.apply(all);
        let catalog = Catalog::new(fake.sender().clone());
        tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
        catalog.barrier().await;
        let (leadership, _source) = ready_for_tests(1).await;
        let writer = CatalogWriter::start(
            catalog.clone(),
            fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        let ts_manager = TsManager::new(
            Arc::default(),
            TsManagerOptions {
                persist_registry: || true,
                ..TsManagerOptions::default()
            },
        );
        ts_manager.on_leader_ready(&catalog.snapshot());
        let local = Arc::new(FakeLocal::default());
        let locks = ObjectLocks::new(
            catalog.clone(),
            writer,
            leadership.clone(),
            ts_manager,
            local.clone(),
            TsRpcExecutor::new(|_| 100),
            LockOptions::default(),
        );
        let sent: Sent = Arc::default();
        let recorded = sent.clone();
        locks.set_rpc(Arc::new(FakeRpcClient::new(
            move |host, _port, service, method, request| {
                assert_eq!(service, "yb.tserver.TabletServerService");
                recorded.lock().unwrap().push((
                    host.to_string(),
                    method.to_string(),
                    request.to_vec(),
                ));
                Ok(match method {
                    "AcquireObjectLocks" => AcquireObjectLockResponsePB::new().write_to_bytes(),
                    _ => ReleaseObjectLockResponsePB::new().write_to_bytes(),
                }
                .unwrap())
            },
        )));
        locks.on_term_start(&catalog.snapshot()).unwrap();
        Harness {
            catalog,
            locks,
            local,
            sent,
            token: leadership.token().unwrap(),
            _leadership: leadership,
        }
    }

    fn entry_of(h: &Harness, uuid: &str) -> SysObjectLockEntryPB {
        h.catalog
            .snapshot()
            .get(SysRowEntryType::OBJECT_LOCK_ENTRY, uuid.as_bytes())
            .unwrap()
            .parse()
            .unwrap()
    }

    fn acquire_req(epoch: u64, subtxn: u32) -> AcquireObjectLocksGlobalRequestPB {
        let mut req = AcquireObjectLocksGlobalRequestPB::new();
        req.set_txn_id(tests_txn_bytes(TXN));
        req.set_subtxn_id(subtxn);
        req.set_session_host_uuid(b"ts1".to_vec());
        req.set_lease_epoch(epoch);
        req.set_status_tablet(b"status".to_vec());
        let mut lock = ObjectLockPB::new();
        lock.set_database_oid(16384);
        lock.set_relation_oid(16385);
        req.object_locks.push(lock);
        req
    }

    fn deadline() -> Instant {
        Instant::now() + Duration::from_secs(10)
    }

    /// A lock is taken on the master, persisted under the requester's lease epoch, and sent to
    /// every tserver with a live lease, with each one's epoch and every tserver's floor.
    #[tokio::test]
    async fn a_lock_is_taken_persisted_and_sent_to_every_live_lease() {
        let h = harness(vec![
            lock_row("ts1", true, 3, &[]),
            lock_row("ts2", true, 7, &[]),
        ])
        .await;
        h.locks
            .acquire_global(&h.token, &acquire_req(3, 1), deadline())
            .await
            .unwrap();
        assert_eq!(h.local.acquired.lock().unwrap().len(), 1);
        let entry = entry_of(&h, "ts1");
        let txn = &entry.lease_epochs[&3].transactions[TXN];
        assert_eq!(txn.status_tablet(), b"status");
        assert_eq!(txn.subtxns[&1].locks.len(), 1);
        let sent = h.sent.lock().unwrap().clone();
        let mut recipients: Vec<_> = sent
            .iter()
            .map(|(host, method, request)| {
                assert_eq!(method, "AcquireObjectLocks");
                let req = AcquireObjectLockRequestPB::parse_from_bytes(request).unwrap();
                assert_eq!(req.lease_epoch_floors.len(), 2);
                (host.clone(), req.recipient_lease_epoch())
            })
            .collect();
        recipients.sort();
        assert_eq!(
            recipients,
            vec![("ts1.host".to_string(), 3), ("ts2.host".to_string(), 7)]
        );
    }

    /// `ValidateLockRequest`: a subtransaction is required, and the requester's latest epoch.
    #[tokio::test]
    async fn a_lock_needs_a_subtxn_and_the_latest_lease_epoch() {
        let h = harness(vec![lock_row("ts1", true, 3, &[])]).await;
        let e = h
            .locks
            .acquire_global(&h.token, &acquire_req(3, 0), deadline())
            .await
            .unwrap_err();
        assert_eq!(e.code, StatusCode::InvalidArgument);
        let e = h
            .locks
            .acquire_global(&h.token, &acquire_req(2, 1), deadline())
            .await
            .unwrap_err();
        assert_eq!(e.code, StatusCode::InvalidArgument);
        assert!(h.local.acquired.lock().unwrap().is_empty());
    }

    /// A release is sent to every live lease, then removed from the row and the master.
    #[tokio::test]
    async fn a_release_is_sent_then_removed() {
        let h = harness(vec![
            lock_row("ts1", true, 3, &[1, 2]),
            lock_row("ts2", true, 7, &[]),
        ])
        .await;
        // The term start loaded the held locks into the master's lock manager.
        assert_eq!(h.local.resets.lock().unwrap()[0].lock_entries.len(), 2);
        let mut req = ReleaseObjectLocksGlobalRequestPB::new();
        req.set_txn_id(tests_txn_bytes(TXN));
        req.set_subtxn_id(2);
        req.set_session_host_uuid(b"ts1".to_vec());
        req.set_lease_epoch(3);
        req.set_wait_for_completion(true);
        h.locks
            .release_global(&h.token, &req, deadline())
            .await
            .unwrap();
        let entry = entry_of(&h, "ts1");
        let subtxns = &entry.lease_epochs[&3].transactions[TXN].subtxns;
        assert!(subtxns.contains_key(&1) && !subtxns.contains_key(&2));
        assert!(entry.in_progress_release_request.is_empty());
        assert_eq!(h.local.released.lock().unwrap().len(), 1);
        assert_eq!(h.sent.lock().unwrap().len(), 2);

        // The whole transaction, named by its id alone (yb-admin).
        let mut req = ReleaseObjectLocksGlobalRequestPB::new();
        req.set_txn_id(tests_txn_bytes(TXN));
        req.set_wait_for_completion(true);
        h.locks
            .release_global(&h.token, &req, deadline())
            .await
            .unwrap();
        assert!(entry_of(&h, "ts1").lease_epochs.is_empty());
    }

    /// The locks of a lease that ended are released by the master, and floors keep the old
    /// epoch's requests out.
    #[tokio::test]
    async fn an_ended_leases_locks_are_released() {
        let h = harness(vec![
            lock_row("ts1", true, 3, &[]),
            lock_row("ts2", false, 7, &[1]),
        ])
        .await;
        h.locks.release_ended(&h.token).await.unwrap();
        for _ in 0..100 {
            if entry_of(&h, "ts2").lease_epochs.is_empty() {
                break;
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
        assert!(entry_of(&h, "ts2").lease_epochs.is_empty());
        let sent = h.sent.lock().unwrap().clone();
        // Only ts1 has a live lease.
        assert_eq!(sent.len(), 1);
        let req = ReleaseObjectLockRequestPB::parse_from_bytes(&sent[0].2).unwrap();
        assert_eq!(req.ignore_lease_epochs_before(), 8);
        assert_eq!(req.session_host_uuid(), b"ts2");
        // Every persisted lock, also an ended lease's, is the master's at a term start.
        let exported = export_for_master(&h.catalog.snapshot()).unwrap();
        assert!(exported.lock_entries.is_empty());
    }
}

#[test]
fn pg_namespace_ids_carry_the_database_oid() {
    assert_eq!(
        crate::backends::pg_namespace_id(13665),
        b"00003561000030008000000000000000".to_vec()
    );
}
