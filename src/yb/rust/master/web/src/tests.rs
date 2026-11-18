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

use std::sync::Arc;
use std::time::Instant;

use master_catalog_state::{run_materializer, Catalog};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_tablet_locations::{DriveInfo, Replica};
use master_ts_manager::TsManager;
use protobuf::{EnumOrUnknown, Message};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB,
    SysNamespaceEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::{PeerRole, TableType, YQLDatabase};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_heartbeat::TSInformationPB;
use yb_proto::pb::master_types::SysRowEntryType;

use crate::json::{format_double, humanize_bytes, JsonWriter, Mode};
use crate::{Args, View, WebOptions};

#[test]
fn compact_writer() {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.field("a", "x\"y\\z\n\u{1}/é");
    jw.string("b");
    jw.start_array();
    jw.int(-1);
    jw.uint(2);
    jw.bool(true);
    jw.double(0.5);
    jw.end_array();
    jw.string("c");
    jw.start_object();
    jw.end_object();
    jw.string("d");
    jw.start_array();
    jw.end_array();
    jw.end_object();
    assert_eq!(
        String::from_utf8(jw.into_bytes()).unwrap(),
        r#"{"a":"x\"y\\z\n\u0001/é","b":[-1,2,true,0.5],"c":{},"d":[]}"#
    );
}

#[test]
fn pretty_writer() {
    let mut jw = JsonWriter::new(Mode::Pretty);
    jw.start_object();
    jw.string("a");
    jw.start_array();
    jw.int(1);
    jw.start_object();
    jw.field("b", "c");
    jw.end_object();
    jw.end_array();
    jw.string("e");
    jw.start_object();
    jw.end_object();
    jw.end_object();
    assert_eq!(
        String::from_utf8(jw.into_bytes()).unwrap(),
        "{\n    \"a\": [\n        1,\n        {\n            \"b\": \"c\"\n        }\n    ],\n    \
         \"e\": {}\n}"
    );
}

/// An early return leaves the document open, as the C++ handlers do.
#[test]
fn unterminated_document() {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.field("error", "e");
    assert_eq!(jw.into_bytes(), br#"{"error":"e""#);
}

#[test]
fn doubles_as_rapidjson() {
    for (value, text) in [
        (0.0, "0.0"),
        (-0.0, "-0.0"),
        (1.0, "1.0"),
        (100.0, "100.0"),
        (1.5, "1.5"),
        (-2.25, "-2.25"),
        (0.001234, "0.001234"),
        (1.234e-7, "1.234e-7"),
        (1e30, "1e30"),
        (1.5e300, "1.5e300"),
        (123456789012.5, "123456789012.5"),
    ] {
        assert_eq!(format_double(value), text, "{value}");
    }
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_array();
    jw.double(f64::NAN);
    jw.double(1.0);
    jw.end_array();
    assert_eq!(jw.into_bytes(), b"[1.0]");
}

#[test]
fn humanized_bytes() {
    for (bytes, text) in [
        (0, "0 B"),
        (999, "999 B"),
        (1000, "1.00 KB"),
        (1536, "1.54 KB"),
        (999_999, "1.00 MB"),
        (2_500_000_000, "2.50 GB"),
    ] {
        assert_eq!(humanize_bytes(bytes), text, "{bytes}");
    }
}

/// `JsonWriter::Protobuf`: set fields by number, enums by name, nested messages and repeated
/// fields.
#[test]
fn protobuf_fields() {
    let mut config = SysClusterConfigEntryPB::new();
    config.set_version(3);
    config.set_cluster_uuid("u".into());
    let live = config
        .replication_info
        .mut_or_insert_default()
        .live_replicas
        .mut_or_insert_default();
    live.set_num_replicas(3);
    live.set_placement_uuid(b"p".to_vec());
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.protobuf(&config);
    assert_eq!(
        String::from_utf8(jw.into_bytes()).unwrap(),
        r#"{"version":3,"replication_info":{"live_replicas":{"num_replicas":3,"placement_uuid":"p"}},"cluster_uuid":"u"}"#
    );
    let mut table = SysTablesEntryPB::new();
    table.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.protobuf(&table);
    assert_eq!(jw.into_bytes(), br#"{"state":"RUNNING"}"#);
}

#[test]
fn query_arguments() {
    let args = Args::parse(Some("id=a%20b&raw&x=1&x=2&p=c+d"));
    assert_eq!(args.get("id"), Some("a b"));
    assert_eq!(args.get("raw"), Some(""));
    assert_eq!(args.get("x"), Some("1"));
    assert_eq!(args.get("p"), Some("c d"));
    assert_eq!(args.get("missing"), None);
}

const KS: &str = "0000000000000000000000000000000a";
const TABLE: &str = "0000000000000000000000000000000b";
const TABLET: &str = "0000000000000000000000000000000c";

fn rows() -> Vec<RowChange> {
    let row = |entry_type, id: &str, data: Vec<u8>| RowChange::Upsert {
        entry_type,
        id: id.as_bytes().to_vec(),
        data,
    };
    let mut config = SysClusterConfigEntryPB::new();
    config
        .replication_info
        .mut_or_insert_default()
        .live_replicas
        .mut_or_insert_default()
        .set_num_replicas(1);
    let namespace = |name: &str| {
        let mut ns = SysNamespaceEntryPB::new();
        ns.set_name(name.into());
        ns.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_CQL));
        ns.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
        ns.write_to_bytes().unwrap()
    };
    let mut table = SysTablesEntryPB::new();
    table.set_name("t".into());
    table.set_namespace_id(KS.into());
    table.set_namespace_name("ks".into());
    table.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
    table.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    table.set_version(0);
    table.schema = protobuf::MessageField::some(Default::default());
    table.partition_schema = protobuf::MessageField::some(Default::default());
    let mut tablet = SysTabletsEntryPB::new();
    tablet.set_table_id(TABLE.into());
    tablet.partition = Some(Default::default()).into();
    tablet.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
    vec![
        row(
            SysRowEntryType::CLUSTER_CONFIG,
            "",
            config.write_to_bytes().unwrap(),
        ),
        row(SysRowEntryType::NAMESPACE, KS, namespace("ks")),
        row(
            SysRowEntryType::NAMESPACE,
            "00000000000000000000000000000001",
            namespace("system"),
        ),
        row(
            SysRowEntryType::TABLE,
            TABLE,
            table.write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLET,
            TABLET,
            tablet.write_to_bytes().unwrap(),
        ),
    ]
}

