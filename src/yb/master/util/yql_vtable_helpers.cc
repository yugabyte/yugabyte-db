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
  QLValue::set_set_value(&value_pb);
  QLValuePB *token = QLValue::add_set_elem(&value_pb);
  token->set_string_value(YBPartition::CqlTokenSplit(node_count, index));
  return value_pb;
}

bool RemoteEndpointMatchesTServer(const TSInformationPB& ts_info,
                                  const InetAddress& remote_endpoint) {
  for (HostPortPB rpc_address : ts_info.registration().common().rpc_addresses()) {
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

QLValuePB GetReplicationValue(int replication_factor) {
  QLValuePB value_pb;
  QLValue::set_map_value(&value_pb);

  // replication strategy
  QLValuePB *elem = QLValue::add_map_key(&value_pb);
  elem->set_string_value("class");
  elem = QLValue::add_map_value(&value_pb);
  elem->set_string_value("org.apache.cassandra.locator.SimpleStrategy");

  // replication factor
  elem = QLValue::add_map_key(&value_pb);
  elem->set_string_value("replication_factor");
  elem = QLValue::add_map_value(&value_pb);
  elem->set_string_value(std::to_string(replication_factor));

  return value_pb;
}

}  // namespace util
}  // namespace master
}  // namespace yb
