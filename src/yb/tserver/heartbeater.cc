// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/heartbeater.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include <boost/function.hpp>

#include "yb/common/common_flags.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/master_heartbeat.proxy.h"
#include "yb/master/master_rpc.h"
#include "yb/master/master_types.pb.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/server_base.proxy.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/async_util.h"
#include "yb/util/callsite_profiling.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"
#include "yb/util/threadpool.h"

using namespace std::literals;

DEFINE_RUNTIME_int32(heartbeat_rpc_timeout_ms, 15000,
    "Timeout used for the TS->Master heartbeat RPCs.");
TAG_FLAG(heartbeat_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(heartbeat_interval_ms, 1000,
    "Interval at which the TS heartbeats to the master.");
TAG_FLAG(heartbeat_interval_ms, advanced);

DEFINE_UNKNOWN_int32(heartbeat_max_failures_before_backoff, 3,
             "Maximum number of consecutive heartbeat failures until the "
             "Tablet Server backs off to the normal heartbeat interval, "
             "rather than retrying.");
TAG_FLAG(heartbeat_max_failures_before_backoff, advanced);

DEFINE_test_flag(bool, tserver_disable_heartbeat, false, "Should heartbeat be disabled");

using yb::master::GetLeaderMasterRpc;
using yb::rpc::RpcController;
using std::shared_ptr;
using std::vector;

namespace yb {
namespace tserver {

// Most of the actual logic of the heartbeater is inside this inner class,
// to avoid having too many dependencies from the header itself.
//
// This is basically the "PIMPL" pattern.
class Heartbeater::Thread {
 public:
  Thread(
      const TabletServerOptions& opts, TabletServer* server,
      std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers);
  Thread(const Thread& other) = delete;
  void operator=(const Thread& other) = delete;

  Status Start();
  Status Stop();
  void TriggerASAP();

  void set_master_addresses(server::MasterAddressesPtr master_addresses) {
    std::lock_guard l(master_meta_mtx_);
    master_addresses_ = std::move(master_addresses);
    VLOG_WITH_PREFIX(1) << "Setting master addresses to " << yb::ToString(master_addresses_);
  }

  std::string get_leader_master_hostport() {
    std::lock_guard l(master_meta_mtx_);
    return leader_master_hostport_.ToString();
  }

 private:
  void RunThread();
  Status FindLeaderMaster(CoarseTimePoint deadline,
                          HostPort* leader_hostport) REQUIRES(master_meta_mtx_);;
  Status ConnectToMaster();
  int GetMinimumHeartbeatMillis() const;
  int GetMillisUntilNextHeartbeat() const;
  Status DoHeartbeat();
  Status TryHeartbeat();
  Status SetupRegistration(master::TSRegistrationPB* reg);
  void SetupCommonField(master::TSToMasterCommonPB* common);
  bool IsCurrentThread() const;
  const std::string& LogPrefix() const {
    return server_->LogPrefix();
  }

  server::MasterAddressesPtr get_master_addresses_unlocked() {
    CHECK_NOTNULL(master_addresses_.get());
    return master_addresses_;
  }

  server::MasterAddressesPtr get_master_addresses() {
    std::lock_guard l(master_meta_mtx_);
    return get_master_addresses_unlocked();
  }

  // Protecting master list and leader.
  std::mutex master_meta_mtx_;

  // The hosts/ports of masters that we may heartbeat to.
  //
  // We keep the HostPort around rather than a Sockaddr because the
  // masters may change IP addresses, and we'd like to re-resolve on
  // every new attempt at connecting.
  server::MasterAddressesPtr master_addresses_;

  // The server for which we are heartbeating.
  TabletServer* const server_;

  // Roundtrip time of previous heartbeat to yb-master.
  MonoDelta heartbeat_rtt_ = MonoDelta::kZero;

  // The actual running thread (NULL before it is started)
  scoped_refptr<yb::Thread> thread_;

  // Host and port of the most recent leader master.
  HostPort leader_master_hostport_;

  // Current RPC proxy to the leader master.
  std::unique_ptr<master::MasterHeartbeatProxy> proxy_;

  // The most recent response from a heartbeat.
  master::TSHeartbeatResponsePB last_hb_response_;

  // Full reports can take multiple heartbeats.
  // Flag to indicate if next heartbeat is part of a full report.
  bool sending_full_report_ = false;

  // The number of heartbeats which have failed in a row.
  // This is tracked so as to back-off heartbeating.
  int consecutive_failed_heartbeats_ = 0;

  // Mutex/condition pair to trigger the heartbeater thread
  // to either heartbeat early or exit.
  Mutex mutex_;
  ConditionVariable cond_;

  // Protected by mutex_.
  bool should_run_ = false;
  bool heartbeat_asap_ = false;

  rpc::Rpcs rpcs_;

  std::vector<std::unique_ptr<HeartbeatDataProvider>> data_providers_;
};

////////////////////////////////////////////////////////////
// Heartbeater
////////////////////////////////////////////////////////////

Heartbeater::Heartbeater(
    const TabletServerOptions& opts, TabletServer* server,
    std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers)
  : thread_(new Thread(opts, server, std::move(data_providers))) {
}

Heartbeater::~Heartbeater() {
  WARN_NOT_OK(Stop(), "Unable to stop heartbeater thread");
}

Status Heartbeater::Start() { return thread_->Start(); }
Status Heartbeater::Stop() { return thread_->Stop(); }
void Heartbeater::TriggerASAP() { thread_->TriggerASAP(); }

void Heartbeater::set_master_addresses(server::MasterAddressesPtr master_addresses) {
  thread_->set_master_addresses(std::move(master_addresses));
}

std::string Heartbeater::get_leader_master_hostport() {
  return thread_->get_leader_master_hostport();
}


////////////////////////////////////////////////////////////
// Heartbeater::Thread
////////////////////////////////////////////////////////////

Heartbeater::Thread::Thread(
    const TabletServerOptions& opts, TabletServer* server,
    std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers)
  : master_addresses_(opts.GetMasterAddresses()),
    server_(server),
    cond_(&mutex_),
    data_providers_(std::move(data_providers)) {
  CHECK_NOTNULL(master_addresses_.get());
  CHECK(!master_addresses_->empty());
  VLOG_WITH_PREFIX(1) << "Initializing heartbeater thread with master addresses: "
          << yb::ToString(master_addresses_);
}

namespace {

struct FindLeaderMasterData {
  HostPort result;
  Synchronizer sync;
  std::shared_ptr<GetLeaderMasterRpc> rpc;
};

void LeaderMasterCallback(const std::shared_ptr<FindLeaderMasterData>& data,
                          const Status& status,
                          const HostPort& result) {
  if (status.ok()) {
    data->result = result;
  }
  data->sync.StatusCB(status);
}

} // anonymous namespace

Status Heartbeater::Thread::FindLeaderMaster(CoarseTimePoint deadline,
                                             HostPort* leader_hostport) {
  Status s = Status::OK();
  const auto master_addresses = get_master_addresses_unlocked();
  if (master_addresses->size() == 1 && (*master_addresses)[0].size() == 1) {
    // "Shortcut" the process when a single master is specified.
    *leader_hostport = (*master_addresses)[0][0];
    return Status::OK();
  }
  auto master_sock_addrs = *master_addresses;
  if (master_sock_addrs.empty()) {
    return STATUS(NotFound, "Unable to resolve any of the master addresses!");
  }
  auto data = std::make_shared<FindLeaderMasterData>();
  data->rpc = std::make_shared<GetLeaderMasterRpc>(
      Bind(&LeaderMasterCallback, data),
      master_sock_addrs,
      deadline,
      server_->messenger(),
      &server_->proxy_cache(),
      &rpcs_,
      true /* should_timeout_to_follower_ */);
  data->rpc->SendRpc();
  auto status = data->sync.WaitFor(deadline - CoarseMonoClock::Now() + 1s);
  if (status.ok()) {
    *leader_hostport = data->result;
  }
  rpcs_.RequestAbortAll();
  return status;
}

Status Heartbeater::Thread::ConnectToMaster() {
  std::lock_guard l(master_meta_mtx_);
  auto deadline = CoarseMonoClock::Now() + FLAGS_heartbeat_rpc_timeout_ms * 1ms;
  // TODO send heartbeats without tablet reports to non-leader masters.
  Status s = FindLeaderMaster(deadline, &leader_master_hostport_);
  if (!s.ok()) {
    LOG_WITH_PREFIX(INFO) << "Find leader master " <<  leader_master_hostport_.ToString()
                          << " hit error " << s;
    return s;
  }

  // Reset report state if we have master failover.
  sending_full_report_ = false;

  // Pings are common for both Master and Tserver.
  auto new_proxy = std::make_unique<server::GenericServiceProxy>(
      &server_->proxy_cache(), leader_master_hostport_);

  // Ping the master to verify that it's alive.
  server::PingRequestPB req;
  server::PingResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));
  RETURN_NOT_OK_PREPEND(new_proxy->Ping(req, &resp, &rpc),
                        Format("Failed to ping master at $0", leader_master_hostport_));
  LOG_WITH_PREFIX(INFO) << "Connected to a leader master server at " << leader_master_hostport_;

  // Save state in the instance.
  proxy_ = std::make_unique<master::MasterHeartbeatProxy>(
      &server_->proxy_cache(), leader_master_hostport_);
  return Status::OK();
}

void Heartbeater::Thread::SetupCommonField(master::TSToMasterCommonPB* common) {
  common->mutable_ts_instance()->CopyFrom(server_->instance_pb());
}

Status Heartbeater::Thread::SetupRegistration(master::TSRegistrationPB* reg) {
  reg->Clear();
  RETURN_NOT_OK(server_->GetRegistration(reg->mutable_common()));
  auto* resources = reg->mutable_resources();
  resources->set_core_count(base::NumCPUs());
  int64_t tablet_overhead_limit = yb::tserver::ComputeTabletOverheadLimit();
  if (tablet_overhead_limit > 0) {
    resources->set_tablet_overhead_ram_in_bytes(tablet_overhead_limit);
  }
  return Status::OK();
}

int Heartbeater::Thread::GetMinimumHeartbeatMillis() const {
  // If we've failed a few heartbeats in a row, back off to the normal
  // interval, rather than retrying in a loop.
  if (consecutive_failed_heartbeats_ == FLAGS_heartbeat_max_failures_before_backoff) {
    LOG_WITH_PREFIX(WARNING) << "Failed " << consecutive_failed_heartbeats_  <<" heartbeats "
                             << "in a row: no longer allowing fast heartbeat attempts.";
  }

  return consecutive_failed_heartbeats_ > FLAGS_heartbeat_max_failures_before_backoff ?
    FLAGS_heartbeat_interval_ms : 0;
}

int Heartbeater::Thread::GetMillisUntilNextHeartbeat() const {
  // If the master needs something from us, we should immediately
  // send another heartbeat with that info, rather than waiting for the interval.
  if (sending_full_report_ ||
      last_hb_response_.needs_reregister() ||
      last_hb_response_.needs_full_tablet_report()) {
    return GetMinimumHeartbeatMillis();
  }

  return FLAGS_heartbeat_interval_ms;
}


Status Heartbeater::Thread::TryHeartbeat() {
  master::TSHeartbeatRequestPB req;

  SetupCommonField(req.mutable_common());
  if (last_hb_response_.needs_reregister()) {
    LOG_WITH_PREFIX(INFO) << "Registering TS with master...";
    RETURN_NOT_OK_PREPEND(SetupRegistration(req.mutable_registration()),
                          "Unable to set up registration");
  }

  if (last_hb_response_.needs_full_tablet_report()) {
    LOG_WITH_PREFIX(INFO) << "Sending a full tablet report to master...";
    server_->tablet_manager()->StartFullTabletReport(req.mutable_tablet_report());
    sending_full_report_ = true;
  } else {
    if (sending_full_report_) {
      LOG_WITH_PREFIX(INFO) << "Continuing full tablet report to master...";
    } else {
      VLOG_WITH_PREFIX(2) << "Sending an incremental tablet report to master...";
    }
    server_->tablet_manager()->GenerateTabletReport(req.mutable_tablet_report(),
                                                    !sending_full_report_ /* include_bootstrap */);
  }

  auto universe_uuid = VERIFY_RESULT(
      server_->fs_manager()->GetUniverseUuidFromTserverInstanceMetadata());
  if (!universe_uuid.empty()) {
    req.set_universe_uuid(universe_uuid);
  }

  req.mutable_tablet_report()->set_is_incremental(!sending_full_report_);
  req.set_num_live_tablets(server_->tablet_manager()->GetNumLiveTablets());
  req.set_leader_count(server_->tablet_manager()->GetLeaderCount());
  if (FLAGS_ysql_enable_db_catalog_version_mode) {
    auto fingerprint = server_->GetCatalogVersionsFingerprint();
    if (fingerprint.has_value()) {
      req.set_ysql_db_catalog_versions_fingerprint(*fingerprint);
    }
  }

  RETURN_NOT_OK(server_->XClusterPopulateMasterHeartbeatRequest(
      req, last_hb_response_.needs_full_tablet_report()));

  for (auto& data_provider : data_providers_) {
    data_provider->AddData(last_hb_response_, &req);
  }

  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));

  req.set_config_index(server_->GetCurrentMasterIndex());
  req.set_cluster_config_version(server_->cluster_config_version());
  auto result = server_->XClusterConfigVersion();
  if (result.ok()) {
    req.set_xcluster_config_version(*result);
  } else if (!result.status().IsNotFound()) {
    return result.status();
  }
  req.set_rtt_us(heartbeat_rtt_.ToMicroseconds());
  if (server_->has_faulty_drive()) {
    req.set_faulty_drive(true);
  }

  // Include the hybrid time of this tablet server in the heartbeat.
  auto* hybrid_clock = dynamic_cast<server::HybridClock*>(server_->Clock());
  HybridTime heartbeat_send_time;
  if (hybrid_clock) {
    heartbeat_send_time = hybrid_clock->Now();
    req.set_ts_hybrid_time(heartbeat_send_time.ToUint64());
    // Also include the physical clock time of this tablet server in the heartbeat.
    Result<PhysicalTime> now = hybrid_clock->physical_clock()->Now();
    if (!now.ok()) {
      YB_LOG_EVERY_N_SECS(WARNING, 10) << "Failed to read clock: " << now.status();
      req.set_ts_physical_time(0);
    } else {
      req.set_ts_physical_time(now->time_point);
    }
  } else {
    req.set_ts_hybrid_time(0);
    req.set_ts_physical_time(0);
  }

  req.set_auto_flags_config_version(server_->GetAutoFlagConfigVersion());

  {
    VLOG_WITH_PREFIX(2) << "Sending heartbeat:\n" << req.DebugString();
    heartbeat_rtt_ = MonoDelta::kZero;
    MonoTime start_time = MonoTime::Now();
    master::TSHeartbeatResponsePB resp;
    RETURN_NOT_OK_PREPEND(proxy_->TSHeartbeat(req, &resp, &rpc),
                          "Failed to send heartbeat");
    MonoTime end_time = MonoTime::Now();
    if (!resp.universe_uuid().empty()) {
      auto universe_uuid = VERIFY_RESULT(UniverseUuid::FromString(resp.universe_uuid()));
      RETURN_NOT_OK(server_->ValidateAndMaybeSetUniverseUuid(universe_uuid));
    }
    if (resp.has_op_lease_update()) {
      WARN_NOT_OK(
          server_->BootstrapDdlObjectLocks(resp.op_lease_update()),
          "Error bootstrapping object locks. Not expected.");
    }

    if (resp.has_error()) {
      switch (resp.error().code()) {
        case master::MasterErrorPB::NOT_THE_LEADER: {
          DCHECK(!resp.leader_master());
          // Treat a not-the-leader error code as leader_master=false.
          if (resp.leader_master()) {
            LOG_WITH_PREFIX(WARNING) << "Setting leader master to false for "
                                    << resp.error().code() << " code.";
            resp.set_leader_master(false);
          }
          break;
        }
        default:
          return StatusFromPB(resp.error().status());

      }
    }

    VLOG_WITH_PREFIX(2) << "Received heartbeat response:\n" << resp.DebugString();
    if (resp.has_master_config()) {
      LOG_WITH_PREFIX(INFO) << "Received heartbeat response with config " << resp.DebugString();

      RETURN_NOT_OK(server_->UpdateMasterAddresses(resp.master_config(), resp.leader_master()));
    }

    if (!resp.leader_master()) {
      // If the master is no longer a leader, reset proxy so that we can
      // determine the master and attempt to heartbeat during in the
      // next heartbeat interval.
      proxy_.reset();
      return STATUS_FORMAT(ServiceUnavailable, "Master is no longer the leader: $0", resp.error());
    }

    // Check for a universe key registry for encryption.
    if (resp.has_universe_key_registry()) {
      RETURN_NOT_OK(server_->SetUniverseKeyRegistry(resp.universe_key_registry()));
    }

    RETURN_NOT_OK(server_->XClusterHandleMasterHeartbeatResponse(resp));

    // At this point we know resp is a successful heartbeat response from the master so set it as
    // the last heartbeat response. This invalidates resp so we should use last_hb_response_ instead
    // below (hence using the nested scope for resp until here).
    last_hb_response_.Swap(&resp);
    heartbeat_rtt_ = end_time.GetDeltaSince(start_time);
  }

  if (last_hb_response_.has_cluster_uuid() && !last_hb_response_.cluster_uuid().empty()) {
    server_->set_cluster_uuid(last_hb_response_.cluster_uuid());
  }

  // The Master responds with the max entries for a single Tablet Report to avoid overwhelming it.
  if (last_hb_response_.has_tablet_report_limit()) {
    server_->tablet_manager()->SetReportLimit(last_hb_response_.tablet_report_limit());
  }

  if (last_hb_response_.needs_full_tablet_report()) {
    return STATUS(TryAgain, "");
  }

  // Handle TSHeartbeatResponsePB (e.g. tablets ack'd by master as processed)
  bool all_processed = req.tablet_report().remaining_tablet_count() == 0 &&
                       !last_hb_response_.tablet_report().processing_truncated();
  server_->tablet_manager()->MarkTabletReportAcknowledged(
      req.tablet_report().sequence_number(), last_hb_response_.tablet_report(), all_processed);

  // Trigger another heartbeat ASAP if we didn't process all tablets on this request.
  sending_full_report_ = sending_full_report_ && !all_processed;

  // Update the master's YSQL catalog version (i.e. if there were schema changes for YSQL objects).
  // We only check --enable_ysql when --ysql_enable_db_catalog_version_mode=true
  // to keep the logic backward compatible.
  if (FLAGS_ysql_enable_db_catalog_version_mode && FLAGS_enable_ysql) {
    // We never expect rolling gflag change of --ysql_enable_db_catalog_version_mode. In per-db
    // mode, we do not use ysql_catalog_version.
    DCHECK(!last_hb_response_.has_ysql_catalog_version());
    if (last_hb_response_.has_db_catalog_version_data()) {
      if (FLAGS_log_ysql_catalog_versions) {
        VLOG_WITH_FUNC(1) << "got master db catalog version data: "
                          << last_hb_response_.db_catalog_version_data().ShortDebugString();
      }
      server_->SetYsqlDBCatalogVersions(last_hb_response_.db_catalog_version_data());
    } else {
      // The master does not pass back any catalog versions. This can happen in
      // several cases:
      // * The fingerprints matched at master side which we assume tserver and
      //   master have identical catalog versions.
      // * If catalog versions cache is used, the cache is empty, either becuase it
      //   has never been populated yet, or because master reading of the table
      //   pg_yb_catalog_version has failed so the cache is cleared.
      // * If catalog versions cache is not used, master reading of the table
      //   pg_yb_catalog_version has failed, this is an unexpected case that is
      //   ignored by the heartbeat service at the master side.
      VLOG_WITH_FUNC(2) << "got no master catalog version data";
    }
  } else {
    if (last_hb_response_.has_ysql_catalog_version()) {
      DCHECK(!last_hb_response_.has_db_catalog_version_data());
      if (FLAGS_log_ysql_catalog_versions) {
        VLOG_WITH_FUNC(1) << "got master catalog version: "
                          << last_hb_response_.ysql_catalog_version()
                          << ", breaking version: "
                          << (last_hb_response_.has_ysql_last_breaking_catalog_version()
                              ? Format("$0", last_hb_response_.ysql_last_breaking_catalog_version())
                              : "(none)");
      }
      if (last_hb_response_.has_ysql_last_breaking_catalog_version()) {
        server_->SetYsqlCatalogVersion(last_hb_response_.ysql_catalog_version(),
                                       last_hb_response_.ysql_last_breaking_catalog_version());
      } else {
        /* Assuming all changes are breaking if last breaking version not explicitly set. */
        server_->SetYsqlCatalogVersion(last_hb_response_.ysql_catalog_version(),
                                       last_hb_response_.ysql_catalog_version());
      }
    } else if (last_hb_response_.has_db_catalog_version_data()) {
      // --ysql_enable_db_catalog_version_mode is still false on this tserver but master
      // already has --ysql_enable_db_catalog_version_mode=true. This can happen during
      // rolling upgrade from an old release where this gflag defaults to false to a new
      // release where this gflag defaults to true: the change of this gflag from false
      // to true happens on master first.
      // This can also happen when such an upgrade is rolled back: where yb-tservers are
      // first rolled back so the gflag changes to false before yb-masters.
      // Here we take care of both cases in the same way: extracting the global catalog
      // versions from last_hb_response_.db_catalog_version_data().
      if (FLAGS_log_ysql_catalog_versions) {
        VLOG_WITH_FUNC(1) << "got master db catalog version data: "
                          << last_hb_response_.db_catalog_version_data().ShortDebugString();
      }
      const auto& version_data = last_hb_response_.db_catalog_version_data();

      // The upgrade of the pg_yb_catalog_version table to one row per database happens in
      // the finalization phase of cluster upgrade, at that time all the tservers should have
      // completed rolling upgrade and have --ysql_enable_db_catalog_version_mode=true.
      // Since we still have FLAGS_ysql_enable_db_catalog_version_mode=false, the table
      // must still only have one row for template1.
      DCHECK_EQ(version_data.db_catalog_versions_size(), 1);
      DCHECK_EQ(version_data.db_catalog_versions(0).db_oid(), kTemplate1Oid);

      server_->SetYsqlCatalogVersion(version_data.db_catalog_versions(0).current_version(),
                                     version_data.db_catalog_versions(0).last_breaking_version());
    }
  }

  RETURN_NOT_OK(server_->tablet_manager()->UpdateSnapshotsInfo(last_hb_response_.snapshots_info()));

  if (last_hb_response_.has_transaction_tables_version()) {
    server_->UpdateTransactionTablesVersion(last_hb_response_.transaction_tables_version());
  }

  std::optional<AutoFlagsConfigPB> new_config;
  if (last_hb_response_.has_auto_flags_config()) {
    new_config = last_hb_response_.auto_flags_config();
  }
  server_->HandleMasterHeartbeatResponse(heartbeat_send_time, std::move(new_config));

  // Update the live tserver list.
  return server_->PopulateLiveTServers(last_hb_response_);
}

