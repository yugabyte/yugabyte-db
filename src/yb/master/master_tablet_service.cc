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
//

#include "yb/master/master_tablet_service.h"

#include <optional>

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_auth_catalog_read.h"

#include "yb/consensus/consensus.h"

#include "yb/dockv/doc_key.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/ysql/ysql_manager.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/service_util.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_test_flag(int32, ysql_catalog_write_rejection_percentage, 0,
    "Reject specified percentage of writes to the YSQL catalog tables.");

DEFINE_test_flag(bool, ysql_require_force_catalog_modifications, false,
    "Fail YSQL catalog writes requests if force_catalog_modifications is not set.");

DECLARE_bool(ysql_enable_auth_catalog_follower_reads);

METRIC_DEFINE_counter(
    server, ysql_auth_catalog_follower_reads, "YSQL authentication catalog follower reads",
    yb::MetricUnit::kRequests,
    "Validated authentication catalog reads admitted on a nonleader after the snapshot is safe.");
METRIC_DEFINE_counter(
    server, ysql_auth_catalog_leader_reads, "YSQL authentication catalog leader reads",
    yb::MetricUnit::kRequests,
    "Validated authentication catalog reads admitted on a leader after the snapshot is safe.");

using namespace std::chrono_literals;

namespace yb {
namespace master {

// Only SysTablet 0 is bootstrapped on all master peers, and only the master leader
// reads other sys tablets. We check only for tablet 0 so as to have same readiness
// level across all masters.
// Note: If this value changes, then IsTabletServerReady has to be revisited.
constexpr int NUM_TABLETS_SYS_CATALOG = 1;

namespace {

// Allow the ordinary 500ms safe-time propagation cadence while reserving leader fallback time.
constexpr auto kAuthCatalogFollowerWait = 1s;

Status ValidateAuthCatalogRead(const tserver::ReadRequestMsg& req) {
  SCHECK(req.tablet_id() == kSysCatalogTabletId && req.pgsql_batch_size() > 0 &&
             req.redis_batch_size() == 0 && req.ql_batch_size() == 0 &&
             !req.has_transaction() && !req.has_subtransaction() && !req.use_async_write() &&
             !req.has_pending_async_write_op_id(),
         InvalidArgument, "Invalid authentication catalog read envelope");
  const auto read_time = ReadHybridTime::FromReadTimePB(req);
  SCHECK(req.has_read_time() && !read_time.read.is_special() &&
             read_time == ReadHybridTime::SingleTime(read_time.read),
         InvalidArgument, "Authentication catalog reads require one explicit fixed snapshot");
  for (const auto& pg_req : req.pgsql_batch()) {
    SCHECK(IsYsqlAuthCatalogRead(pg_req) &&
               YsqlAuthCatalogPagingMatchesReadTime(pg_req, read_time.read),
           InvalidArgument, "Request is outside the authentication catalog read scope");
  }
  return Status::OK();
}

}  // namespace

MasterTabletServiceImpl::MasterTabletServiceImpl(MasterTabletServer* server, Master* master)
    : TabletServiceImpl(server), master_(master),
      auth_catalog_follower_reads_(
          METRIC_ysql_auth_catalog_follower_reads.Instantiate(master->metric_entity())),
      auth_catalog_leader_reads_(
          METRIC_ysql_auth_catalog_leader_reads.Instantiate(master->metric_entity())) {
}

Result<std::shared_ptr<tablet::AbstractTablet>> MasterTabletServiceImpl::GetTabletForRead(
  TabletIdView tablet_id, tablet::TabletPeerPtr tablet_peer,
  YBConsistencyLevel consistency_level, tserver::AllowSplitTablet allow_split_tablet,
  tserver::ReadResponseMsg* resp) {
  if (tablet_id == kSysCatalogTabletId) {
    return tserver::GetTablet(
        master_->tablet_server(), tablet_id, std::move(tablet_peer), consistency_level,
        allow_split_tablet, resp);
  }
  // Virtual system tablets have no tablet peer.
  return master_->catalog_manager()->GetSystemTablet(tablet_id);
}

void MasterTabletServiceImpl::AcquireObjectLocks(
    const tserver::AcquireObjectLockRequestPB* req, tserver::AcquireObjectLockResponsePB* resp,
    rpc::RpcContext context) {
  context.RespondRpcFailure(
      rpc::ErrorStatusPB::ERROR_APPLICATION,
      STATUS(NotSupported, "AcquireObjectLocks is not implemented at masters"));
}

void MasterTabletServiceImpl::ReleaseObjectLocks(
    const tserver::ReleaseObjectLockRequestPB* req, tserver::ReleaseObjectLockResponsePB* resp,
    rpc::RpcContext context) {
  context.RespondRpcFailure(
      rpc::ErrorStatusPB::ERROR_APPLICATION,
      STATUS(NotSupported, "ReleaseObjectLocks is not implemented at masters"));
}

Result<tserver::GetYSQLLeaseInfoResponsePB> MasterTabletServiceImpl::GetYSQLLeaseInfo(
    const tserver::GetYSQLLeaseInfoRequestPB& req, CoarseTimePoint deadline) {
  return STATUS(NotSupported, "GetYSQLLeaseInfo is not implemented at masters");
}

void MasterTabletServiceImpl::Read(const tserver::ReadRequestMsg* req,
                                   tserver::ReadResponseMsg* resp,
                                   rpc::RpcContext context) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  const bool auth_read = req->ysql_auth_catalog_read();
  const bool follower_read =
      auth_read && req->consistency_level() == YBConsistencyLevel::CONSISTENT_PREFIX;
  if (follower_read) {
    if (!l.CheckIsInitializedOrRespondTServer(resp, &context)) {
      return;
    }
  } else if (!l.CheckIsInitializedAndIsLeaderOrRespondTServer(resp, &context)) {
    return;
  }

