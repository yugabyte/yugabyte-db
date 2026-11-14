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

//! `GetCDCStream`, `GetCDCDBStreamInfo`, `ListCDCStreams` and `UpdateCDCStream`.

use std::collections::BTreeMap;

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::{EnumOrUnknown, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, SysCDCStreamEntryPB,
};
use yb_proto::pb::common::{ReplicationSlotLsnType, ReplicationSlotOrderingMode};
use yb_proto::pb::master_replication::{
    get_cdcdbstream_info_response_pb, CDCStreamInfoOptionsPB, CDCStreamInfoPB,
    GetCDCDBStreamInfoRequestPB, GetCDCDBStreamInfoResponsePB, GetCDCStreamRequestPB,
    GetCDCStreamResponsePB, IdTypePB, ListCDCStreamsRequestPB, ListCDCStreamsResponsePB,
    UpdateCDCStreamRequestPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::{
    error, Xrepl, ID_TYPE, NAMESPACE_ID, NOTIFICATIONS_SLOT_PREFIX, STREAM_STATE, YB_SYSTEM_DB_NAME,
};

/// A `CDC_STREAM` row.
#[derive(Clone, Debug)]
pub struct StreamRow {
    /// The row id: the stream id as `Uuid::ToHexString` writes it.
    pub id: String,
    /// The row.
    pub pb: SysCDCStreamEntryPB,
}

impl StreamRow {
    /// `CDCStreamInfo::is_deleting`: DELETING, DELETING_METADATA or DELETED.
    pub fn is_deleting(&self) -> bool {
        matches!(
            self.pb.state(),
            StreamState::DELETING | StreamState::DELETING_METADATA | StreamState::DELETED
        )
    }

    /// The value of the `id_type` option, `TABLEID` when absent.
    pub fn id_type(&self) -> Option<&[u8]> {
        self.pb
            .options
            .iter()
            .rev()
            .find(|o| o.key() == ID_TYPE)
            .map(|o| o.value())
    }
}

/// The streams, by id.
pub fn stream_rows(snapshot: &CatalogSnapshot) -> Vec<StreamRow> {
    let mut rows: Vec<StreamRow> = snapshot
        .iter(SysRowEntryType::CDC_STREAM)
        .filter_map(|e| {
            Some(StreamRow {
                id: String::from_utf8_lossy(&e.key.id).into_owned(),
                pb: e.parse().ok()?,
            })
        })
        .collect();
    rows.sort_by(|a, b| a.id.cmp(&b.id));
    rows
}

/// `xrepl::StreamId::FromString` (`Uuid::FromHexString`): 32 hex digits, which the rows hold in
/// lower case.
pub(crate) fn parse_stream_id(id: &[u8]) -> Result<String, MasterErrorPB> {
    if id.len() != 32 {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::InvalidArgument,
            format!("Size of hex_string is invalid: {}, expected: 32", id.len()),
        ));
    }
    if !id.iter().all(|c| c.is_ascii_hexdigit()) {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::InvalidArgument,
            format!("Invalid hex string: {}", String::from_utf8_lossy(id)),
        ));
    }
    Ok(String::from_utf8_lossy(id).to_ascii_lowercase())
}

/// The stream of a replication slot, as `cdcsdk_replication_slots_to_stream_map_` maps it: the
/// stream of the slot that is not being deleted, else the last one.
pub(crate) fn stream_of_slot<'a>(rows: &'a [StreamRow], slot: &str) -> Option<&'a StreamRow> {
    let mut found = None;
    for row in rows
        .iter()
        .filter(|r| r.pb.cdcsdk_ysql_replication_slot_name() == slot)
    {
        if !row.is_deleting() {
            return Some(row);
        }
        found = Some(row);
    }
    found
}

impl Xrepl {
    /// The streams of the current catalog.
    pub fn streams(&self) -> Vec<StreamRow> {
        stream_rows(&self.catalog.snapshot())
    }

