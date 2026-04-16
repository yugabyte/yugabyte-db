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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/version_info.h"
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

#include "yb/tserver/master_leader_poller.h"
#include "yb/tserver/service_util.h"
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
DEFINE_test_flag(bool, tserver_disable_catalog_refresh_on_heartbeat, false,
    "When set, disable trigger of catalog cache refresh from tserver-master heartbeat path.");

using yb::master::GetLeaderMasterRpc;
using yb::rpc::RpcController;
using std::shared_ptr;
using std::vector;

namespace yb::tserver {

class HeartbeatPoller : public MasterLeaderPollerInterface {
 public:
  HeartbeatPoller(
      TabletServer& server, MasterLeaderFinder& finder,
      std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers);
  ~HeartbeatPoller() = default;
  Status Poll() override;
  MonoDelta IntervalToNextPoll(int32_t consecutive_failures) override;
  void Init() override;
  void ResetProxy() override;
  std::string name() override;
  const std::string& LogPrefix() const override;

 private:
  Status TryHeartbeat();
  Status SetupRegistration(master::TSRegistrationPB* reg);
  void SetupCommonField(master::TSToMasterCommonPB* common);
  MonoDelta GetMinimumHeartbeatMillis(int32_t consecutive_failures) const;

  TabletServer& server_;
  MasterLeaderFinder& finder_;
  std::optional<master::MasterHeartbeatProxy> proxy_;

  // Roundtrip time of previous heartbeat to yb-master.
  MonoDelta heartbeat_rtt_ = MonoDelta::kZero;

  // The most recent response from a heartbeat.
  master::TSHeartbeatResponsePB last_hb_response_;

  // Full reports can take multiple heartbeats.
  // This field has value of the sequence number of the first heartbeat in this scenario.
  std::optional<int32> full_report_seq_no_;

  std::vector<std::unique_ptr<HeartbeatDataProvider>> data_providers_;
};

class Heartbeater::Impl {
 public:
  Impl(
      const TabletServerOptions& opts, TabletServer& server,
      std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers);
  Impl(const Impl& other) = delete;
  void operator=(const Impl& other) = delete;

  Status Start();
  void Shutdown();
  void TriggerASAP();

  void set_master_addresses(server::MasterAddressesPtr master_addresses) {
    VLOG_WITH_PREFIX(1) << "Setting master addresses to " << yb::ToString(master_addresses);
    finder_.set_master_addresses(std::move(master_addresses));
  }

  HostPort get_master_leader_hostport() { return finder_.get_master_leader_hostport(); }

 private:
  const std::string& LogPrefix() const { return server_.LogPrefix(); }

