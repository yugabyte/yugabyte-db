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

//! The C++ master's gflags the lifecycle honors.

use super::*;
use master_catalog_state::Mutation;
use yb_proto::pb::common::TransactionMetadataPB;

fn pg_db_id(oid: u32) -> Vec<u8> {
    format!("{oid:08x}000030008000000000000000").into_bytes()
}

async fn create_ysql_database(h: &Harness, name: &str, oid: u32) -> Result<(), MasterErrorPB> {
    let mut req = CreateNamespaceRequestPB::new();
    req.set_name(name.into());
    req.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    req.set_namespace_id(pg_db_id(oid));
    let mut resp = CreateNamespaceResponsePB::new();
    h.lifecycle
        .create_namespace(&h.token, Harness::deadline(), &req, &mut resp)
        .await
}

/// `qos_max_db_count`, with `enable_qos`: CREATE DATABASE fails once that many databases other
/// than the templates exist.
#[tokio::test]
async fn qos_limits_the_number_of_databases() {
    let h = Harness::with_options(
        3,
        LifecycleOptions {
            enable_qos: || true,
            qos_max_db_count: || 1,
            ..LifecycleOptions::default()
        },
    )
    .await;
    create_ysql_database(&h, "template1", 1).await.unwrap();
    create_ysql_database(&h, "db1", 0x4000).await.unwrap();
    let e = create_ysql_database(&h, "db2", 0x4001).await.unwrap_err();
    assert_eq!(e.code(), Code::INVALID_REQUEST);
    assert!(
        e.status.message().contains("Too many databases: 1"),
        "{e:?}"
    );

    // Without QoS the flag has no effect.
    let h = Harness::with_options(
        3,
        LifecycleOptions {
            qos_max_db_count: || 1,
            ..LifecycleOptions::default()
        },
    )
    .await;
    create_ysql_database(&h, "db1", 0x4000).await.unwrap();
    create_ysql_database(&h, "db2", 0x4001).await.unwrap();
}

/// `enable_transactional_ddl_gc`: off, a table created in a DDL transaction without DDL atomicity
/// does not record the transaction, so the verifier never drops it.
#[tokio::test]
async fn transactional_ddl_gc_records_the_transaction_only_when_on() {
    const DB: &[u8] = b"000033e8000030008000000000000000";
    const TABLE: &[u8] = b"000033e8000030008000000000004000";
    for gc in [true, false] {
        let h = Harness::with_options(
            3,
            LifecycleOptions {
                enable_transactional_ddl_gc: if gc { || true } else { || false },
                ..LifecycleOptions::default()
            },
        )
        .await;
        let mut ns = SysNamespaceEntryPB::new();
        ns.set_name(b"yugabyte".to_vec());
        ns.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
        ns.set_state(sys_namespace_entry_pb::State::RUNNING);
        h.lifecycle
            .write(&h.token, Harness::deadline(), |_| {
                Ok((
                    Mutation::new()
                        .upsert_message(SysRowEntryType::NAMESPACE, DB, &ns)
                        .unwrap(),
                    (),
                ))
            })
            .await
            .unwrap();
        let mut txn = TransactionMetadataPB::new();
        txn.set_transaction_id(vec![1; 16]);
        txn.set_status_tablet("status".into());
        let mut req = CreateTableRequestPB::new();
        req.set_name("t".into());
        req.set_table_type(TableType::PGSQL_TABLE_TYPE);
        req.set_table_id(TABLE.to_vec());
        req.schema = Some(Default::default()).into();
        req.namespace.mut_or_insert_default().set_id(DB.to_vec());
        req.set_num_tablets(1);
        req.transaction = Some(txn).into();
        let mut resp = CreateTableResponsePB::new();
        h.lifecycle
            .create_table(&h.token, Harness::deadline(), &req, &mut resp)
            .await
            .unwrap();
        let row: SysTablesEntryPB = h
            .snapshot()
            .get(SysRowEntryType::TABLE, TABLE)
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(row.transaction.is_some(), gc, "gc={gc}");
    }
}

