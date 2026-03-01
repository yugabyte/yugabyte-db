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

#include "yb/tserver/service_util.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_error.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/master/master_heartbeat.pb.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver_error.h"
#include "yb/tserver/tserver_types.messages.h"

#include "yb/util/flags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"

using std::string;

DEFINE_test_flag(bool, assert_reads_from_follower_rejected_because_of_staleness, false,
                 "If set, we verify that the consistency level is CONSISTENT_PREFIX, and that "
                 "a follower receives the request, but that it gets rejected because it's a stale "
                 "follower");

DEFINE_RUNTIME_uint64(max_stale_read_bound_time_ms, 60000,
    "If we are allowed to read from followers, specify the maximum time a follower can be behind "
    "by using the last message received from the leader. If set to zero, a read can be served by a "
    "follower regardless of when was the last time it received a message from the leader or how "
    "far behind this follower is.");
TAG_FLAG(max_stale_read_bound_time_ms, evolving);

DEFINE_RUNTIME_uint64(sst_files_soft_limit, 24,
    "When majority SST files number is greater that this limit, we will start rejecting "
    "part of write requests. The higher the number of SST files, the higher probability "
    "of rejection.");

DEFINE_RUNTIME_uint64(sst_files_hard_limit, 48,
    "When majority SST files number is greater that this limit, we will reject all write "
    "requests.");

DEFINE_test_flag(int32, write_rejection_percentage, 0,
                 "Reject specified percentage of writes.");

DEFINE_RUNTIME_uint64(min_rejection_delay_ms, 100,
    "Minimal delay for rejected write to be retried in milliseconds.");

DEFINE_RUNTIME_uint64(max_rejection_delay_ms, 5000,
    "Maximal delay for rejected write to be retried in milliseconds.");

DECLARE_int32(memory_limit_warn_threshold_percentage);

namespace yb {
namespace tserver {

namespace {

template <class PB>
void DoSetupErrorAndRespond(PB* error,
                            const Status& s,
                            TabletServerErrorPB::Code code,
                            rpc::RpcContext* context) {
  // Generic "service unavailable" errors will cause the client to retry later.
  if (code == TabletServerErrorPB::UNKNOWN_ERROR) {
    if (s.IsServiceUnavailable()) {
      TabletServerDelay delay(s);
      if (!delay.value().Initialized()) {
        context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY, s);
        return;
      }
    }
    consensus::ConsensusError consensus_error(s);
    if (consensus_error.value() == consensus::ConsensusErrorPB::TABLET_SPLIT) {
      code = TabletServerErrorPB::TABLET_SPLIT;
    }
  }

  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  context->RespondSuccess();
}

template <class PB>
void DoSetupErrorAndRespond(PB* error,
                            const Status& s,
                            rpc::RpcContext* context) {
  auto ts_error = TabletServerError::FromStatus(s);
  DoSetupErrorAndRespond(
      error, s, ts_error ? ts_error->value() : TabletServerErrorPB::UNKNOWN_ERROR, context);
}

} // namespace

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context) {
  DoSetupErrorAndRespond(error, s, code, context);
}

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          rpc::RpcContext* context) {
  DoSetupErrorAndRespond(error, s, context);
}

void SetupErrorAndRespond(LWTabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context) {
  DoSetupErrorAndRespond(error, s, code, context);
}

void SetupErrorAndRespond(LWTabletServerErrorPB* error,
                          const Status& s,
                          rpc::RpcContext* context) {
  DoSetupErrorAndRespond(error, s, context);
}

