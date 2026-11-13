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

//! The YSQL major catalog upgrade on the leader (`YsqlInitDBAndMajorUpgradeHandler`). The state is
//! the YSQL catalog config's `ysql_major_catalog_upgrade_info`; the steps that run processes or
//! work on the sys catalog tablet's YSQL rows (initdb, pg_upgrade, the catalog version fix-up) run
//! on the C++ host, and the DDL side (new-version catalog tables, rollback, cleanup) is
//! `master_table_lifecycle::major_upgrade`.
//!
//! State machine (`kAllowedTransitions`): DONE -> PERFORMING_INIT_DB -> PERFORMING_PG_UPGRADE ->
//! MONITORING -> DONE (finalize, which also records the new catalog version and asks for the old
//! catalog's cleanup); a running state -> FAILED or PERFORMING_ROLLBACK; FAILED ->
//! PERFORMING_ROLLBACK -> DONE. A new leader fails an operation its predecessor was running.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use master_catalog_state::defaults::YSQL_CATALOG_CONFIG_ID;
use master_catalog_state::{Catalog, CatalogWriter, Mutation, WriteOptions};
use master_leadership::{LeaderToken, Leadership};
use master_table_lifecycle::major_upgrade::{major_upgrade_in_progress, upgrade_info};
use master_table_lifecycle::{SchemaHost, TableLifecycle};
use master_ts_manager::TsManager;
use protobuf::{Enum, Message, MessageField};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    ysql_major_catalog_upgrade_info_pb::State, SysConfigEntryPB,
};
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_admin::YsqlMajorCatalogUpgradeState;
use yb_proto::pb::master_rust_peer::{
    auto_flags_change_request_pb::TserverVersion, run_ysql_init_db_request_pb::DatabaseOidPB,
    validate_tserver_versions_request_pb, ysql_major_upgrade_catalog_step_request_pb::Step,
    ForwardUpgradeYsqlRequestPB, ForwardUpgradeYsqlResponsePB, RunYsqlInitDbRequestPB,
    RunYsqlInitDbResponsePB, RunYsqlPgUpgradeRequestPB, RunYsqlPgUpgradeResponsePB,
    SetYsqlMajorUpgradeStateRequestPB, SetYsqlMajorUpgradeStateResponsePB,
    ValidateTserverVersionsRequestPB, ValidateTserverVersionsResponsePB,
    YsqlMajorUpgradeCatalogStepRequestPB, YsqlMajorUpgradeCatalogStepResponsePB,
};
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};
use yb_proto::pb::wire_protocol::AppStatusPB;

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct MajorUpgradeOptions {
    /// `enable_ysql`.
    pub enable_ysql: fn() -> bool,
    /// `TEST_fail_ysql_catalog_upgrade_state_transition_from`: the state name to refuse to leave.
    pub fail_transition_from: fn() -> String,
    /// `TEST_ysql_fail_cleanup_previous_version_catalog`.
    pub fail_cleanup: fn() -> bool,
    /// `skip_tserver_version_checks`.
    pub skip_tserver_version_checks: fn() -> bool,
}

/// The leader's YSQL major upgrade handler.
pub struct MajorUpgrade {
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    leadership: Arc<Leadership>,
    ts_manager: Arc<TsManager>,
    host: Arc<dyn SchemaHost>,
    lifecycle: Arc<TableLifecycle>,
    /// This build's YSQL major version; None when YSQL is off.
    build_major_version: Option<u32>,
    options: MajorUpgradeOptions,
    /// An upgrade, rollback or cleanup is running (`is_running_`): one at a time.
    running: AtomicBool,
}

fn status(code: StatusCode, message: impl Into<String>) -> Status {
    Status::new(code, message.into())
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    status(StatusCode::Corruption, e.to_string())
}

fn from_master_error(e: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(e.status.code().value()),
        e.status.message().to_string(),
    )
}

fn to_app_status(s: &Status) -> AppStatusPB {
    let mut pb = AppStatusPB::new();
    pb.code = Some(protobuf::EnumOrUnknown::from_i32(s.code as i32));
    pb.set_message(s.message.clone());
    pb
}

/// `YsqlMajorCatalogUpgradeInfoPB::State_Name`.
fn state_name(state: State) -> &'static str {
    match state {
        State::INVALID => "INVALID",
        State::DONE => "DONE",
        State::FAILED => "FAILED",
        State::PERFORMING_INIT_DB => "PERFORMING_INIT_DB",
        State::PERFORMING_PG_UPGRADE => "PERFORMING_PG_UPGRADE",
        State::MONITORING => "MONITORING",
        State::PERFORMING_ROLLBACK => "PERFORMING_ROLLBACK",
    }
}

