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

//! The rows every catalog has (K10): what a C++ leader writes in `CatalogManager::VisitSysCatalog`
//! when they are missing, which on a new universe is all of them. A leader writes them once per
//! term before it becomes ready, in one mutation.
//!
//! C++ writes them one at a time; here they are one Raft write, so a crash cannot leave a universe
//! with some of them. Right after the initial sys catalog snapshot is restored
//! (`master-ysql-manager`), the same write replaces the snapshot's cluster config, xCluster config
//! and persistence block map with new ones and marks initdb done, as C++ does after the restore.

use std::sync::OnceLock;

use master_leadership::LeaderToken;
use master_sys_catalog::RowChange;
use protobuf::{EnumOrUnknown, Message};
use rand::RngCore;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, SysClusterConfigEntryPB, SysConfigEntryPB, SysNamespaceEntryPB,
    SysPersistenceBlockMapEntryPB, SysRoleEntryPB, SysSecurityConfigEntryPB,
    SysTransactionTablesConfigEntryPB, SysXClusterConfigEntryPB, SysYSQLCatalogConfigEntryPB,
};
use yb_proto::pb::common_types::{PersistenceBlockMapPB, TableType, YQLDatabase};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::{CatalogSnapshot, CatalogWriter, Mutation};

/// `kSecurityConfigType`.
pub const SECURITY_CONFIG_ID: &[u8] = b"security-configuration";
/// `kYsqlCatalogConfigType`.
pub const YSQL_CATALOG_CONFIG_ID: &[u8] = b"ysql-catalog-configuration";
/// `kTransactionTablesConfigType`.
pub const TRANSACTION_TABLES_CONFIG_ID: &[u8] = b"transaction-tables-configuration";
/// `kDefaultCassandraUsername`.
pub const DEFAULT_YCQL_ROLE: &[u8] = b"cassandra";
/// The YCQL system namespaces: `kSystemNamespaceId`, `kSystemSchemaNamespaceId`,
/// `kSystemAuthNamespaceId`.
pub const SYSTEM_NAMESPACES: [(&[u8], &str); 3] = [
    (b"00000000000000000000000000000001", "system"),
    (b"00000000000000000000000000000002", "system_schema"),
    (b"00000000000000000000000000000003", "system_auth"),
];

/// What only the host can build.
pub trait DefaultsHost: Send + Sync {
    /// The system table rows missing from `tables` (TABLE rows of the system namespaces as
    /// `(id, row)`) given the ids among them that have a tablet: `MasterHost::SystemTableRows`.
    fn system_table_rows(
        &self,
        tables: &[(Vec<u8>, Vec<u8>)],
        tables_with_tablets: &[String],
    ) -> Result<Vec<RowChange>, Status>;
    /// The salted hash of the default YCQL superuser's password.
    fn default_ycql_password_hash(&self) -> Result<Vec<u8>, Status>;
    /// This release's YSQL major version.
    fn ysql_major_version(&self) -> u32;
}

/// Flags that decide the default rows, read when they are written.
#[derive(Clone, Copy)]
pub struct DefaultsOptions {
    /// `cluster_uuid`: the new universe's cluster uuid; empty for a random one.
    pub cluster_uuid: fn() -> String,
    /// The `master_enable_universe_uuid_heartbeat_check` AutoFlag: give the universe a uuid.
    pub universe_uuid_check: fn() -> bool,
    /// `ycql_allow_cassandra_drop`: the default role is recorded as created, so dropping it
    /// does not bring it back.
    pub allow_ycql_role_drop: fn() -> bool,
}

/// A random UUID in its string form, as `Uuid::Generate().ToString()` gives.
pub fn random_uuid() -> String {
    let mut bytes = [0u8; 16];
    rand::rng().fill_bytes(&mut bytes);
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    format!(
        "{}-{}-{}-{}-{}",
        &hex[0..8],
        &hex[8..12],
        &hex[12..16],
        &hex[16..20],
        &hex[20..32]
    )
}

fn encode(message: &impl Message) -> Result<Vec<u8>, Status> {
    message
        .write_to_bytes()
        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))
}

fn parse<M: protobuf::MessageFull>(
    snapshot: &CatalogSnapshot,
    kind: SysRowEntryType,
    id: &[u8],
) -> Result<Option<M>, Status> {
    snapshot
        .get(kind, id)
        .map(|entity| {
            entity
                .parse::<M>()
                .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))
        })
        .transpose()
}

