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

//! YCQL user-defined types: `CreateUDType`, `DeleteUDType`, `GetUDTypeInfo`, `ListUDTypes`. A
//! type is unique by keyspace and name; the name lock is held across the check and the write.

use master_catalog_entity::Entity;
use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{SysTablesEntryPB, SysUDTypeEntryPB};
use yb_proto::pb::common::QLTypePB;
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_ddl::{
    CreateUDTypeRequestPB, CreateUDTypeResponsePB, DeleteUDTypeRequestPB, GetUDTypeInfoRequestPB,
    GetUDTypeInfoResponsePB, ListUDTypesRequestPB, ListUDTypesResponsePB, UDTypeInfoPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::lookup::find_namespace;
use crate::namespaces::{short_debug, unused_id};
use crate::{error, TableLifecycle};

use yb_proto::pb::catalog_entity_info::sys_tables_entry_pb::State as TableState;

fn corrupt(e: impl std::fmt::Display) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
}

/// `QLType::DoesUserDefinedTypeIdExist`: `transitive` also looks inside the type's own params.
fn uses_type(ty: &QLTypePB, transitive: bool, id: &str) -> bool {
    if let Some(info) = ty.udtype_info.as_ref() {
        if info.id() == id {
            return true;
        }
        if !transitive {
            return false;
        }
    }
    ty.params.iter().any(|p| uses_type(p, transitive, id))
}

/// `QLType::GetUserDefinedTypeIds` with `transitive`.
fn type_ids(ty: &QLTypePB, out: &mut Vec<String>) {
    if let Some(info) = ty.udtype_info.as_ref() {
        out.push(info.id().to_string());
    }
    for param in &ty.params {
        type_ids(param, out);
    }
}

/// The type named `name` in the keyspace.
fn by_name<'a>(
    snapshot: &'a CatalogSnapshot,
    namespace_id: &[u8],
    name: &str,
) -> Option<(&'a std::sync::Arc<Entity>, SysUDTypeEntryPB)> {
    snapshot.iter(SysRowEntryType::UDTYPE).find_map(|row| {
        let pb = row.parse::<SysUDTypeEntryPB>().ok()?;
        (pb.namespace_id().as_bytes() == namespace_id && pb.name() == name).then_some((row, pb))
    })
}

fn info_pb(id: &[u8], pb: &SysUDTypeEntryPB) -> UDTypeInfoPB {
    let mut info = UDTypeInfoPB::new();
    info.set_id(id.to_vec());
    info.set_name(pb.name().to_string());
    info.field_names = pb.field_names.clone();
    info.field_types = pb.field_types.clone();
    info
}

