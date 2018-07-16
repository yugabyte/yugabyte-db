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

#include "yb/util/yb_partition.h"
#include "yb/master/util/yql_vtable_helpers.h"

namespace yb {
namespace master {
namespace util {

// Ideally, we want clients to use YB's own load-balancing policy for Cassandra to route the
// requests to the respective nodes hosting the partition keys. But for clients using vanilla
// drivers and thus Cassandra's own token-aware policy, we still want the requests to hit our nodes
// evenly. To do that, we split Cassandra's token ring (signed 64-bit number space) evenly and
// return the token for each node in the node list.
QLValuePB GetTokensValue(size_t index, size_t node_count) {
  CHECK_GT(node_count, 0);
  QLValuePB value_pb;
  QLValuePB *token = value_pb.mutable_set_value()->add_elems();
  token->set_string_value(YBPartition::CqlTokenSplit(node_count, index));
  return value_pb;
}

bool RemoteEndpointMatchesList(const google::protobuf::RepeatedPtrField<HostPortPB>& host_ports,
                               const InetAddress& remote_endpoint) {
  for (const HostPortPB& rpc_address : host_ports) {
    // host portion of rpc_address might be a hostname and hence we need to resolve it.
    vector<InetAddress> resolved_addresses;
    if (!InetAddress::Resolve(rpc_address.host(), &resolved_addresses).ok()) {
      LOG (WARNING) << "Could not resolve host: " << rpc_address.host();
      continue;
    }
    if (std::find(resolved_addresses.begin(), resolved_addresses.end(), remote_endpoint) !=
        resolved_addresses.end()) {
      return true;
    }
  }

  return false;
}

bool RemoteEndpointMatchesTServer(const TSInformationPB& ts_info,
                                  const InetAddress& remote_endpoint) {
  const auto& common = ts_info.registration().common();
  if (RemoteEndpointMatchesList(common.private_rpc_addresses(), remote_endpoint)) {
    return true;
  }
  if (RemoteEndpointMatchesList(common.broadcast_addresses(), remote_endpoint)) {
    return true;
  }
  return false;
}

QLValuePB GetReplicationValue(int replication_factor) {
  QLValuePB value_pb;
  QLMapValuePB *map_value = value_pb.mutable_map_value();

  // replication strategy
  QLValuePB *elem = map_value->add_keys();
  elem->set_string_value("class");
  elem = map_value->add_values();
  elem->set_string_value("org.apache.cassandra.locator.SimpleStrategy");

  // replication factor
  elem = map_value->add_keys();
  elem->set_string_value("replication_factor");
  elem = map_value->add_values();
  elem->set_string_value(std::to_string(replication_factor));

  return value_pb;
}

Result<PublicPrivateIPs> GetPublicPrivateIPs(const TSInformationPB& ts_info) {
  const auto& private_host = ts_info.registration().common().private_rpc_addresses()[0].host();
  if (private_host.empty()) {
    return STATUS_SUBSTITUTE(IllegalState,
        "tserver $0 doesn't have any rpc addresses registered",
        ts_info.tserver_instance().permanent_uuid());
  }
  const auto* broadcast_address = !ts_info.registration().common().broadcast_addresses().empty() ?
      &ts_info.registration().common().broadcast_addresses()[0].host() : &private_host;

  PublicPrivateIPs result;
  RETURN_NOT_OK(result.private_ip.FromString(private_host));
  if (broadcast_address != &private_host) {
    RETURN_NOT_OK(result.public_ip.FromString(*broadcast_address));
  } else {
    result.public_ip = result.private_ip;
  }

  return result;
}

}  // namespace util
}  // namespace master
}  // namespace yb
