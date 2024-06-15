//
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
//

#pragma once

#include <functional>

#include <boost/optional.hpp>

#include "yb/cdc/cdc_service.pb.h"

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_error.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/rpc/rpc_context.h"
#include "yb/server/clock.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tablet/tablet_error.h"
#include "yb/tserver/tserver_error.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_format.h"

DECLARE_bool(ysql_enable_db_catalog_version_mode);

namespace yb {
namespace tserver {

// Non-template helpers.

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context);

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          rpc::RpcContext* context);

void SetupError(TabletServerErrorPB* error, const Status& s);

void SetupErrorAndRespond(LWTabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context);

void SetupErrorAndRespond(LWTabletServerErrorPB* error,
                          const Status& s,
                          rpc::RpcContext* context);

void SetupError(LWTabletServerErrorPB* error, const Status& s);

Result<int64_t> LeaderTerm(const tablet::TabletPeer& tablet_peer);

std::shared_ptr<TabletConsensusInfoPB> GetTabletConsensusInfoFromTabletPeer(
    const tablet::TabletPeerPtr& peer);

// Template helpers.

template<class ReqClass>
Result<bool> CheckUuidMatch(TabletPeerLookupIf* tablet_manager,
                            const char* method_name,
                            const ReqClass* req,
                            const std::string& requestor_string) {
  const std::string& local_uuid = tablet_manager->NodeInstance().permanent_uuid();
  if (req->dest_uuid().empty()) {
    // Maintain compat in release mode, but complain.
    std::string msg = strings::Substitute("$0: Missing destination UUID in request from $1: $2",
        method_name, requestor_string, req->ShortDebugString());
#ifdef NDEBUG
    YB_LOG_EVERY_N(ERROR, 100) << msg;
#else
    LOG(FATAL) << msg;
#endif
    return true;
  }
  if (PREDICT_FALSE(req->dest_uuid() != local_uuid)) {
    const Status s = STATUS_FORMAT(InvalidArgument,
        "$0: Wrong destination UUID requested. Local UUID: $1. Requested UUID: $2",
        method_name, local_uuid, req->dest_uuid());
    LOG(WARNING) << s.ToString() << ": from " << requestor_string
                 << ": " << req->ShortDebugString();
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::WRONG_SERVER_UUID));
  }
  return true;
}

template<class ReqClass, class RespClass>
bool CheckUuidMatchOrRespond(TabletPeerLookupIf* tablet_manager,
                             const char* method_name,
                             const ReqClass* req,
                             RespClass* resp,
                             rpc::RpcContext* context) {
  Result<bool> result = CheckUuidMatch(tablet_manager, method_name,
                                       req, context->requestor_string());
  if (!result.ok()) {
     SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
     return false;
  }
  return result.get();
}

template <class RespType>
void HandleErrorResponse(RespType* resp, rpc::RpcContext* context, const Status& s,
    const boost::optional<TabletServerErrorPB::Code>& error_code = boost::none) {
  resp->Clear();
  SetupErrorAndRespond(resp->mutable_error(), s,
      error_code.get_value_or(TabletServerErrorPB::UNKNOWN_ERROR), context);
}

template <class RespType>
void HandleResponse(RespType* resp,
                    const std::shared_ptr<rpc::RpcContext>& context,
                    const Status& s) {
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(resp, context.get(), s);
    return;
  }
  context->RespondSuccess();
}

template <class RespType>
StdStatusCallback BindHandleResponse(RespType* resp,
                                  const std::shared_ptr<rpc::RpcContext>& context) {
  return std::bind(&HandleResponse<RespType>, resp, context, std::placeholders::_1);
}

struct TabletPeerTablet {
  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  tablet::TabletPtr tablet;
};

// Lookup the given tablet, ensuring that it both exists and is RUNNING.
// If it is not, respond to the RPC associated with 'context' after setting
// resp->mutable_error() to indicate the failure reason.
//
// Returns true if successful.
Result<TabletPeerTablet> LookupTabletPeer(
    TabletPeerLookupIf* tablet_manager,
    const TabletId& tablet_id);

Result<TabletPeerTablet> LookupTabletPeer(
    TabletPeerLookupIf* tablet_manager,
    const Slice& tablet_id);

bool IsErrorCodeNotTheLeader(const Status& status);