  if (auth_read) {
    auto prepare = [&]() -> Result<bool> {
      RETURN_NOT_OK(ValidateAuthCatalogRead(*req));
      SCHECK(!master_->IsShellMode(), IllegalState, "Master is in shell mode");
      SCHECK(!follower_read || FLAGS_ysql_enable_auth_catalog_follower_reads, IllegalState,
             "Authentication catalog follower reads are disabled");
      auto peer_tablet = VERIFY_RESULT(tserver::LookupTabletPeer(
          master_->tablet_server(), req->tablet_id()));
      auto tablet = VERIFY_RESULT(GetTabletForRead(
          req->tablet_id(), peer_tablet.tablet_peer, req->consistency_level(),
          tserver::AllowSplitTablet::kFalse, resp));
      auto deadline = context.GetClientDeadline();
      if (follower_read) {
        const auto now = CoarseMonoClock::Now();
        SCHECK(deadline > now, IllegalState, "No time remains for authentication follower read");
        deadline = std::min(now + kAuthCatalogFollowerWait, now + (deadline - now) / 2);
      }
      const auto safe_time = tablet->SafeTime(
          tablet::RequireLease(!follower_read),
          ReadHybridTime::FromReadTimePB(*req).read, deadline);
      if (!safe_time.ok()) {
        // A follower timeout must remain retryable without consuming the leader's budget.
        if (follower_read) {
          return STATUS_FORMAT(IllegalState, "Authentication catalog snapshot is not safe: $0",
                               safe_time.status());
        }
        return safe_time.status();
      }
      auto consensus = VERIFY_RESULT(peer_tablet.tablet_peer->GetConsensus());
      if (follower_read) {
        // Safe time does not wait for non-MVCC operations such as sys-catalog restore. Each
        // operation at or before T arrives before safe time can reach T, so serve only after
        // applying everything received; otherwise the client retries the leader at T.
        const auto received = consensus->GetLastReceivedOpId();
        SCHECK_GE(consensus->GetLastAppliedOpId().index, received.index, IllegalState,
                  "Authentication catalog snapshot is not safe: unapplied operations");
      } else {
        const auto leader_state = consensus->GetLeaderState();
        SCHECK(leader_state.ok() && leader_state.term == l.epoch().leader_term, IllegalState,
               "Master leadership changed while waiting for authentication catalog snapshot");
      }
      return consensus->role() == PeerRole::LEADER;
    };
    auto is_leader = prepare();
    if (!is_leader.ok()) {
      tserver::SetupErrorAndRespond(resp->mutable_error(), is_leader.status(), &context);
      return;
    }
    // CONSISTENT_PREFIX can reach the leader too; classify the actual serving replica.
    (*is_leader ? auth_catalog_leader_reads_ : auth_catalog_follower_reads_)->Increment();
  }

