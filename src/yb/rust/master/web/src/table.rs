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

//! `/api/v1/table` (`MasterPathHandlers::HandleTablePageJSON`).

use std::sync::Arc;

use master_catalog_entity::Entity;
use master_catalog_state::CatalogSnapshot;
use protobuf::Message;
use yb_proto::pb::catalog_entity_info::SysTabletsEntryPB;
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_rust_peer::{WebTableLayoutRequestPB, WebTableLayoutResponsePB};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::classify::{pg_database_oid, TableRow};
use crate::json::{enum_name, JsonWriter, Mode};
use crate::tservers::{cluster_config, host_port_string, public_http_host_port};
use crate::{Args, View};

/// `Capitalize`: the first letter upper case, the rest lower.
pub(crate) fn capitalize(s: &str) -> String {
    let mut chars = s.chars();
    match chars.next() {
        Some(first) => {
            first.to_ascii_uppercase().to_string() + &chars.as_str().to_ascii_lowercase()
        }
        None => String::new(),
    }
}

/// `DatabaseTypeByName`.
fn database_type_by_name(name: &str) -> YQLDatabase {
    match name {
        "ycql" => YQLDatabase::YQL_DATABASE_CQL,
        "ysql" => YQLDatabase::YQL_DATABASE_PGSQL,
        "yedis" => YQLDatabase::YQL_DATABASE_REDIS,
        _ => YQLDatabase::YQL_DATABASE_UNKNOWN,
    }
}

/// `CatalogManager::GetTableInfoFromNamespaceNameAndTableName` without a PG schema name: YSQL
/// tables are not found by name.
fn table_by_name<'a>(
    snapshot: &'a CatalogSnapshot,
    database_type: YQLDatabase,
    keyspace: &str,
    table: &str,
) -> Option<&'a Arc<Entity>> {
    if database_type == YQLDatabase::YQL_DATABASE_PGSQL {
        return None;
    }
    let namespace = master_table_lifecycle::lookup::find_namespace_by_name(
        snapshot,
        keyspace,
        database_type as i32,
    )?;
    snapshot.table_by_name(&namespace.key.id, table)
}

/// `TransactionId::ToString`: the 16 bytes as a UUID.
fn uuid_string(bytes: &[u8]) -> Option<String> {
    if bytes.len() != 16 {
        return None;
    }
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    Some(format!(
        "{}-{}-{}-{}-{}",
        &hex[0..8],
        &hex[8..12],
        &hex[12..16],
        &hex[16..20],
        &hex[20..32]
    ))
}

fn error(jw: &mut JsonWriter, message: impl AsRef<[u8]>) -> Vec<u8> {
    jw.field("error", message);
    jw.end_object();
    std::mem::replace(jw, JsonWriter::new(Mode::Compact)).into_bytes()
}