fn tserver(uuid: &str) -> (TSInformationPB, bool) {
    let mut info = TSInformationPB::new();
    info.tserver_instance
        .mut_or_insert_default()
        .set_permanent_uuid(uuid.as_bytes().to_vec());
    let mut hp = HostPortPB::new();
    hp.set_host("h1".into());
    hp.set_port(9000);
    info.registration
        .mut_or_insert_default()
        .common
        .mut_or_insert_default()
        .http_addresses
        .push(hp);
    (info, true)
}

async fn view() -> View {
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(rows());
    let catalog = Catalog::new(fake.sender().clone());
    tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
    catalog.barrier().await;
    let ts_manager = TsManager::new(Arc::default(), Default::default());
    ts_manager.apply_peer_tservers(&[tserver("ts1")], true);
    let mut replica = Replica::new(
        b"ts1".as_slice().into(),
        PeerRole::LEADER,
        PeerMemberType::VOTER,
    );
    replica.drive = DriveInfo {
        sst_files_size: 2000,
        wal_files_size: 10,
        ..DriveInfo::default()
    };
    ts_manager
        .locations
        .update_replica(TABLET.as_bytes(), replica);
    View {
        snapshot: catalog.snapshot(),
        tservers: ts_manager.all(),
        locations: ts_manager.locations.clone(),
        now: Instant::now(),
        lease_deadline: Box::new(|_| Instant::now()),
        replication_status: Box::new(|_, _| true),
        observable_from: Instant::now(),
        last_lease_report: Box::new(|_| None),
        tablespaces: None,
        host_call: Box::new(|_, _| Ok(Vec::new())),
        balancer: Default::default(),
        running_tasks: Default::default(),
        table_tasks: Default::default(),
        remote_bootstraps: Vec::new(),
        options: WebOptions {
            enable_ysql: || false,
            ysql_lease_enabled: || false,
            replication_factor: || 3,
            leaderless_alert_delay: || std::time::Duration::from_secs(5),
            tablespace_refresh_secs: || 30,
            https_links: || false,
            tablet_replicas_per_core_limit: || 0,
            tablet_replicas_per_gib_limit: || 0,
            enforce_tablet_replica_limits: || false,
            hide_dead_node_threshold_mins: || 0,
            sleep_before_reporting_lb_ui: || std::time::Duration::ZERO,
        },
    }
}

