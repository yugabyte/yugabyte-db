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
#ifndef YB_TOOLS_YB_ADMIN_CLIENT_H
#define YB_TOOLS_YB_ADMIN_CLIENT_H

#include <boost/optional.hpp>

#include "yb/client/yb_table_name.h"
#include "yb/util/status.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/common/entity_ids.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"

namespace yb {

namespace consensus {
class ConsensusServiceProxy;
}

namespace client {
class YBClient;
}

namespace rpc {
class Messenger;
}

namespace tools {

class ClusterAdminClient {
 public:
  enum PeerMode {
    LEADER = 1,
    FOLLOWER
  };

  // Creates an admin client for host/port combination e.g.,
  // "localhost" or "127.0.0.1:7050".
  ClusterAdminClient(std::string addrs, int64_t timeout_millis);

  virtual ~ClusterAdminClient() = default;

  // Initialized the client and connects to the specified tablet
  // server.
  virtual CHECKED_STATUS Init();

  // Parse the user-specified change type to consensus change type
  CHECKED_STATUS ParseChangeType(
      const std::string& change_type,
      consensus::ChangeConfigType* cc_type);

    // Change the configuration of the specified tablet.
  CHECKED_STATUS ChangeConfig(
      const TabletId& tablet_id,
      const std::string& change_type,
      const PeerId& peer_uuid,
      const boost::optional<std::string>& member_type);

  // Change the configuration of the master tablet.
  CHECKED_STATUS ChangeMasterConfig(
      const std::string& change_type,
      const std::string& peer_host,
      int16 peer_port);

  CHECKED_STATUS DumpMasterState();

  // List all the tables.
  CHECKED_STATUS ListTables();

  // List all tablets of this table
  CHECKED_STATUS ListTablets(const client::YBTableName& table_name, const int max_tablets);

  // Per Tablet list of all tablet servers
  CHECKED_STATUS ListPerTabletTabletServers(const PeerId& tablet_id);

  // Delete a single table by name.
  CHECKED_STATUS DeleteTable(const client::YBTableName& table_name);

  // List all tablet servers known to master
  CHECKED_STATUS ListAllTabletServers();

  // List all masters
  CHECKED_STATUS ListAllMasters();

  // List the log locations of all tablet servers, by uuid
  CHECKED_STATUS ListTabletServersLogLocations();

  // List all the tablets a certain tablet server is serving
  CHECKED_STATUS ListTabletsForTabletServer(const PeerId& ts_uuid);

  CHECKED_STATUS SetLoadBalancerEnabled(const bool is_enabled);

  CHECKED_STATUS GetLoadMoveCompletion();

  CHECKED_STATUS ListLeaderCounts(const client::YBTableName& table_name);

  CHECKED_STATUS SetupRedisTable();

  CHECKED_STATUS DropRedisTable();

 protected:
  // Fetch the locations of the replicas for a given tablet from the Master.
  CHECKED_STATUS GetTabletLocations(const TabletId& tablet_id,
                                    master::TabletLocationsPB* locations);

  // Fetch information about the location of a tablet peer from the leader master.
  CHECKED_STATUS GetTabletPeer(
      const TabletId& tablet_id,
      PeerMode mode,
      master::TSInfoPB* ts_info);

  // Set the uuid and the socket information for a peer of this tablet.
  CHECKED_STATUS SetTabletPeerInfo(
      const TabletId& tablet_id,
      PeerMode mode,
      PeerId* peer_uuid,
      Endpoint* peer_socket);

  // Fetch the latest list of tablet servers from the Master.
  CHECKED_STATUS ListTabletServers(
      google::protobuf::RepeatedPtrField<master::ListTabletServersResponsePB::Entry>* servers);

  // Look up the RPC address of the server with the specified UUID from the Master.
  CHECKED_STATUS GetFirstRpcAddressForTS(const std::string& uuid, HostPort* hp);

  CHECKED_STATUS GetEndpointForHostPort(const HostPort& hp, Endpoint* addr);

  CHECKED_STATUS GetEndpointForTS(const std::string& ts_uuid, Endpoint* ts_addr);

  CHECKED_STATUS LeaderStepDown(
      const PeerId& leader_uuid,
      const std::string& tablet_id,
      std::unique_ptr<consensus::ConsensusServiceProxy>* leader_proxy);

  CHECKED_STATUS StartElection(const std::string& tablet_id);

  CHECKED_STATUS MasterLeaderStepDown(const std::string& leader_uuid);
  CHECKED_STATUS GetMasterLeaderInfo(std::string* leader_uuid);

  const std::string master_addr_list_;
  const MonoDelta timeout_;
  Endpoint leader_sock_;
  bool initted_ = false;
  std::shared_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<master::MasterServiceProxy> master_proxy_;
  std::shared_ptr<client::YBClient> yb_client_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ClusterAdminClient);
};

static constexpr const char* kColumnSep = " \t";

std::string RightPadToUuidWidth(const std::string &s);

}  // namespace tools
}  // namespace yb

#endif // YB_TOOLS_YB_ADMIN_CLIENT_H