template<class RespClass, class Key>
Result<TabletPeerTablet> LookupTabletPeerOrRespond(
    TabletPeerLookupIf* tablet_manager,
    const Key& tablet_id,
    RespClass* resp,
    rpc::RpcContext* context) {
  Result<TabletPeerTablet> result = LookupTabletPeer(tablet_manager, tablet_id);
  if (!result.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
    return result.status();
  }
  return result.get();
}

template <class Response>
auto MakeRpcOperationCompletionCallback(
    rpc::RpcContext context,
    Response* response,
    const server::ClockPtr& clock) {
  return [context = std::make_shared<rpc::RpcContext>(std::move(context)),
          response, clock](const Status& status) {
    if (clock) {
      response->set_propagated_hybrid_time(clock->Now().ToUint64());
    }
    if (!status.ok()) {
      SetupErrorAndRespond(response->mutable_error(), status, context.get());
    } else {
      context->RespondSuccess();
    }
  };
}

struct LeaderTabletPeer {
  tablet::TabletPeerPtr peer;
  tablet::TabletPtr tablet;
  int64_t leader_term = -1;

  bool operator!() const {
    return !peer || !tablet;
  }

  Status FillTerm();
  void FillTabletPeer(TabletPeerTablet source);
};

// Note this method expects that the Resp class must have a field tablet_consensus_info field;
// it is used to piggyback a TabletConsensusInfo for the receiver of this response to refresh
// its meta-cache.
template <class Resp>
void FillTabletConsensusInfo(Resp* resp, const TabletId& tablet_id, tablet::TabletPeerPtr peer) {
  if constexpr (HasTabletConsensusInfo<Resp>::value) {
    auto outgoing_tablet_consensus_info = resp->mutable_tablet_consensus_info();
    outgoing_tablet_consensus_info->set_tablet_id(tablet_id);

    if (auto consensus = peer->GetRaftConsensus()) {
      *(outgoing_tablet_consensus_info->mutable_consensus_state()) =
          consensus.get()->GetConsensusStateFromCache();
      VLOG(1) << "Sending out Consensus state for tablet: " << tablet_id << ", leader TServer is: "
              << outgoing_tablet_consensus_info->consensus_state().leader_uuid();
    }
  }
}

template <class Resp>
Result<LeaderTabletPeer> LookupLeaderTablet(
    TabletPeerLookupIf* tablet_manager, const TabletId& tablet_id, Resp* resp,
    TabletPeerTablet peer = TabletPeerTablet()) {
  if (peer.tablet_peer) {
    LOG_IF(DFATAL, peer.tablet_peer->tablet_id() != tablet_id)
        << "Mismatching table ids: peer " << peer.tablet_peer->tablet_id() << " vs " << tablet_id;
    LOG_IF(DFATAL, !peer.tablet) << "Empty tablet pointer for tablet id : " << tablet_id;
  } else {
    peer = VERIFY_RESULT(LookupTabletPeer(tablet_manager, tablet_id));
  }
  LeaderTabletPeer result;
  result.FillTabletPeer(std::move(peer));
  auto status = result.FillTerm();
  if(!status.ok()) {
    if(IsErrorCodeNotTheLeader(status)) {
      FillTabletConsensusInfo(resp, tablet_id, result.peer);
    }
    return status;
  }
  return result;
}

template <class Req, class Resp>
void FillTabletConsensusInfoIfRequestOpIdStale(
    tablet::TabletPeerPtr peer, const Req* req, Resp* resp) {
  if constexpr (HasTabletConsensusInfo<Resp>::value) {
    auto outgoing_tablet_consensus_info = resp->mutable_tablet_consensus_info();
    if (auto consensus = peer->GetRaftConsensus()) {
      auto cstate = consensus.get()->GetConsensusStateFromCache();
      if (cstate.has_config() && req->raft_config_opid_index() < cstate.config().opid_index()) {
        outgoing_tablet_consensus_info->set_tablet_id(peer->tablet_id());
        *(outgoing_tablet_consensus_info->mutable_consensus_state()) = cstate;
        VLOG(1) << "Sending out Consensus state for tablet: " << peer->tablet_id()
                  << ", leader TServer is: "
                  << outgoing_tablet_consensus_info->consensus_state().leader_uuid();
      }
    }
  }
}