Status Heartbeater::Thread::DoHeartbeat() {
  if (PREDICT_FALSE(server_->fail_heartbeats_for_tests())) {
    return STATUS(IOError, "failing all heartbeats for tests");
  }

  if (PREDICT_FALSE(FLAGS_TEST_tserver_disable_heartbeat)) {
    YB_LOG_EVERY_N_SECS(INFO, 1) << "Heartbeat disabled for testing.";
    return Status::OK();
  }

  CHECK(IsCurrentThread());

  if (!proxy_) {
    VLOG_WITH_PREFIX(1) << "No valid master proxy. Connecting...";
    RETURN_NOT_OK(ConnectToMaster());
    DCHECK(proxy_);
  }

  for (;;) {
    auto status = TryHeartbeat();
    if (!status.ok() && status.IsTryAgain()) {
      continue;
    }
    return status;
  }

  return Status::OK();
}

void Heartbeater::Thread::RunThread() {
  CHECK(IsCurrentThread());
  VLOG_WITH_PREFIX(1) << "Heartbeat thread starting";

  // Config the "last heartbeat response" to indicate that we need to register
  // -- since we've never registered before, we know this to be true.
  last_hb_response_.set_needs_reregister(true);

  // Have the Master request a full tablet report on 2nd HB.
  last_hb_response_.set_needs_full_tablet_report(false);

  while (true) {
    MonoTime next_heartbeat = MonoTime::Now();
    next_heartbeat.AddDelta(MonoDelta::FromMilliseconds(GetMillisUntilNextHeartbeat()));

    // Wait for either the heartbeat interval to elapse, or for an "ASAP" heartbeat,
    // or for the signal to shut down.
    {
      MutexLock l(mutex_);
      while (true) {
        MonoDelta remaining = next_heartbeat.GetDeltaSince(MonoTime::Now());
        if (remaining.ToMilliseconds() <= 0 ||
            heartbeat_asap_ ||
            !should_run_) {
          break;
        }
        cond_.TimedWait(remaining);
      }

      heartbeat_asap_ = false;

      if (!should_run_) {
        VLOG_WITH_PREFIX(1) << "Heartbeat thread finished";
        return;
      }
    }

    Status s = DoHeartbeat();
    if (!s.ok()) {
      const auto master_addresses = get_master_addresses();
      LOG_WITH_PREFIX(WARNING)
          << "Failed to heartbeat to " << get_leader_master_hostport()
          << ": " << s << " tries=" << consecutive_failed_heartbeats_
          << ", num=" << master_addresses->size()
          << ", masters=" << yb::ToString(master_addresses)
          << ", code=" << s.CodeAsString();
      consecutive_failed_heartbeats_++;
      // If there's multiple masters...
      if (master_addresses->size() > 1 || (*master_addresses)[0].size() > 1) {
        // If we encountered a network error (e.g., connection refused) or reached our failure
        // threshold.  Try determining the leader master again.  Heartbeats function as a watchdog,
        // so timeouts should be considered normal failures.
        if (s.IsNetworkError() ||
            consecutive_failed_heartbeats_ == FLAGS_heartbeat_max_failures_before_backoff) {
          proxy_.reset();
        }
      }
      continue;
    }
    consecutive_failed_heartbeats_ = 0;
  }
}

