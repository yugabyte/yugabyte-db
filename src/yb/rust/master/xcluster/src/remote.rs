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

//! Calls to the masters of the other universe (K9). The host owns the connection: a
//! `XClusterRemoteClientHolder` per replication group, which carries the group's certificates
//! (`certs_for_cdc_dir`) and finds the leader. Leader changes are retried here.

use std::time::Duration;

use master_task_runtime::BoxFuture;
use protobuf::{Message, MessageFull};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB};

/// The wire name every master service but MasterBackup shares (`custom_service_name`).
pub const MASTER_SERVICE: &str = "yb.master.MasterService";

/// The leader master of another universe, reached with a replication group's credentials.
pub trait RemoteMasters: Send + Sync {
    /// Sends one call to the leader `masters` name, finding it first when `refresh_leader` (or
    /// when none is known). A transport failure is an error; the method's own error is in the
    /// response.
    #[allow(clippy::too_many_arguments)]
    fn call(
        &self,
        replication_group_id: &str,
        masters: &[HostPortPB],
        service: &'static str,
        method: &'static str,
        request: Vec<u8>,
        timeout: Duration,
        refresh_leader: bool,
    ) -> BoxFuture<'static, Result<Vec<u8>, Status>>;
}

/// The response's `error`, if any.
fn response_error(resp: &dyn protobuf::MessageDyn) -> Option<MasterErrorPB> {
    let field = resp.descriptor_dyn().field_by_name("error")?;
    let value = field.get_singular(resp)?;
    let message = value.to_message()?;
    let message: &dyn protobuf::MessageDyn = &*message;
    message.downcast_ref::<MasterErrorPB>().cloned()
}

/// Whether a master's answer means another master leads, or this one is not ready yet.
fn leader_moved(e: &MasterErrorPB) -> bool {
    matches!(
        e.code(),
        Code::NOT_THE_LEADER | Code::CATALOG_MANAGER_NOT_INITIALIZED
    ) || matches!(
        StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
        StatusCode::LeaderNotReadyToServe | StatusCode::LeaderHasNoLease
    )
}

fn retryable(s: &Status) -> bool {
    matches!(
        s.code,
        StatusCode::NetworkError
            | StatusCode::ServiceUnavailable
            | StatusCode::TimedOut
            | StatusCode::TryAgain
            | StatusCode::NotFound
    )
}

/// `SyncLeaderMasterRpc`: the call to the remote leader, retried on leader changes and transport
/// errors until `deadline`. The method's error is returned as the response's `error`, as the
/// caller of `XClusterClient` sees it.
pub async fn call_leader<Req: Message, Resp: MessageFull>(
    remote: &dyn RemoteMasters,
    replication_group_id: &str,
    masters: &[HostPortPB],
    service: &'static str,
    method: &'static str,
    req: &Req,
    deadline: Instant,
) -> Result<Resp, Status> {
    let bytes = req
        .write_to_bytes()
        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
    let mut refresh = false;
    let mut backoff = Duration::from_millis(50);
    loop {
        let left = deadline.saturating_duration_since(Instant::now());
        if left.is_zero() {
            return Err(Status::new(
                StatusCode::TimedOut,
                format!("{method} to the masters of the other universe timed out"),
            ));
        }
        let attempt = remote
            .call(
                replication_group_id,
                masters,
                service,
                method,
                bytes.clone(),
                left,
                refresh,
            )
            .await;
        match attempt {
            Ok(out) => {
                let resp = Resp::parse_from_bytes(&out)
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                match response_error(&resp) {
                    Some(e) if leader_moved(&e) => {
                        tracing::info!("{method}: the remote leader moved: {:?}", e.status);
                    }
                    _ => return Ok(resp),
                }
            }
            Err(s) if retryable(&s) => {
                tracing::info!("{method} to the remote masters failed, retrying: {s}");
            }
            Err(s) => return Err(s),
        }
        refresh = true;
        tokio::time::sleep(backoff.min(deadline.saturating_duration_since(Instant::now()))).await;
        backoff = (backoff * 2).min(Duration::from_secs(1));
    }
}

/// The error a remote master answered with, as a status.
pub(crate) fn remote_status(e: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
        e.status.message().to_string(),
    )
}

/// The `error` of a remote response as a result.
pub(crate) fn check<Resp: MessageFull>(resp: Resp) -> Result<Resp, Status> {
    match response_error(&resp) {
        Some(e) => Err(remote_status(&e)),
        None => Ok(resp),
    }
}

use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_replication::{
    AlterUniverseReplicationRequestPB, AlterUniverseReplicationResponsePB,
    DeleteUniverseReplicationRequestPB, DeleteUniverseReplicationResponsePB,
    IsSetupUniverseReplicationDoneRequestPB, IsSetupUniverseReplicationDoneResponsePB,
    SetupUniverseReplicationRequestPB, SetupUniverseReplicationResponsePB,
};
use yb_proto::pb::master_types::NamespaceIdentifierPB;

use crate::XCluster;

impl XCluster {
    async fn target_call<Req: Message, Resp: MessageFull>(
        &self,
        id: &str,
        masters: &[HostPortPB],
        method: &'static str,
        req: &Req,
        deadline: Instant,
    ) -> Result<Resp, Status> {
        call_leader(
            &*self.remote,
            id,
            masters,
            MASTER_SERVICE,
            method,
            req,
            deadline,
        )
        .await
    }