/// The mutation that writes every missing default row of `snapshot`; empty when none is missing.
/// `after_initial_snapshot`: the rows are the initial sys catalog snapshot's, just restored.
pub fn default_rows(
    snapshot: &CatalogSnapshot,
    host: &dyn DefaultsHost,
    options: &DefaultsOptions,
    after_initial_snapshot: bool,
) -> Result<Mutation, Status> {
    let mut mutation = Mutation::new();
    let add = |mutation: Mutation, kind, id: &[u8], data: Vec<u8>| -> Mutation {
        let mutation = match snapshot.get(kind, id) {
            Some(entity) => mutation.read(entity),
            None => mutation.read_absent(kind, id),
        };
        mutation.upsert(kind, id, data)
    };

    // PrepareDefaultSysConfig: the security config, and the transaction tables config.
    let stored_security: Option<SysConfigEntryPB> =
        parse(snapshot, SysRowEntryType::SYS_CONFIG, SECURITY_CONFIG_ID)?;
    let mut security = stored_security.clone().unwrap_or_else(|| {
        let mut config = SysConfigEntryPB::new();
        let mut security = SysSecurityConfigEntryPB::new();
        security.set_roles_version(0);
        config.set_security_config(security);
        config
    });
    if snapshot
        .get(SysRowEntryType::SYS_CONFIG, TRANSACTION_TABLES_CONFIG_ID)
        .is_none()
    {
        let mut config = SysConfigEntryPB::new();
        let mut tables = SysTransactionTablesConfigEntryPB::new();
        tables.set_version(0);
        config.set_transaction_tables_config(tables);
        mutation = add(
            mutation,
            SysRowEntryType::SYS_CONFIG,
            TRANSACTION_TABLES_CONFIG_ID,
            encode(&config)?,
        );
    }

    // YsqlManager::PrepareDefaultSysConfig. Then, as MakeYsqlSysCatalogTablesTransactional does
    // when initdb does not run, the YSQL catalog is marked transactional; migrating existing
    // PGSQL catalog tables is W9's, so only a catalog without them is marked here.
    let stored_ysql: Option<SysConfigEntryPB> = parse(
        snapshot,
        SysRowEntryType::SYS_CONFIG,
        YSQL_CATALOG_CONFIG_ID,
    )?;
    let mut ysql = stored_ysql.clone().unwrap_or_else(|| {
        let mut config = SysConfigEntryPB::new();
        let mut ysql = SysYSQLCatalogConfigEntryPB::new();
        ysql.set_version(0);
        ysql.ysql_major_catalog_upgrade_info
            .mut_or_insert_default()
            .set_catalog_version(host.ysql_major_version());
        config.set_ysql_catalog_config(ysql);
        config
    });
    let has_pgsql_tables = || {
        snapshot.iter(SysRowEntryType::TABLE).any(|e| {
            e.table()
                .is_some_and(|t| t.table_type == TableType::PGSQL_TABLE_TYPE as i32)
        })
    };
    if !ysql
        .ysql_catalog_config()
        .transactional_sys_catalog_enabled()
        && !has_pgsql_tables()
    {
        ysql.mut_ysql_catalog_config()
            .set_transactional_sys_catalog_enabled(true);
    }
    if after_initial_snapshot {
        // `YsqlCatalogConfig::SetInitDbDone`.
        let config = ysql.mut_ysql_catalog_config();
        config.set_initdb_done(true);
        config.clear_initdb_error();
    }
    if stored_ysql.as_ref() != Some(&ysql) {
        mutation = add(
            mutation,
            SysRowEntryType::SYS_CONFIG,
            YSQL_CATALOG_CONFIG_ID,
            encode(&ysql)?,
        );
    }

    // PrepareDefaultNamespaces.
    for (id, name) in SYSTEM_NAMESPACES {
        if snapshot.get(SysRowEntryType::NAMESPACE, id).is_none() {
            let mut namespace = SysNamespaceEntryPB::new();
            namespace.set_name(name.as_bytes().to_vec());
            namespace.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_CQL));
            namespace.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
            mutation = add(
                mutation,
                SysRowEntryType::NAMESPACE,
                id,
                encode(&namespace)?,
            );
        }
    }

    // PrepareSystemTables: the host builds the rows. It gets the system tables C++ finds by name
    // in `table_names_map_` (not deleting, deleted or hidden), and which of them have a tablet
    // that is not deleted or replaced, as `TableInfo::GetTablets` returns them.
    let mut tables = Vec::new();
    let mut with_tablets = Vec::new();
    for entity in snapshot.iter(SysRowEntryType::TABLE) {
        let Some(table) = entity.table() else {
            continue;
        };
        let system = SYSTEM_NAMESPACES
            .iter()
            .any(|(id, _)| table.namespace_id == *id);
        if !system
            || snapshot
                .table_by_name(&table.namespace_id, &table.name)
                .is_none_or(|found| found.key.id != entity.key.id)
        {
            continue;
        }
        if !snapshot.active_tablets_of(&entity.key.id).is_empty() {
            with_tablets.push(String::from_utf8_lossy(&entity.key.id).into_owned());
        }
        tables.push((entity.key.id.to_vec(), entity.raw.to_vec()));
    }
    for row in host.system_table_rows(&tables, &with_tablets)? {
        let RowChange::Upsert {
            entry_type,
            id,
            data,
        } = row
        else {
            return Err(Status::new(
                StatusCode::IllegalState,
                "The host asked to delete a system table row",
            ));
        };
        mutation = add(mutation, entry_type, &id, data);
    }

    // PermissionsManager::PrepareDefaultRoles.
    let allow_drop = (options.allow_ycql_role_drop)();
    let role_exists = snapshot
        .get(SysRowEntryType::ROLE, DEFAULT_YCQL_ROLE)
        .is_some();
    let recorded = security.security_config().cassandra_user_created();
    // A dropped default role comes back unless ycql_allow_cassandra_drop recorded its creation.
    if !(role_exists || allow_drop && recorded) {
        let mut role = SysRoleEntryPB::new();
        role.set_role(DEFAULT_YCQL_ROLE.to_vec());
        role.set_can_login(true);
        role.set_is_superuser(true);
        role.set_salted_hash(host.default_ycql_password_hash()?);
        mutation = add(
            mutation,
            SysRowEntryType::ROLE,
            DEFAULT_YCQL_ROLE,
            encode(&role)?,
        );
    }
    if allow_drop && !recorded {
        security
            .mut_security_config()
            .set_cassandra_user_created(true);
    }
    if stored_security.as_ref() != Some(&security) {
        mutation = add(
            mutation,
            SysRowEntryType::SYS_CONFIG,
            SECURITY_CONFIG_ID,
            encode(&security)?,
        );
    }

    // PrepareDefaultClusterConfig, and SetUniverseUuidIfNeeded on an existing universe. The
    // snapshot's configs belong to the universe that made it.
    let stored_cluster = if after_initial_snapshot {
        None
    } else {
        parse::<SysClusterConfigEntryPB>(snapshot, SysRowEntryType::CLUSTER_CONFIG, b"")?
    };
    match stored_cluster {
        None => {
            let mut config = SysClusterConfigEntryPB::new();
            config.set_version(0);
            let cluster_uuid = (options.cluster_uuid)();
            if cluster_uuid.is_empty() {
                config.set_cluster_uuid(random_uuid());
            } else if is_uuid(&cluster_uuid) {
                config.set_cluster_uuid(cluster_uuid);
            } else {
                // `Uuid::FromString`'s error: no universe is created with a malformed flag.
                return Err(Status::new(
                    StatusCode::Corruption,
                    format!("Couldn't read Uuid from string: {cluster_uuid}"),
                ));
            }
            if (options.universe_uuid_check)() {
                config.set_universe_uuid(random_uuid());
            }
            mutation = add(
                mutation,
                SysRowEntryType::CLUSTER_CONFIG,
                b"",
                encode(&config)?,
            );
        }
        Some(config) => {
            if let Some(config) = with_universe_uuid(config, options) {
                mutation = add(
                    mutation,
                    SysRowEntryType::CLUSTER_CONFIG,
                    b"",
                    encode(&config)?,
                );
            }
        }
    }

    // XClusterManager::PrepareDefaultXClusterConfig and PrepareDefaultPersistenceBlockMap.
    if after_initial_snapshot
        || snapshot
            .get(SysRowEntryType::XCLUSTER_CONFIG, b"")
            .is_none()
    {
        let mut config = SysXClusterConfigEntryPB::new();
        config.set_version(0);
        mutation = add(
            mutation,
            SysRowEntryType::XCLUSTER_CONFIG,
            b"",
            encode(&config)?,
        );
    }
    if after_initial_snapshot
        || snapshot
            .get(SysRowEntryType::PERSISTENCE_BLOCK_MAP, b"")
            .is_none()
    {
        let mut map = SysPersistenceBlockMapEntryPB::new();
        map.set_version(0);
        map.persistence_block_map = Some(PersistenceBlockMapPB::new()).into();
        mutation = add(
            mutation,
            SysRowEntryType::PERSISTENCE_BLOCK_MAP,
            b"",
            encode(&map)?,
        );
    }
    Ok(mutation)
}