    /// `GetCDCStream`.
    pub fn get_cdc_stream(
        &self,
        req: &GetCDCStreamRequestPB,
        resp: &mut GetCDCStreamResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if !req.has_stream_id() && !req.has_cdcsdk_ysql_replication_slot_name() {
            return Err(master_ts_manager::master_error_tagged(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                "One of CDC Stream ID or Replication slot name must be provided",
            ));
        }
        let snapshot = self.catalog.snapshot();
        let rows = stream_rows(&snapshot);
        let row = if req.has_stream_id() {
            let id = parse_stream_id(req.stream_id())?;
            rows.iter().find(|r| r.id == id)
        } else {
            let slot = req.cdcsdk_ysql_replication_slot_name();
            let Some(row) = stream_of_slot(&rows, slot) else {
                tracing::warn!(
                    "Did not find replication_slot_name: {slot} in \
                     cdcsdk_replication_slots_to_stream_map_."
                );
                return Err(tagged_not_found());
            };
            Some(row)
        };
        let Some(row) = row.filter(|r| !r.is_deleting()) else {
            return Err(tagged_not_found());
        };
        // An internal LISTEN/NOTIFY stream: its slot has the reserved prefix and it is on
        // yb_system.
        let is_notification_slot = row
            .pb
            .cdcsdk_ysql_replication_slot_name()
            .starts_with(NOTIFICATIONS_SLOT_PREFIX)
            && snapshot
                .get(SysRowEntryType::NAMESPACE, row.pb.namespace_id().as_bytes())
                .and_then(|n| n.namespace().map(|h| h.name == YB_SYSTEM_DB_NAME))
                .unwrap_or(false);
        let mut info = self.stream_info(row, true);
        info.set_is_notification_slot(is_notification_slot);
        resp.stream = MessageField::some(info);
        Ok(())
    }

    /// The `CDCStreamInfoPB` of a stream, as `GetCDCStream` (`for_get`) or `ListCDCStreams`
    /// renders it.
    fn stream_info(&self, row: &StreamRow, for_get: bool) -> CDCStreamInfoPB {
        let pb = &row.pb;
        let mut info = CDCStreamInfoPB::new();
        info.set_stream_id(row.id.clone().into_bytes());
        if for_get {
            if row.id_type() == Some(NAMESPACE_ID.as_bytes()) {
                info.set_namespace_id(pb.namespace_id().as_bytes().to_vec());
            }
        } else if pb.has_namespace_id() {
            info.set_namespace_id(pb.namespace_id().as_bytes().to_vec());
        }
        info.table_id = pb.table_id.iter().map(|t| t.as_bytes().to_vec()).collect();
        info.unqualified_table_id = pb.unqualified_table_id.clone();
        info.options = pb.options.clone();
        if for_get {
            info.set_transactional(pb.transactional());
        }
        if pb.has_state() {
            let mut state = yb_proto::pb::catalog_entity_info::CDCStreamOptionsPB::new();
            state.set_key(STREAM_STATE.to_string());
            state.set_value(format!("{:?}", pb.state()).into_bytes());
            info.options.push(state);
        }
        if pb.has_cdcsdk_ysql_replication_slot_name() {
            info.set_cdcsdk_ysql_replication_slot_name(
                pb.cdcsdk_ysql_replication_slot_name().to_string(),
            );
        }
        if pb.has_cdcsdk_ysql_replication_slot_plugin_name() {
            info.set_cdcsdk_ysql_replication_slot_plugin_name(
                pb.cdcsdk_ysql_replication_slot_plugin_name().to_string(),
            );
        }
        if let Some(meta) = pb.cdcsdk_stream_metadata.as_ref() {
            if meta.has_snapshot_time() {
                info.set_cdcsdk_consistent_snapshot_time(meta.snapshot_time());
            }
            if meta.has_consistent_snapshot_option() {
                info.cdcsdk_consistent_snapshot_option = meta.consistent_snapshot_option;
            }
        }
        if pb.has_stream_creation_time() {
            info.set_stream_creation_time(pb.stream_creation_time());
        }
        if (self.options.dynamic_tables_disable_option)()
            && pb.has_cdcsdk_disable_dynamic_table_addition()
        {
            info.set_cdcsdk_disable_dynamic_table_addition(
                pb.cdcsdk_disable_dynamic_table_addition(),
            );
        }
        if for_get {
            // Only for a replication slot (logical replication): the slot's LSN type and ordering
            // mode, with C++'s defaults.
            if pb.has_cdcsdk_ysql_replication_slot_name() {
                let mut options = CDCStreamInfoOptionsPB::new();
                options.cdcsdk_ysql_replication_slot_lsn_type = Some(
                    if (self.options.allow_lsn_types)()
                        && pb.has_cdcsdk_ysql_replication_slot_lsn_type()
                    {
                        pb.cdcsdk_ysql_replication_slot_lsn_type
                            .unwrap_or(EnumOrUnknown::new(
                                ReplicationSlotLsnType::ReplicationSlotLsnType_SEQUENCE,
                            ))
                    } else {
                        EnumOrUnknown::new(ReplicationSlotLsnType::ReplicationSlotLsnType_SEQUENCE)
                    },
                );
                options.cdcsdk_ysql_replication_slot_ordering_mode = Some(
                    if (self.options.allow_ordering_modes)()
                        && pb.has_cdcsdk_ysql_replication_slot_ordering_mode()
                    {
                        pb.cdcsdk_ysql_replication_slot_ordering_mode
                            .unwrap_or(EnumOrUnknown::new(
                            ReplicationSlotOrderingMode::ReplicationSlotOrderingMode_TRANSACTION,
                        ))
                    } else {
                        EnumOrUnknown::new(
                            ReplicationSlotOrderingMode::ReplicationSlotOrderingMode_TRANSACTION,
                        )
                    },
                );
                info.cdc_stream_info_options = MessageField::some(options);
            }
        } else {
            let lsn =
                (self.options.allow_lsn_types)() && pb.has_cdcsdk_ysql_replication_slot_lsn_type();
            let ordering = (self.options.allow_ordering_modes)()
                && pb.has_cdcsdk_ysql_replication_slot_ordering_mode();
            if lsn || ordering {
                let options = info.cdc_stream_info_options.mut_or_insert_default();
                if lsn {
                    options.cdcsdk_ysql_replication_slot_lsn_type =
                        pb.cdcsdk_ysql_replication_slot_lsn_type;
                }
                if ordering {
                    options.cdcsdk_ysql_replication_slot_ordering_mode =
                        pb.cdcsdk_ysql_replication_slot_ordering_mode;
                }
            }
        }
        info.replica_identity_map = pb.replica_identity_map.clone();
        info.set_allow_tables_without_primary_key(pb.allow_tables_without_primary_key());
        info.set_detect_publication_changes_implicitly(pb.detect_publication_changes_implicitly());
        info.set_xcluster_use_target_applied_filter(pb.xcluster_use_target_applied_filter());
        info.set_xcluster_is_wal_anchor(pb.xcluster_is_wal_anchor());
        info
    }