  TabletServer& server_;
  MasterLeaderFinder finder_;
  std::unique_ptr<HeartbeatPoller> poller_;
  MasterLeaderPollScheduler poll_scheduler_;
};

////////////////////////////////////////////////////////////
// Heartbeater
////////////////////////////////////////////////////////////

Heartbeater::Heartbeater(
    const TabletServerOptions& opts, TabletServer* server,
    std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers)
    : impl_(std::make_unique<Impl>(opts, *server, std::move(data_providers))) {
}

Heartbeater::~Heartbeater() {
  Shutdown();
}

Status Heartbeater::Start() { return impl_->Start(); }

void Heartbeater::Shutdown() {
  return impl_->Shutdown();
}

void Heartbeater::TriggerASAP() { impl_->TriggerASAP(); }

void Heartbeater::set_master_addresses(server::MasterAddressesPtr master_addresses) {
  impl_->set_master_addresses(std::move(master_addresses));
}

HostPort Heartbeater::get_master_leader_hostport() {
  return impl_->get_master_leader_hostport();
}


////////////////////////////////////////////////////////////
// Heartbeater::Impl
////////////////////////////////////////////////////////////

Heartbeater::Impl::Impl(
    const TabletServerOptions& opts, TabletServer& server,
    std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers)
    : server_(server),
      finder_(server.messenger(), server.proxy_cache(), opts.GetMasterAddresses()),
      poller_(std::make_unique<HeartbeatPoller>(server_, finder_, std::move(data_providers))),
      poll_scheduler_(finder_, *poller_.get()) {
  auto master_addresses = CHECK_NOTNULL(finder_.get_master_addresses());
  CHECK(!master_addresses->empty());
  VLOG_WITH_PREFIX(1) << "Initializing heartbeater thread with master addresses: "
                      << AsString(master_addresses);
}

Status Heartbeater::Impl::Start() {
  return poll_scheduler_.Start();
}

void Heartbeater::Impl::Shutdown() {
  poll_scheduler_.Shutdown();
}

void Heartbeater::Impl::TriggerASAP() {
  poll_scheduler_.TriggerASAP();
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

////////////////////////////////////////////////////////////
// HeartbeatPoller
////////////////////////////////////////////////////////////

HeartbeatPoller::HeartbeatPoller(
    TabletServer& server, MasterLeaderFinder& finder,
    std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers)
    : server_(server), finder_(finder), data_providers_(std::move(data_providers)) {}

Status HeartbeatPoller::Poll() {
  if (PREDICT_FALSE(server_.fail_heartbeats_for_tests())) {
    return STATUS(IOError, "failing all heartbeats for tests");
  }

  if (PREDICT_FALSE(FLAGS_TEST_tserver_disable_heartbeat)) {
    YB_LOG_EVERY_N_SECS(INFO, 1) << "Heartbeat disabled for testing.";
    return Status::OK();
  }

  if (!proxy_) {
    VLOG_WITH_PREFIX(1) << "No valid master proxy. Connecting...";
    proxy_ = VERIFY_RESULT(finder_.CreateProxy<master::MasterHeartbeatProxy>(
        MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms)));
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

MonoDelta HeartbeatPoller::IntervalToNextPoll(int32_t consecutive_failures) {
  if (full_report_seq_no_ || last_hb_response_.needs_reregister() ||
      last_hb_response_.needs_full_tablet_report()) {
    return GetMinimumHeartbeatMillis(consecutive_failures);
  }

  return MonoDelta::FromMilliseconds(FLAGS_heartbeat_interval_ms);
}

void HeartbeatPoller::Init() {
  // Config the "last heartbeat response" to indicate that we need to register
  // -- since we've never registered before, we know this to be true.
  last_hb_response_.set_needs_reregister(true);

  // Have the Master request a full tablet report on 2nd HB.
  last_hb_response_.set_needs_full_tablet_report(false);
}

void HeartbeatPoller::ResetProxy() { proxy_ = std::nullopt; }

std::string HeartbeatPoller::name() {
  return "heartbeat";
}

const std::string& HeartbeatPoller::LogPrefix() const { return server_.LogPrefix(); }

Status HeartbeatPoller::TryHeartbeat() {
  master::TSHeartbeatRequestPB req;

  SetupCommonField(req.mutable_common());
  if (last_hb_response_.needs_reregister()) {
    LOG_WITH_PREFIX(INFO) << "Registering TS with master...";
    RETURN_NOT_OK_PREPEND(SetupRegistration(req.mutable_registration()),
                          "Unable to set up registration");
  }

  auto& tablet_report = *req.mutable_tablet_report();
  if (last_hb_response_.needs_full_tablet_report()) {
    LOG_WITH_PREFIX(INFO) << "Sending a full tablet report to master...";
    server_.tablet_manager()->StartFullTabletReport(&tablet_report);
    full_report_seq_no_ = tablet_report.sequence_number();
    tablet_report.set_full_report_seq_no(*full_report_seq_no_);
  } else {
    server_.tablet_manager()->GenerateTabletReport(
        &tablet_report, !full_report_seq_no_ /* include_bootstrap */);
    if (full_report_seq_no_) {
      LOG_WITH_PREFIX(INFO) << "Continuing full tablet report to master...";
      tablet_report.set_full_report_seq_no(*full_report_seq_no_);
    } else {
      VLOG_WITH_PREFIX(2) << "Sending an incremental tablet report to master...";
    }
  }

  auto universe_uuid = VERIFY_RESULT(
      server_.fs_manager()->GetUniverseUuidFromTserverInstanceMetadata());
  if (!universe_uuid.empty()) {
    req.set_universe_uuid(universe_uuid);
  }

  tablet_report.set_is_incremental(!full_report_seq_no_);
  req.set_num_live_tablets(server_.tablet_manager()->GetNumLiveTablets());
  req.set_leader_count(server_.tablet_manager()->GetLeaderCount());
  if (FLAGS_ysql_enable_db_catalog_version_mode) {
    auto fingerprint = server_.GetCatalogVersionsFingerprint();
    if (fingerprint.has_value()) {
      req.set_ysql_db_catalog_versions_fingerprint(*fingerprint);
    }
  }

  RETURN_NOT_OK(server_.XClusterPopulateMasterHeartbeatRequest(
      req, last_hb_response_.needs_full_tablet_report()));

  for (auto& data_provider : data_providers_) {
    data_provider->AddData(last_hb_response_, &req);
  }

  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_heartbeat_rpc_timeout_ms));

  req.set_config_index(server_.GetCurrentMasterIndex());
  req.set_cluster_config_version(server_.cluster_config_version());
  auto result = server_.XClusterConfigVersion();
  if (result.ok()) {
    req.set_xcluster_config_version(*result);
  } else if (!result.status().IsNotFound()) {
    return result.status();
  }
  req.set_rtt_us(heartbeat_rtt_.ToMicroseconds());
  if (server_.has_faulty_drive()) {
    req.set_faulty_drive(true);
  }

  // Include the hybrid time of this tablet server in the heartbeat.
  auto* hybrid_clock = dynamic_cast<server::HybridClock*>(server_.Clock());
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

  req.set_auto_flags_config_version(server_.GetAutoFlagConfigVersion());

  {
    VLOG_WITH_PREFIX(2) << "Sending heartbeat:\n" << req.DebugString();
    heartbeat_rtt_ = MonoDelta::kZero;
    MonoTime start_time = MonoTime::Now();
    master::TSHeartbeatResponsePB resp;
    RETURN_NOT_OK_PREPEND(proxy_->TSHeartbeat(req, &resp, &rpc),
                          "Failed to send heartbeat");
    MonoTime end_time = MonoTime::Now();
    if (!resp.universe_uuid().empty()) {
      RETURN_NOT_OK(server_.ValidateAndMaybeSetUniverseUuid(
          VERIFY_RESULT(UniverseUuid::FromString(resp.universe_uuid()))));
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
          auto status = StatusFromPB(resp.error().status());
          if (resp.is_fatal_error()) {
            // yb-master has requested us to terminate the process immediately.
            LOG(FATAL) << "Unable to join universe: " << status;
          }
          return status;
      }
    }

    VLOG_WITH_PREFIX(2) << "Received heartbeat response:\n" << resp.DebugString();
    if (resp.has_master_config()) {
      LOG_WITH_PREFIX(INFO) << "Received heartbeat response with config " << resp.DebugString();

      RETURN_NOT_OK(server_.UpdateMasterAddresses(resp.master_config(), resp.leader_master()));
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
      RETURN_NOT_OK(server_.SetUniverseKeyRegistry(resp.universe_key_registry()));
    }

    server_.set_oid_cache_invalidations_count(resp.oid_cache_invalidations_count());

    RETURN_NOT_OK(server_.XClusterHandleMasterHeartbeatResponse(resp));

    // At this point we know resp is a successful heartbeat response from the master so set it as
    // the last heartbeat response. This invalidates resp so we should use last_hb_response_ instead
    // below (hence using the nested scope for resp until here).
    last_hb_response_.Swap(&resp);
    heartbeat_rtt_ = end_time.GetDeltaSince(start_time);
  }

