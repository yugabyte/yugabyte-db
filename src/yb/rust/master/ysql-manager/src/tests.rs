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
}
