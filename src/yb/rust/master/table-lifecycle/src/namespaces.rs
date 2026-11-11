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

//! YCQL keyspaces: `CreateNamespace`, `DeleteNamespace` and their `Is*Done`, `ListNamespaces`,
//! `GetNamespaceInfo`. YSQL databases are refused until YSQL DDL is served.

use std::sync::Arc;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use protobuf::{Enum, EnumOrUnknown};
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysClusterConfigEntryPB, SysNamespaceEntryPB,
    SysUDTypeEntryPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_client::{ReservePgsqlOidsRequestPB, ReservePgsqlOidsResponsePB};
use yb_proto::pb::master_ddl::{
    AlterNamespaceRequestPB, CreateNamespaceRequestPB, CreateNamespaceResponsePB,
    DeleteNamespaceRequestPB, GetNamespaceInfoRequestPB, GetNamespaceInfoResponsePB,
    IsCreateNamespaceDoneRequestPB, IsCreateNamespaceDoneResponsePB,
    IsDeleteNamespaceDoneRequestPB, IsDeleteNamespaceDoneResponsePB, ListNamespacesRequestPB,
    ListNamespacesResponsePB,
};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, NamespaceIdentifierPB, SysRowEntryType,
};

use crate::lookup::{find_namespace, find_namespace_by_name, find_table};
use crate::schedules::{covering_schedules, schedule_list};
use crate::{error, generate_object_id, TableLifecycle};

use sys_namespace_entry_pb::State;

fn namespace_state(entity: &master_catalog_entity::Entity) -> i32 {
    entity.namespace().map_or(0, |n| n.state)
}

fn state_name(state: i32) -> String {
    State::from_i32(state).map_or_else(|| state.to_string(), |s| format!("{s:?}"))
}

/// A YSQL or YEDIS request the Rust leader does not serve.
fn unsupported_database(db_type: YQLDatabase) -> MasterErrorPB {
    error(
        Code::INVALID_REQUEST,
        StatusCode::NotSupported,
        match db_type {
            YQLDatabase::YQL_DATABASE_PGSQL => {
                "YSQL databases are not served by a Rust master leader yet (YSQL DDL, W7)"
            }
            _ => "YEDIS is not supported by the Rust master (D16)",
        },
    )
}

impl TableLifecycle {
    /// `CreateNamespace` for a YCQL keyspace. C++ writes the row PREPARING and then RUNNING; for a
    /// keyspace nothing happens in between, so one write of the RUNNING row has the same effect.
    pub async fn create_namespace(
        self: &Arc<Self>,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateNamespaceRequestPB,
        resp: &mut CreateNamespaceResponsePB,
    ) -> Result<(), MasterErrorPB> {
        // `GetDatabaseType`: a keyspace named system_redis with no type is YEDIS.
        let db_type = if req.has_database_type() {
            req.database_type()
        } else if req.name() == "system_redis" {
            YQLDatabase::YQL_DATABASE_REDIS
        } else {
            YQLDatabase::YQL_DATABASE_CQL
        };
        if db_type == YQLDatabase::YQL_DATABASE_PGSQL {
            return self.create_ysql_namespace(token, deadline, req, resp).await;
        }
        if db_type != YQLDatabase::YQL_DATABASE_CQL {
            return Err(unsupported_database(db_type));
        }
        let _names = self.names.lock().await;
        // A duplicate is not a write error: the response also carries the existing id.
        let created = self
            .write(token, deadline, |snapshot| {
                let existing = |id: &[u8], message: String| {
                    let e = error(
                        Code::NAMESPACE_ALREADY_PRESENT,
                        StatusCode::AlreadyPresent,
                        message,
                    );
                    Ok((Mutation::new(), Err((id.to_vec(), e))))
                };
                if !req.namespace_id().is_empty() {
                    if let Some(found) =
                        snapshot.get(SysRowEntryType::NAMESPACE, req.namespace_id())
                    {
                        return existing(
                            &found.key.id,
                            format!(
                                "Keyspace with id '{}' already exists",
                                String::from_utf8_lossy(req.namespace_id())
                            ),
                        );
                    }
                }
                if let Some(found) = find_namespace_by_name(snapshot, req.name(), db_type as i32) {
                    return existing(
                        &found.key.id,
                        format!("Keyspace '{}' already exists", req.name()),
                    );
                }
                let id = if req.namespace_id().is_empty() {
                    unused_id(snapshot, SysRowEntryType::NAMESPACE)
                } else {
                    req.namespace_id().to_vec()
                };
                let mut namespace = SysNamespaceEntryPB::new();
                namespace.set_name(req.name().as_bytes().to_vec());
                namespace.database_type = Some(EnumOrUnknown::new(db_type));
                namespace.set_colocated(req.colocated());
                namespace.state = Some(EnumOrUnknown::new(State::RUNNING));
                let mutation = Mutation::new()
                    .read_absent(SysRowEntryType::NAMESPACE, &id)
                    .upsert_message(SysRowEntryType::NAMESPACE, &id, &namespace)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((mutation, Ok(id)))
            })
            .await?;
        let id = match created {
            Ok(id) => id,
            Err((id, e)) => {
                resp.set_id(id);
                return Err(e);
            }
        };
        resp.set_id(id);
        Ok(())
    }