fn render(f: crate::Render, view: &View, query: Option<&str>) -> String {
    String::from_utf8(f(view, &Args::parse(query))).unwrap()
}

#[tokio::test]
async fn namespaces_json() {
    let view = view().await;
    assert_eq!(
        render(crate::tables::namespaces, &view, None),
        format!(
            r#"{{"User Namespaces":[{{"name":"ks","id":"{KS}","language":"ycql","state":"RUNNING","colocated":"false"}}],"System Namespaces":[{{"name":"system","id":"00000000000000000000000000000001","language":"ycql","state":"RUNNING","colocated":"false"}}]}}"#
        )
    );
}

#[tokio::test]
async fn tables_json() {
    let view = view().await;
    let expected = format!(
        r#"{{"user":[{{"keyspace":"ks","table_name":"t","state":"RUNNING","message":"","uuid":"{TABLE}","ysql_oid":"","hidden":false,"on_disk_size":{{"wal_files_size":"10 B","wal_files_size_bytes":10,"sst_files_size":"2.00 KB","sst_files_size_bytes":2000,"uncompressed_sst_file_size":"0 B","uncompressed_sst_file_size_bytes":0,"vector_index_size":"0 B","vector_index_size_bytes":0,"has_missing_size":false}}}}],"index":[],"system":[]}}"#
    );
    assert_eq!(render(crate::tables::tables, &view, None), expected);
    assert_eq!(
        render(crate::tables::tables, &view, Some("only_user_tables=true")),
        expected.replace(r#","system":[]"#, "")
    );
}

#[tokio::test]
async fn dump_entities_json() {
    let view = view().await;
    assert_eq!(
        render(crate::tables::dump_entities, &view, None),
        format!(
            r#"{{"keyspaces":[{{"keyspace_id":"00000000000000000000000000000001","keyspace_name":"system","keyspace_type":"ycql"}},{{"keyspace_id":"{KS}","keyspace_name":"ks","keyspace_type":"ycql"}}],"tables":[{{"table_id":"{TABLE}","keyspace_id":"{KS}","table_name":"t","state":"RUNNING"}}],"tablets":[{{"table_id":"{TABLE}","tablet_id":"{TABLET}","state":"RUNNING"}}]}}"#
        )
    );
}

#[tokio::test]
async fn tablet_servers_json() {
    let view = view().await;
    let json = render(crate::tservers::tablet_servers, &view, None);
    assert!(
        json.starts_with(r#"{"":{"h1:9000":{"time_since_hb":""#),
        "{json}"
    );
    for part in [
        r#""status":"ALIVE""#,
        r#""user_tablets_total":1,"user_tablets_leaders":1,"system_tablets_total":0"#,
        r#""permanent_uuid":"ts1"}}}"#,
    ] {
        assert!(json.contains(part), "{part} in {json}");
    }
}

#[tokio::test]
async fn health_check_json() {
    let view = view().await;
    assert_eq!(
        render(crate::tservers::health_check, &view, None),
        r#"{"dead_nodes":[],"most_recent_uptime":0,"under_replicated_tablets":[]}"#
    );
}

#[tokio::test]
async fn cluster_config_json() {
    let view = view().await;
    assert_eq!(
        render(crate::config::cluster_config_json, &view, None),
        r#"{"replication_info":{"live_replicas":{"num_replicas":1}}}"#
    );
}

#[tokio::test]
async fn replication_json() {
    let view = view().await;
    assert_eq!(
        render(crate::replication::tablet_replication, &view, None),
        r#"{"leaderless_tablets":[]}"#
    );
    // RF 1 and one replica: nothing is missing.
    assert_eq!(
        render(crate::replication::tablet_under_replication, &view, None),
        r#"{"underreplicated_tablets":[]}"#
    );
}

#[tokio::test]
async fn overview_and_distribution_json() {
    let view = view().await;
    let overview = render(crate::ui_json::overview, &view, None);
    for part in [
        r#""replication_factor":1"#,
        r#""live_tservers":1,"dead_tservers":0"#,
        r#""is_load_balanced":null"#,
        r#""encryption_at_rest":"Disabled""#,
        r#""tablet_limits":{"tablet_peer_limit":null,"tablet_peers":0,"enforced":false}"#,
        r#""placements":[{"placement_uuid":"","is_primary":true,"live_tservers":1"#,
        r#""user_tablet_peers":1"#,
    ] {
        assert!(overview.contains(part), "{part} in {overview}");
    }
    let distribution = render(crate::ui_json::load_distribution, &view, None);
    assert!(
        distribution.contains(&format!(
            r#""tables":[{{"table_id":"{TABLE}","keyspace":"ks","table_name":"t","tablet_count":1,"counts":[{{"uuid":"ts1","replicas":1,"leaders":1}}]}}]"#
        )),
        "{distribution}"
    );
    let clocks = render(crate::ui_json::tablet_server_clocks, &view, None);
    assert!(
        clocks.contains(r#""ysql_lease":{"state":"N/A"}"#),
        "{clocks}"
    );
}

#[tokio::test]
async fn table_json() {
    use yb_proto::pb::master_rust_peer::WebTableLayoutResponsePB;
    let mut view = view().await;
    view.host_call = Box::new(|method, _| {
        assert_eq!(method, "WebTableLayout");
        let mut resp = WebTableLayoutResponsePB::new();
        let mut column =
            yb_proto::pb::master_rust_peer::web_table_layout_response_pb::Column::new();
        column.set_name(b"k".to_vec());
        column.set_id(b"0".to_vec());
        column.set_type(b"int32 NOT NULL HASH".to_vec());
        resp.columns.push(column);
        resp.partitions
            .push(b"hash_split: [0x0000, 0xFFFF]".to_vec());
        Ok(resp.write_to_bytes().unwrap())
    });
    assert_eq!(
        render(crate::table::table, &view, Some(&format!("id={TABLE}"))),
        format!(
            r#"{{"table_name":"ks.t","table_id":"{TABLE}","table_version":0,"table_type":"YQL_TABLE_TYPE","table_state":"Running","table_state_message":"","table_replication_info":{{"live_replicas":{{"num_replicas":1}}}},"columns":[{{"column":"k","id":"0","type":"int32 NOT NULL HASH"}}],"tablets":[{{"tablet_id":"{TABLET}","partition":"hash_split: [0x0000, 0xFFFF]","split_depth":0,"state":"Running","hidden":"false","message":"","locations":[{{"uuid":"ts1","role":"LEADER","location":"http://h1:9000/tablet?id={TABLET}"}}]}}],"Tasks":[]}}"#
        )
    );
    assert_eq!(
        render(crate::table::table, &view, Some("id=missing")),
        r#"{"error":"Table not found!"}"#
    );
    assert_eq!(
        render(
            crate::table::table,
            &view,
            Some("keyspace_name=ks&table_name=t&keyspace_type=x")
        ),
        r#"{"error":"Wrong keyspace_type found 'x'. Possible values are: ycql, ysql, yedis"}"#
    );
}

#[test]
fn short_elapsed_matches_printf_g() {
    use crate::json::short_elapsed;
    // Values from C's "%0.3g" of the same inputs.
    for (seconds, expected) in [
        (0.5, "500 ms"),
        (1.234, "1.23 s"),
        (59.99, "60 s"),
        (90.0, "1.5 min"),
        (0.0000012, "1.2 us"),
        (0.9999, "1e+03 ms"),
        (12.0, "12 s"),
        (0.0, "0 ns"),
    ] {
        assert_eq!(short_elapsed(seconds), expected, "{seconds}");
    }
}

#[tokio::test]
async fn table_json_lists_tasks_in_flight() {
    let view = view().await;
    let _task =
        view.table_tasks
            .track(TABLE.as_bytes(), "Create Tablet", "CreateTablet RPC".into());
    let json = render(crate::table::table, &view, Some(&format!("id={TABLE}")));
    assert!(
        json.contains(r#""Tasks":[{"task_type":"Create Tablet","task_state":"kRunning","#),
        "{json}"
    );
    assert!(
        json.contains(r#""task_description":"CreateTablet RPC"}]"#),
        "{json}"
    );
}

#[test]
fn hybrid_time_sentinels_read_as_in_cpp() {
    use crate::config::hybrid_time_string;
    // kMaxHybridTimeValue is u64::MAX and kInvalidHybridTimeValue one less.
    assert_eq!(hybrid_time_string(u64::MAX), "<max>");
    assert_eq!(hybrid_time_string(u64::MAX - 1), "<invalid>");
    assert_eq!(hybrid_time_string(0), "<min>");
    assert_eq!(hybrid_time_string(1), "<initial>");
    assert_eq!(
        hybrid_time_string(5 << 12 | 3),
        "{ physical: 5 logical: 3 }"
    );
}
