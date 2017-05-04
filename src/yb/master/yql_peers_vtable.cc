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
  // Retrieve all lives nodes known by the master.
  // TODO: Ideally we would like to populate this table with all valid nodes of the cluster, but
  // currently the master just has a list of all nodes it has heard from and which one of those
  // are dead. As a result, the master can't distinguish between nodes that are part of the
  // cluster and are dead vs nodes that have been removed from the cluster. Since, we might
  // change the cluster topology often, for now its safe to just have the live nodes here.
  vector<std::shared_ptr<TSDescriptor> > descs;
  master_->ts_manager()->GetAllLiveDescriptors(&descs);

  // Collect all unique ip addresses.
  std::map<InetAddress, TSInformationPB> peers;
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    TSInformationPB ts_info;
    // This is thread safe since all operations are reads.
    desc->GetTSInformationPB(&ts_info);

    if (ts_info.registration().common().rpc_addresses_size() == 0) {
      return STATUS_SUBSTITUTE(IllegalState,
                               "tserver $0 doesn't have any rpc addresses registered",
                               desc->permanent_uuid());
    }

    // Need to use only 1 rpc address per node since system.peers has only 1 entry for each host,
    // so pick the first one.
    InetAddress addr;
    RETURN_NOT_OK(addr.FromString(ts_info.registration().common().rpc_addresses(0).host()));
    peers[addr] = ts_info;
  }

  // Populate the YQL rows.
  vtable->reset(new YQLRowBlock(schema_));
  for (const auto& kv : peers) {
    YQLValuePB value;
    YQLValue::set_inetaddress_value(kv.first, &value);

    YQLRow& row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kPeer, value, &row));
    RETURN_NOT_OK(SetColumnValue(kRPCAddress, value, &row));
    RETURN_NOT_OK(SetColumnValue(kPreferredIp, value, &row));

    // Datacenter and rack.
    CloudInfoPB cloud_info = kv.second.registration().common().cloud_info();
    YQLValue::set_string_value(cloud_info.placement_region(), &value);
    RETURN_NOT_OK(SetColumnValue(kDataCenter, value, &row));
    YQLValue::set_string_value(cloud_info.placement_zone(), &value);
    RETURN_NOT_OK(SetColumnValue(kRack, value, &row));

    // HostId.
    Uuid host_id;
    RETURN_NOT_OK(host_id.FromHexString(kv.second.tserver_instance().permanent_uuid()));
    YQLValue::set_uuid_value(host_id, &value);
    RETURN_NOT_OK(SetColumnValue(kHostId, value, &row));

    // schema_version.
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
