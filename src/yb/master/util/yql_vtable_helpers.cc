// Copyright (c) YugaByte, Inc.

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
YQLValuePB GetTokensValue(size_t index, size_t node_count) {
  CHECK_GT(node_count, 0);
  YQLValuePB value_pb;
  YQLValue::set_set_value(&value_pb);
  YQLValuePB *token = YQLValue::add_set_elem(&value_pb);
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

YQLValuePB GetReplicationValue(int replication_factor) {
  YQLValuePB value_pb;
  YQLValue::set_map_value(&value_pb);

  // replication strategy
  YQLValuePB *elem = YQLValue::add_map_key(&value_pb);
  elem->set_string_value("class");
  elem = YQLValue::add_map_value(&value_pb);
  elem->set_string_value("org.apache.cassandra.locator.SimpleStrategy");

  // replication factor
  elem = YQLValue::add_map_key(&value_pb);
  elem->set_string_value("replication_factor");
  elem = YQLValue::add_map_value(&value_pb);
  elem->set_string_value(std::to_string(replication_factor));

  return value_pb;
}

}  // namespace util
}  // namespace master
}  // namespace yb