bool Heartbeater::Thread::IsCurrentThread() const {
  return thread_.get() == yb::Thread::current_thread();
}

Status Heartbeater::Thread::Start() {
  CHECK(thread_ == nullptr);

  should_run_ = true;
  return yb::Thread::Create("heartbeater", "heartbeat",
      &Heartbeater::Thread::RunThread, this, &thread_);
}

Status Heartbeater::Thread::Stop() {
  if (!thread_) {
    return Status::OK();
  }

  {
    MutexLock l(mutex_);
    should_run_ = false;
    YB_PROFILE(cond_.Signal());
  }

  rpcs_.Shutdown();

  RETURN_NOT_OK(ThreadJoiner(thread_.get()).Join());
  thread_ = nullptr;
  return Status::OK();
}

void Heartbeater::Thread::TriggerASAP() {
  MutexLock l(mutex_);
  heartbeat_asap_ = true;
  YB_PROFILE(cond_.Signal());
}


const std::string& HeartbeatDataProvider::LogPrefix() const {
  return server_.LogPrefix();
}

void PeriodicalHeartbeatDataProvider::AddData(
    const master::TSHeartbeatResponsePB& last_resp, master::TSHeartbeatRequestPB* req) {
  // Save that fact that we will need to send a full report the next time we run.
  needs_full_tablet_report_ |= last_resp.needs_full_tablet_report();

  if (prev_run_time_ + Period() < CoarseMonoClock::Now()) {
    DoAddData(needs_full_tablet_report_, req);
    prev_run_time_ = CoarseMonoClock::Now();
    needs_full_tablet_report_ = false;
  }
}

} // namespace tserver
} // namespace yb