Result<int64_t> LeaderTerm(const tablet::TabletPeer& tablet_peer) {
  auto consensus_result = tablet_peer.GetConsensus();
  if (!consensus_result) {
    auto state = tablet_peer.state();
    if (state != tablet::RaftGroupStatePB::SHUTDOWN) {
      // Should not happen.
      return consensus_result.status();
    }
    return STATUS(Aborted, "Tablet peer was closed");
  }
  auto& consensus = consensus_result.get();
  auto leader_state = consensus->GetLeaderState();

  VLOG(1) << Format(
      "Check for tablet $0 peer $1. Peer role is $2. Leader status is $3.",
      tablet_peer.tablet_id(), tablet_peer.permanent_uuid(),
      consensus->role(), std::to_underlying(leader_state.status));

  if (!leader_state.ok()) {
    typedef consensus::LeaderStatus LeaderStatus;
    auto status = leader_state.CreateStatus();
    switch (leader_state.status) {
      case LeaderStatus::NOT_LEADER: FALLTHROUGH_INTENDED;
      case LeaderStatus::LEADER_BUT_NO_MAJORITY_REPLICATED_LEASE:
        // We are returning a NotTheLeader as opposed to LeaderNotReady, because there is a chance
        // that we're a partitioned-away leader, and the client needs to do another leader lookup.
        return status.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::NOT_THE_LEADER));
      case LeaderStatus::LEADER_BUT_NO_OP_NOT_COMMITTED: FALLTHROUGH_INTENDED;
      case LeaderStatus::LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE:
        return status.CloneAndAddErrorCode(TabletServerError(
            TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE));
      case LeaderStatus::LEADER_AND_READY:
        LOG(FATAL) << "Unexpected status: " << std::to_underlying(leader_state.status);
    }
    FATAL_INVALID_ENUM_VALUE(LeaderStatus, leader_state.status);
  }

  return leader_state.term;
}

std::string CatalogInvalMessagesDataDebugString(const master::TSHeartbeatResponsePB& resp) {
  std::string str;
  if (resp.has_db_catalog_inval_messages_data()) {
    str = CatalogInvalMessagesDataDebugString(resp.db_catalog_inval_messages_data());
  }
  return str;
}

std::string CatalogInvalMessagesDataDebugString(
    const tserver::DBCatalogInvalMessagesDataPB& db_catalog_inval_messages_data) {
  std::map<uint32_t, std::vector<std::pair<uint64_t, size_t>>> dbg_map;
  for (int i = 0; i < db_catalog_inval_messages_data.db_catalog_inval_messages_size(); ++i) {
    const auto& db_inval_messages = db_catalog_inval_messages_data.db_catalog_inval_messages(i);
    const uint32_t db_oid = db_inval_messages.db_oid();
    const uint64_t current_version = db_inval_messages.current_version();
    const size_t msg_sz =
        db_inval_messages.has_message_list() ? db_inval_messages.message_list().size() : 0;
    dbg_map[db_oid].emplace_back(current_version, msg_sz);
  }
  return yb::ToString(dbg_map);
}

void LeaderTabletPeer::FillTabletPeer(TabletPeerTablet source) {
  peer = std::move(source.tablet_peer);
  tablet = std::move(source.tablet);
}

Status LeaderTabletPeer::FillTerm() {
  auto leader_term_result = LeaderTerm(*peer);
  if (!leader_term_result.ok()) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (tablet) {
      // It could happen that tablet becomes nullptr due to shutdown.
      tablet->metrics()->Increment(tablet::TabletCounters::kNotLeaderRejections);
    }
    return leader_term_result.status();
  }
  leader_term = *leader_term_result;

  return Status::OK();
}