    /// `IsCreateNamespaceDone`.
    pub fn is_create_namespace_done(
        &self,
        snapshot: &CatalogSnapshot,
        req: &IsCreateNamespaceDoneRequestPB,
        resp: &mut IsCreateNamespaceDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let namespace = find_namespace(snapshot, &req.namespace)?;
        let state = namespace_state(&namespace);
        match State::from_i32(state) {
            Some(State::RUNNING) | Some(State::DELETING) | Some(State::DELETED) => {
                resp.set_done(true)
            }
            Some(State::PREPARING) => resp.set_done(false),
            Some(State::FAILED) => {
                resp.set_done(true);
                return Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::InternalError,
                    "Namespace Create Failed: not onlined.",
                ));
            }
            _ => {
                resp.set_done(true);
                return Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::IllegalState,
                    format!("IsCreateNamespaceDone failure: state={}", state_name(state)),
                ));
            }
        }
        Ok(())
    }

    /// `DeleteNamespace` for a YCQL keyspace: one write that removes the row, as C++ does.
    pub async fn delete_namespace(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteNamespaceRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let _names = self.names.lock().await;
        self.write(token, deadline, |snapshot| {
            let namespace = find_namespace(snapshot, &req.namespace)?;
            let ns = namespace.namespace().expect("namespace row");
            if req.has_database_type() && req.database_type() as i32 != ns.database_type {
                return Err(error(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Keyspace not found: {}", ns.name),
                ));
            }
            match State::from_i32(ns.state) {
                Some(State::RUNNING) | Some(State::FAILED) => {}
                Some(State::DELETED) => {
                    return Err(error(
                        Code::NAMESPACE_NOT_FOUND,
                        StatusCode::NotFound,
                        format!("Keyspace already deleted: {}", ns.name),
                    ))
                }
                _ => {
                    return Err(error(
                        Code::IN_TRANSITION_CAN_RETRY,
                        StatusCode::TryAgain,
                        format!(
                            "Namespace deletion not allowed when State = {}",
                            state_name(ns.state)
                        ),
                    ))
                }
            }
            if let Some(db_type) = YQLDatabase::from_i32(ns.database_type)
                .filter(|t| *t != YQLDatabase::YQL_DATABASE_CQL)
            {
                return Err(unsupported_database(db_type));
            }
            let mut mutation = Mutation::new().read(&namespace);
            for table in snapshot.iter(SysRowEntryType::TABLE) {
                let Some(hot) = table.table() else { continue };
                if hot.namespace_id != *namespace.key.id
                    || hot.state == sys_tables_entry_pb::State::DELETING as i32
                    || hot.state == sys_tables_entry_pb::State::DELETED as i32
                {
                    continue;
                }
                let indexed = table
                    .parse::<yb_proto::pb::catalog_entity_info::SysTablesEntryPB>()
                    .is_ok_and(|pb| !pb.indexed_table_id().is_empty() || pb.index_info.is_some());
                return Err(error(
                    Code::NAMESPACE_IS_NOT_EMPTY,
                    StatusCode::InvalidArgument,
                    format!(
                        "Cannot delete keyspace which has {}: {} [id={}], request: {}",
                        if indexed { "index" } else { "table" },
                        hot.name,
                        String::from_utf8_lossy(&table.key.id),
                        short_debug(req)
                    ),
                ));
            }
            for udt in snapshot.iter(SysRowEntryType::UDTYPE) {
                let Ok(pb) = udt.parse::<SysUDTypeEntryPB>() else {
                    continue;
                };
                if pb.namespace_id().as_bytes() == &*namespace.key.id {
                    return Err(error(
                        Code::NAMESPACE_IS_NOT_EMPTY,
                        StatusCode::InvalidArgument,
                        format!(
                            "Cannot delete keyspace which has type: {} [id={}], request: {}",
                            pb.name(),
                            String::from_utf8_lossy(&udt.key.id),
                            short_debug(req)
                        ),
                    ));
                }
            }
            let schedules =
                covering_schedules(snapshot, SysRowEntryType::NAMESPACE, &namespace.key.id);
            if !schedules.is_empty() {
                return Err(error(
                    Code::NAMESPACE_IS_NOT_EMPTY,
                    StatusCode::InvalidArgument,
                    format!(
                        "Cannot delete keyspace which has schedule: {}, request: {}",
                        schedule_list(&schedules),
                        short_debug(req)
                    ),
                ));
            }
            mutation = mutation.delete(SysRowEntryType::NAMESPACE, &namespace.key.id);
            Ok((mutation, ()))
        })
        .await
    }

    /// `IsDeleteNamespaceDone`. A YCQL keyspace's row is gone once it is deleted.
    pub fn is_delete_namespace_done(
        &self,
        snapshot: &CatalogSnapshot,
        req: &IsDeleteNamespaceDoneRequestPB,
        resp: &mut IsDeleteNamespaceDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let Ok(namespace) = find_namespace(snapshot, &req.namespace) else {
            resp.set_done(true);
            return Ok(());
        };
        let state = namespace_state(&namespace);
        match State::from_i32(state) {
            Some(State::DELETED) => resp.set_done(true),
            Some(State::DELETING) => resp.set_done(false),
            _ => {
                resp.set_done(true);
                return Err(error(
                    Code::INTERNAL_ERROR,
                    StatusCode::IllegalState,
                    format!(
                        "Servicing IsDeleteNamespaceDone request for {}: NOT deleted (state={})",
                        namespace.namespace().map_or("", |n| n.name.as_str()),
                        state_name(state)
                    ),
                ));
            }
        }
        Ok(())
    }

    /// `ListNamespaces`. C++ lists in hash-map order; this lists by id.
    pub fn list_namespaces(
        &self,
        snapshot: &CatalogSnapshot,
        req: &ListNamespacesRequestPB,
        resp: &mut ListNamespacesResponsePB,
    ) {
        let mut namespaces: Vec<_> = snapshot
            .iter(SysRowEntryType::NAMESPACE)
            .filter(|e| e.namespace().is_some())
            .collect();
        namespaces.sort_by(|a, b| a.key.id.cmp(&b.key.id));
        for entity in namespaces {
            let ns = entity.namespace().expect("namespace row");
            if req.has_database_type() && ns.database_type != req.database_type() as i32 {
                continue;
            }
            if ns.state != State::RUNNING as i32 && !req.include_nonrunning() {
                continue;
            }
            resp.namespaces
                .push(identifier(&entity.key.id, &ns.name, ns.database_type));
            resp.states.push(ns.state);
            let colocated = entity
                .parse::<SysNamespaceEntryPB>()
                .is_ok_and(|pb| pb.colocated());
            resp.colocated.push(colocated as i32);
        }
    }

    /// `GetNamespaceInfo`: by namespace, or by a table in it. The state is not checked.
    pub fn get_namespace_info(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetNamespaceInfoRequestPB,
        resp: &mut GetNamespaceInfoResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let namespace = if req.namespace.is_some() {
            find_namespace(snapshot, &req.namespace)?
        } else if req.has_table_id() {
            let mut ident = yb_proto::pb::master_types::TableIdentifierPB::new();
            ident.set_table_id(req.table_id().to_vec());
            let table = find_table(snapshot, &ident, false)?;
            let mut ns_ident = NamespaceIdentifierPB::new();
            ns_ident.set_id(table.table().expect("table row").namespace_id.clone());
            find_namespace(snapshot, &ns_ident)?
        } else {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "Need either namespace identifier or table id set in GetNamespaceInfoRequest",
            ));
        };
        let pb = namespace
            .parse::<SysNamespaceEntryPB>()
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        let ns = namespace.namespace().expect("namespace row");
        resp.namespace = Some(identifier(&namespace.key.id, &ns.name, ns.database_type)).into();
        resp.set_colocated(pb.colocated());
        if pb.colocated() {
            // `IsColocatedNamespace`: a database colocated before tablegroups has a parent table.
            let mut parent = namespace.key.id.to_vec();
            parent.extend_from_slice(b".colocated.parent.uuid");
            let legacy = snapshot
                .get(SysRowEntryType::TABLE, &parent)
                .and_then(|t| t.table())
                .is_some_and(|t| t.state != sys_tables_entry_pb::State::DELETED as i32);
            resp.set_legacy_colocated_database(legacy);
        }
        resp.state = pb.state;
        resp.ysql_next_major_version_state = pb.ysql_next_major_version_state;
        Ok(())
    }
}

