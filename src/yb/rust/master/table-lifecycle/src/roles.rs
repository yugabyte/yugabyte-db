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

//! YCQL roles and permissions: C++'s `PermissionsManager` over the role rows. Each request is one
//! catalog write with the security config's `roles_version` bump, so a role change and its
//! version change commit together. The recursive role graph and the permissions cache are built
//! from the snapshot on each `GetPermissions` instead of being kept in memory.

use std::collections::{BTreeMap, BTreeSet};

use master_catalog_state::defaults::SECURITY_CONFIG_ID;
use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use protobuf::{Enum, EnumOrUnknown};
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{ResourcePermissionsPB, SysConfigEntryPB, SysRoleEntryPB};
use yb_proto::pb::common_types::{PermissionType, ResourceType};
use yb_proto::pb::master_dcl::{
    AlterRoleRequestPB, CreateRoleRequestPB, DeleteRoleRequestPB, GetPermissionsRequestPB,
    GetPermissionsResponsePB, GrantRevokePermissionRequestPB, GrantRevokeRoleRequestPB,
    ResourcePermissionInfoPB, RolePermissionInfoPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::lookup::find_namespace;
use crate::{error, TableLifecycle};

/// `kRolesDataResource`: the canonical resource of ALL KEYSPACES, and the prefix of keyspaces
/// and tables.
const DATA_RESOURCE: &str = "data";
/// `kRolesRoleResource`: the canonical resource of ALL ROLES, and the prefix of roles.
const ROLES_RESOURCE: &str = "roles";
/// Every permission bit (`Permissions` is a `std::bitset<kMaxPermissions = 7>`).
const ALL_PERMISSION_BITS: u32 = (1 << 7) - 1;

/// `get_canonical_keyspace`.
pub(crate) fn canonical_keyspace(keyspace: &str) -> String {
    format!("{DATA_RESOURCE}/{keyspace}")
}

/// `get_canonical_table`.
pub(crate) fn canonical_table(keyspace: &str, table: &str) -> String {
    format!("{DATA_RESOURCE}/{keyspace}/{table}")
}

/// `get_canonical_role`.
fn canonical_role(role: &str) -> String {
    format!("{ROLES_RESOURCE}/{role}")
}

/// `all_permissions_for_resource`.
pub(crate) fn all_permissions_for_resource(resource: ResourceType) -> &'static [PermissionType] {
    use PermissionType::*;
    match resource {
        ResourceType::KEYSPACE | ResourceType::ALL_KEYSPACES => &[
            ALTER_PERMISSION,
            AUTHORIZE_PERMISSION,
            CREATE_PERMISSION,
            DROP_PERMISSION,
            MODIFY_PERMISSION,
            SELECT_PERMISSION,
        ],
        ResourceType::TABLE => &[
            ALTER_PERMISSION,
            AUTHORIZE_PERMISSION,
            DROP_PERMISSION,
            MODIFY_PERMISSION,
            SELECT_PERMISSION,
        ],
        ResourceType::ROLE => &[ALTER_PERMISSION, AUTHORIZE_PERMISSION, DROP_PERMISSION],
        ResourceType::ALL_ROLES => &[
            ALTER_PERMISSION,
            AUTHORIZE_PERMISSION,
            CREATE_PERMISSION,
            DESCRIBE_PERMISSION,
            DROP_PERMISSION,
        ],
    }
}

fn corrupt(e: impl std::fmt::Display) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
}

fn roles(snapshot: &CatalogSnapshot) -> BTreeMap<String, SysRoleEntryPB> {
    snapshot
        .iter(SysRowEntryType::ROLE)
        .filter_map(|row| {
            let pb = row.parse::<SysRoleEntryPB>().ok()?;
            Some((String::from_utf8_lossy(&row.key.id).into_owned(), pb))
        })
        .collect()
}

fn role(snapshot: &CatalogSnapshot, name: &str) -> Result<Option<SysRoleEntryPB>, MasterErrorPB> {
    snapshot
        .get(SysRowEntryType::ROLE, name.as_bytes())
        .map(|row| row.parse::<SysRoleEntryPB>().map_err(corrupt))
        .transpose()
}

/// The role's row written in `mutation`.
fn upsert_role(
    snapshot: &CatalogSnapshot,
    mutation: Mutation,
    name: &str,
    pb: &SysRoleEntryPB,
) -> Result<Mutation, MasterErrorPB> {
    let mutation = match snapshot.get(SysRowEntryType::ROLE, name.as_bytes()) {
        Some(row) => mutation.read(row),
        None => mutation.read_absent(SysRowEntryType::ROLE, name.as_bytes()),
    };
    mutation
        .upsert_message(SysRowEntryType::ROLE, name.as_bytes(), pb)
        .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))
}