    /// `GetCDCDBStreamInfo`.
    pub fn get_cdc_db_stream_info(
        &self,
        req: &GetCDCDBStreamInfoRequestPB,
        resp: &mut GetCDCDBStreamInfoResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if !req.has_db_stream_id() {
            return Err(master_ts_manager::master_error_tagged(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                "CDC DB Stream ID must be provided",
            ));
        }
        let id = parse_stream_id(req.db_stream_id())?;
        let rows = self.streams();
        let Some(row) = rows.iter().find(|r| r.id == id && !r.is_deleting()) else {
            return Err(tagged_not_found());
        };
        if row.pb.has_namespace_id() && !row.pb.namespace_id().is_empty() {
            resp.set_namespace_id(row.pb.namespace_id().as_bytes().to_vec());
        }
        let table_info = |table_id: &[u8]| {
            let mut t = get_cdcdbstream_info_response_pb::TableInfo::new();
            t.set_stream_id(req.db_stream_id().to_vec());
            t.set_table_id(table_id.to_vec());
            t
        };
        resp.table_info = row
            .pb
            .table_id
            .iter()
            .map(|t| table_info(t.as_bytes()))
            .collect();
        resp.unqualified_table_info = row
            .pb
            .unqualified_table_id
            .iter()
            .map(|t| table_info(t))
            .collect();
        Ok(())
    }

    /// `ListCDCStreams`.
    pub fn list_cdc_streams(
        &self,
        req: &ListCDCStreamsRequestPB,
        resp: &mut ListCDCStreamsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let table_id = req.has_table_id().then(|| req.table_id().to_string());
        if let Some(table_id) = &table_id {
            // An xCluster sequences_data alias names the sequences_data table.
            let stripped = strip_sequences_data_alias(table_id);
            if snapshot
                .get(SysRowEntryType::TABLE, stripped.as_bytes())
                .is_none()
            {
                return Err(error(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Table with identifier {stripped} not found"),
                ));
            }
        }
        for row in stream_rows(&snapshot) {
            if req.has_namespace_id() && req.namespace_id() != row.pb.namespace_id() {
                continue;
            }
            if let Some(table_id) = &table_id {
                if row.pb.table_id.first().is_some_and(|t| t != table_id) {
                    continue;
                }
            }
            if row.is_deleting() {
                continue;
            }
            let id_type = row.id_type();
            let wanted = match req.id_type() {
                IdTypePB::NAMESPACE_ID => id_type == Some(NAMESPACE_ID.as_bytes()),
                IdTypePB::TABLE_ID => id_type != Some(NAMESPACE_ID.as_bytes()),
            };
            if !wanted {
                continue;
            }
            resp.streams.push(self.stream_info(&row, false));
        }
        Ok(())
    }

