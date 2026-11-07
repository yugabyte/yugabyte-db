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
