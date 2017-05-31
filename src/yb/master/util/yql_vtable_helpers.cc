// Copyright (c) YugaByte, Inc.

#include "yb/master/util/yql_vtable_helpers.h"

namespace yb {
namespace master {
namespace util {

// TODO (mihnea) when partitioning issue is solved this should take arguments and return the
// appropriate result for each node.
YQLValuePB GetTokensValue() {
  YQLValuePB value_pb;
  YQLValue::set_set_value(&value_pb);
  YQLValuePB *token = YQLValue::add_set_elem(&value_pb);
  token->set_string_value("0");
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

// TODO (mihnea) when partitioning issue is solved use the right values here
YQLValuePB GetReplicationValue() {
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
  elem->set_string_value("1");

  return value_pb;
}

}  // namespace util
}  // namespace master
}  // namespace yb