    /// `UpdateCDCStream`: replaces each stream's entry, keeping its xCluster target-applied
    /// filter and WAL anchor flags, in one write.
    pub async fn update_cdc_stream(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &UpdateCDCStreamRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("UpdateCDCStream: {:?}", req);
        if (self.options.fail_setup_stream_update)() {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                "Test flag to fail setup stream update is set",
            ));
        }
        let mut updates: BTreeMap<String, SysCDCStreamEntryPB> = BTreeMap::new();
        if req.streams.is_empty() {
            if !req.has_stream_id() {
                return Err(master_ts_manager::master_error_tagged(
                    Code::INVALID_REQUEST,
                    StatusCode::InvalidArgument,
                    "Stream ID must be provided",
                ));
            }
            if req.entry.is_none() {
                return Err(master_ts_manager::master_error_tagged(
                    Code::INVALID_REQUEST,
                    StatusCode::InvalidArgument,
                    "CDC Stream Entry must be provided",
                ));
            }
            updates.insert(
                parse_stream_id(req.stream_id())?,
                req.entry.clone().unwrap_or_default(),
            );
        } else {
            for s in &req.streams {
                updates.insert(
                    parse_stream_id(s.stream_id())?,
                    s.entry.clone().unwrap_or_default(),
                );
            }
        }
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let mut mutation = Mutation::new();
                    for (id, entry) in &updates {
                        let entity = snapshot
                            .get(SysRowEntryType::CDC_STREAM, id.as_bytes())
                            .ok_or_else(|| {
                                Status::new(
                                    StatusCode::NotFound,
                                    format!("Could not find CDC stream: {id}"),
                                )
                            })?;
                        let current: SysCDCStreamEntryPB = entity
                            .parse()
                            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                        let row = StreamRow {
                            id: id.clone(),
                            pb: current.clone(),
                        };
                        if row.is_deleting() {
                            return Err(Status::new(
                                StatusCode::NotFound,
                                format!("CDC stream has been deleted: {id}"),
                            ));
                        }
                        let mut pb = entry.clone();
                        pb.set_xcluster_use_target_applied_filter(
                            current.xcluster_use_target_applied_filter(),
                        );
                        pb.set_xcluster_is_wal_anchor(current.xcluster_is_wal_anchor());
                        // The state has its own field; the options form is CDCStreamInfoPB's.
                        pb.options.retain(|o| {
                            if o.key() == STREAM_STATE {
                                tracing::warn!(
                                    "Ignoring cdc state option {} for stream {id}",
                                    String::from_utf8_lossy(o.value())
                                );
                                return false;
                            }
                            true
                        });
                        mutation = mutation.read(entity).upsert_message(
                            SysRowEntryType::CDC_STREAM,
                            id.as_bytes(),
                            &pb,
                        )?;
                    }
                    Ok((mutation, ()))
                },
            )
            .await
            .map(|_| ())
            .map_err(|s| {
                if s.code == StatusCode::NotFound {
                    master_ts_manager::master_error_tagged(
                        Code::OBJECT_NOT_FOUND,
                        s.code,
                        &s.message,
                    )
                } else {
                    master_ts_manager::master_error(Code::UNKNOWN_ERROR, s.code, &s.message)
                }
            })
    }
}

/// `STATUS(NotFound, "Could not find CDC stream", ..., MasterError(OBJECT_NOT_FOUND))`.
fn tagged_not_found() -> MasterErrorPB {
    master_ts_manager::master_error_tagged(
        Code::OBJECT_NOT_FOUND,
        StatusCode::NotFound,
        "Could not find CDC stream",
    )
}

/// `xcluster::StripSequencesDataAliasIfPresent`: `<sequences_data id>.sequences_data_for.<ns>`
/// names the sequences_data table.
pub fn strip_sequences_data_alias(table_id: &str) -> &str {
    const TABLE_ID_SIZE: usize = 32;
    if table_id.len() > TABLE_ID_SIZE
        && table_id[TABLE_ID_SIZE..].starts_with(".sequences_data_for.")
    {
        &table_id[..TABLE_ID_SIZE]
    } else {
        table_id
    }
}
