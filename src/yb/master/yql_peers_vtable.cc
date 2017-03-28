// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_peers_vtable.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

namespace yb {
namespace master {

PeersVTable::PeersVTable(const Schema& schema, Master* master)
    : YQLVirtualTable(schema),
      master_(master) {
}

Status PeersVTable::RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const {
  // Retrieve all the nodes known by the master.
  vector<std::shared_ptr<TSDescriptor> > descs;
  master_->ts_manager()->GetAllDescriptors(&descs);

  // Collect all unique ip addresses.
  std::set<InetAddress> peers;
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    TSRegistrationPB registration;
    // This is thread safe since all operations are reads.
    desc->GetRegistration(&registration);

    if (registration.common().rpc_addresses_size() == 0) {
      return STATUS_SUBSTITUTE(IllegalState,
                               "tserver $0 doesn't have any rpc addresses registered",
                               desc->permanent_uuid());
    }

    // Need to use only 1 rpc address per node since system.peers has only 1 entry for each host,
    // so pick the first one.
    InetAddress addr;
    RETURN_NOT_OK(addr.FromString(registration.common().rpc_addresses(0).host()));
    peers.insert(addr);
  }

  // Populate the YQL rows.
  vtable->reset(new YQLRowBlock(schema_));
  for (InetAddress addr : peers) {
    YQLValuePB value;
    YQLValue::set_inetaddress_value(addr, &value);
    YQLRow& row = (*vtable)->Extend();
    *row.mutable_column(kPeerColumnIndex) = value;
    *row.mutable_column(kRpcAddrColumnIndex) = value;
  }
  return Status::OK();
}

}  // namespace master
}  // namespace yb