/// `IncrementRolesVersionUnlocked`.
fn bump_roles_version(
    snapshot: &CatalogSnapshot,
    mutation: Mutation,
) -> Result<Mutation, MasterErrorPB> {
    bump_roles_version_by(snapshot, mutation, 1)
}

/// The roles version moved `by` increments in one write.
fn bump_roles_version_by(
    snapshot: &CatalogSnapshot,
    mutation: Mutation,
    by: u64,
) -> Result<Mutation, MasterErrorPB> {
    let row = snapshot
        .get(SysRowEntryType::SYS_CONFIG, SECURITY_CONFIG_ID)
        .ok_or_else(|| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                "No security config",
            )
        })?;
    let mut pb: SysConfigEntryPB = row.parse().map_err(corrupt)?;
    let security = pb.mut_security_config();
    let version = security.roles_version();
    if version > u64::MAX - by {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::IllegalState,
            format!("Roles version reached max allowable integer: {version}"),
        ));
    }
    security.set_roles_version(version + by);
    mutation
        .read(row)
        .upsert_message(SysRowEntryType::SYS_CONFIG, SECURITY_CONFIG_ID, &pb)
        .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))
}

/// For each role, the roles granted to it directly or through inheritance
/// (`BuildRecursiveRolesUnlocked`).
fn recursive_granted_roles(
    roles: &BTreeMap<String, SysRoleEntryPB>,
) -> BTreeMap<String, BTreeSet<String>> {
    fn traverse(
        name: &str,
        roles: &BTreeMap<String, SysRoleEntryPB>,
        out: &mut BTreeMap<String, BTreeSet<String>>,
    ) -> BTreeSet<String> {
        if let Some(granted) = out.get(name) {
            return granted.clone();
        }
        // Marks the role visited, as C++'s map entry does; the graph has no cycles.
        out.insert(name.to_string(), BTreeSet::new());
        let mut granted = BTreeSet::new();
        if let Some(pb) = roles.get(name) {
            for member_of in &pb.member_of {
                let member_of = String::from_utf8_lossy(member_of).into_owned();
                granted.extend(traverse(&member_of, roles, out));
                granted.insert(member_of);
            }
        }
        out.insert(name.to_string(), granted.clone());
        granted
    }
    let mut out = BTreeMap::new();
    for name in roles.keys() {
        traverse(name, roles, &mut out);
    }
    out
}

/// `IsMemberOf`: `granted` was granted to `role`, directly or through inheritance.
fn is_member_of(roles: &BTreeMap<String, SysRoleEntryPB>, granted: &str, role: &str) -> bool {
    recursive_granted_roles(roles)
        .get(role)
        .is_some_and(|set| set.contains(granted))
}

/// A resource permissions are granted on.
pub(crate) struct Resource<'a> {
    pub(crate) canonical: &'a str,
    pub(crate) name: &'a str,
    pub(crate) keyspace: &'a str,
    pub(crate) resource_type: ResourceType,
}

/// `GrantPermissions`: a new resource entry on the role with the permissions the resource takes.
pub(crate) fn grant_permissions(
    snapshot: &CatalogSnapshot,
    mutation: Mutation,
    role_name: &str,
    resource: &Resource<'_>,
) -> Result<Mutation, MasterErrorPB> {
    let Resource {
        canonical: canonical_resource,
        name: resource_name,
        keyspace,
        resource_type,
    } = *resource;
    let mut pb = role(snapshot, role_name)?.ok_or_else(|| {
        error(
            Code::ROLE_NOT_FOUND,
            StatusCode::NotFound,
            format!("Role {role_name} was not found"),
        )
    })?;
    let mutation = bump_roles_version(snapshot, mutation)?;
    let mut resource = ResourcePermissionsPB::new();
    resource.set_canonical_resource(canonical_resource.to_string());
    resource.set_resource_type(resource_type);
    resource.set_resource_name(resource_name.to_string());
    resource.set_namespace_name(keyspace.to_string());
    for permission in all_permissions_for_resource(resource_type) {
        // DESCRIBE is for roles only.
        if *permission == PermissionType::DESCRIBE_PERMISSION
            && resource_type != ResourceType::ROLE
            && resource_type != ResourceType::ALL_ROLES
        {
            continue;
        }
        resource.permissions.push(EnumOrUnknown::new(*permission));
    }
    pb.resources.push(resource);
    upsert_role(snapshot, mutation, role_name, &pb)
}