    /// `XClusterClient::SetupDbScopedUniverseReplication`: the target's universe uuid.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn setup_db_scoped_replication_on_target(
        &self,
        id: &str,
        target_masters: &[HostPortPB],
        source_masters: Vec<HostPortPB>,
        names: &[String],
        namespace_ids: &[&str],
        tables: &[String],
        streams: &[String],
        automatic_ddl_mode: bool,
        deadline: Instant,
    ) -> Result<String, Status> {
        let mut req = SetupUniverseReplicationRequestPB::new();
        req.set_replication_group_id(id.to_string());
        req.set_transactional(true);
        req.producer_master_addresses = source_masters;
        req.producer_table_ids = tables.to_vec();
        req.producer_bootstrap_ids = streams.to_vec();
        req.set_automatic_ddl_mode(automatic_ddl_mode);
        for (name, ns) in names.iter().zip(namespace_ids) {
            let mut n = NamespaceIdentifierPB::new();
            n.set_id(ns.as_bytes().to_vec());
            n.set_name(name.clone());
            n.database_type = Some(protobuf::EnumOrUnknown::new(
                YQLDatabase::YQL_DATABASE_PGSQL,
            ));
            req.producer_namespaces.push(n);
        }
        let resp: SetupUniverseReplicationResponsePB = self
            .target_call(
                id,
                target_masters,
                "SetupUniverseReplication",
                &req,
                deadline,
            )
            .await?;
        // The C++ client ignores AlreadyPresent here, but its RPC layer has already failed the
        // call on any response error; the target's duplicate-namespace error must reach the user.
        let resp = check(resp)?;
        if resp.universe_uuid().is_empty() {
            return Err(Status::new(
                StatusCode::IllegalState,
                "Universe UUID is empty",
            ));
        }
        Ok(resp.universe_uuid().to_string())
    }

    /// `XClusterClient::IsSetupUniverseReplicationDone`: None until done.
    pub(crate) async fn is_setup_done_on_target(
        &self,
        id: &str,
        target_masters: &[HostPortPB],
        setup_id: &str,
        deadline: Instant,
    ) -> Result<Option<Result<(), Status>>, Status> {
        let mut req = IsSetupUniverseReplicationDoneRequestPB::new();
        req.set_replication_group_id(setup_id.to_string());
        let resp: IsSetupUniverseReplicationDoneResponsePB = check(
            self.target_call(
                id,
                target_masters,
                "IsSetupUniverseReplicationDone",
                &req,
                deadline,
            )
            .await?,
        )?;
        if !resp.done() {
            return Ok(None);
        }
        let e = &resp.replication_error;
        if e.is_some() && protobuf::Enum::value(&e.code()) != 0 {
            return Ok(Some(Err(Status::new(
                StatusCode::from_i32(protobuf::Enum::value(&e.code())),
                e.message().to_string(),
            ))));
        }
        Ok(Some(Ok(())))
    }

    /// `XClusterClient::RemoveNamespaceFromUniverseReplication`.
    pub(crate) async fn remove_namespace_from_target(
        &self,
        id: &str,
        target_masters: &[HostPortPB],
        ns: &str,
        target_uuid: &[u8],
        deadline: Instant,
    ) -> Result<(), Status> {
        tracing::info!("Removing Namespace {ns} from target replication group {id}");
        let mut req = AlterUniverseReplicationRequestPB::new();
        req.set_replication_group_id(id.to_string());
        req.set_producer_namespace_id_to_remove(ns.to_string());
        if !target_uuid.is_empty() {
            req.set_universe_uuid(String::from_utf8_lossy(target_uuid).into_owned());
        }
        let _: AlterUniverseReplicationResponsePB = check(
            self.target_call(
                id,
                target_masters,
                "AlterUniverseReplication",
                &req,
                deadline,
            )
            .await?,
        )?;
        Ok(())
    }

    /// `XClusterClient::DeleteUniverseReplication`, ignoring errors of the target's cleanup.
    pub(crate) async fn delete_universe_replication_on_target(
        &self,
        id: &str,
        target_masters: &[HostPortPB],
        target_uuid: &[u8],
        oids: std::collections::BTreeMap<String, u32>,
        deadline: Instant,
    ) -> Result<(), Status> {
        let mut req = DeleteUniverseReplicationRequestPB::new();
        req.set_replication_group_id(id.to_string());
        req.set_ignore_errors(true);
        if !target_uuid.is_empty() {
            req.set_universe_uuid(String::from_utf8_lossy(target_uuid).into_owned());
        }
        req.producer_namespace_oids = oids.into_iter().collect();
        let _: DeleteUniverseReplicationResponsePB = check(
            self.target_call(
                id,
                target_masters,
                "DeleteUniverseReplication",
                &req,
                deadline,
            )
            .await?,
        )?;
        Ok(())
    }

    /// `XClusterClient::AddNamespaceToDbScopedUniverseReplication`.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn add_namespace_on_target(
        &self,
        id: &str,
        target_masters: &[HostPortPB],
        target_uuid: &[u8],
        name: &str,
        ns: &str,
        tables: &[String],
        streams: &[String],
        deadline: Instant,
    ) -> Result<(), Status> {
        let mut req = AlterUniverseReplicationRequestPB::new();
        req.set_replication_group_id(id.to_string());
        req.set_universe_uuid(String::from_utf8_lossy(target_uuid).into_owned());
        let n = req.producer_namespace_to_add.mut_or_insert_default();
        n.set_id(ns.as_bytes().to_vec());
        n.set_name(name.to_string());
        n.database_type = Some(protobuf::EnumOrUnknown::new(
            YQLDatabase::YQL_DATABASE_PGSQL,
        ));
        req.producer_table_ids_to_add = tables.to_vec();
        req.producer_bootstrap_ids_to_add = streams.to_vec();
        let resp: AlterUniverseReplicationResponsePB = self
            .target_call(
                id,
                target_masters,
                "AlterUniverseReplication",
                &req,
                deadline,
            )
            .await?;
        check(resp).map(|_| ())
    }
}