impl TableLifecycle {
    /// `ReservePgsqlOids`: the next `count` OIDs of a YSQL database's normal or secondary space,
    /// from the namespace row, which records where the next reservation starts.
    pub async fn reserve_pgsql_oids(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &ReservePgsqlOidsRequestPB,
        resp: &mut ReservePgsqlOidsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let (begin, end, invalidations) = self
            .write(token, deadline, |snapshot| {
                let config: SysClusterConfigEntryPB = snapshot
                    .cluster_config()
                    .ok_or_else(|| {
                        error(
                            Code::UNKNOWN_ERROR,
                            StatusCode::IllegalState,
                            "No cluster config",
                        )
                    })?
                    .parse()
                    .map_err(|e| {
                        error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                    })?;
                let namespace = snapshot
                    .get(SysRowEntryType::NAMESPACE, req.namespace_id())
                    .ok_or_else(|| {
                        error(
                            Code::NAMESPACE_NOT_FOUND,
                            StatusCode::NotFound,
                            format!(
                                "Namespace not found: {}",
                                String::from_utf8_lossy(req.namespace_id())
                            ),
                        )
                    })?;
                let mut pb: SysNamespaceEntryPB = namespace.parse().map_err(|e| {
                    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                })?;
                let (next, upper) = if req.use_secondary_space() {
                    (
                        pb.next_secondary_pg_oid().max(PG_FIRST_SECONDARY_SPACE_OID),
                        PG_UPPER_BOUND_SECONDARY_SPACE_OID,
                    )
                } else {
                    (pb.next_normal_pg_oid(), PG_UPPER_BOUND_NORMAL_OID)
                };
                let begin = next.max(req.next_oid());
                if begin >= upper {
                    tracing::warn!(
                        "No more {} object identifiers are available for Postgres database {} \
                         ({})",
                        if req.use_secondary_space() {
                            "secondary"
                        } else {
                            "normal"
                        },
                        String::from_utf8_lossy(pb.name()),
                        String::from_utf8_lossy(req.namespace_id())
                    );
                    return Err(error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::InvalidArgument,
                        "No more object identifiers are available",
                    ));
                }
                let end = begin.saturating_add(req.count()).min(upper);
                if req.use_secondary_space() {
                    pb.set_next_secondary_pg_oid(end);
                } else {
                    pb.set_next_normal_pg_oid(end);
                }
                let mutation = Mutation::new()
                    .read(namespace)
                    .upsert_message(SysRowEntryType::NAMESPACE, &namespace.key.id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                Ok((
                    mutation,
                    (begin, end, config.oid_cache_invalidations_count()),
                ))
            })
            .await?;
        resp.set_begin_oid(begin);
        resp.set_end_oid(end);
        resp.set_oid_cache_invalidations_count(invalidations);
        Ok(())
    }
}

