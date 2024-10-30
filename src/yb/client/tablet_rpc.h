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

#include <memory>
#include <string>
#include <unordered_set>

#include "yb/util/flags.h"
#include <gtest/gtest_prod.h>

#include "yb/client/client_fwd.h"

#include "yb/common/hybrid_time.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_fwd.h"

DECLARE_bool(TEST_always_return_consensus_info_for_succeeded_rpc);

namespace yb {

namespace tserver {
class TabletConsensusInfoPB;
class TabletServerServiceProxy;
}

namespace client {
namespace internal {

// A TabletRpc is an RPC that is being sent to a tablet rather than a particular TServer or the
// master leader.  This requires extra work like looking up where the tablet participants are and
// dealing with stale information about where tablet participants/leaders are when retrying.
//
// Most of the code for handling this has been put in a separate class, TabletInvoker; this class
// (TabletRpc) acts as an interface, providing access to RPC class functionality needed by
// TabletInvoker that is not already provided by the rpc::RpcCommand interface.
//
// Many of these virtual methods exist to allow the untemplated TabletInvoker to work with the
// request and/or response of the RPC in a generic manner.  For example,
// SetRequestRaftConfigOpidIndex(-) is used to set the set_raft_config_opid_index field of the
// request if the request type has such a field.
class TabletRpc {
 public:
  virtual const tserver::TabletServerErrorPB* response_error() const = 0;
  virtual void Failed(const Status& status) = 0;

  // attempt_num starts with 1.
  virtual void SendRpcToTserver(int attempt_num) = 0;

  // Called to partially refresh a tablet's tablet peers using information
  // piggybacked from a successful or failed response of a tablet RPC.
  // The responses in this case will have a field called tablet_consensus_info,
  // which carries the tablet server and replicas' raft config information.
  // Returns true if we successfully updated the metacache, otherwise false.
  virtual bool RefreshMetaCacheWithResponse() { return false; }

  virtual void SetRequestRaftConfigOpidIndex(int64_t opid_index) {}

 protected:
  ~TabletRpc() {}
};

tserver::TabletServerErrorPB_Code ErrorCode(const tserver::TabletServerErrorPB* error);

// Returns false iff we detect that the consensus info is unexpectedly missing.
template <class Resp, class Req>
inline bool CheckIfConsensusInfoUnexpectedlyMissing(const Req& request, const Resp& response) {
  if constexpr (tserver::HasTabletConsensusInfo<Resp>::value) {
    if (GetAtomicFlag(&FLAGS_TEST_always_return_consensus_info_for_succeeded_rpc)) {
      // If that flag is set, we expect every successful RPC response (i.e., the response that comes
      // back has no error field) to have its tablet_consensus_info field filled in if the response
      // type has that field.
      if (!response.has_error() && !response.has_tablet_consensus_info()) {
        // This should be a debug build at this point, but this test is somewhat expensive so do it
        // last to minimize how often we need to do it.
        Resp empty;
        if (pb_util::ArePBsEqual(response, empty, /*diff_str=*/ nullptr)) {
          return true;  // we haven't gotten an actual response from an RPC yet...
        }
        LOG(ERROR) << "Detected consensus info unexpectedly missing; request: "
                   << request.DebugString() << ", response: " << response.DebugString();
        return false;
      }
    }
  }
  return true;
}

// See class comment for TabletRpc.
class TabletInvoker {
 public:
  // If table is specified, TabletInvoker can detect that table partitions are stale in case tablet
  // is no longer available and return ClientErrorCode::kTablePartitionListIsStale.
  //
  // Precondition: command and rpc are different interfaces of the same object or (in tests) both
  // nullptr.
  explicit TabletInvoker(const bool local_tserver_only,
                         const bool consistent_prefix,
                         YBClient* client,
                         rpc::RpcCommand* command,
                         TabletRpc* rpc,
                         RemoteTablet* tablet,
                         const std::shared_ptr<const YBTable>& table,
                         rpc::RpcRetrier* retrier,
                         Trace* trace,
                         master::IncludeInactive include_inactive = master::IncludeInactive::kFalse,
                         master::IncludeDeleted include_deleted = master::IncludeDeleted::kFalse);

  virtual ~TabletInvoker();

  void Execute(const std::string& tablet_id, bool leader_only = false);

  // Returns true when whole operation is finished, false otherwise.
  bool Done(Status* status);

  bool IsLocalCall() const;

