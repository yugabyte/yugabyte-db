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

#include "yb/common/common_flags.h"
#include "yb/common/common_util.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_heartbeat.service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/ts_manager.h"

#include "yb/util/flags.h"

DEFINE_UNKNOWN_int32(tablet_report_limit, 1000,
             "Max Number of tablets to report during a single heartbeat. "
             "If this is set to INT32_MAX, then heartbeat will report all dirty tablets.");
TAG_FLAG(tablet_report_limit, advanced);

DEFINE_test_flag(string, master_universe_uuid, "",
                 "When set, use this mocked uuid to compare against the universe_uuid "
                 "from the tserver request.");

DEFINE_RUNTIME_AUTO_bool(
    master_enable_universe_uuid_heartbeat_check, kLocalPersisted, false, true,
    "When true, enables a sanity check between masters and tservers to prevent tservers from "
    "mistakenly heartbeating to masters in different universes. Master leader will check the "
    "universe_uuid passed by tservers against with its own copy of universe_uuid "
    "and reject the request if there is a mismatch. Master sends its universe_uuid with the "
    "response.");
TAG_FLAG(master_enable_universe_uuid_heartbeat_check, advanced);

DEFINE_test_flag(bool, skip_processing_tablet_metadata, false,
                 "Whether to skip processing tablet metadata for TSHeartbeat.");

DECLARE_int32(heartbeat_rpc_timeout_ms);

DECLARE_CAPABILITY(TabletReportLimit);

using namespace std::literals;

namespace yb {
namespace master {

namespace {

class MasterHeartbeatServiceImpl : public MasterServiceBase, public MasterHeartbeatIf {
 public:
  explicit MasterHeartbeatServiceImpl(Master* master)
      : MasterServiceBase(master), MasterHeartbeatIf(master->metric_entity()) {}

  static Status CheckUniverseUuidMatchFromTserver(
      const UniverseUuid& tserver_universe_uuid,
      const UniverseUuid& master_universe_uuid) {
    if (!GetAtomicFlag(&FLAGS_master_enable_universe_uuid_heartbeat_check)) {
      return Status::OK();
    }

    // If the universe uuid on the master is empty, return an error. Eventually this field will be
    // set via a background thread.
    if (master_universe_uuid.IsNil()) {
      return STATUS(TryAgain, "universe_uuid is not yet set in the cluster config");
    }

    if (tserver_universe_uuid.IsNil()) {
      // The tserver must send universe_uuid with the request, ask it to retry.
      return STATUS(TryAgain, "universe_uuid needs to be set in the request");
    }

    if (tserver_universe_uuid != master_universe_uuid) {
      return STATUS(InvalidArgument,
          Format("Received wrong universe_uuid $0, expected $1",
              tserver_universe_uuid.ToString(), master_universe_uuid.ToString()));
    }
    return Status::OK();
  }

  void TSHeartbeat(const TSHeartbeatRequestPB* req,
                   TSHeartbeatResponsePB* resp,
                   rpc::RpcContext rpc) override {
    LongOperationTracker long_operation_tracker("TSHeartbeat", 1s);

    // If CatalogManager is not initialized don't even know whether or not we will
    // be a leader (so we can't tell whether or not we can accept tablet reports).
    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());

    if (req->common().ts_instance().permanent_uuid().empty()) {
      // In FSManager, we have already added empty UUID protection so that TServer will
      // crash before even sending heartbeat to Master. Here is only for the case that
      // new updated Master might received empty UUID from old version of TServer that
      // doesn't have the crash code in FSManager.
      rpc.RespondFailure(STATUS(InvalidArgument, "Recevied Empty UUID from instance: ",
                                req->common().ts_instance().ShortDebugString()));
      return;
    }

    consensus::ConsensusStatePB cpb;
    Status s = server_->catalog_manager_impl()->GetCurrentConfig(&cpb);
    if (!s.ok()) {
      // For now, we skip setting the config on errors (hopefully next heartbeat will work).
      // We could enhance to fail rpc, if there are too many error, on a case by case error basis.
      LOG(WARNING) << "Could not set master raft config : " << s.ToString();
    } else if (cpb.has_config()) {
      if (cpb.config().opid_index() > req->config_index()) {
        *resp->mutable_master_config() = std::move(cpb.config());
        LOG(INFO) << "Set config at index " << resp->master_config().opid_index() << " for ts uuid "
                  << req->common().ts_instance().permanent_uuid();
      }
    } // Do nothing if config not ready.

    if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, &rpc)) {
      resp->set_leader_master(false);
      return;
    }

    resp->mutable_master_instance()->CopyFrom(server_->instance_pb());
    resp->set_leader_master(true);