Status CheckPeerIsReady(
    const tablet::TabletPeer& tablet_peer, AllowSplitTablet allow_split_tablet) {
  auto consensus_result = tablet_peer.GetConsensus();
  if (!consensus_result) {
    return consensus_result.status().CloneAndAddErrorCode(
        TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }

  Status s = tablet_peer.CheckRunning();
  if (!s.ok()) {
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }

  auto tablet = VERIFY_RESULT(tablet_peer.shared_tablet());
  SCHECK(tablet != nullptr, IllegalState, "Expected tablet peer to have a tablet");
  const auto tablet_data_state = tablet->metadata()->tablet_data_state();
  if (!allow_split_tablet &&
      tablet_data_state == tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
    auto split_child_tablet_ids = tablet->metadata()->split_child_tablet_ids();
    return STATUS(
               IllegalState,
               Format("The tablet $0 is in $1 state",
                      tablet->tablet_id(),
                      TabletDataState_Name(tablet_data_state)),
               TabletServerError(TabletServerErrorPB::TABLET_SPLIT))
        .CloneAndAddErrorCode(SplitChildTabletIdsData(split_child_tablet_ids));
    // TODO(tsplit): If we get FS corruption on 1 node, we can just delete that tablet copy and
    // bootstrap from a good leader. If there's a way that all peers replicated the SPLIT and
    // modified their data state, but all had some failures (code bug?).
    // Perhaps we should consider a tool for editing the data state?
  }
  return Status::OK();
}


Status CheckPeerIsLeader(const tablet::TabletPeer& tablet_peer) {
  return ResultToStatus(LeaderTerm(tablet_peer));
}

bool IsErrorCodeNotTheLeader(const Status& status) {
  auto code = TabletServerError::FromStatus(status);
  return code && code.value() == TabletServerErrorPB::NOT_THE_LEADER;
}

std::shared_ptr<TabletConsensusInfoPB> GetTabletConsensusInfoFromTabletPeer(
    const tablet::TabletPeerPtr& peer) {
  if (auto consensus = peer->GetRaftConsensus()) {
    std::shared_ptr<TabletConsensusInfoPB> tablet_consensus_info =
        std::make_shared<TabletConsensusInfoPB>();
    tablet_consensus_info->set_tablet_id(peer->tablet_id());
    *(tablet_consensus_info->mutable_consensus_state()) =
        consensus.get()->GetConsensusStateFromCache();
    return tablet_consensus_info;
  }
  return nullptr;
}

Result<TabletPeerTablet> LookupTabletPeer(
    TabletPeerLookupIf* tablet_manager,
    TabletIdView tablet_id) {
  ash::WaitStateInfo::UpdateCurrentTabletId(tablet_id);
  TabletPeerTablet result;
  auto tablet_peer_result = tablet_manager->GetServingTablet(tablet_id);
  if (PREDICT_FALSE(!tablet_peer_result.ok())) {
    auto code = tablet_peer_result.status().IsServiceUnavailable()
        ? TabletServerErrorPB::UNKNOWN_ERROR : TabletServerErrorPB::TABLET_NOT_FOUND;
    return tablet_peer_result.status().CloneAndAddErrorCode(TabletServerError(code));
  }
  result.tablet_peer = std::move(*tablet_peer_result);

  // Check RUNNING state.
  tablet::RaftGroupStatePB state = result.tablet_peer->state();
  if (PREDICT_FALSE(state != tablet::RUNNING)) {
    Status s = STATUS(IllegalState,  Format("Tablet $0 not RUNNING", tablet_id),
                      tablet::RaftGroupStateError(state))
        .CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
    return s;
  }

  auto tablet_result = result.tablet_peer->shared_tablet();
  if (!tablet_result.ok()) {
    return tablet_result.status().CloneAndAddErrorCode(TabletServerError(
        TabletServerErrorPB::TABLET_NOT_RUNNING));
  }
  result.tablet = *tablet_result;
  return result;
}

Result<std::shared_ptr<tablet::AbstractTablet>> GetTablet(
    TabletPeerLookupIf* tablet_manager, TabletIdView tablet_id,
    tablet::TabletPeerPtr tablet_peer, YBConsistencyLevel consistency_level,
    AllowSplitTablet allow_split_tablet, ReadResponseMsg* resp,
    HybridTime* follower_safe_time) {
  tablet::TabletPtr tablet_ptr = nullptr;
  if (tablet_peer) {
    DCHECK_EQ(tablet_peer->tablet_id(), tablet_id);
    tablet_ptr = VERIFY_RESULT(tablet_peer->shared_tablet());
  } else {
    auto tablet_peer_result = VERIFY_RESULT(LookupTabletPeer(tablet_manager, tablet_id));

    tablet_peer = std::move(tablet_peer_result.tablet_peer);
    tablet_ptr = std::move(tablet_peer_result.tablet);
  }
  RETURN_NOT_OK(CheckPeerIsReady(*tablet_peer, allow_split_tablet));

  // Check for leader only in strong consistency level.
  if (consistency_level == YBConsistencyLevel::STRONG) {
    if (PREDICT_FALSE(FLAGS_TEST_assert_reads_from_follower_rejected_because_of_staleness)) {
      LOG(FATAL) << "--TEST_assert_reads_from_follower_rejected_because_of_staleness is true but "
                    "consistency level is invalid: YBConsistencyLevel::STRONG";
    }
    auto status = CheckPeerIsLeader(*tablet_peer);
    if (!status.ok()) {
      if (IsErrorCodeNotTheLeader(status)) {
        FillTabletConsensusInfo(resp, tablet_id, tablet_peer);
      }
      return status;
    }
  } else {
    auto s = CheckPeerIsLeader(*tablet_peer.get());
    // Peer is not the leader, so check that the time since it last heard from the leader is less
    // than FLAGS_max_stale_read_bound_time_ms.
    if (PREDICT_FALSE(!s.ok())) {
      if (FLAGS_max_stale_read_bound_time_ms > 0) {
        auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet());
        auto safe_time = tablet->mvcc_manager()->SafeTimeForFollower(
            HybridTime::kMin, CoarseTimePoint::min());
        if (follower_safe_time) {
          *follower_safe_time = safe_time;
        }
        auto now = tablet_peer->clock_ptr()->Now();
        auto follower_staleness = now.PhysicalDiff(safe_time);
        if (follower_staleness > MonoDelta::FromMilliseconds(FLAGS_max_stale_read_bound_time_ms)) {
          VLOG(1) << "Rejecting stale read with staleness " << follower_staleness.ToPrettyString();
          return STATUS_EC_FORMAT(
              IllegalState, TabletServerError(TabletServerErrorPB::STALE_FOLLOWER),
              "Stale follower $0 with staleness $1", tablet_peer->LogPrefix(),
              follower_staleness.ToPrettyString());
        }
        if (PREDICT_FALSE(
            FLAGS_TEST_assert_reads_from_follower_rejected_because_of_staleness)) {
          LOG(FATAL) << "--TEST_assert_reads_from_follower_rejected_because_of_staleness is true,"
                     << " but peer " << tablet_peer->permanent_uuid()
                     << " for tablet: " << tablet_id
                     << " is not stale. Time since last update from leader: "
                     << follower_staleness.ToPrettyString();
        } else {
          VLOG(3)
              << "Reading from follower with staleness: " << follower_staleness.ToPrettyString();
        }
      }
    } else {
      // We are here because we are the leader.
      if (PREDICT_FALSE(FLAGS_TEST_assert_reads_from_follower_rejected_because_of_staleness)) {
        LOG(FATAL) << "--TEST_assert_reads_from_follower_rejected_because_of_staleness is true but "
                   << " peer " << tablet_peer->permanent_uuid()
                   << " is the leader for tablet " << tablet_id;
      }
    }
  }
  auto tablet = tablet_peer->shared_tablet_maybe_null();
  if (PREDICT_FALSE(!tablet)) {
    return STATUS_EC_FORMAT(
        IllegalState, TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING),
        "Tablet $0 is not running", tablet_id);
  }
  return tablet;
}