/// Writes the missing default rows, rebuilding on conflict.
pub async fn write_default_rows(
    writer: &CatalogWriter,
    token: &LeaderToken,
    host: &dyn DefaultsHost,
    options: &DefaultsOptions,
    after_initial_snapshot: bool,
) -> Result<(), Status> {
    // bcrypt is slow on purpose; hash once, not on every rebuild after a conflict.
    let host = HashOnce {
        host,
        hash: OnceLock::new(),
    };
    writer
        .mutate(token, |snapshot| {
            Ok((
                default_rows(snapshot, &host, options, after_initial_snapshot)?,
                (),
            ))
        })
        .await
        .map(|_| ())
}

/// A host whose password hash is computed at most once.
struct HashOnce<'a> {
    host: &'a dyn DefaultsHost,
    hash: OnceLock<Result<Vec<u8>, Status>>,
}

impl DefaultsHost for HashOnce<'_> {
    fn system_table_rows(
        &self,
        tables: &[(Vec<u8>, Vec<u8>)],
        tables_with_tablets: &[String],
    ) -> Result<Vec<RowChange>, Status> {
        self.host.system_table_rows(tables, tables_with_tablets)
    }

    fn default_ycql_password_hash(&self) -> Result<Vec<u8>, Status> {
        self.hash
            .get_or_init(|| self.host.default_ycql_password_hash())
            .clone()
    }

    fn ysql_major_version(&self) -> u32 {
        self.host.ysql_major_version()
    }
}