/// `kPgUpperBoundNormalObjectId` and the secondary space after it (`entity_ids.h`); both upper
/// bounds are exclusive.
const PG_UPPER_BOUND_NORMAL_OID: u32 = 2_199_999_999;
const PG_FIRST_SECONDARY_SPACE_OID: u32 = PG_UPPER_BOUND_NORMAL_OID + 1;
const PG_UPPER_BOUND_SECONDARY_SPACE_OID: u32 = 0xffff_ffff;

impl TableLifecycle {
    /// `AlterNamespace`: renames a running namespace.
    pub async fn alter_namespace(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &AlterNamespaceRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let _names = self.names.lock().await;
        self.write(token, deadline, |snapshot| {
            let namespace = find_namespace(snapshot, &req.namespace)?;
            let mut pb: SysNamespaceEntryPB = namespace
                .parse()
                .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
            if req.namespace.has_database_type()
                && pb.database_type() != req.namespace.database_type()
            {
                return Err(error(
                    Code::NAMESPACE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Database not found: {}", String::from_utf8_lossy(pb.name())),
                ));
            }
            if pb.state() != State::RUNNING {
                return Err(error(
                    match pb.state() {
                        State::PREPARING | State::DELETING => Code::IN_TRANSITION_CAN_RETRY,
                        _ => Code::INTERNAL_ERROR,
                    },
                    StatusCode::TryAgain,
                    format!("Namespace not running.  State = {:?}", pb.state()),
                ));
            }
            if !req.has_new_name() || req.new_name().as_bytes() == pb.name() {
                return Ok((Mutation::new(), ()));
            }
            if req.namespace.has_database_type() {
                if let Some(other) = find_namespace_by_name(
                    snapshot,
                    req.new_name(),
                    req.namespace.database_type() as i32,
                ) {
                    return Err(error(
                        Code::OBJECT_ALREADY_PRESENT,
                        StatusCode::AlreadyPresent,
                        format!(
                            "Keyspace '{}' already exists",
                            other.namespace().map_or("", |n| n.name.as_str())
                        ),
                    ));
                }
            }
            pb.set_name(req.new_name().as_bytes().to_vec());
            let mutation = Mutation::new()
                .read(&namespace)
                .upsert_message(SysRowEntryType::NAMESPACE, &namespace.key.id, &pb)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            Ok((mutation, ()))
        })
        .await
    }
}

/// A `NamespaceIdentifierPB` with id, name and type.
pub(crate) fn identifier(id: &[u8], name: &str, database_type: i32) -> NamespaceIdentifierPB {
    let mut ident = NamespaceIdentifierPB::new();
    ident.set_id(id.to_vec());
    ident.set_name(name.to_string());
    ident.database_type = Some(EnumOrUnknown::from_i32(database_type));
    ident
}

/// An id of `entry_type` no row has, as `GenerateIdUnlocked` retries until it finds one.
pub(crate) fn unused_id(snapshot: &CatalogSnapshot, entry_type: SysRowEntryType) -> Vec<u8> {
    loop {
        let id = generate_object_id();
        if snapshot.get(entry_type, &id).is_none() {
            return id;
        }
    }
}

/// `ShortDebugString` of a request, for messages that quote it.
pub(crate) fn short_debug(message: &dyn protobuf::MessageDyn) -> String {
    protobuf::text_format::print_to_string(message)
}
