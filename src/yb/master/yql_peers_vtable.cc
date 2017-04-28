// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_peers_vtable.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

namespace yb {
namespace master {

PeersVTable::PeersVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemPeersTableName, master, CreateSchema()) {
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
    RETURN_NOT_OK(SetColumnValue(kPeer, value, &row));
    RETURN_NOT_OK(SetColumnValue(kRPCAddress, value, &row));

    // uuid.
    Uuid schema_version;
    CHECK_OK(schema_version.FromString("00000000-0000-0000-0000-000000000000"));
    YQLValue::set_uuid_value(schema_version, &value);
    RETURN_NOT_OK(SetColumnValue(kSchemaVersion, value, &row));
  }
  return Status::OK();
}

Schema PeersVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kPeer, DataType::INET));
  CHECK_OK(builder.AddColumn(kDataCenter, DataType::STRING));
  CHECK_OK(builder.AddColumn(kHostId, DataType::UUID));
  CHECK_OK(builder.AddColumn(kPreferredIp, DataType::INET));
  CHECK_OK(builder.AddColumn(kRack, DataType::STRING));
  CHECK_OK(builder.AddColumn(kReleaseVersion, DataType::STRING));
  CHECK_OK(builder.AddColumn(kRPCAddress, DataType::INET));
  CHECK_OK(builder.AddColumn(kSchemaVersion, DataType::UUID));
  CHECK_OK(builder.AddColumn(kTokens, YQLType(DataType::SET, { YQLType(DataType::STRING) })));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