  if (last_hb_response_.has_cluster_uuid() && !last_hb_response_.cluster_uuid().empty()) {
    server_.set_cluster_uuid(last_hb_response_.cluster_uuid());
  }

  // The Master responds with the max entries for a single Tablet Report to avoid overwhelming it.
  if (last_hb_response_.has_tablet_report_limit()) {
    server_.tablet_manager()->SetReportLimit(last_hb_response_.tablet_report_limit());
  }

  if (last_hb_response_.needs_full_tablet_report()) {
    return STATUS(TryAgain, "");
  }

  // Handle TSHeartbeatResponsePB (e.g. tablets ack'd by master as processed)
  bool all_processed = req.tablet_report().remaining_tablet_count() == 0 &&
                       !last_hb_response_.tablet_report().processing_truncated();
  server_.tablet_manager()->MarkTabletReportAcknowledged(
      req.tablet_report().sequence_number(), last_hb_response_.tablet_report(), all_processed);

  // Trigger another heartbeat ASAP if we didn't process all tablets on this request.
  if (all_processed) {
    full_report_seq_no_.reset();
  }

  // Update the master's YSQL catalog version (i.e. if there were schema changes for YSQL objects).
  // We only check --enable_ysql when --ysql_enable_db_catalog_version_mode=true
  // to keep the logic backward compatible.
  if (FLAGS_ysql_enable_db_catalog_version_mode && FLAGS_enable_ysql &&
      PREDICT_TRUE(!FLAGS_TEST_tserver_disable_catalog_refresh_on_heartbeat)) {
    // We never expect rolling gflag change of --ysql_enable_db_catalog_version_mode. In per-db
    // mode, we do not use ysql_catalog_version.
    DCHECK(!last_hb_response_.has_ysql_catalog_version());
    if (last_hb_response_.has_db_catalog_version_data()) {
      if (FLAGS_log_ysql_catalog_versions) {
        VLOG_WITH_FUNC(1) << "got master db catalog version data: "
                          << last_hb_response_.db_catalog_version_data().ShortDebugString()
                          << " db inval messages: "
                          << tserver::CatalogInvalMessagesDataDebugString(last_hb_response_);
      }
      if (FLAGS_ysql_yb_enable_invalidation_messages) {
        if (last_hb_response_.has_db_catalog_inval_messages_data()) {
          server_.SetYsqlDBCatalogVersionsWithInvalMessages(
              last_hb_response_.db_catalog_version_data(),
              last_hb_response_.db_catalog_inval_messages_data());
        } else {
          server_.SetYsqlDBCatalogVersions(last_hb_response_.db_catalog_version_data());
          // If we only have catalog versions but not invalidation messages, it means that the last
          // heartbeat response was only able to read pg_yb_catalog_version, but the reading of
          // pg_yb_invalidation_messages failed. Clear the fingerprint so that next heartbeat
          // can read pg_yb_invalidation_messages again.
          server_.ResetCatalogVersionsFingerprint();
        }
      } else {
        server_.SetYsqlDBCatalogVersions(last_hb_response_.db_catalog_version_data());
      }
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
        server_.SetYsqlCatalogVersion(last_hb_response_.ysql_catalog_version(),
                                       last_hb_response_.ysql_last_breaking_catalog_version());
      } else {
        /* Assuming all changes are breaking if last breaking version not explicitly set. */
        server_.SetYsqlCatalogVersion(last_hb_response_.ysql_catalog_version(),
                                       last_hb_response_.ysql_catalog_version());
      }
    } else if (last_hb_response_.has_db_catalog_version_data() &&
               PREDICT_TRUE(!FLAGS_TEST_tserver_disable_catalog_refresh_on_heartbeat)) {
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
                          << last_hb_response_.db_catalog_version_data().ShortDebugString()
                          << " db inval messages: "
                          << tserver::CatalogInvalMessagesDataDebugString(last_hb_response_);
      }
      const auto& version_data = last_hb_response_.db_catalog_version_data();