    // At the time of this check, we need to know that we're the master leader to access the
    // cluster config.
    SysClusterConfigEntryPB cluster_config;
    s = server_->catalog_manager_impl()->GetClusterConfig(&cluster_config);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to get cluster configuration: " << s.ToString();
      rpc.RespondFailure(s);
      return;
    }

    auto tserver_universe_uuid_res = UniverseUuid::FromString(req->universe_uuid());
    if (!tserver_universe_uuid_res) {
      LOG(WARNING) << "Could not decode request universe_uuid: " <<
          tserver_universe_uuid_res.status().ToString();
      rpc.RespondFailure(tserver_universe_uuid_res.status());
    }
    auto tserver_universe_uuid = *tserver_universe_uuid_res;

    auto master_universe_uuid_res =  UniverseUuid::FromString(
        FLAGS_TEST_master_universe_uuid.empty() ?
            cluster_config.universe_uuid() : FLAGS_TEST_master_universe_uuid);
    if (!master_universe_uuid_res) {
      LOG(WARNING) << "Could not decode cluster config universe_uuid: " <<
          master_universe_uuid_res.status().ToString();
      rpc.RespondFailure(master_universe_uuid_res.status());
    }
    auto master_universe_uuid = *master_universe_uuid_res;

    s = CheckUniverseUuidMatchFromTserver(tserver_universe_uuid, master_universe_uuid);

    if (!s.ok()) {
      LOG(WARNING) << "Failed CheckUniverseUuidMatchFromTserver check: " << s.ToString();
      if (master_universe_uuid.IsNil()) {
        auto* error = resp->mutable_error();
        error->set_code(MasterErrorPB::INVALID_CLUSTER_CONFIG);
        StatusToPB(s, error->mutable_status());
        rpc.RespondSuccess();
        return;
      }

      if (tserver_universe_uuid.IsNil()) {
        resp->set_universe_uuid((*master_universe_uuid_res).ToString());
        auto* error = resp->mutable_error();
        error->set_code(MasterErrorPB::INVALID_REQUEST);
        StatusToPB(s, error->mutable_status());
        rpc.RespondSuccess();
        return;
      }

      rpc.RespondFailure(s);
      return;
    }

    // If the TS is registering, register in the TS manager.
    if (req->has_registration()) {
      Status s = server_->ts_manager()->RegisterTS(req->common().ts_instance(),
                                                   req->registration(),
                                                   server_->MakeCloudInfoPB(),
                                                   &server_->proxy_cache());
      if (!s.ok()) {
        LOG(WARNING) << "Unable to register tablet server (" << rpc.requestor_string() << "): "
                     << s.ToString();
        // TODO: add service-specific errors.
        rpc.RespondFailure(s);
        return;
      }
      resp->set_cluster_uuid(cluster_config.cluster_uuid());
    }

