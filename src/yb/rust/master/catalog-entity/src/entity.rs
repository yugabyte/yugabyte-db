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

//! In-memory catalog entities: the row as applied, plus the fields readers need on hot paths,
//! decoded once per apply.

use std::sync::Arc;

use protobuf::rt::WireType;
use protobuf::{Enum, MessageFull};
use yb_proto::pb::catalog_entity_info::sys_namespace_entry_pb;
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::EntityError;

/// An entry id, shared by every map and index that refers to the entity.
pub type EntityId = Arc<[u8]>;

/// A row's key: its entry type and id.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct RowKey {
    /// The entry type.
    pub entry_type: SysRowEntryType,
    /// The entry id.
    pub id: EntityId,
}

impl RowKey {
    /// A key for `id` of `entry_type`.
    pub fn new(entry_type: SysRowEntryType, id: &[u8]) -> RowKey {
        RowKey {
            entry_type,
            id: id.into(),
        }
    }
}

impl Ord for RowKey {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        (self.entry_type as i32, &self.id).cmp(&(other.entry_type as i32, &other.id))
    }
}

impl PartialOrd for RowKey {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

/// One catalog row.
#[derive(Debug)]
pub struct Entity {
    /// Its key.
    pub key: RowKey,
    /// The row as applied. Mutations start from these bytes, so fields this build does not know
    /// survive.
    pub raw: Arc<[u8]>,
    /// Fields decoded at apply time.
    pub hot: HotFields,
    /// Changes every time the row is applied or reloaded; a mutation that read an older version
    /// conflicts.
    pub version: u64,
}

impl Entity {
    /// Parses the full row.
    pub fn parse<M: MessageFull>(&self) -> Result<M, EntityError> {
        let mut message = M::parse_from_bytes(&self.raw)?;
        yb_proto::move_unknown_enum_values(&mut message);
        Ok(message)
    }

    /// The tablet fields, if this is a tablet.
    pub fn tablet(&self) -> Option<&TabletHot> {
        match &self.hot {
            HotFields::Tablet(t) => Some(t),
            _ => None,
        }
    }

    /// The table fields, if this is a table.
    pub fn table(&self) -> Option<&TableHot> {
        match &self.hot {
            HotFields::Table(t) => Some(t),
            _ => None,
        }
    }