/// `RemoveAllPermissionsForResourceUnlocked`: each role loses its first entry on the resource;
/// the roles version moves only if one did.
pub(crate) fn remove_all_permissions_for_resource(
    snapshot: &CatalogSnapshot,
    mut mutation: Mutation,
    canonical_resource: &str,
) -> Result<Mutation, MasterErrorPB> {
    let mut modified = false;
    for (name, mut pb) in roles(snapshot) {
        if let Some(i) = pb
            .resources
            .iter()
            .position(|r| r.canonical_resource() == canonical_resource)
        {
            pb.resources.remove(i);
            mutation = upsert_role(snapshot, mutation, &name, &pb)?;
            modified = true;
        }
    }
    if modified {
        mutation = bump_roles_version(snapshot, mutation)?;
    }
    Ok(mutation)
}

impl TableLifecycle {
    /// `GrantPermissions` for the creator of a keyspace or table (`creator_role_name`), after the
    /// object is created, as C++ grants them.
    pub(crate) async fn grant_creator_permissions(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        role_name: &str,
        resource: Resource<'_>,
    ) -> Result<(), MasterErrorPB> {
        self.write(token, deadline, |snapshot| {
            let mutation = grant_permissions(snapshot, Mutation::new(), role_name, &resource)?;
            Ok((mutation, ()))
        })
        .await
    }

    /// `CreateRole`; the creator gets every permission on the new role.
    pub async fn create_role(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateRoleRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("CreateRole: {}", req.name());
        self.write(token, deadline, |snapshot| {
            // Only a superuser can create a superuser, checked before the new role's existence,
            // as Apache Cassandra does.
            if req.superuser() {
                let creator = role(snapshot, req.creator_role_name())?.ok_or_else(|| {
                    error(
                        Code::ROLE_NOT_FOUND,
                        StatusCode::NotFound,
                        format!("role {} does not exist", req.creator_role_name()),
                    )
                })?;
                if !creator.is_superuser() {
                    return Err(error(
                        Code::NOT_AUTHORIZED,
                        StatusCode::NotAuthorized,
                        "Only superusers can create a role with superuser status",
                    ));
                }
            }
            if role(snapshot, req.name())?.is_some() {
                return Err(error(
                    Code::ROLE_ALREADY_PRESENT,
                    StatusCode::AlreadyPresent,
                    format!("Role {} already exists", req.name()),
                ));
            }
            let mut pb = SysRoleEntryPB::new();
            pb.set_role(req.name().as_bytes().to_vec());
            pb.set_can_login(req.login());
            pb.set_is_superuser(req.superuser());
            if !req.salted_hash().is_empty() {
                pb.set_salted_hash(req.salted_hash().as_bytes().to_vec());
            }
            let mut mutation = bump_roles_version(snapshot, Mutation::new())?;
            mutation = upsert_role(snapshot, mutation, req.name(), &pb)?;
            Ok((mutation, ()))
        })
        .await?;
        if req.has_creator_role_name() {
            let canonical = canonical_role(req.name());
            let resource = Resource {
                canonical: &canonical,
                name: req.name(),
                keyspace: "",
                resource_type: ResourceType::ROLE,
            };
            self.grant_creator_permissions(token, deadline, req.creator_role_name(), resource)
                .await?;
        }
        Ok(())
    }

    /// `AlterRole`.
    pub async fn alter_role(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &AlterRoleRequestPB,
    ) -> Result<(), MasterErrorPB> {
        self.write(token, deadline, |snapshot| {
            let mut pb = role(snapshot, req.name())?.ok_or_else(|| {
                error(
                    Code::ROLE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Role {} does not exist", req.name()),
                )
            })?;
            if req.has_superuser() {
                let current = role(snapshot, req.current_role())?.ok_or_else(|| {
                    error(
                        Code::ROLE_NOT_FOUND,
                        StatusCode::NotFound,
                        format!("Internal error: role {} does not exist", req.current_role()),
                    )
                })?;
                // #2505: a role cannot change its own superuser status, nor that of a role
                // granted to it.
                if req.name() == req.current_role()
                    || is_member_of(&roles(snapshot), req.name(), req.current_role())
                {
                    return Err(error(
                        Code::NOT_AUTHORIZED,
                        StatusCode::NotAuthorized,
                        "You aren't allowed to alter your own superuser status or that of a role \
                         granted to you",
                    ));
                }
                if !current.is_superuser() {
                    return Err(error(
                        Code::NOT_AUTHORIZED,
                        StatusCode::NotAuthorized,
                        "Only superusers are allowed to alter superuser status",
                    ));
                }
            }
            if req.has_login() {
                pb.set_can_login(req.login());
            }
            if req.has_superuser() {
                pb.set_is_superuser(req.superuser());
            }
            if req.has_salted_hash() {
                pb.set_salted_hash(req.salted_hash().as_bytes().to_vec());
            }
            let mut mutation = bump_roles_version(snapshot, Mutation::new())?;
            mutation = upsert_role(snapshot, mutation, req.name(), &pb)?;
            Ok((mutation, ()))
        })
        .await
    }