    s = server_->catalog_manager_impl()->FillHeartbeatResponse(req, resp);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to fill heartbeat response: " << s.ToString();
      rpc.RespondFailure(s);
    }

    // Look up the TS -- if it just registered above, it will be found here.
    // This allows the TS to register and tablet-report in the same RPC.
    TSDescriptorPtr ts_desc;
    s = server_->ts_manager()->LookupTS(req->common().ts_instance(), &ts_desc);
    if (s.IsNotFound()) {
      LOG(INFO) << "Got heartbeat from unknown tablet server { "
                << req->common().ts_instance().ShortDebugString() << " } as "
                << rpc.requestor_string()
                << "; Asking this server to re-register. Status from ts lookup: " << s;
      resp->set_needs_reregister(true);
      resp->set_needs_full_tablet_report(true);
      rpc.RespondSuccess();
      return;
    } else if (!s.ok()) {
      LOG(WARNING) << "Unable to look up tablet server for heartbeat request "
                   << req->DebugString() << " from " << rpc.requestor_string()
                   << "\nStatus: " << s.ToString();
      rpc.RespondFailure(s.CloneAndPrepend("Unable to lookup TS"));
      return;
    }

    ts_desc->UpdateHeartbeat(req);

    // Adjust the table report limit per heartbeat so this can be dynamically changed.
    if (ts_desc->HasCapability(CAPABILITY_TabletReportLimit)) {
      resp->set_tablet_report_limit(FLAGS_tablet_report_limit);
    }

    // Set the TServer metrics in TS Descriptor.
    if (req->has_metrics()) {
      ts_desc->UpdateMetrics(req->metrics());
    }

    if (req->has_tablet_report()) {
      s = server_->catalog_manager_impl()->ProcessTabletReport(
        ts_desc.get(), req->tablet_report(), resp->mutable_tablet_report(), &rpc);
      if (!s.ok()) {
        rpc.RespondFailure(s.CloneAndPrepend("Failed to process tablet report"));
        return;
      }
    }

    if (!req->has_tablet_report() || req->tablet_report().is_incremental()) {
      // Only process metadata if we have plenty of time to process the work (> 50% of
      // timeout).
      auto safe_time_left = CoarseMonoClock::Now() + (FLAGS_heartbeat_rpc_timeout_ms * 1ms / 2);
      if (rpc.GetClientDeadline() > safe_time_left &&
          PREDICT_TRUE(!ANNOTATE_UNPROTECTED_READ(FLAGS_TEST_skip_processing_tablet_metadata))) {
        std::unordered_map<TabletId, TabletLeaderMetricsPB> id_to_leader_metrics;
        for (auto& info : req->leader_info()) {
          id_to_leader_metrics[info.tablet_id()] = info;
        }
        for (const auto& metadata : req->storage_metadata()) {
          std::optional<TabletLeaderMetricsPB> leader_metrics;
          auto iter = id_to_leader_metrics.find(metadata.tablet_id());
          if (iter != id_to_leader_metrics.end()) {
            leader_metrics = iter->second;
          }
          server_->catalog_manager_impl()->ProcessTabletMetadata(ts_desc.get()->permanent_uuid(),
                                                                 metadata, leader_metrics);
        }
      }

      // Only process the replication status if we have plenty of time to process the work (> 50% of
      // timeout).
      safe_time_left = CoarseMonoClock::Now() + (FLAGS_heartbeat_rpc_timeout_ms * 1ms / 2);
      if (rpc.GetClientDeadline() > safe_time_left) {
        for (const auto& replication_state : req->replication_state()) {
          ERROR_NOT_OK(
            server_->catalog_manager_impl()->ProcessTabletReplicationStatus(replication_state),
            "Failed to process tablet replication status");
        }
      }

      // Only set once. It may take multiple heartbeats to receive a full tablet report.
      if (!ts_desc->has_tablet_report()) {
        resp->set_needs_full_tablet_report(true);
      }
    }

    // Retrieve all the nodes known by the master.
    std::vector<std::shared_ptr<TSDescriptor>> descs;
    server_->ts_manager()->GetAllLiveDescriptors(&descs);
    for (const auto& desc : descs) {
      *resp->add_tservers() = *desc->GetTSInformationPB();
    }

    // Retrieve the ysql catalog schema version.
    if (FLAGS_TEST_enable_db_catalog_version_mode) {
      DbOidToCatalogVersionMap versions;
      s = server_->catalog_manager_impl()->GetYsqlAllDBCatalogVersions(&versions);
      if (s.ok()) {
        auto* const mutable_version_data = resp->mutable_db_catalog_version_data();
        for (const auto& it : versions) {
          auto* const catalog_version = mutable_version_data->add_db_catalog_versions();
          catalog_version->set_db_oid(it.first);
          catalog_version->set_current_version(it.second.first);
          catalog_version->set_last_breaking_version(it.second.second);
        }
        if (FLAGS_log_ysql_catalog_versions) {
          VLOG_WITH_FUNC(2) << "responding (to ts " << req->common().ts_instance().permanent_uuid()
                            << ") db catalog versions: "
                            << resp->db_catalog_version_data().ShortDebugString();
        }
      } else {
        LOG(WARNING) << "Could not get YSQL db catalog versions for heartbeat response: "
                     << s.ToUserMessage();
      }
    } else {
      uint64_t last_breaking_version = 0;
      uint64_t catalog_version = 0;
      s = server_->catalog_manager_impl()->GetYsqlCatalogVersion(
          &catalog_version, &last_breaking_version);
      if (s.ok()) {
        resp->set_ysql_catalog_version(catalog_version);
        resp->set_ysql_last_breaking_catalog_version(last_breaking_version);
        if (FLAGS_log_ysql_catalog_versions) {
          VLOG_WITH_FUNC(1) << "responding (to ts " << req->common().ts_instance().permanent_uuid()
                            << ") catalog version: " << catalog_version
                            << ", breaking version: " << last_breaking_version;
        }
      } else {
        LOG(WARNING) << "Could not get YSQL catalog version for heartbeat response: "
                     << s.ToUserMessage();
      }
    }

    uint64_t transaction_tables_version = server_->catalog_manager()->GetTransactionTablesVersion();
    resp->set_transaction_tables_version(transaction_tables_version);

    if (req->has_auto_flags_config_version() &&
        req->auto_flags_config_version() < server_->GetAutoFlagConfigVersion()) {
      *resp->mutable_auto_flags_config() = server_->GetAutoFlagsConfig();
    }

    rpc.RespondSuccess();
  }

};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterHeartbeatService(Master* master) {
  return std::make_unique<MasterHeartbeatServiceImpl>(master);
}

} // namespace master
} // namespace yb