    /// The namespace fields, if this is a namespace.
    pub fn namespace(&self) -> Option<&NamespaceHot> {
        match &self.hot {
            HotFields::Namespace(n) => Some(n),
            _ => None,
        }
    }
}

/// Per-type fields read on hot paths.
#[derive(Debug, Clone)]
pub enum HotFields {
    /// A namespace.
    Namespace(NamespaceHot),
    /// A table.
    Table(TableHot),
    /// A tablet.
    Tablet(TabletHot),
    /// A persisted tserver registration.
    TServer(TServerHot),
    /// The cluster config.
    ClusterConfig {
        /// `version`.
        version: i32,
    },
    /// Any other type: readers parse the row.
    Other,
}

/// `SysNamespaceEntryPB` fields.
#[derive(Debug, Clone)]
pub struct NamespaceHot {
    /// `name`.
    pub name: String,
    /// `database_type` (`YQLDatabase`).
    pub database_type: i32,
    /// `state`.
    pub state: i32,
}

/// `SysTablesEntryPB` fields.
#[derive(Debug, Clone)]
pub struct TableHot {
    /// `name`.
    pub name: String,
    /// `namespace_id`.
    pub namespace_id: Vec<u8>,
    /// `table_type`.
    pub table_type: i32,
    /// `state`.
    pub state: i32,
    /// `version`, the schema version.
    pub schema_version: u32,
    /// `partition_list_version`.
    pub partition_list_version: u32,
    /// `parent_table_id`: the table whose tablets a colocated table lives on.
    pub parent_table_id: Vec<u8>,
    /// `hide_state` is set to anything but VISIBLE.
    pub hidden: bool,
}

/// `SysTabletsEntryPB` fields.
#[derive(Debug, Clone)]
pub struct TabletHot {
    /// `table_id`.
    pub table_id: Vec<u8>,
    /// `table_ids`, or `[table_id]` when empty. With `mapped_by_parent_id` the row lists no
    /// colocated tables: they are the tables whose `parent_table_id` is `table_id`.
    pub table_ids: Vec<Vec<u8>>,
    /// `hosted_tables_mapped_by_parent_id`.
    pub mapped_by_parent_id: bool,
    /// `state`.
    pub state: i32,
    /// `partition.partition_key_start`.
    pub partition_start: Vec<u8>,
    /// `partition.partition_key_end`.
    pub partition_end: Vec<u8>,
    /// `split_depth`.
    pub split_depth: u64,
    /// `split_tablet_ids`.
    pub split_tablet_ids: Vec<Vec<u8>>,
    /// `hide_hybrid_time` is set.
    pub hidden: bool,
}

/// `SysTabletServerEntryPB` fields.
#[derive(Debug, Clone)]
pub struct TServerHot {
    /// `instance_seqno`.
    pub instance_seqno: i64,
    /// `state`.
    pub state: i32,
}

/// Builds an entity from a row, enforcing what the Rust master refuses to load (D16).
///
/// The hot fields come from the row's top-level fields on the wire, and the full parse only
/// validates the row: the C++ master loads a row whose nested `string` fields are not UTF-8 (it
/// only logs), which the Rust parser rejects. Such a row is kept, and readers that parse it fail.
pub fn decode_entity(
    entry_type: SysRowEntryType,
    id: &[u8],
    data: &[u8],
    version: u64,
) -> Result<Entity, EntityError> {
    if entry_type == SysRowEntryType::REDIS_CONFIG {
        return Err(EntityError::Unsupported(
            "YEDIS configuration rows; the Rust master does not support YEDIS".into(),
        ));
    }
    match crate::parse_entity(entry_type, data) {
        Ok(_) => {}
        Err(EntityError::Parse(e)) if e.to_string().contains("UTF-8") => {
            tracing::warn!(
                "Sys catalog row {entry_type:?} {} has a string that is not UTF-8: {e}",
                String::from_utf8_lossy(id)
            );
        }
        Err(e) => return Err(e),
    }
    let fields = WireFields::read(data)?;
    let hot = match entry_type {
        SysRowEntryType::NAMESPACE => {
            // As `NamespaceLoader` reads a row an older release wrote: a missing or UNKNOWN
            // database type is `GetDefaultDatabaseType`'s, a missing state RUNNING.
            let name = fields.string(1);
            let database_type = fields
                .int(2)
                .filter(|t| *t != YQLDatabase::YQL_DATABASE_UNKNOWN.value())
                .unwrap_or(if name == "system_redis" {
                    YQLDatabase::YQL_DATABASE_REDIS.value()
                } else {
                    YQLDatabase::YQL_DATABASE_CQL.value()
                });
            HotFields::Namespace(NamespaceHot {
                name,
                database_type,
                state: fields
                    .int(5)
                    .unwrap_or(sys_namespace_entry_pb::State::RUNNING.value()),
            })
        }
        SysRowEntryType::TABLE => {
            let name = fields.string(1);
            let mut table_type = fields
                .int(10)
                .unwrap_or(TableType::DEFAULT_TABLE_TYPE.value());
            if table_type == TableType::REDIS_TABLE_TYPE.value() {
                // TableLoader::Visit rewrites the type of a transaction table created by a release
                // that made it a YEDIS table.
                if name != GLOBAL_TRANSACTIONS_TABLE_NAME {
                    return Err(EntityError::Unsupported(
                        "YEDIS tables; the Rust master does not support YEDIS".into(),
                    ));
                }
                table_type = TableType::TRANSACTION_STATUS_TABLE_TYPE.value();
            }
            HotFields::Table(TableHot {
                name,
                namespace_id: fields.bytes(11).to_vec(),
                table_type,
                state: fields.int(6).unwrap_or(UNKNOWN_STATE),
                schema_version: fields.int(2).unwrap_or(0) as u32,
                partition_list_version: fields.int(28).unwrap_or(0) as u32,
                parent_table_id: fields.bytes(40).to_vec(),
                // HideState::VISIBLE is 0.
                hidden: fields.int(33).is_some_and(|s| s != 0),
            })
        }
        SysRowEntryType::TABLET => {
            let table_id = fields.bytes(6).to_vec();
            let mut table_ids = fields.repeated(8);
            if table_ids.is_empty() {
                table_ids.push(table_id.clone());
            }
            let partition = WireFields::read(fields.bytes(7))?;
            HotFields::Tablet(TabletHot {
                table_id,
                table_ids,
                mapped_by_parent_id: fields.int(18).is_some_and(|v| v != 0),
                state: fields.int(4).unwrap_or(UNKNOWN_STATE),
                partition_start: partition.bytes(2).to_vec(),
                partition_end: partition.bytes(3).to_vec(),
                split_depth: fields.varint(11).unwrap_or(0),
                split_tablet_ids: fields.repeated(14),
                hidden: fields.fixed64(16).unwrap_or(0) != 0,
            })
        }
        SysRowEntryType::TSERVER_REGISTRATION => HotFields::TServer(TServerHot {
            instance_seqno: fields.varint(1).unwrap_or(0) as i64,
            state: fields.int(2).unwrap_or(0),
        }),
        SysRowEntryType::CLUSTER_CONFIG => HotFields::ClusterConfig {
            version: fields.int(1).unwrap_or(0),
        },
        _ => HotFields::Other,
    };
    Ok(Entity {
        key: RowKey::new(entry_type, id),
        raw: data.into(),
        hot,
        version,
    })
}

/// `kGlobalTransactionsTableName`.
const GLOBAL_TRANSACTIONS_TABLE_NAME: &str = "transactions";

/// The `UNKNOWN = 999` default of the `state` enums.
const UNKNOWN_STATE: i32 = 999;

/// A message's top-level fields as they are on the wire; the last value of a field wins, as in a
/// parse.
#[derive(Default)]
struct WireFields {
    varints: Vec<(u32, u64)>,
    fixed64: Vec<(u32, u64)>,
    delimited: Vec<(u32, Vec<u8>)>,
}

impl WireFields {
    fn read(data: &[u8]) -> Result<WireFields, EntityError> {
        let mut fields = WireFields::default();
        let mut input = protobuf::CodedInputStream::from_bytes(data);
        while let Some(tag) = input.read_raw_tag_or_eof()? {
            let number = tag >> 3;
            match WireType::new(tag & 7) {
                Some(WireType::Varint) => fields.varints.push((number, input.read_raw_varint64()?)),
                Some(WireType::Fixed64) => fields.fixed64.push((number, input.read_fixed64()?)),
                Some(WireType::LengthDelimited) => {
                    fields.delimited.push((number, input.read_bytes()?))
                }
                Some(wire_type) => input.skip_field(wire_type)?,
                None => {
                    return Err(EntityError::Parse(protobuf::Error::from(
                        std::io::Error::new(
                            std::io::ErrorKind::InvalidData,
                            format!("invalid wire type in tag {tag}"),
                        ),
                    )))
                }
            }
        }
        Ok(fields)
    }

