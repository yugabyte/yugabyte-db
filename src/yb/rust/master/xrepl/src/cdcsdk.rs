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

//! The CDCSDK stream maintenance RPCs.

use master_catalog_state::{Mutation, WriteOptions};
use master_leadership::LeaderToken;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysCDCStreamEntryPB;
use yb_proto::pb::catalog_entity_info::SysTablesEntryPB;
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_replication::{
    DisableDynamicTableAdditionOnCDCSDKStreamRequestPB, GetTableSchemaFromSysCatalogRequestPB,
    GetTableSchemaFromSysCatalogResponsePB, GetUDTypeMetadataRequestPB,
    GetUDTypeMetadataResponsePB,
};
use yb_proto::pb::master_rust_peer::ReadTableSchemaRequestPB;
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::streams::{parse_stream_id, StreamRow};
use crate::{error, Xrepl};

impl Xrepl {
    /// `GetTableSchemaFromSysCatalog`: the table's schema as of `read_time` (the latest without
    /// it), read from the sys catalog by the host, with its PG schema name then.
    pub async fn get_table_schema_from_sys_catalog(
        &self,
        req: &GetTableSchemaFromSysCatalogRequestPB,
        resp: &mut GetTableSchemaFromSysCatalogResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let table_id = req.table.table_id();
        let read_time = if req.has_read_time() {
            req.read_time() as u64
        } else {
            tracing::info!(
                "Reading latest schema version for: {} from system catalog table",
                String::from_utf8_lossy(table_id)
            );
            u64::MAX
        };
        let mut host_req = ReadTableSchemaRequestPB::new();
        host_req.set_table_id(table_id.to_vec());
        host_req.set_read_time(read_time);
        let oids = self
            .catalog
            .snapshot()
            .get(SysRowEntryType::TABLE, table_id)
            .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
            .and_then(|pb| master_table_lifecycle::pg_table_all_oids(table_id, &pb));
        if let Some(oids) = oids {
            host_req.set_database_oid(oids.database);
            host_req.set_relfilenode_oid(oids.relfilenode);
            host_req.set_pg_table_oid(oids.pg_table);
        }
        let mut out: GetTableSchemaFromSysCatalogResponsePB = self
            .host_call("ReadTableSchema", host_req)
            .await
            .map_err(|_| {
                error(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    format!(
                        "Could not find specific schema from system catalog for request {}.",
                        protobuf::text_format::print_to_string(req)
                    ),
                )
            })?;
        out.error = Default::default();
        *resp = out;
        Ok(())
    }

    /// `GetUDTypeMetadata`: the enum labels or composite type attributes of a YSQL database, read
    /// from its pg_catalog by the host.
    pub async fn get_ud_type_metadata(
        &self,
        req: &GetUDTypeMetadataRequestPB,
        resp: &mut GetUDTypeMetadataResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let namespace = master_table_lifecycle::lookup::find_namespace(
            &self.catalog.snapshot(),
            &req.namespace,
        )?;
        let database_type = namespace.namespace().map_or(0, |n| n.database_type);
        if database_type != YQLDatabase::YQL_DATABASE_PGSQL as i32 {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InternalError,
                format!("Expected YSQL database, got: {database_type}"),
            ));
        }
        let mut host_req = req.clone();
        host_req.namespace.mut_or_insert_default().clear_name();
        host_req
            .namespace
            .mut_or_insert_default()
            .set_id(namespace.key.id.to_vec());
        let mut out: GetUDTypeMetadataResponsePB = self
            .host_call("ReadUDTypeMetadata", host_req)
            .await
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        resp.enums = std::mem::take(&mut out.enums);
        resp.composites = std::mem::take(&mut out.composites);
        Ok(())
    }

    /// `ValidateStreamForTableRemoval`: a live CDCSDK stream.
    pub(crate) fn cdcsdk_stream(&self, id: &str) -> Result<StreamRow, MasterErrorPB> {
        let row = self
            .streams()
            .into_iter()
            .find(|r| r.id == id && !r.is_deleting())
            .ok_or_else(|| {
                master_ts_manager::master_error_tagged(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    "Could not find CDC stream",
                )
            })?;
        if row.pb.namespace_id().is_empty() {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::NotSupported,
                "Not a CDC stream",
            ));
        }
        Ok(row)
    }

    /// `DisableDynamicTableAdditionOnCDCSDKStream`.
    pub async fn disable_dynamic_table_addition(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DisableDynamicTableAdditionOnCDCSDKStreamRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!(
            "Servicing DisableDynamicTableAdditionOnCDCSDKStream request: {:?}",
            req
        );
        if !req.has_stream_id() {
            return Err(invalid_request("CDC Stream ID must be provided"));
        }
        if !(self.options.dynamic_tables_disable_option)() {
            return Err(invalid_request(
                "Disabling addition of dynamic tables to CDC stream is disallowed in the middle \
                 of an upgrade. Finalize the upgrade and try again",
            ));
        }
        let id = parse_stream_id(req.stream_id().as_bytes())?;
        let row = self.cdcsdk_stream(&id)?;
        if row.pb.cdcsdk_disable_dynamic_table_addition() {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::AlreadyPresent,
                "Dynamic table addition already disabled on the CDC stream",
            ));
        }
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let entity = snapshot
                        .get(SysRowEntryType::CDC_STREAM, id.as_bytes())
                        .ok_or_else(|| {
                            Status::new(StatusCode::NotFound, "Could not find CDC stream")
                        })?;
                    let mut pb: SysCDCStreamEntryPB = entity
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    pb.set_cdcsdk_disable_dynamic_table_addition(true);
                    Ok((
                        Mutation::new().read(entity).upsert_message(
                            SysRowEntryType::CDC_STREAM,
                            id.as_bytes(),
                            &pb,
                        )?,
                        (),
                    ))
                },
            )
            .await
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        tracing::info!("Successfully disabled dynamic table addition on CDC stream: {id}");
        Ok(())
    }
}

/// `RETURN_INVALID_REQUEST_STATUS`.
pub(crate) fn invalid_request(message: &str) -> MasterErrorPB {
    error(Code::INVALID_REQUEST, StatusCode::InvalidArgument, message)
}