  tserver::TabletServiceImpl::Read(req, resp, std::move(context));
}

void MasterTabletServiceImpl::Write(const tserver::WriteRequestMsg* req,
                                    tserver::WriteResponseMsg* resp,
                                    rpc::RpcContext context) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  if (!l.CheckIsInitializedAndIsLeaderOrRespondTServer(resp, &context)) {
    return;
  }

  if (PREDICT_FALSE(FLAGS_TEST_ysql_catalog_write_rejection_percentage > 0) &&
      req->pgsql_write_batch_size() > 0 &&
      RandomUniformInt(1, 99) <= FLAGS_TEST_ysql_catalog_write_rejection_percentage) {
    context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
        STATUS(InternalError, "Injected random failure for testing."));
      return;
  }

  bool log_versions = false;
  std::unordered_set<uint32_t> db_oids;
  for (const auto &pg_req : req->pgsql_write_batch()) {
    if (FLAGS_TEST_ysql_require_force_catalog_modifications &&
        !pg_req.force_catalog_modifications()) {
      context.RespondRpcFailure(
          rpc::ErrorStatusPB::ERROR_APPLICATION,
          STATUS(
              InternalError,
              "Catalog update without force_catalog_modifications when "
              "TEST_ysql_require_force_catalog_modifications is set"));
      return;
    }

    {
      auto write_allowed = master_->ysql_manager_impl().ValidateWriteToCatalogTableAllowed(
          pg_req.table_id(), pg_req.force_catalog_modifications());
      if (!write_allowed.ok()) {
        VLOG(1) << "Write to catalog table while it is not allowed: " << pg_req.ShortDebugString();
        context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION, std::move(write_allowed));
        return;
      }
    }

    if (pg_req.is_ysql_catalog_change_using_protobuf()) {
      const auto& res = master_->catalog_manager()->IncrementYsqlCatalogVersion();
      if (!res.ok()) {
        context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
            STATUS(InternalError, "Failed to increment YSQL catalog version"));
      }
    } else if (FLAGS_log_ysql_catalog_versions && pg_req.table_id() == kPgYbCatalogVersionTableId) {
      log_versions = true;
      // The contents of req->pgsql_write_batch() are freed after the next call to
      // tserver::TabletServiceImpl::Write, save db_oid to use for later debugging log.

      // The write op to increment the catalog version number is special and may not have
      // set any of ysql_catalog_version, ysql_db_catalog_version, and ysql_db_oid. Therefore
      // we need to get db oid by decoding from ybctid.
      dockv::DocKey doc_key;
      if (!pg_req.has_ybctid_column_value() ||
          !doc_key.FullyDecodeFrom(pg_req.ybctid_column_value().value().binary_value()).ok() ||
          doc_key.range_group().size() != 1) {
        context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
            STATUS(InternalError, "Failed to get db oid"));
      }
      const uint32_t db_oid = doc_key.range_group()[0].GetUInt32();
      // In per-db catalog version mode, there can be multiple write ops to the
      // pg_yb_catalog_version table for different rows. We do not expect multiple write ops
      // that write to the same db_oid because db_oid is the primary key and a duplicate-key
      // error should be reported.
      if (!db_oids.insert(db_oid).second) {
        LOG(DFATAL) << "Unexpected multiple writes to db_oid "
                    << db_oid << ", req: " << req->ShortDebugString();
      }
    }
  }

  VLOG(5) << "Master write req: " << req->ShortDebugString();
  tserver::TabletServiceImpl::Write(req, resp, std::move(context));

  if (log_versions) {
    uint64_t catalog_version;
    uint64_t last_breaking_version;
    // The above Write is async, so delay a bit to hopefully read the newly written values.  If the
    // delay was not sufficient, it's not a big deal since this is just for logging.
    // The lookups below can also legitimately fail while the sys catalog is being restored by
    // PITR, so they must not be fatal.
    SleepFor(100ms);
    if (!db_oids.empty()) {
      for (const auto db_oid : db_oids) {
        if (!master_->catalog_manager()->GetYsqlDBCatalogVersion(db_oid, &catalog_version,
                                                                 &last_breaking_version).ok()) {
          LOG_WITH_FUNC(WARNING) << "failed to get db catalog version for "
                                 << db_oid << ", ignoring";
        } else {
          LOG_WITH_FUNC(INFO) << "db catalog version for " << db_oid << ": "
                              << catalog_version << ", breaking version: "
                              << last_breaking_version;
        }
      }
    } else {
      if (!master_->catalog_manager()->GetYsqlCatalogVersion(&catalog_version,
                                                             &last_breaking_version).ok()) {
        LOG_WITH_FUNC(WARNING) << "failed to get catalog version, ignoring";
      } else {
        LOG_WITH_FUNC(INFO) << "catalog version: " << catalog_version << ", breaking version: "
                            << last_breaking_version;
      }
    }
  }
}

