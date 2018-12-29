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

#include "yb/master/yql_peers_vtable.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/ts_descriptor.h"

namespace yb {
namespace master {

using std::string;
using std::shared_ptr;
using std::unique_ptr;
using std::map;

PeersVTable::PeersVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemPeersTableName, master, CreateSchema()) {
}

Status PeersVTable::RetrieveData(const QLReadRequestPB& request,
                                 unique_ptr<QLRowBlock>* vtable) const {
  // Retrieve all lives nodes known by the master.
  // TODO: Ideally we would like to populate this table with all valid nodes of the cluster, but
  // currently the master just has a list of all nodes it has heard from and which one of those
  // are dead. As a result, the master can't distinguish between nodes that are part of the
  // cluster and are dead vs nodes that have been removed from the cluster. Since, we might
  // change the cluster topology often, for now its safe to just have the live nodes here.
  vector<shared_ptr<TSDescriptor> > descs;
  GetSortedLiveDescriptors(&descs);

  // Collect all unique ip addresses.
  InetAddress remote_endpoint;
  RETURN_NOT_OK(remote_endpoint.FromString(request.remote_endpoint().host()));

  const auto& proxy_uuid = request.proxy_uuid();

  // Populate the YQL rows.
  vtable->reset(new QLRowBlock(schema_));

  size_t index = 0;
  for (const shared_ptr<TSDescriptor>& desc : descs) {
    size_t current_index = index++;

    TSInformationPB ts_info;
    // This is thread safe since all operations are reads.
    desc->GetTSInformationPB(&ts_info);

    if (!proxy_uuid.empty()) {
      if (desc->permanent_uuid() == proxy_uuid) {
        continue;
      }
    } else {
      // In case of old proxy, fallback to old endpoint based mechanism.
      if (util::RemoteEndpointMatchesTServer(ts_info, remote_endpoint)) {
        continue;
      }
    }

    // The system.peers table has one entry for each of its peers, whereas there is no entry for
    // the node that the CQL client connects to. In this case, this node is the 'remote_endpoint'
    // in QLReadRequestPB since that is address of the CQL proxy which sent this request. As a
    // result, skip 'remote_endpoint' in the results.
    auto ips = util::GetPublicPrivateIPs(ts_info);
    if (!ips.ok()) {
      LOG(ERROR) << "Failed to get IPs from " << ts_info.ShortDebugString() << ": " << ips.status();
      continue;
    }

    // Need to use only 1 rpc address per node since system.peers has only 1 entry for each host,
    // so pick the first one.
    QLRow &row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kPeer, ips->public_ip, &row));
    RETURN_NOT_OK(SetColumnValue(kRPCAddress, ips->public_ip, &row));
    RETURN_NOT_OK(SetColumnValue(kPreferredIp, ips->private_ip, &row));

    // Datacenter and rack.
    CloudInfoPB cloud_info = ts_info.registration().common().cloud_info();
    RETURN_NOT_OK(SetColumnValue(kDataCenter, cloud_info.placement_region(), &row));
    RETURN_NOT_OK(SetColumnValue(kRack, cloud_info.placement_zone(), &row));

    // HostId.
    Uuid host_id;
    RETURN_NOT_OK(host_id.FromHexString(ts_info.tserver_instance().permanent_uuid()));
    RETURN_NOT_OK(SetColumnValue(kHostId, host_id, &row));

    // schema_version.
    Uuid schema_version;
    RETURN_NOT_OK(schema_version.FromString(master::kDefaultSchemaVersion));
    RETURN_NOT_OK(SetColumnValue(kSchemaVersion, schema_version, &row));

    // Tokens.
    RETURN_NOT_OK(SetColumnValue(
        kTokens, util::GetTokensValue(current_index, descs.size()), &row));
  }

  return Status::OK();
}

Schema PeersVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kPeer, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kDataCenter, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kHostId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kPreferredIp, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kRack, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kReleaseVersion, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kRPCAddress, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSchemaVersion, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kTokens, QLType::CreateTypeSet(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