// The "peer" argument could be provided by the caller in case the caller has already performed
// the LookupTabletPeerOrRespond call, and we only need to fill the leader term.
template<class RespClass>
LeaderTabletPeer LookupLeaderTabletOrRespond(
    TabletPeerLookupIf* tablet_manager,
    const std::string& tablet_id,
    RespClass* resp,
    rpc::RpcContext* context,
    TabletPeerTablet peer = TabletPeerTablet()) {
  auto result = LookupLeaderTablet(tablet_manager, tablet_id, resp, std::move(peer));
  if (!result.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
    return LeaderTabletPeer();
  }

  resp->clear_error();
  return *result;
}

Status CheckPeerIsLeader(const tablet::TabletPeer& tablet_peer);

// Checks if the peer is ready for servicing IOs.
// allow_split_tablet specifies whether to reject requests to tablets which have been already
// split.
Status CheckPeerIsReady(
    const tablet::TabletPeer& tablet_peer, AllowSplitTablet allow_split_tablet);

Result<std::shared_ptr<tablet::AbstractTablet>> GetTablet(
    TabletPeerLookupIf* tablet_manager, const TabletId& tablet_id,
    tablet::TabletPeerPtr tablet_peer, YBConsistencyLevel consistency_level,
    AllowSplitTablet allow_split_tablet, ReadResponsePB* resp = nullptr);

Status CheckWriteThrottling(double score, tablet::TabletPeer* tablet_peer);

class CatalogVersionChecker {
 public:
  explicit CatalogVersionChecker(std::reference_wrapper<TabletServerIf> tablet_server)
      : tablet_server_(tablet_server.get()) {}

  template<class PB>
  Status operator()(const PB& request) {
    if (!(request.has_ysql_db_catalog_version() || request.has_ysql_catalog_version())) {
      return Status::OK();
    }
    SCHECK(!(request.has_ysql_db_catalog_version() && request.has_ysql_catalog_version()),
          InvalidArgument,
          "Both fields ysql_db_catalog_version and ysql_catalog_version are set");
    auto version_info = VERIFY_RESULT(FetchVersionInfo(request));
    if (!tserver_version_info_) {
      tserver_version_info_.emplace(
          version_info.db_oid, GetLastBreakingVersion(version_info.db_oid));
    }

    if (*tserver_version_info_ != version_info) {
      SCHECK_EQ(
          tserver_version_info_->db_oid, version_info.db_oid,
          InvalidArgument, "Different db_oid values are not expected");
      if (version_info.version < tserver_version_info_->version) {
        return STATUS(
            QLError,
            Format("The catalog snapshot used for this transaction has been invalidated: "
                   "expected: $0, got: $1", tserver_version_info_->version, version_info.version),
            TabletServerError(TabletServerErrorPB::MISMATCHED_SCHEMA));
      }
    }
    return Status::OK();
  }

 private:
  using DbOid = boost::optional<uint32_t>;

  struct VersionInfo {
    DbOid db_oid;
    uint64_t version;

    VersionInfo(DbOid db_oid_, uint64_t version_)
        : db_oid(db_oid_), version(version_) {}

    friend bool operator==(const VersionInfo&, const VersionInfo&) = default;
  };

  template<class PB>
  Result<VersionInfo> FetchVersionInfo(const PB& request) const {
    if (request.has_ysql_catalog_version()) {
      return VersionInfo(boost::none, request.ysql_catalog_version());
    }
    DCHECK(request.has_ysql_db_catalog_version());
    SCHECK(FLAGS_ysql_enable_db_catalog_version_mode,
           InvalidArgument,
           "enable_db_catalog_version_mode is not enabled");
    SCHECK(request.has_ysql_db_oid(), InvalidArgument, "ysql_db_oid is not specified");
    return VersionInfo(request.ysql_db_oid(), request.ysql_db_catalog_version());
  }

  [[nodiscard]] uint64_t GetLastBreakingVersion(DbOid db_oid) const;

  TabletServerIf& tablet_server_;
  boost::optional<VersionInfo> tserver_version_info_;
};

}  // namespace tserver
}  // namespace yb

// Macro helpers.

#define RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, context)       \
  do {                                                         \
    Status ss = s;                                             \
    if (PREDICT_FALSE(!ss.ok())) {                             \
      SetupErrorAndRespond((resp)->mutable_error(), ss,        \
                           (context));                         \
      return;                                                  \
    }                                                          \
  } while (0)