void MasterTabletServiceImpl::IsTabletServerReady(
    const tserver::IsTabletServerReadyRequestPB* req,
    tserver::IsTabletServerReadyResponsePB* resp,
    rpc::RpcContext context) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  int total_tablets = NUM_TABLETS_SYS_CATALOG;
  resp->set_total_tablets(total_tablets);
  resp->set_num_tablets_not_running(total_tablets);

  // Tablet 0 being ready corresponds to state_ = kRunning in catalog manager.
  // If catalog_status_ in not OK, then catalog manager state_ is not kRunning.
  if (!l.CheckIsInitializedOrRespondTServer(resp, &context, false /* set_error */)) {
    LOG(INFO) << "Zero tablets not running out of " << total_tablets;
  } else {
    LOG(INFO) << "All " << total_tablets << " tablets running.";
    resp->set_num_tablets_not_running(0);
    context.RespondSuccess();
  }
}

namespace {

void HandleUnsupportedMethod(const char* method_name, rpc::RpcContext* context) {
  context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
                             STATUS_FORMAT(NotSupported, "$0 Not Supported!", method_name));
}

}  // namespace

void MasterTabletServiceImpl::ListMasterServers(
    const tserver::ListMasterServersRequestPB* req, tserver::ListMasterServersResponsePB* resp,
    rpc::RpcContext context) {
  HandleUnsupportedMethod("ListMasterServers", &context);
}

void MasterTabletServiceImpl::ListTablets(const tserver::ListTabletsRequestPB* req,
                                          tserver::ListTabletsResponsePB* resp,
                                          rpc::RpcContext context)  {
  HandleUnsupportedMethod("ListTablets", &context);
}

void MasterTabletServiceImpl::ListTabletsForTabletServer(
    const tserver::ListTabletsForTabletServerRequestPB* req,
    tserver::ListTabletsForTabletServerResponsePB* resp,
    rpc::RpcContext context)  {
  HandleUnsupportedMethod("ListTabletsForTabletServer", &context);
}

void MasterTabletServiceImpl::GetLogLocation(const tserver::GetLogLocationRequestPB* req,
                                             tserver::GetLogLocationResponsePB* resp,
                                             rpc::RpcContext context)  {
  HandleUnsupportedMethod("GetLogLocation", &context);
}

void MasterTabletServiceImpl::Checksum(const tserver::ChecksumRequestPB* req,
                                       tserver::ChecksumResponsePB* resp,
                                       rpc::RpcContext context)  {
  HandleUnsupportedMethod("Checksum", &context);
}

void MasterTabletServiceImpl::AdminExecutePgsql(
    const tserver::AdminExecutePgsqlRequestPB* req, tserver::AdminExecutePgsqlResponsePB* resp,
    rpc::RpcContext context) {
  HandleUnsupportedMethod("AdminExecutePgsql", &context);
}

} // namespace master
} // namespace yb
