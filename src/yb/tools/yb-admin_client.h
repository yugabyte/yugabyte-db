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

#include "yb/consensus/consensus.pb.h"
#include "yb/master/master.pb.h"

namespace yb {

namespace consensus {
class ConsensusServiceProxy;
}

namespace client {
class YBClient;
}

namespace master {
class MasterServiceProxy;
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

  // Initialized the client and connects to the specified tablet
  // server.
  Status Init();

  // Parse the user-specified change type to consensus change type
  Status ParseChangeType(
    const std::string& change_type,
    consensus::ChangeConfigType* cc_type);

    // Change the configuration of the specified tablet.
  Status ChangeConfig(
    const std::string& tablet_id,
    const std::string& change_type,
    const std::string& peer_uuid,
    const boost::optional<std::string>& member_type);

  // Change the configuration of the master tablet.
  Status ChangeMasterConfig(
    const std::string& change_type,
    const std::string& peer_host,
    int16 peer_port);

  Status DumpMasterState();

  // List all the tables.
  Status ListTables();

  // List all tablets of this table
  Status ListTablets(const client::YBTableName& table_name, const int max_tablets);

  // Per Tablet list of all tablet servers
  Status ListPerTabletTabletServers(const std::string& tablet_id);

  // Delete a single table by name.
  Status DeleteTable(const client::YBTableName& table_name);

  // List all tablet servers known to master
  Status ListAllTabletServers();

  // List all masters
  Status ListAllMasters();

  // List the log locations of all tablet servers, by uuid
  Status ListTabletServersLogLocations();

  // List all the tablets a certain tablet server is serving
  Status ListTabletsForTabletServer(const std::string& ts_uuid);

  Status SetLoadBalancerEnabled(const bool is_enabled);

  Status GetLoadMoveCompletion();

  Status ListLeaderCounts(const client::YBTableName& table_name);

  Status SetupRedisTable();

  Status DropRedisTable();

 private:
  // Fetch the locations of the replicas for a given tablet from the Master.
  Status GetTabletLocations(const std::string& tablet_id,
                            master::TabletLocationsPB* locations);

  // Fetch information about the location of a tablet peer from the leader master.
  Status GetTabletPeer(
    const std::string& tablet_id,
    PeerMode mode,
    master::TSInfoPB* ts_info);

  // Set the uuid and the socket information for a peer of this tablet.
  Status SetTabletPeerInfo(
    const std::string& tablet_id,
    PeerMode mode,
    std::string* peer_uuid,
    Endpoint* peer_socket);

  // Fetch the latest list of tablet servers from the Master.
  Status ListTabletServers(
      google::protobuf::RepeatedPtrField<master::ListTabletServersResponsePB::Entry>* servers);

  // Look up the RPC address of the server with the specified UUID from the Master.
  Status GetFirstRpcAddressForTS(const std::string& uuid, HostPort* hp);

  Status GetEndpointForHostPort(const HostPort& hp, Endpoint* addr);

  Status GetEndpointForTS(const std::string& ts_uuid, Endpoint* ts_addr);

  Status LeaderStepDown(
    const std::string& leader_uuid,
    const std::string& tablet_id,
    std::unique_ptr<consensus::ConsensusServiceProxy>* leader_proxy);

  Status StartElection(const std::string& tablet_id);

  Status MasterLeaderStepDown(const std::string& leader_uuid);
  Status GetMasterLeaderInfo(std::string* leader_uuid);

  const std::string master_addr_list_;
  const MonoDelta timeout_;
  Endpoint leader_sock_;
  bool initted_;
  std::shared_ptr<rpc::Messenger> messenger_;
  gscoped_ptr<master::MasterServiceProxy> master_proxy_;
  std::shared_ptr<client::YBClient> yb_client_;

  DISALLOW_COPY_AND_ASSIGN(ClusterAdminClient);
};

}  // namespace tools
}  // namespace yb

#endif // YB_TOOLS_YB_ADMIN_CLIENT_H
