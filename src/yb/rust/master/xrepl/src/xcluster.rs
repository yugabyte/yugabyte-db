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

//! xCluster streams on the source universe.

use master_catalog_state::{Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::{EnumOrUnknown, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, CDCStreamOptionsPB, SysCDCStreamEntryPB,
};
use yb_proto::pb::master_rust_peer::{
    CdcStateEntryPB, CdcStateUpsertRequestPB, CdcStateUpsertResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::opid::OpIdPB;

use crate::create::CreatesTables;
use crate::streams::{stream_rows, strip_sequences_data_alias};
use crate::{error, new_stream_id, Xrepl};

/// `GetXClusterStreamOptions`: the options every xCluster stream has.
pub(crate) fn xcluster_stream_options() -> Vec<CDCStreamOptionsPB> {
    [
        ("source_type", "XCLUSTER"),
        ("record_type", "CHANGE"),
        ("record_format", "WAL"),
        ("checkpoint_type", "IMPLICIT"),
    ]
    .into_iter()
    .map(|(k, v)| {
        let mut o = CDCStreamOptionsPB::new();
        o.set_key(k.to_string());
        o.set_value(v.as_bytes().to_vec());
        o
    })
    .collect()
}

fn now_micros() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_micros() as u64)
}

impl Xrepl {
    /// `CreateNewXClusterStreamForTable`: the stream row, and for an ACTIVE stream its tablets'
    /// `cdc_state` rows at op 0.0 (`CheckpointStreamsToOp0`); a bootstrapping (INITIATED)
    /// stream's rows come from `BootstrapProducer`.
    pub(crate) async fn create_xcluster_stream(
        &self,
        tables: &dyn CreatesTables,
        token: &LeaderToken,
        deadline: Instant,
        table_id: &str,
        transactional: bool,
        initial_state: Option<StreamState>,
    ) -> Result<String, MasterErrorPB> {
        let e = |s: Status| error(Code::UNKNOWN_ERROR, s.code, s.message);
        self.create_cdc_state_table_if_not_found(tables, token, deadline)
            .await?;
        let stripped = strip_sequences_data_alias(table_id).to_string();
        let snapshot = self.catalog.snapshot();
        let visible = snapshot
            .get(SysRowEntryType::TABLE, stripped.as_bytes())
            .is_some_and(|t| master_table_lifecycle::lookup::is_visible_to_client(t));
        if !visible {
            return Err(error(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                format!("Table {stripped} does not exist"),
            ));
        }
        let state = initial_state.unwrap_or(StreamState::ACTIVE);
        let mut pb = SysCDCStreamEntryPB::new();
        pb.table_id.push(table_id.to_string());
        pb.set_transactional(transactional);
        pb.options = xcluster_stream_options();
        pb.state = Some(EnumOrUnknown::new(state));
        let stream_id = new_stream_id(&stream_rows(&snapshot));
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |_| {
                    Ok((
                        Mutation::new()
                            .read_absent(SysRowEntryType::CDC_STREAM, stream_id.as_bytes())
                            .upsert_message(
                                SysRowEntryType::CDC_STREAM,
                                stream_id.as_bytes(),
                                &pb,
                            )?,
                        (),
                    ))
                },
            )
            .await
            .map_err(|s| {
                error(
                    Code::UNKNOWN_ERROR,
                    s.code,
                    format!(
                        "Failed to insert xCluster stream into sys-catalog: {}",
                        s.message
                    ),
                )
            })?;
        if (self.options.disable_cdc_state_insert_on_setup)() || state != StreamState::ACTIVE {
            return Ok(stream_id);
        }
        self.checkpoint_to_op0(&stripped, &stream_id)
            .await
            .map_err(e)?;
        Ok(stream_id)
    }

    /// `CheckpointStreamsToOp0`: a row per tablet of the table at op 0.0.
    async fn checkpoint_to_op0(&self, table_id: &str, stream_id: &str) -> Result<(), Status> {
        let snapshot = self.catalog.snapshot();
        let mut upsert = CdcStateUpsertRequestPB::new();
        for tablet in snapshot.hosting_tablets_of(table_id.as_bytes()) {
            let mut entry = CdcStateEntryPB::new();
            entry.set_tablet_id(tablet.key.id.to_vec());
            entry.set_stream_id(stream_id.as_bytes().to_vec());
            let mut min = OpIdPB::new();
            min.set_term(0);
            min.set_index(0);
            entry.checkpoint = MessageField::some(min);
            entry.set_last_replication_time(now_micros());
            upsert.entries.push(entry);
        }
        if upsert.entries.is_empty() {
            return Ok(());
        }
        let _: CdcStateUpsertResponsePB = self.host_call("CdcStateUpsert", upsert).await?;
        Ok(())
    }
}

/// The request's table for an xCluster stream: a table id, and no NAMESPACEID id type.
pub(crate) fn xcluster_table(
    table_id: Option<&str>,
    id_type: &str,
) -> Result<String, MasterErrorPB> {
    let Some(table_id) = table_id.filter(|t| !t.is_empty()) else {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::InvalidArgument,
            "table_id must be specified",
        ));
    };
    if id_type == crate::NAMESPACE_ID {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::InvalidArgument,
            "NamespaceId option should not be set for xCluster streams",
        ));
    }
    Ok(table_id.to_string())
}