/// `MasterPathHandlers::HandleTablePageJSON`.
pub(crate) fn table(view: &View, args: &Args) -> Vec<u8> {
    let snapshot = &*view.snapshot;
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();

    let entity = match args.get("id") {
        Some(id) => snapshot.get(SysRowEntryType::TABLE, id.as_bytes()),
        None => {
            let (Some(keyspace), Some(name)) = (args.get("keyspace_name"), args.get("table_name"))
            else {
                return error(
                    &mut jw,
                    "Missing 'id' argument or 'keyspace_name, table_name' argument pair. \
                     Arguments must either contain the table id or the (keyspace_name, \
                     table_name) pair.",
                );
            };
            let database_type = match args.get("keyspace_type") {
                Some(t) => database_type_by_name(t),
                None if keyspace == "system_redis" => YQLDatabase::YQL_DATABASE_REDIS,
                None => YQLDatabase::YQL_DATABASE_CQL,
            };
            if database_type == YQLDatabase::YQL_DATABASE_UNKNOWN {
                return error(
                    &mut jw,
                    format!(
                        "Wrong keyspace_type found '{}'. Possible values are: ycql, ysql, yedis",
                        args.get("keyspace_type").unwrap_or_default()
                    ),
                );
            }
            table_by_name(snapshot, database_type, keyspace, name)
        }
    };
    let Some(table) = entity.and_then(TableRow::new) else {
        return error(&mut jw, "Table not found!");
    };
    let pb = &table.pb;

    let keyspace = crate::classify::namespace_name(snapshot, pb.namespace_id());
    let name = String::from_utf8_lossy(pb.name());
    jw.field(
        "table_name",
        if keyspace.is_empty() {
            name.to_string()
        } else {
            format!("{keyspace}.{name}")
        },
    );
    jw.field("table_id", table.id());
    jw.string("table_version");
    jw.uint(pb.version().into());
    jw.field("table_type", enum_name(pb.table_type()));
    jw.field("table_state", capitalize(&enum_name(pb.state())));
    jw.field("table_state_message", pb.state_msg());

    table_placement(view, &table, &mut jw);
    ddl_verifier_state(&table, &mut jw);

    // A vector index's partitions are encoded with its indexed table's schema.
    let mut layout = WebTableLayoutRequestPB::new();
    layout.schema = pb.schema.clone();
    layout.partition_schema = pb.partition_schema.clone();
    let is_vector_index = pb.index_info.is_some() && pb.index_info.vector_idx_options.is_some();
    if is_vector_index {
        let indexed_id = table.indexed_table_id();
        let Some(indexed) = snapshot
            .get(SysRowEntryType::TABLE, indexed_id)
            .and_then(TableRow::new)
        else {
            return error(
                &mut jw,
                format!(
                    "Indexed table not found: {}",
                    String::from_utf8_lossy(indexed_id)
                ),
            );
        };
        layout.partition_keys = indexed.pb.schema.clone();
        layout.partition_schema = indexed.pb.partition_schema.clone();
    }
    let tablets: Vec<(Arc<Entity>, SysTabletsEntryPB)> = table
        .tablets(snapshot)
        .into_iter()
        .filter_map(|e| Some((e.clone(), e.parse().ok()?)))
        .collect();
    for (_, tablet) in &tablets {
        layout
            .partitions
            .push(tablet.partition.clone().unwrap_or_default());
    }
    let layout = match host_layout(view, &layout) {
        Ok(layout) => layout,
        Err(message) => return error(&mut jw, message),
    };

    // JsonOutputSchemaTable.
    jw.string("columns");
    jw.start_array();
    for column in &layout.columns {
        jw.start_object();
        jw.field("column", column.name());
        jw.field("id", column.id());
        jw.field("type", column.type_());
        jw.end_object();
    }
    jw.end_array();

    jw.string("tablets");
    jw.start_array();
    for ((entity, tablet), partition) in tablets.iter().zip(&layout.partitions) {
        jw.start_object();
        jw.field("tablet_id", &*entity.key.id);
        jw.field("partition", partition);
        jw.string("split_depth");
        jw.uint(tablet.split_depth());
        jw.field("state", capitalize(&enum_name(tablet.state())));
        jw.field(
            "hidden",
            if tablet.hide_hybrid_time() != 0 {
                "true"
            } else {
                "false"
            },
        );
        jw.field("message", tablet.state_msg());
        locations(view, &entity.key.id, &mut jw);
        jw.end_object();
    }
    jw.end_array();

    // A Rust leader keeps no per-table MonitoredTasks.
    jw.string("Tasks");
    jw.start_array();
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}

fn host_layout(
    view: &View,
    request: &WebTableLayoutRequestPB,
) -> Result<WebTableLayoutResponsePB, String> {
    let bytes = request.write_to_bytes().map_err(|e| e.to_string())?;
    let response = (view.host_call)("WebTableLayout", &bytes)
        .map_err(|e| format!("Unable to decode schema: {e}"))?;
    let response =
        WebTableLayoutResponsePB::parse_from_bytes(&response).map_err(|e| e.to_string())?;
    if let Some(error) = response.error.as_ref() {
        return Err(format!(
            "Unable to decode schema: {}",
            error.status.message()
        ));
    }
    Ok(response)
}