/// `catalog_manager_inject_latency_in_delete_table_ms`: `DeleteTable` answers that much later.
#[tokio::test]
async fn delete_table_waits_the_injected_latency() {
    for latency in [Duration::ZERO, Duration::from_millis(300)] {
        let h = Harness::with_options(
            3,
            LifecycleOptions {
                inject_latency_in_delete_table: if latency.is_zero() {
                    || Duration::ZERO
                } else {
                    || Duration::from_millis(300)
                },
                ..LifecycleOptions::default()
            },
        )
        .await;
        h.create_namespace("ks").await.unwrap();
        let (result, _) = h.create_table("ks", "t", Some(1)).await;
        result.unwrap();
        let mut req = DeleteTableRequestPB::new();
        let table = req.table.mut_or_insert_default();
        table.set_table_name("t".into());
        table
            .namespace
            .mut_or_insert_default()
            .set_name("ks".into());
        let mut resp = DeleteTableResponsePB::new();
        let started = std::time::Instant::now();
        h.lifecycle
            .delete_table(&h.token, Harness::deadline(), &req, &mut resp)
            .await
            .unwrap();
        let elapsed = started.elapsed();
        if latency.is_zero() {
            assert!(elapsed < Duration::from_millis(300), "{elapsed:?}");
        } else {
            assert!(elapsed >= latency, "{elapsed:?}");
        }
    }
}

/// `HasOutstandingSplits`, which the backfill waits on: a split child that is not running yet.
#[tokio::test]
async fn a_split_child_not_running_is_an_outstanding_split() {
    const TABLE: &[u8] = b"t";
    let h = Harness::new(3).await;
    let tablet = |start: &[u8], end: &[u8], depth: u64, parent: Option<&[u8]>, running: bool| {
        let mut pb = SysTabletsEntryPB::new();
        pb.set_table_id(TABLE.to_vec());
        pb.table_ids.push(TABLE.to_vec());
        let partition = pb.partition.mut_or_insert_default();
        partition.set_partition_key_start(start.to_vec());
        partition.set_partition_key_end(end.to_vec());
        pb.set_split_depth(depth);
        if let Some(parent) = parent {
            pb.set_split_parent_tablet_id(parent.to_vec());
        }
        pb.state = Some(EnumOrUnknown::new(if running {
            sys_tablets_entry_pb::State::RUNNING
        } else {
            sys_tablets_entry_pb::State::CREATING
        }));
        pb
    };
    let write = |children_running: bool| {
        let mut table = SysTablesEntryPB::new();
        table.set_name(b"t".to_vec());
        table.set_namespace_id(b"ns".to_vec());
        table.set_version(0);
        table.schema = MessageField::some(SchemaPB::new());
        table.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
        let mut parent = tablet(b"", b"", 0, None, true);
        parent.split_tablet_ids = vec![b"c1".to_vec(), b"c2".to_vec()];
        Mutation::new()
            .upsert_message(SysRowEntryType::TABLE, TABLE, &table)
            .unwrap()
            .upsert_message(SysRowEntryType::TABLET, b"p", &parent)
            .unwrap()
            .upsert_message(
                SysRowEntryType::TABLET,
                b"c1",
                &tablet(b"", b"m", 1, Some(b"p"), children_running),
            )
            .unwrap()
            .upsert_message(
                SysRowEntryType::TABLET,
                b"c2",
                &tablet(b"m", b"", 1, Some(b"p"), children_running),
            )
            .unwrap()
    };
    for running in [false, true] {
        h.lifecycle
            .write(&h.token, Harness::deadline(), |_| Ok((write(running), ())))
            .await
            .unwrap();
        assert_eq!(
            crate::backfill::has_outstanding_splits(&h.snapshot(), TABLE),
            !running
        );
    }
}