    /// `DeleteRole`: the role leaves every role it was granted to, and loses the permissions
    /// granted on it. C++ writes these one by one and checks the superuser rule after editing the
    /// other roles; here the check comes first and everything is one write.
    pub async fn delete_role(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteRoleRequestPB,
    ) -> Result<(), MasterErrorPB> {
        if !req.has_name() {
            return Err(error(
                Code::ROLE_NOT_FOUND,
                StatusCode::InvalidArgument,
                "No role name given",
            ));
        }
        tracing::info!("DeleteRole: {}", req.name());
        self.write(token, deadline, |snapshot| {
            let pb = role(snapshot, req.name())?.ok_or_else(|| {
                error(
                    Code::ROLE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Role {} does not exist", req.name()),
                )
            })?;
            if pb.is_superuser() {
                let current = role(snapshot, req.current_role())?.ok_or_else(|| {
                    error(
                        Code::ROLE_NOT_FOUND,
                        StatusCode::NotFound,
                        format!("Internal error: role {} does not exist", req.current_role()),
                    )
                })?;
                if !current.is_superuser() {
                    return Err(error(
                        Code::NOT_AUTHORIZED,
                        StatusCode::NotAuthorized,
                        "Only superusers can drop a role with superuser status",
                    ));
                }
            }
            // The role leaves the roles it was granted to, and the permissions on it go.
            let name = req.name().as_bytes();
            let canonical = canonical_role(req.name());
            let mut removed_permission = false;
            let mut changed = Vec::new();
            for (other, mut other_pb) in roles(snapshot) {
                if other.as_bytes() == name {
                    continue;
                }
                let before = other_pb.clone();
                other_pb.member_of.retain(|m| m != name);
                if let Some(i) = other_pb
                    .resources
                    .iter()
                    .position(|r| r.canonical_resource() == canonical)
                {
                    other_pb.resources.remove(i);
                    removed_permission = true;
                }
                if other_pb != before {
                    changed.push((other, other_pb));
                }
            }
            // C++ bumps the version for the delete, and again if a permission went.
            let mut mutation = bump_roles_version_by(
                snapshot,
                Mutation::new(),
                if removed_permission { 2 } else { 1 },
            )?;
            for (other, other_pb) in &changed {
                mutation = upsert_role(snapshot, mutation, other, other_pb)?;
            }
            let row = snapshot
                .get(SysRowEntryType::ROLE, name)
                .expect("the role was found above");
            mutation = mutation.read(row).delete(SysRowEntryType::ROLE, name);
            Ok((mutation, ()))
        })
        .await
    }

