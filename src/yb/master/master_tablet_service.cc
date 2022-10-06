// Copyright (c) YugaByte, Inc.
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

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_test_flag(int32, ysql_catalog_write_rejection_percentage, 0,
                 "Reject specified percentage of writes to the YSQL catalog tables.");
TAG_FLAG(TEST_ysql_catalog_write_rejection_percentage, runtime);

DECLARE_bool(TEST_enable_db_catalog_version_mode);

using namespace std::chrono_literals;

namespace yb {
namespace master {

// Only SysTablet 0 is bootstrapped on all master peers, and only the master leader
// reads other sys tablets. We check only for tablet 0 so as to have same readiness
// level across all masters.
// Note: If this value changes, then IsTabletServerReady has to be revisited.
constexpr int NUM_TABLETS_SYS_CATALOG = 1;

MasterTabletServiceImpl::MasterTabletServiceImpl(MasterTabletServer* server, Master* master)
    : TabletServiceImpl(server), master_(master) {
}

Result<std::shared_ptr<tablet::AbstractTablet>> MasterTabletServiceImpl::GetTabletForRead(
  const TabletId& tablet_id, tablet::TabletPeerPtr tablet_peer,
  YBConsistencyLevel consistency_level, tserver::AllowSplitTablet allow_split_tablet) {
  // Ignore looked_up_tablet_peer.

  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  RETURN_NOT_OK(l.first_failed_status());

  return master_->catalog_manager()->GetSystemTablet(tablet_id);
}

void MasterTabletServiceImpl::Write(const tserver::WriteRequestPB* req,
                                    tserver::WriteResponsePB* resp,
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
  uint32_t db_oid = kPgInvalidOid;
  for (const auto& pg_req : req->pgsql_write_batch()) {
    if (pg_req.is_ysql_catalog_change()) {
      const auto &res = master_->catalog_manager()->IncrementYsqlCatalogVersion();
      if (!res.ok()) {
        context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
            STATUS(InternalError, "Failed to increment YSQL catalog version"));
      }
    } else if (FLAGS_log_ysql_catalog_versions && pg_req.table_id() == kPgYbCatalogVersionTableId) {
      log_versions = true;
      // The contents of req->pgsql_write_batch() are freed after the next call to
      // tserver::TabletServiceImpl::Write, save db_oid to use for later debugging log.
      if (FLAGS_TEST_enable_db_catalog_version_mode) {
        for (const auto& pg_req : req->pgsql_write_batch()) {
          if (db_oid == kPgInvalidOid) {
            db_oid = pg_req.ysql_db_oid();
          } else {
            DCHECK_EQ(db_oid, pg_req.ysql_db_oid());
          }
        }
      }
    }
  }

  tserver::TabletServiceImpl::Write(req, resp, std::move(context));

  if (log_versions) {
    uint64_t catalog_version;
    uint64_t last_breaking_version;
    // The above Write is async, so delay a bit to hopefully read the newly written values.  If the
    // delay was not sufficient, it's not a big deal since this is just for logging.
    SleepFor(100ms);
    if (FLAGS_TEST_enable_db_catalog_version_mode) {
      if (!master_->catalog_manager()->GetYsqlDBCatalogVersion(db_oid, &catalog_version,
                                                               &last_breaking_version).ok()) {
        LOG_WITH_FUNC(ERROR) << "failed to get db catalog version for "
                             << db_oid << ", ignoring";
      } else {
        LOG_WITH_FUNC(INFO) << "db catalog version for " << db_oid << ": "
                            << catalog_version << ", breaking version: "
                            << last_breaking_version;
      }
    } else {
      if (!master_->catalog_manager()->GetYsqlCatalogVersion(&catalog_version,
                                                             &last_breaking_version).ok()) {
        LOG_WITH_FUNC(ERROR) << "failed to get catalog version, ignoring";
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

} // namespace

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

} // namespace master
} // namespace yb