// overlimit - we have 2 bounds, value and random score.
// overlimit is calculated as:
// score + (value - lower_bound) / (upper_bound - lower_bound).
// And it will be >= 1.0 when this function is invoked.
Status RejectWrite(
    tablet::TabletPeer* tablet_peer, const std::string& message, double overlimit) {
  int64_t delay_ms = fit_bounds<int64_t>((overlimit - 1.0) * FLAGS_max_rejection_delay_ms,
                                         FLAGS_min_rejection_delay_ms,
                                         FLAGS_max_rejection_delay_ms);
  auto status = STATUS(
      ServiceUnavailable, message, TabletServerDelay(std::chrono::milliseconds(delay_ms)));
  YB_LOG_EVERY_N_SECS(WARNING, 1)
      << "T " << tablet_peer->tablet_id() << " P " << tablet_peer->permanent_uuid()
      << ": Rejecting Write request, " << status;
  return status;
}

Status CheckWriteThrottling(double score, tablet::TabletPeer* tablet_peer) {
  // Check for memory pressure; don't bother doing any additional work if we've
  // exceeded the limit.
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet());
  auto soft_limit_exceeded_result = tablet->mem_tracker()->AnySoftLimitExceeded(score);
  if (soft_limit_exceeded_result.exceeded) {
    tablet->metrics()->Increment(tablet::TabletCounters::kLeaderMemoryPressureRejections);
    string msg = StringPrintf(
        "Soft memory limit exceeded for %s (at %.2f%% of capacity), score: %.2f",
        soft_limit_exceeded_result.tracker_path.c_str(),
        soft_limit_exceeded_result.current_capacity_pct, score);
    if (soft_limit_exceeded_result.current_capacity_pct >=
            FLAGS_memory_limit_warn_threshold_percentage) {
      YB_LOG_EVERY_N_SECS(WARNING, 1) << "Rejecting Write request: " << msg;
    } else {
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Rejecting Write request: " << msg;
    }
    return STATUS(ServiceUnavailable, msg);
  }

  const uint64_t num_sst_files =
      VERIFY_RESULT(tablet_peer->GetRaftConsensus())->MajorityNumSSTFiles();
  const auto sst_files_soft_limit = FLAGS_sst_files_soft_limit;
  const int64_t sst_files_used_delta = num_sst_files - sst_files_soft_limit;
  if (sst_files_used_delta >= 0) {
    const auto sst_files_hard_limit = FLAGS_sst_files_hard_limit;
    const auto sst_files_full_delta = sst_files_hard_limit - sst_files_soft_limit;
    if (sst_files_used_delta >= sst_files_full_delta * (1 - score)) {
      tablet->metrics()->Increment(tablet::TabletCounters::kMajoritySstFilesRejections);
      auto message = Format("SST files limit exceeded $0 against ($1, $2), score: $3",
                            num_sst_files, sst_files_soft_limit, sst_files_hard_limit, score);
      auto overlimit = sst_files_full_delta > 0
          ? score + static_cast<double>(sst_files_used_delta) / sst_files_full_delta
          : 2.0;
      return RejectWrite(tablet_peer, message, overlimit);
    }
  }

  if (FLAGS_TEST_write_rejection_percentage != 0 &&
      score >= 1.0 - FLAGS_TEST_write_rejection_percentage * 0.01) {
    auto status = Format("TEST: Write request rejected, desired percentage: $0, score: $1",
                         FLAGS_TEST_write_rejection_percentage, score);
    return RejectWrite(tablet_peer, status, score + FLAGS_TEST_write_rejection_percentage * 0.01);
  }

  return Status::OK();
}

uint64_t CatalogVersionChecker::GetLastBreakingVersion(DbOid db_oid) const {
  uint64_t last_breaking_catalog_version;
  if (db_oid) {
    tablet_server_.get_ysql_db_catalog_version(
        *db_oid, nullptr /* current_version */, &last_breaking_catalog_version);
  } else {
    tablet_server_.get_ysql_catalog_version(
        nullptr /* current_version */, &last_breaking_catalog_version);
  }
  return last_breaking_catalog_version;
}

} // namespace tserver
} // namespace yb