    /// `GrantRevokeRole`.
    pub async fn grant_revoke_role(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &GrantRevokeRoleRequestPB,
    ) -> Result<(), MasterErrorPB> {
        if req.granted_role() == req.recipient_role() {
            // Revoking a role from itself is ignored, as Apache Cassandra does.
            if req.revoke() {
                return Ok(());
            }
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                format!(
                    "{} is a member of {}",
                    req.recipient_role(),
                    req.granted_role()
                ),
            ));
        }
        if !req.has_granted_role() || !req.has_recipient_role() {
            return Err(error(
                Code::ROLE_NOT_FOUND,
                StatusCode::InvalidArgument,
                "No role name given",
            ));
        }
        self.write(token, deadline, |snapshot| {
            let not_found = |name: &str| {
                error(
                    Code::ROLE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("{name} doesn't exist"),
                )
            };
            role(snapshot, req.granted_role())?.ok_or_else(|| not_found(req.granted_role()))?;
            let mut recipient = role(snapshot, req.recipient_role())?
                .ok_or_else(|| not_found(req.recipient_role()))?;
            let granted = req.granted_role().as_bytes();
            if req.revoke() {
                if !recipient.member_of.iter().any(|m| m == granted) {
                    return Err(error(
                        Code::INVALID_REQUEST,
                        StatusCode::InvalidArgument,
                        format!(
                            "{} is not a member of {}",
                            req.recipient_role(),
                            req.granted_role()
                        ),
                    ));
                }
                recipient.member_of.retain(|m| m != granted);
            } else {
                // No cycles.
                let all = roles(snapshot);
                if is_member_of(&all, req.granted_role(), req.recipient_role())
                    || is_member_of(&all, req.recipient_role(), req.granted_role())
                {
                    return Err(error(
                        Code::INVALID_REQUEST,
                        StatusCode::InvalidArgument,
                        format!(
                            "{} is a member of {}",
                            req.recipient_role(),
                            req.granted_role()
                        ),
                    ));
                }
                recipient.member_of.push(granted.to_vec());
            }
            let mut mutation =
                upsert_role(snapshot, Mutation::new(), req.recipient_role(), &recipient)?;
            mutation = bump_roles_version(snapshot, mutation)?;
            Ok((mutation, ()))
        })
        .await
    }

    /// `GrantRevokePermission`.
    pub async fn grant_revoke_permission(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &GrantRevokePermissionRequestPB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!(
            "{} permission {:?} on {} for role {}",
            if req.revoke() { "Revoke" } else { "Grant" },
            req.permission(),
            req.canonical_resource(),
            req.role_name()
        );
        self.write(token, deadline, |snapshot| {
            self.check_resource(snapshot, req)?;
            if req.resource_type() == ResourceType::ROLE
                && role(snapshot, req.resource_name())?.is_none()
            {
                return Err(error(
                    Code::ROLE_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Resource <role {}> does not exist", req.role_name()),
                ));
            }
            let mut pb = role(snapshot, req.role_name())?.ok_or_else(|| {
                error(
                    Code::ROLE_NOT_FOUND,
                    StatusCode::InvalidArgument,
                    format!("Role {} doesn't exist", req.role_name()),
                )
            })?;
            let i = match pb
                .resources
                .iter()
                .position(|r| r.canonical_resource() == req.canonical_resource())
            {
                Some(i) => i,
                // Nothing to revoke.
                None if req.revoke() => return Ok((Mutation::new(), ())),
                None => {
                    let mut resource = ResourcePermissionsPB::new();
                    resource.set_canonical_resource(req.canonical_resource().to_string());
                    resource.set_resource_type(req.resource_type());
                    if req.has_resource_name() {
                        resource.set_resource_name(req.resource_name().to_string());
                    }
                    if req.namespace.is_some() {
                        resource.set_namespace_name(req.namespace.name().to_string());
                    }
                    pb.resources.push(resource);
                    pb.resources.len() - 1
                }
            };
            let resource = &mut pb.resources[i];
            if req.permission() != PermissionType::ALL_PERMISSION {
                let at = resource
                    .permissions
                    .iter()
                    .position(|p| p.enum_value() == Ok(req.permission()));
                match at {
                    None if !req.revoke() => {
                        if !all_permissions_for_resource(req.resource_type())
                            .contains(&req.permission())
                        {
                            return Err(error(
                                Code::INVALID_REQUEST,
                                StatusCode::InvalidArgument,
                                format!(
                                    "Invalid permission {} for resource type {:?}",
                                    req.permission().value(),
                                    req.resource_type()
                                ),
                            ));
                        }
                        resource
                            .permissions
                            .push(EnumOrUnknown::new(req.permission()));
                    }
                    Some(at) if req.revoke() => {
                        resource.permissions.remove(at);
                    }
                    _ => {}
                }
            } else {
                // REVOKE ALL clears; GRANT ALL sets what the resource takes.
                resource.permissions.clear();
                if !req.revoke() {
                    resource.permissions.extend(
                        all_permissions_for_resource(req.resource_type())
                            .iter()
                            .map(|p| EnumOrUnknown::new(*p)),
                    );
                }
            }
            if pb.resources[i].permissions.is_empty() {
                pb.resources.remove(i);
            }
            let mut mutation = bump_roles_version(snapshot, Mutation::new())?;
            mutation = upsert_role(snapshot, mutation, req.role_name(), &pb)?;
            Ok((mutation, ()))
        })
        .await
    }

    /// `CatalogManager::CheckResource`: the keyspace or table a permission is on exists.
    fn check_resource(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GrantRevokePermissionRequestPB,
    ) -> Result<(), MasterErrorPB> {
        match req.resource_type() {
            ResourceType::KEYSPACE => {
                find_namespace(snapshot, &req.namespace).map_err(|_| {
                    error(
                        Code::NAMESPACE_NOT_FOUND,
                        StatusCode::NotFound,
                        format!("Resource <keyspace {}> doesn't exist", req.namespace.name()),
                    )
                })?;
            }
            ResourceType::TABLE => {
                let found = find_namespace(snapshot, &req.namespace)
                    .ok()
                    .and_then(|ns| snapshot.table_by_name(&ns.key.id, req.resource_name()));
                if found.is_none() {
                    return Err(error(
                        Code::OBJECT_NOT_FOUND,
                        StatusCode::NotFound,
                        format!(
                            "Resource <object '{}.{}'> doesn't exist",
                            req.namespace.name(),
                            req.resource_name()
                        ),
                    ));
                }
            }
            _ => {}
        }
        Ok(())
    }

    /// `GetPermissions`: every role's permissions with those of the roles granted to it
    /// (`BuildResourcePermissionsUnlocked`), unless the caller has this version.
    pub fn get_permissions(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetPermissionsRequestPB,
        resp: &mut GetPermissionsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let version = snapshot
            .get(SysRowEntryType::SYS_CONFIG, SECURITY_CONFIG_ID)
            .map(|row| row.parse::<SysConfigEntryPB>().map_err(corrupt))
            .transpose()?
            .map_or(0, |pb| pb.security_config().roles_version());
        if req.has_if_version_greater_than() {
            let asked = req.if_version_greater_than();
            if version == asked {
                resp.set_version(version);
                return Ok(());
            }
            if version < asked {
                return Err(error(
                    Code::CONFIG_VERSION_MISMATCH,
                    StatusCode::IllegalState,
                    format!(
                        "GetPermissionsRequestPB version {asked} is greater than master's version \
                         {version}. Should call GetPermissions again"
                    ),
                ));
            }
        }
        let roles = roles(snapshot);
        for (name, granted) in recursive_granted_roles(&roles) {
            let Some(own) = roles.get(&name) else {
                continue;
            };
            let mut info = RolePermissionInfoPB::new();
            info.set_role(name.clone());
            info.set_salted_hash(String::from_utf8_lossy(own.salted_hash()).into_owned());
            info.set_can_login(own.can_login());
            let mut all_keyspaces = 0u32;
            let mut all_roles = 0u32;
            let mut superuser = false;
            for granted_name in granted.iter().chain(std::iter::once(&name)) {
                let Some(pb) = roles.get(granted_name) else {
                    continue;
                };
                for resource in &pb.resources {
                    let bits = resource
                        .permissions
                        .iter()
                        .filter_map(|p| p.enum_value().ok())
                        .fold(0u32, |bits, p| bits | (1 << p.value()));
                    match resource.canonical_resource() {
                        ROLES_RESOURCE => all_roles |= bits,
                        DATA_RESOURCE => all_keyspaces |= bits,
                        canonical => {
                            let mut out = ResourcePermissionInfoPB::new();
                            out.set_canonical_resource(canonical.to_string());
                            out.set_permissions(bits);
                            info.resource_permissions.push(out);
                        }
                    }
                }
                superuser |= pb.is_superuser();
            }
            if superuser {
                all_keyspaces = ALL_PERMISSION_BITS;
                all_roles = ALL_PERMISSION_BITS;
            }
            info.set_all_keyspaces_permissions(all_keyspaces);
            info.set_all_roles_permissions(all_roles);
            resp.role_permissions.push(info);
        }
        resp.set_version(version);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn role_pb(member_of: &[&str]) -> SysRoleEntryPB {
        let mut pb = SysRoleEntryPB::new();
        pb.member_of = member_of.iter().map(|m| m.as_bytes().to_vec()).collect();
        pb
    }

    #[test]
    fn granted_roles_are_transitive() {
        let mut roles = BTreeMap::new();
        roles.insert("a".to_string(), role_pb(&["b"]));
        roles.insert("b".to_string(), role_pb(&["c"]));
        roles.insert("c".to_string(), role_pb(&[]));
        let granted = recursive_granted_roles(&roles);
        assert_eq!(
            granted["a"],
            ["b", "c"]
                .iter()
                .map(|s| s.to_string())
                .collect::<BTreeSet<_>>()
        );
        assert!(granted["c"].is_empty());
        assert!(is_member_of(&roles, "c", "a"));
        assert!(!is_member_of(&roles, "a", "c"));
    }
}