    fn varint(&self, number: u32) -> Option<u64> {
        self.varints
            .iter()
            .rev()
            .find(|(n, _)| *n == number)
            .map(|(_, v)| *v)
    }

    /// An `int32`, `uint32` or enum field: the low 32 bits, as the C++ parser takes them.
    fn int(&self, number: u32) -> Option<i32> {
        self.varint(number).map(|v| v as i32)
    }

    fn fixed64(&self, number: u32) -> Option<u64> {
        self.fixed64
            .iter()
            .rev()
            .find(|(n, _)| *n == number)
            .map(|(_, v)| *v)
    }

    fn bytes(&self, number: u32) -> &[u8] {
        self.delimited
            .iter()
            .rev()
            .find(|(n, _)| *n == number)
            .map_or(&[], |(_, v)| v)
    }

    fn string(&self, number: u32) -> String {
        String::from_utf8_lossy(self.bytes(number)).into_owned()
    }

    fn repeated(&self, number: u32) -> Vec<Vec<u8>> {
        self.delimited
            .iter()
            .filter(|(n, _)| *n == number)
            .map(|(_, v)| v.clone())
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use protobuf::Message;
    use yb_proto::pb::catalog_entity_info::{
        sys_tablets_entry_pb, SysTablesEntryPB, SysTabletsEntryPB,
    };

    #[test]
    fn yedis_is_refused() {
        assert!(matches!(
            decode_entity(SysRowEntryType::REDIS_CONFIG, b"x", &[], 1),
            Err(EntityError::Unsupported(_))
        ));
        let mut table = SysTablesEntryPB::new();
        table.set_name(b"redis".to_vec());
        table.set_version(0);
        table.schema = Some(Default::default()).into();
        table.set_table_type(TableType::REDIS_TABLE_TYPE);
        let bytes = table.write_to_bytes().unwrap();
        assert!(matches!(
            decode_entity(SysRowEntryType::TABLE, b"t", &bytes, 1),
            Err(EntityError::Unsupported(_))
        ));
        table.set_table_type(TableType::YQL_TABLE_TYPE);
        let bytes = table.write_to_bytes().unwrap();
        let entity = decode_entity(SysRowEntryType::TABLE, b"t", &bytes, 1).unwrap();
        assert_eq!(entity.table().unwrap().name, "redis");
    }

    #[test]
    fn legacy_yedis_transaction_table_loads() {
        let mut table = SysTablesEntryPB::new();
        table.set_name(b"transactions".to_vec());
        table.set_version(0);
        table.schema = Some(Default::default()).into();
        table.set_table_type(TableType::REDIS_TABLE_TYPE);
        let bytes = table.write_to_bytes().unwrap();
        let entity = decode_entity(SysRowEntryType::TABLE, b"t", &bytes, 1).unwrap();
        assert_eq!(
            entity.table().unwrap().table_type,
            TableType::TRANSACTION_STATUS_TABLE_TYPE.value()
        );
    }

    #[test]
    fn non_utf8_column_name_loads() {
        let mut table = SysTablesEntryPB::new();
        table.set_name(b"t".to_vec());
        table.set_version(3);
        let schema = table.schema.mut_or_insert_default();
        let mut column = yb_proto::pb::common::ColumnSchemaPB::new();
        column.set_name("PLACEHOLDER".into());
        column.type_ = Some(Default::default()).into();
        schema.columns.push(column);
        let mut bytes = table.write_to_bytes().unwrap();
        // PLACEHOLDER becomes 11 bytes that are not UTF-8.
        let at = bytes.windows(11).position(|w| w == b"PLACEHOLDER").unwrap();
        bytes[at..at + 11].fill(0xff);
        let entity = decode_entity(SysRowEntryType::TABLE, b"t", &bytes, 1).unwrap();
        let hot = entity.table().unwrap();
        assert_eq!((hot.name.as_str(), hot.schema_version), ("t", 3));
        assert!(entity.parse::<SysTablesEntryPB>().is_err());
    }

    #[test]
    fn tablet_hot_fields() {
        let mut tablet = SysTabletsEntryPB::new();
        tablet.set_table_id(b"tbl".to_vec());
        tablet.set_state(sys_tablets_entry_pb::State::RUNNING);
        tablet
            .partition
            .mut_or_insert_default()
            .set_partition_key_start(b"a".to_vec());
        let bytes = tablet.write_to_bytes().unwrap();
        let entity = decode_entity(SysRowEntryType::TABLET, b"tab", &bytes, 7).unwrap();
        let hot = entity.tablet().unwrap();
        assert_eq!(hot.table_ids, vec![b"tbl".to_vec()]);
        assert_eq!(hot.partition_start, b"a");
        assert_eq!(hot.state, sys_tablets_entry_pb::State::RUNNING.value());
        assert!(!hot.hidden && !hot.mapped_by_parent_id);
        let parsed: SysTabletsEntryPB = entity.parse().unwrap();
        assert_eq!(parsed, tablet);
        assert!(decode_entity(SysRowEntryType::TABLET, b"tab", &[], 1).is_err());
    }
}