      // The upgrade of the pg_yb_catalog_version table to one row per database happens in
      // the finalization phase of cluster upgrade, at that time all the tservers should have
      // completed rolling upgrade and have --ysql_enable_db_catalog_version_mode=true.
      // Since we still have FLAGS_ysql_enable_db_catalog_version_mode=false, the table
      // must still only have one row for template1.
      DCHECK_EQ(version_data.db_catalog_versions_size(), 1);
      DCHECK_EQ(version_data.db_catalog_versions(0).db_oid(), kTemplate1Oid);

      server_.SetYsqlCatalogVersion(version_data.db_catalog_versions(0).current_version(),
                                     version_data.db_catalog_versions(0).last_breaking_version());
    }
  }

  RETURN_NOT_OK(server_.tablet_manager()->UpdateSnapshotsInfo(last_hb_response_.snapshots_info()));

  if (last_hb_response_.has_transaction_tables_version()) {
    server_.UpdateTransactionTablesVersion(last_hb_response_.transaction_tables_version());
  }

  std::optional<AutoFlagsConfigPB> new_config;
  if (last_hb_response_.has_auto_flags_config()) {
    new_config = last_hb_response_.auto_flags_config();
  }
  server_.HandleMasterHeartbeatResponse(heartbeat_send_time, std::move(new_config));

  // Update the live tserver list.
  return server_.PopulateLiveTServers(last_hb_response_);
}

Status HeartbeatPoller::SetupRegistration(master::TSRegistrationPB* reg) {
  reg->Clear();
  RETURN_NOT_OK(server_.GetRegistration(reg->mutable_common()));
  auto* resources = reg->mutable_resources();
  resources->set_core_count(base::NumCPUs());
  int64_t tablet_overhead_limit = yb::tserver::ComputeTabletOverheadLimit();
  if (tablet_overhead_limit > 0) {
    resources->set_tablet_overhead_ram_in_bytes(tablet_overhead_limit);
  }
  VersionInfo::GetVersionInfoPB(reg->mutable_version_info());
  return Status::OK();
}

void HeartbeatPoller::SetupCommonField(master::TSToMasterCommonPB* common) {
  common->mutable_ts_instance()->CopyFrom(server_.instance_pb());
}

MonoDelta HeartbeatPoller::GetMinimumHeartbeatMillis(int32_t consecutive_failures) const {
  // If we've failed a few heartbeats in a row, back off to the normal
  // interval, rather than retrying in a loop.
  if (consecutive_failures == FLAGS_heartbeat_max_failures_before_backoff) {
    LOG_WITH_PREFIX(WARNING) << "Failed " << consecutive_failures << " heartbeats "
                             << "in a row: no longer allowing fast heartbeat attempts.";
  }
  return consecutive_failures > FLAGS_heartbeat_max_failures_before_backoff
             ? MonoDelta::FromMilliseconds(FLAGS_heartbeat_interval_ms)
             : MonoDelta::kZero;
}

}  // namespace yb::tserver