/// `SetUniverseUuidIfNeeded`, which C++ runs from its background task and the leader runs
/// periodically too: the `master_enable_universe_uuid_heartbeat_check` AutoFlag is promoted while
/// masters run, and until the universe has a uuid every tserver heartbeat is refused.
pub async fn set_universe_uuid_if_needed(
    writer: &CatalogWriter,
    token: &LeaderToken,
    options: &DefaultsOptions,
) -> Result<(), Status> {
    if !(options.universe_uuid_check)() {
        return Ok(());
    }
    writer
        .mutate(token, |snapshot| {
            let mut mutation = Mutation::new();
            let Some(entity) = snapshot.cluster_config() else {
                return Ok((mutation, ()));
            };
            let config = entity
                .parse::<SysClusterConfigEntryPB>()
                .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
            if let Some(config) = with_universe_uuid(config, options) {
                mutation = mutation.read(entity).upsert(
                    SysRowEntryType::CLUSTER_CONFIG,
                    b"",
                    encode(&config)?,
                );
            }
            Ok((mutation, ()))
        })
        .await
        .map(|_| ())
}

/// The cluster config with a new universe uuid, if the check is on and it has none.
fn with_universe_uuid(
    mut config: SysClusterConfigEntryPB,
    options: &DefaultsOptions,
) -> Option<SysClusterConfigEntryPB> {
    if !(options.universe_uuid_check)() || !config.universe_uuid().is_empty() {
        return None;
    }
    config.set_universe_uuid(random_uuid());
    config.set_version(config.version() + 1);
    Some(config)
}

/// Whether `value` is a UUID in the form `Uuid::FromString` reads: 8-4-4-4-12 hex digits.
fn is_uuid(value: &str) -> bool {
    let bytes = value.as_bytes();
    bytes.len() == 36
        && bytes.iter().enumerate().all(|(i, b)| match i {
            8 | 13 | 18 | 23 => *b == b'-',
            _ => b.is_ascii_hexdigit(),
        })
}