  const RemoteTabletPtr& tablet() const { return tablet_; }
  std::shared_ptr<tserver::TabletServerServiceProxy> proxy() const;
  ::yb::HostPort ProxyEndpoint() const;
  YBClient& client() const { return *client_; }
  const RemoteTabletServer& current_ts() { return *current_ts_; }
  bool local_tserver_only() const { return local_tserver_only_; }

  bool is_consistent_prefix() const { return consistent_prefix_; }

  bool RefreshTabletInfoWithConsensusInfo(
      const tserver::TabletConsensusInfoPB& tablet_consensus_info);

 private:
  friend class TabletRpcTest;
  FRIEND_TEST(TabletRpcTest, TabletInvokerSelectTabletServerRace);

  void SelectTabletServer();

  // This is an implementation of ReadRpc with consistency level as CONSISTENT_PREFIX. As a result,
  // there is no requirement that the read needs to hit the leader.
  void SelectTabletServerWithConsistentPrefix();

  // This is for Redis ops which always prefer to invoke the local tablet server. In case when it
  // is not the leader, a MOVED response will be returned.
  void SelectLocalTabletServer();

  // Marks all replicas on current_ts_ as failed and retries the write on a
  // new replica.
  Status FailToNewReplica(const Status& reason,
                          const tserver::TabletServerErrorPB* error_code = nullptr,
                          bool consensus_info_refresh_succeeded = false);

  // Called when we finish a lookup (to find the new consensus leader). Retries
  // the rpc after a short delay.
  void LookupTabletCb(const Result<RemoteTabletPtr>& result);

  void InitialLookupTabletDone(const Result<RemoteTabletPtr>& result);

  // If we receive TABLET_NOT_FOUND and current_ts_ is set, that means we contacted a tserver
  // with a tablet_id, but the tserver no longer has that tablet.
  // If we receive ShutdownInProgress status then the tablet is about to shutdown and such tablet
  // should also be considered as not found.
  bool IsTabletConsideredNotFound(
      const tserver::TabletServerErrorPB* error_code, const Status& status) {
    return (status.IsNotFound() &&
        ErrorCode(error_code) == tserver::TabletServerErrorPB::TABLET_NOT_FOUND &&
        current_ts_ != nullptr) || status.IsShutdownInProgress();
  }

  bool IsTabletConsideredNonLeader(
      const tserver::TabletServerErrorPB* error_code, const Status& status) {
    // The error code is undefined for some statuses like Aborted where we don't even send an RPC
    // because the service is unavailable and thus don't have a response with an error code; to
    // handle that here, we only check the error code for statuses we know have valid error codes
    // and may have the error code we are looking for.
    if (ErrorCode(error_code) == tserver::TabletServerErrorPB::NOT_THE_LEADER &&
        current_ts_ != nullptr) {
      return status.IsNotFound() || status.IsIllegalState();
}
    return false;
  }

  YBClient* const client_;

  rpc::RpcCommand* const command_;

  TabletRpc* const rpc_;

  // The tablet that should receive this rpc.
  RemoteTabletPtr tablet_;

  std::string tablet_id_;

  const std::shared_ptr<const YBTable> table_;

  rpc::RpcRetrier* const retrier_;

  // Trace is provided externally and owner of this object should guarantee that it will be alive
  // while this object is alive.
  Trace* const trace_;

  // Whether or not to allow lookups of inactive (hidden) tablets.
  const master::IncludeInactive include_inactive_;

  // Whether or not to allow deleted tablets.
  const master::IncludeDeleted include_deleted_;

  // Used to retry some failed RPCs.
  // Tablet servers that refused the write because they were followers at the time.
  // Cleared when new consensus configuration information arrives from the master.
  struct FollowerData {
    // Last replica error, i.e. reason why it was marked as follower.
    Status status;
    // Error time.
    CoarseTimePoint time;

    std::string ToString() const;
  };

  std::unordered_map<RemoteTabletServer*, FollowerData> followers_;

  const bool local_tserver_only_;

  const bool consistent_prefix_;

  // The TS receiving the write. May change if the write is retried.
  // RemoteTabletServer is taken from YBClient cache, so it is guaranteed that those objects are
  // alive while YBClient is alive. Because we don't delete them, but only add and update.
  RemoteTabletServer* current_ts_ = nullptr;

  // Should we assign new leader in meta cache when successful response is received.
  bool assign_new_leader_ = false;
};

Status ErrorStatus(const tserver::TabletServerErrorPB* error);
template <class Response>
HybridTime GetPropagatedHybridTime(const Response& response) {
  return response.has_propagated_hybrid_time() ? HybridTime(response.propagated_hybrid_time())
                                               : HybridTime::kInvalid;
}

} // namespace internal
} // namespace client
} // namespace yb