/// `IsYsqlMajorCatalogOperationRunning`.
fn operation_running(state: State) -> bool {
    matches!(
        state,
        State::PERFORMING_INIT_DB | State::PERFORMING_PG_UPGRADE | State::PERFORMING_ROLLBACK
    )
}

/// `kAllowedTransitions`.
fn allowed(from: State, to: State) -> bool {
    use State::*;
    match from {
        INVALID => false,
        DONE => to == PERFORMING_INIT_DB,
        FAILED => to == PERFORMING_ROLLBACK,
        PERFORMING_INIT_DB => matches!(to, PERFORMING_PG_UPGRADE | PERFORMING_ROLLBACK | FAILED),
        PERFORMING_PG_UPGRADE => matches!(to, MONITORING | PERFORMING_ROLLBACK | FAILED),
        MONITORING => matches!(to, DONE | PERFORMING_ROLLBACK | FAILED),
        PERFORMING_ROLLBACK => matches!(to, DONE | FAILED),
    }
}

/// A YSQL database's oid from its namespace id (`GetPgsqlDatabaseOid`).
fn database_oid(id: &[u8]) -> Option<u32> {
    if id.len() != 32 {
        return None;
    }
    u32::from_str_radix(std::str::from_utf8(&id[0..8]).ok()?, 16).ok()
}