impl TableLifecycle {
    /// `CreateUDType`.
    pub async fn create_udtype(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateUDTypeRequestPB,
        resp: &mut CreateUDTypeResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("CreateUDType: {}", short_debug(req));
        let mut referenced = Vec::new();
        for ty in &req.field_types {
            type_ids(ty, &mut referenced);
        }
        let _names = self.names.lock().await;
        let created = self
            .write(token, deadline, |snapshot| {
                let namespace = find_namespace(snapshot, &req.namespace)?;
                let ns = namespace.namespace().expect("namespace row");
                if ns.database_type != YQLDatabase::YQL_DATABASE_CQL as i32 {
                    return Err(error(
                        Code::NAMESPACE_NOT_FOUND,
                        StatusCode::NotFound,
                        "Namespace not found",
                    ));
                }
                if let Some((row, _)) = by_name(snapshot, &namespace.key.id, req.name()) {
                    let e = error(
                        Code::TYPE_ALREADY_PRESENT,
                        StatusCode::AlreadyPresent,
                        format!("Type '{}.{}' already exists", ns.name, req.name()),
                    );
                    return Ok((Mutation::new(), Err((row.key.id.to_vec(), e))));
                }
                // A stale name cache can name a deleted type; InvalidArgument has the query layer
                // clear it and retry.
                for id in &referenced {
                    if snapshot
                        .get(SysRowEntryType::UDTYPE, id.as_bytes())
                        .is_none()
                    {
                        return Err(error(
                            Code::INVALID_REQUEST,
                            StatusCode::InvalidArgument,
                            format!(
                                "Type id '{id}' referenced by type '{}' does not exist",
                                req.name()
                            ),
                        ));
                    }
                }
                let id = unused_id(snapshot, SysRowEntryType::UDTYPE);
                let mut pb = SysUDTypeEntryPB::new();
                pb.set_name(req.name().to_string());
                pb.set_namespace_id(String::from_utf8_lossy(&namespace.key.id).into_owned());
                pb.field_names = req.field_names.clone();
                pb.field_types = req.field_types.clone();
                let mutation = Mutation::new()
                    .read(&namespace)
                    .read_absent(SysRowEntryType::UDTYPE, &id)
                    .upsert_message(SysRowEntryType::UDTYPE, &id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((mutation, Ok(id)))
            })
            .await?;
        match created {
            Ok(id) => {
                resp.set_id(id);
                Ok(())
            }
            Err((id, e)) => {
                resp.set_id(id);
                Err(e)
            }
        }
    }

    /// `DeleteUDType`: refused while a live table's column or another type's field uses it.
    pub async fn delete_udtype(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteUDTypeRequestPB,
    ) -> Result<(), MasterErrorPB> {
        if req.type_.is_none() {
            return Err(error(
                Code::NAMESPACE_NOT_FOUND,
                StatusCode::InvalidArgument,
                "No type given",
            ));
        }
        tracing::info!("DeleteUDType: {}", short_debug(req));
        let _names = self.names.lock().await;
        self.write(token, deadline, |snapshot| {
            let ident = &req.type_;
            let namespace = if ident.namespace.is_some() {
                Some(find_namespace(snapshot, &ident.namespace)?)
            } else {
                None
            };
            let found = if ident.has_type_id() {
                snapshot
                    .get(SysRowEntryType::UDTYPE, ident.type_id())
                    .map(|row| row.parse::<SysUDTypeEntryPB>().map(|pb| (row, pb)))
                    .transpose()
                    .map_err(corrupt)?
            } else if let (true, Some(ns)) = (ident.has_type_name(), &namespace) {
                by_name(snapshot, &ns.key.id, ident.type_name())
            } else {
                None
            };
            let (row, pb) = found.ok_or_else(|| {
                error(
                    Code::TYPE_NOT_FOUND,
                    StatusCode::NotFound,
                    "The type does not exist",
                )
            })?;
            let id = String::from_utf8_lossy(&row.key.id).into_owned();
            let ns_name = namespace
                .as_ref()
                .and_then(|n| n.namespace().map(|n| n.name.clone()))
                .unwrap_or_default();
            for table in snapshot.iter(SysRowEntryType::TABLE) {
                let Ok(table_pb) = table.parse::<SysTablesEntryPB>() else {
                    continue;
                };
                if matches!(table_pb.state(), TableState::DELETING | TableState::DELETED) {
                    continue;
                }
                for column in &table_pb.schema.columns {
                    if uses_type(&column.type_, true, &id) {
                        return Err(error(
                            Code::INVALID_REQUEST,
                            StatusCode::QlError,
                            format!(
                                "Cannot delete type '{ns_name}.{}'. It is used in column {} of \
                                 table {}",
                                pb.name(),
                                column.name(),
                                String::from_utf8_lossy(table_pb.name())
                            ),
                        ));
                    }
                }
            }
            for other in snapshot.iter(SysRowEntryType::UDTYPE) {
                let Ok(other_pb) = other.parse::<SysUDTypeEntryPB>() else {
                    continue;
                };
                for (i, field) in other_pb.field_types.iter().enumerate() {
                    if uses_type(field, false, &id) {
                        return Err(error(
                            Code::INVALID_REQUEST,
                            StatusCode::QlError,
                            format!(
                                "Cannot delete type '{ns_name}.{}'. It is used in field {} of \
                                 type '{}'",
                                pb.name(),
                                other_pb.field_names.get(i).map_or("", |s| s.as_str()),
                                other_pb.name()
                            ),
                        ));
                    }
                }
            }
            let mutation = Mutation::new()
                .read(row)
                .delete(SysRowEntryType::UDTYPE, &row.key.id);
            Ok((mutation, ()))
        })
        .await
    }

    /// `GetUDTypeInfo`.
    pub fn get_udtype_info(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetUDTypeInfoRequestPB,
        resp: &mut GetUDTypeInfoResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if req.type_.is_none() {
            return Err(error(
                Code::TYPE_NOT_FOUND,
                StatusCode::InvalidArgument,
                "Cannot get type, no type identifier given",
            ));
        }
        let ident = &req.type_;
        let found = if ident.has_type_id() {
            snapshot
                .get(SysRowEntryType::UDTYPE, ident.type_id())
                .map(|row| row.parse::<SysUDTypeEntryPB>().map(|pb| (row, pb)))
                .transpose()
                .map_err(corrupt)?
        } else if ident.has_type_name() && ident.namespace.is_some() {
            let ns = find_namespace(snapshot, &ident.namespace)?;
            by_name(snapshot, &ns.key.id, ident.type_name())
        } else {
            None
        };
        let (row, pb) = found.ok_or_else(|| {
            error(
                Code::TYPE_NOT_FOUND,
                StatusCode::InvalidArgument,
                "Couldn't find type",
            )
        })?;
        let mut info = info_pb(&row.key.id, &pb);
        info.namespace
            .mut_or_insert_default()
            .set_id(pb.namespace_id().as_bytes().to_vec());
        resp.udtype = Some(info).into();
        Ok(())
    }

    /// `ListUDTypes`: the keyspace's types.
    pub fn list_udtypes(
        &self,
        snapshot: &CatalogSnapshot,
        req: &ListUDTypesRequestPB,
        resp: &mut ListUDTypesResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let namespace = find_namespace(snapshot, &req.namespace)?;
        let ns_name = namespace
            .namespace()
            .map(|n| n.name.clone())
            .unwrap_or_default();
        for row in snapshot.iter(SysRowEntryType::UDTYPE) {
            let Ok(pb) = row.parse::<SysUDTypeEntryPB>() else {
                continue;
            };
            if pb.namespace_id().as_bytes() != &*namespace.key.id {
                continue;
            }
            let mut info = info_pb(&row.key.id, &pb);
            let ns = info.namespace.mut_or_insert_default();
            ns.set_id(namespace.key.id.to_vec());
            ns.set_name(ns_name.clone());
            resp.udtypes.push(info);
        }
        Ok(())
    }
}