/// The tablespace and the replication info the table follows.
fn table_placement(view: &View, table: &TableRow, jw: &mut JsonWriter) {
    let config = cluster_config(&view.snapshot).unwrap_or_default();
    let Some(tablespaces) = view.tablespaces.as_ref() else {
        jw.string("table_replication_info");
        jw.protobuf(&master_table_lifecycle::table_replication_info(
            table.pb.replication_info.as_ref(),
            &config,
        ));
        return;
    };
    match tablespaces.tablespace_of(table.id(), &table.pb) {
        Ok(tablespace) => {
            if let Some(tablespace) = &tablespace {
                jw.string("table_tablespace_oid");
                jw.uint(pg_database_oid(tablespace).unwrap_or_default().into());
            }
            jw.string("table_replication_info");
            match tablespaces.table_replication_info(table.id(), &table.pb, &config) {
                Ok(info) => jw.protobuf(&info),
                Err(_) => jw.string("Unable to determine Tablespace information"),
            }
        }
        Err(_) => {
            jw.string("table_replication_info");
            let refresh = (view.options.tablespace_refresh_secs)();
            if refresh > 0 {
                jw.string(format!(
                    "Tablespace information not available now, please try again after {refresh} \
                     seconds"
                ));
            } else {
                jw.string(
                    "Tablespace information is not available as the periodic task used to \
                     refresh it is disabled.",
                );
            }
        }
    }
}

/// A YSQL DDL in progress on the table. The C++ page writes the last key without a value, so the
/// `columns` key that follows becomes its value; kept as the C++ leader writes it.
fn ddl_verifier_state(table: &TableRow, jw: &mut JsonWriter) {
    let pb = &table.pb;
    let Some(state) = pb.ysql_ddl_txn_verifier_state.first() else {
        return;
    };
    jw.string("table_transaction_verifier_state");
    let txn = pb.transaction.transaction_id();
    match uuid_string(txn) {
        Some(id) => jw.string(id),
        None => jw.string(format!(
            "Failed to decode transaction with error:Corruption: Invalid length of binary data \
             with TransactionId '{}': {} (expected 16)",
            String::from_utf8_lossy(txn),
            txn.len()
        )),
    }
    let created = state.contains_create_table_op();
    let alter = state.contains_alter_table_op();
    jw.field(
        "table_transaction_ddl",
        format!(
            "{}{}{}",
            if created { "Create " } else { "" },
            if alter { " Alter " } else { "" },
            if state.contains_drop_table_op() {
                "Delete"
            } else {
                ""
            }
        ),
    );
    if alter && !created {
        jw.field(
            "table_transaction_ddl_previous_table_name",
            state.previous_table_name(),
        );
        if state.previous_schema.is_some() {
            jw.string("table_transaction_ddl_previous_table_previous_schema");
            jw.protobuf(state.previous_schema.get_or_default());
            jw.string("table_transaction_ddl_previous_table_current_schema");
        }
    }
}

/// `RaftConfigToJson`: the replicas by tserver uuid, each linking to its tserver's tablet page.
fn locations(view: &View, tablet_id: &[u8], jw: &mut JsonWriter) {
    let mut replicas = view
        .locations
        .get(tablet_id)
        .map(|r| r.replicas)
        .unwrap_or_default();
    replicas.sort_by(|a, b| a.ts_uuid.cmp(&b.ts_uuid));
    jw.string("locations");
    jw.start_array();
    for replica in &replicas {
        jw.start_object();
        jw.field("uuid", &*replica.ts_uuid);
        jw.field("role", enum_name(replica.role));
        jw.string("location");
        let desc = view.tservers.iter().find(|d| d.uuid == replica.ts_uuid);
        let location = desc
            .and_then(|d| public_http_host_port(d.pb.registration.get_or_default()))
            .map(|hp| {
                format!(
                    "{}://{}/tablet?id={}",
                    if (view.options.https_links)() {
                        "https"
                    } else {
                        "http"
                    },
                    host_port_string(&hp),
                    url_encode(tablet_id)
                )
            });
        match location {
            Some(location) => jw.string(location),
            None => jw.string(html_escape(&replica.ts_uuid)),
        }
        jw.end_object();
    }
    jw.end_array();
}

/// `UrlEncodeToString`: unreserved characters as they are, the rest as `%XX`.
fn url_encode(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|&b| {
            if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
                (b as char).to_string()
            } else {
                format!("%{b:02X}")
            }
        })
        .collect()
}

/// `EscapeForHtmlToString`.
pub(crate) fn html_escape(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(bytes.len());
    for &b in bytes {
        match b {
            b'&' => out.extend_from_slice(b"&amp;"),
            b'<' => out.extend_from_slice(b"&lt;"),
            b'>' => out.extend_from_slice(b"&gt;"),
            b'"' => out.extend_from_slice(b"&quot;"),
            b => out.push(b),
        }
    }
    out
}