impl MajorUpgrade {
    /// A handler over the leader's catalog, tservers and DDL.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        host: Arc<dyn SchemaHost>,
        lifecycle: Arc<TableLifecycle>,
        build_major_version: Option<u32>,
        options: MajorUpgradeOptions,
    ) -> Arc<MajorUpgrade> {
        Arc::new(MajorUpgrade {
            catalog,
            writer,
            leadership,
            ts_manager,
            host,
            lifecycle,
            build_major_version,
            options,
            running: AtomicBool::new(false),
        })
    }

    fn in_progress(&self) -> bool {
        major_upgrade_in_progress(&self.catalog.snapshot(), self.build_major_version)
    }

    fn state(&self) -> State {
        upgrade_info(&self.catalog.snapshot()).map_or(State::DONE, |info| info.state())
    }

    async fn call_host<Resp: Message>(
        &self,
        method: &'static str,
        request: &impl Message,
        error: impl Fn(&Resp) -> Option<&MasterErrorPB>,
    ) -> Result<Resp, Status> {
        let bytes = request.write_to_bytes().map_err(corrupt)?;
        let host = self.host.clone();
        let response = tokio::task::spawn_blocking(move || host.call(method, &bytes))
            .await
            .map_err(corrupt)??;
        let response = Resp::parse_from_bytes(&response).map_err(corrupt)?;
        if let Some(e) = error(&response) {
            return Err(from_master_error(e));
        }
        Ok(response)
    }

    /// Tells the host what its catalog reads and writes need to know of the upgrade. Sent at the
    /// start of each term and after each state change.
    pub async fn push_state(&self) -> Result<(), Status> {
        let mut req = SetYsqlMajorUpgradeStateRequestPB::new();
        req.set_in_progress(self.in_progress());
        req.set_state(self.state());
        self.call_host(
            "SetYsqlMajorUpgradeState",
            &req,
            |r: &SetYsqlMajorUpgradeStateResponsePB| r.error.as_ref(),
        )
        .await
        .map(|_| ())
    }

    /// `SysCatalogLoaded`, before the term is ready: the host learns the state, and an operation
    /// the last leader was running fails with it.
    pub async fn on_term_start(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        if !(self.options.enable_ysql)() {
            return Ok(());
        }
        self.push_state().await?;
        let Some(info) = upgrade_info(&self.catalog.snapshot()) else {
            return Ok(());
        };
        if self.in_progress() && operation_running(info.state()) {
            self.transition(
                token,
                State::FAILED,
                Some(status(
                    StatusCode::InternalError,
                    "yb-master restarted during ysql major catalog upgrade",
                )),
            )
            .await?;
        }
        Ok(())
    }

    /// `SysCatalogLoaded`, once the term is ready: an unfinished cleanup of the old catalog
    /// starts again.
    pub fn resume_cleanup(self: &Arc<Self>, token: LeaderToken) {
        if upgrade_info(&self.catalog.snapshot())
            .is_some_and(|info| info.previous_version_catalog_cleanup_required())
        {
            self.schedule_cleanup(token);
        }
    }

    /// `TransitionMajorCatalogUpgradeState`; `failed` is set exactly when moving to FAILED.
    async fn transition(
        self: &Arc<Self>,
        token: &LeaderToken,
        to: State,
        failed: Option<Status>,
    ) -> Result<(), Status> {
        if !self.in_progress() {
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "Ysql Catalog is already on the current major version: {}",
                    self.build_major_version.unwrap_or_default()
                ),
            ));
        }
        let build_version = self.build_major_version.unwrap_or_default();
        let fail_from = (self.options.fail_transition_from)();
        let (done, _) = self
            .writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let row = snapshot
                    .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)
                    .ok_or_else(|| status(StatusCode::NotFound, "No YSQL catalog config"))?;
                let mut config: SysConfigEntryPB = row.parse().map_err(corrupt)?;
                let info = config
                    .mut_ysql_catalog_config()
                    .ysql_major_catalog_upgrade_info
                    .mut_or_insert_default();
                if info.previous_version_catalog_cleanup_required() {
                    return Err(status(
                        StatusCode::IllegalState,
                        "Previous version catalog cleanup has not completed yet. Cannot start a \
                         new major catalog upgrade.",
                    ));
                }
                let from = info.state();
                if from == to {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!("Major upgrade state already set to {}", state_name(to)),
                    ));
                }
                if state_name(from) == fail_from {
                    return Err(status(
                        StatusCode::IllegalState,
                        "Failed due to FLAGS_TEST_fail_ysql_catalog_upgrade_state_transition_from",
                    ));
                }
                if !allowed(from, to) {
                    return Err(status(
                        StatusCode::IllegalState,
                        format!(
                            "Invalid state transition from {} to {}",
                            state_name(from),
                            state_name(to)
                        ),
                    ));
                }
                let done = from == State::MONITORING && to == State::DONE;
                if done {
                    info.set_catalog_version(build_version);
                    info.set_previous_version_catalog_cleanup_required(true);
                }
                info.set_state(to);
                info.previous_error = match &failed {
                    Some(s) => MessageField::some(to_app_status(s)),
                    None => MessageField::none(),
                };
                tracing::info!(
                    "Transitioned major upgrade state from {} to {}",
                    state_name(from),
                    state_name(to)
                );
                let mutation = Mutation::new().read(row).upsert_message(
                    SysRowEntryType::SYS_CONFIG,
                    YSQL_CATALOG_CONFIG_ID,
                    &config,
                )?;
                Ok((mutation, done))
            })
            .await?;
        self.push_state().await?;
        if done {
            self.schedule_cleanup(*token);
        }
        Ok(())
    }

    /// `RunOperationAsync`: one operation at a time.
    fn try_start(&self) -> Result<(), Status> {
        self.running
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .map(|_| ())
            .map_err(|_| {
                status(
                    StatusCode::IllegalState,
                    "Global initdb or ysql major catalog upgrade/rollback is already in progress",
                )
            })
    }

    fn check_enabled(&self) -> Result<(), Status> {
        if (self.options.enable_ysql)() {
            Ok(())
        } else {
            Err(status(StatusCode::IllegalState, "YSQL is not enabled"))
        }
    }

    /// `StartYsqlMajorCatalogUpgrade`: PERFORMING_INIT_DB, then the upgrade in the background.
    pub async fn start(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        self.check_enabled()?;
        self.transition(token, State::PERFORMING_INIT_DB, None)
            .await?;
        if let Err(e) = self.try_start() {
            let _ = self.transition(token, State::FAILED, Some(e.clone())).await;
            return Err(e);
        }
        let this = self.clone();
        let token = *token;
        tokio::spawn(async move {
            let result = this.run_upgrade(&token).await;
            match result {
                Ok(()) => match this.transition(&token, State::MONITORING, None).await {
                    Ok(()) => tracing::info!("Ysql major catalog upgrade completed successfully"),
                    Err(e) => {
                        tracing::error!("Failed to set major version upgrade state: {e}")
                    }
                },
                Err(e) => {
                    tracing::warn!("Ysql major catalog upgrade failed: {e}");
                    if let Err(e) = this.transition(&token, State::FAILED, Some(e)).await {
                        tracing::warn!("Failed to set major version upgrade state: {e}");
                    }
                }
            }
            this.running.store(false, Ordering::Release);
        });
        Ok(())
    }

    /// `RunMajorVersionUpgradeImpl`.
    async fn run_upgrade(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        let mut abort = YsqlMajorUpgradeCatalogStepRequestPB::new();
        abort.set_step(Step::ABORT_TRANSACTIONS);
        abort.set_term(token.term());
        self.catalog_step(&abort).await?;

        // initdb in major upgrade mode keeps the oids of template0 and yugabyte; template1's is
        // fixed.
        let mut initdb = RunYsqlInitDbRequestPB::new();
        initdb.set_is_major_upgrade(true);
        let snapshot = self.catalog.snapshot();
        for name in ["template0", "yugabyte"] {
            let id = master_table_lifecycle::lookup::find_namespace_by_name(
                &snapshot,
                name,
                YQLDatabase::YQL_DATABASE_PGSQL as i32,
            )
            .map(|e| e.key.id.to_vec())
            .ok_or_else(|| status(StatusCode::NotFound, format!("Keyspace {name} not found")))?;
            let oid = database_oid(&id).ok_or_else(|| {
                status(
                    StatusCode::InvalidArgument,
                    format!("Invalid PostgreSQL namespace id for {name}"),
                )
            })?;
            let mut entry = DatabaseOidPB::new();
            entry.set_name(name.to_string());
            entry.set_oid(oid);
            initdb.database_oids.push(entry);
        }
        tracing::info!("Running initdb for the YSQL major catalog upgrade");
        self.call_host("RunYsqlInitDb", &initdb, |r: &RunYsqlInitDbResponsePB| {
            r.error.as_ref()
        })
        .await
        .map_err(|e| status(e.code, format!("Failed to run initdb: {}", e.message)))?;
        self.leadership.check(token)?;

        self.transition(token, State::PERFORMING_PG_UPGRADE, None)
            .await?;
        let mut upgrade = RunYsqlPgUpgradeRequestPB::new();
        for ts in self.ts_manager.live() {
            upgrade.live_tservers.push(ts.info());
        }
        tracing::info!("Running pg_upgrade for the YSQL major catalog upgrade");
        self.call_host(
            "RunYsqlPgUpgrade",
            &upgrade,
            |r: &RunYsqlPgUpgradeResponsePB| r.error.as_ref(),
        )
        .await
        .map_err(|e| status(e.code, format!("Failed to run pg_upgrade: {}", e.message)))?;
        self.leadership.check(token)?;

        let mut versions = YsqlMajorUpgradeCatalogStepRequestPB::new();
        versions.set_step(Step::UPDATE_CATALOG_VERSIONS);
        versions.set_term(token.term());
        self.catalog_step(&versions).await.map_err(|e| {
            status(
                e.code,
                format!("Failed to update catalog versions: {}", e.message),
            )
        })
    }

    async fn catalog_step(&self, req: &YsqlMajorUpgradeCatalogStepRequestPB) -> Result<(), Status> {
        self.call_host(
            "YsqlMajorUpgradeCatalogStep",
            req,
            |r: &YsqlMajorUpgradeCatalogStepResponsePB| r.error.as_ref(),
        )
        .await
        .map(|_| ())
    }

    /// `ClientUpgradeYsql`: runs `UpgradeYsql` (the YSQL migrations) on the live tserver closest
    /// to this master.
    pub async fn forward_upgrade_ysql(
        &self,
        use_single_connection: bool,
        timeout: std::time::Duration,
    ) -> Result<(), Status> {
        let mut req = ForwardUpgradeYsqlRequestPB::new();
        for ts in self.ts_manager.live() {
            req.live_tservers.push(ts.info());
        }
        req.set_use_single_connection(use_single_connection);
        req.set_timeout_ms(u64::try_from(timeout.as_millis()).unwrap_or(u64::MAX));
        self.call_host(
            "ForwardUpgradeYsql",
            &req,
            |r: &ForwardUpgradeYsqlResponsePB| r.error.as_ref(),
        )
        .await
        .map(|_| ())
    }

    /// `IsYsqlMajorCatalogUpgradeDone`: whether no operation runs, and the last one's error.
    pub fn is_done(&self) -> (bool, Option<Status>) {
        let Some(info) = upgrade_info(&self.catalog.snapshot()) else {
            return (true, None);
        };
        if operation_running(info.state()) {
            return (false, None);
        }
        let error = info.previous_error.as_ref().map(|e| {
            Status::new(
                StatusCode::from_i32(e.code().value()),
                e.message().to_string(),
            )
        });
        (true, error)
    }

    async fn validate_tserver_versions(
        &self,
        op: validate_tserver_versions_request_pb::Op,
        context: &str,
    ) -> Result<(), Status> {
        let mut req = ValidateTserverVersionsRequestPB::new();
        req.set_op(op);
        for descriptor in self.ts_manager.live() {
            let mut ts = TserverVersion::new();
            ts.set_uuid(String::from_utf8_lossy(&descriptor.uuid).into_owned());
            ts.version_info = descriptor.pb.version_info.clone();
            req.live_tservers.push(ts);
        }
        self.call_host(
            "ValidateTserverVersions",
            &req,
            |r: &ValidateTserverVersionsResponsePB| r.error.as_ref(),
        )
        .await
        .map(|_| ())
        .map_err(|e| status(e.code, format!("{context}: {}", e.message)))
    }

    /// `FinalizeYsqlMajorCatalogUpgrade`.
    pub async fn finalize(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        self.check_enabled()?;
        tracing::info!("Finalizing ysql major catalog upgrade");
        self.validate_tserver_versions(
            validate_tserver_versions_request_pb::Op::VERSION_EQ,
            "Cannot finalize YSQL major catalog upgrade before all yb-tservers have been upgraded \
             to the current version",
        )
        .await?;
        self.transition(token, State::DONE, None).await
    }

    /// `RollbackYsqlMajorCatalogVersion`: synchronous.
    pub async fn rollback(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        self.check_enabled()?;
        tracing::info!("Rolling back ysql major catalog upgrade");
        self.validate_tserver_versions(
            validate_tserver_versions_request_pb::Op::YSQL_MAJOR_VERSION_LT,
            "Cannot rollback YSQL major catalog while yb-tservers are running on a newer YSQL \
             major version",
        )
        .await?;
        self.try_start()?;
        let result = self.run_rollback(token).await;
        self.running.store(false, Ordering::Release);
        result
    }

    /// `RunRollbackMajorVersionUpgrade`.
    async fn run_rollback(self: &Arc<Self>, token: &LeaderToken) -> Result<(), Status> {
        if self.state() == State::DONE {
            tracing::info!(
                "No inflight Ysql major catalog upgrade in progress. Nothing to rollback."
            );
            return Ok(());
        }
        self.transition(token, State::PERFORMING_ROLLBACK, None)
            .await?;
        match self.lifecycle.rollback_ysql_major_catalog(token).await {
            Ok(()) => self.transition(token, State::DONE, None).await,
            Err(e) => {
                let e = from_master_error(&e);
                self.transition(token, State::FAILED, Some(e.clone()))
                    .await?;
                Err(e)
            }
        }
    }

    /// `GetYsqlMajorCatalogUpgradeState`.
    pub fn upgrade_state(&self) -> Result<YsqlMajorCatalogUpgradeState, Status> {
        use YsqlMajorCatalogUpgradeState::*;
        if !(self.options.enable_ysql)() {
            return Ok(YSQL_MAJOR_CATALOG_UPGRADE_DONE);
        }
        Ok(match self.state() {
            State::INVALID => {
                return Err(status(
                    StatusCode::IllegalState,
                    "Unknown ysql major upgrade state: INVALID",
                ))
            }
            State::DONE if self.in_progress() => YSQL_MAJOR_CATALOG_UPGRADE_PENDING,
            State::DONE => YSQL_MAJOR_CATALOG_UPGRADE_DONE,
            State::FAILED => YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK,
            State::PERFORMING_INIT_DB | State::PERFORMING_PG_UPGRADE => {
                YSQL_MAJOR_CATALOG_UPGRADE_IN_PROGRESS
            }
            State::MONITORING => YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK,
            State::PERFORMING_ROLLBACK => YSQL_MAJOR_CATALOG_UPGRADE_ROLLBACK_IN_PROGRESS,
        })
    }

    /// `ValidateTServerVersion` for a registering or heartbeating tserver's YSQL major version
    /// (None for a PG11 process without version info). An error makes the tserver FATAL.
    pub fn validate_tserver_version(&self, ts_ysql_major: Option<u32>) -> Result<(), Status> {
        let Some(current) = self.build_major_version else {
            return Ok(());
        };
        if (self.options.skip_tserver_version_checks)() {
            return Ok(());
        }
        if !self.in_progress() {
            return match ts_ysql_major {
                Some(v) if v == current => Ok(()),
                v => Err(status(
                    StatusCode::IllegalState,
                    format!(
                        "yb-tserver YSQL major version {} does not match the cluster version \
                         {current}. Restart the yb-tserver in the correct version.",
                        v.unwrap_or_default()
                    ),
                )),
            };
        }
        let Some(v) = ts_ysql_major else {
            return Ok(());
        };
        if self.state() == State::MONITORING {
            if v <= current {
                return Ok(());
            }
            return Err(status(
                StatusCode::IllegalState,
                format!(
                    "yb-tserver YSQL major version {v} is too high. Restart the yb-tserver in a \
                     version less than or equal to YSQL major version {current}."
                ),
            ));
        }
        if v < current {
            return Ok(());
        }
        Err(status(
            StatusCode::IllegalState,
            format!(
                "yb-tserver YSQL major version {v} is too high. Cluster is not in a state to \
                 accept yb-tservers on a higher version yet. Restart the yb-tserver in the \
                 previous YSQL major version"
            ),
        ))
    }

    /// `ScheduleCleanupPreviousYsqlMajorCatalog`: retried while this term lasts; a new leader
    /// starts it again.
    fn schedule_cleanup(self: &Arc<Self>, token: LeaderToken) {
        if self.try_start().is_err() {
            return;
        }
        let this = self.clone();
        tokio::spawn(async move {
            loop {
                if let Err(e) = this.leadership.check(&token) {
                    tracing::info!(
                        "No longer the leader. New leader will retry the cleanup of the previous \
                         YSQL major version catalog: {e}"
                    );
                    break;
                }
                match this.cleanup(&token).await {
                    Ok(()) => break,
                    Err(e) => {
                        tracing::warn!(
                            "Failed to cleanup previous version ysql major catalog. Retrying: {e}"
                        );
                        tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                    }
                }
            }
            this.running.store(false, Ordering::Release);
        });
    }

    /// `CleanupPreviousYsqlMajorCatalog`.
    async fn cleanup(&self, token: &LeaderToken) -> Result<(), Status> {
        if !upgrade_info(&self.catalog.snapshot())
            .is_some_and(|info| info.previous_version_catalog_cleanup_required())
        {
            return Ok(());
        }
        if (self.options.fail_cleanup)() {
            return Err(status(
                StatusCode::IllegalState,
                "Failed due to FLAGS_TEST_ysql_fail_cleanup_previous_version_catalog",
            ));
        }
        self.lifecycle
            .delete_prior_ysql_major_catalog(token)
            .await
            .map_err(|e| from_master_error(&e))?;
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let row = snapshot
                    .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)
                    .ok_or_else(|| status(StatusCode::NotFound, "No YSQL catalog config"))?;
                let mut config: SysConfigEntryPB = row.parse().map_err(corrupt)?;
                config
                    .mut_ysql_catalog_config()
                    .ysql_major_catalog_upgrade_info
                    .mut_or_insert_default()
                    .set_previous_version_catalog_cleanup_required(false);
                let mutation = Mutation::new().read(row).upsert_message(
                    SysRowEntryType::SYS_CONFIG,
                    YSQL_CATALOG_CONFIG_ID,
                    &config,
                )?;
                Ok((mutation, ()))
            })
            .await?;
        tracing::info!("Cleaned up the previous YSQL major version catalog");
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transitions_follow_cpp_table() {
        use State::*;
        assert!(allowed(DONE, PERFORMING_INIT_DB));
        assert!(!allowed(DONE, MONITORING));
        assert!(allowed(PERFORMING_INIT_DB, PERFORMING_PG_UPGRADE));
        assert!(allowed(PERFORMING_PG_UPGRADE, MONITORING));
        assert!(allowed(MONITORING, DONE));
        assert!(allowed(MONITORING, PERFORMING_ROLLBACK));
        assert!(!allowed(FAILED, DONE));
        assert!(allowed(FAILED, PERFORMING_ROLLBACK));
        assert!(allowed(PERFORMING_ROLLBACK, DONE));
        assert!(!allowed(INVALID, DONE));
        assert!(operation_running(PERFORMING_ROLLBACK));
        assert!(!operation_running(MONITORING));
    }

    #[test]
    fn database_oid_from_namespace_id() {
        assert_eq!(
            database_oid(b"00004000000030008000000000000000"),
            Some(0x4000)
        );
        assert_eq!(database_oid(b"short"), None);
    }
}
