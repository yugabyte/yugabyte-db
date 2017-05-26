// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_peers_vtable.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

namespace yb {
namespace master {

using std::string;
using std::shared_ptr;
using std::unique_ptr;
using std::map;

PeersVTable::PeersVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemPeersTableName, master, CreateSchema()) {
}

Status PeersVTable::RetrieveData(const YQLReadRequestPB& request,
                                 unique_ptr<YQLRowBlock>* vtable) const {
  using util::GetInetValue;
  using util::GetIntValue;
  using util::GetUuidValue;
  using util::GetStringValue;

  // Retrieve all lives nodes known by the master.
  // TODO: Ideally we would like to populate this table with all valid nodes of the cluster, but
  // currently the master just has a list of all nodes it has heard from and which one of those
  // are dead. As a result, the master can't distinguish between nodes that are part of the
  // cluster and are dead vs nodes that have been removed from the cluster. Since, we might
  // change the cluster topology often, for now its safe to just have the live nodes here.
  vector<shared_ptr<TSDescriptor> > descs;
  master_->ts_manager()->GetAllLiveDescriptors(&descs);

  // Collect all unique ip addresses.
  map<InetAddress, TSInformationPB> peers;
  for (const shared_ptr<TSDescriptor>& desc : descs) {
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
    const string& ts_host = ts_info.registration().common().rpc_addresses(0).host();
    // The system.peers table has one entry for each of its peers, whereas there is no entry for
    // the node that the CQL client connects to. In this case, this node is the 'remote_endpoint'
    // in YQLReadRequestPB since that is address of the CQL proxy which sent this request. As a
    // result, skip 'remote_endpoint' in the results.
    if (ts_host != request.remote_endpoint().host()) {
      InetAddress addr;
      RETURN_NOT_OK(addr.FromString(ts_host));
      peers[addr] = ts_info;
    }
  }

  // Populate the YQL rows.
  vtable->reset(new YQLRowBlock(schema_));
  for (const auto& kv : peers) {
    YQLValuePB inet_addr = util::GetInetValue(kv.first);

    YQLRow& row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kPeer, inet_addr, &row));
    RETURN_NOT_OK(SetColumnValue(kRPCAddress, inet_addr, &row));
    RETURN_NOT_OK(SetColumnValue(kPreferredIp, inet_addr, &row));

    // Datacenter and rack.
    CloudInfoPB cloud_info = kv.second.registration().common().cloud_info();
    RETURN_NOT_OK(SetColumnValue(kDataCenter,
                                 util::GetStringValue(cloud_info.placement_region()), &row));
    RETURN_NOT_OK(SetColumnValue(kRack, util::GetStringValue(cloud_info.placement_zone()), &row));

    // HostId.
    Uuid host_id;
    RETURN_NOT_OK(host_id.FromHexString(kv.second.tserver_instance().permanent_uuid()));
    RETURN_NOT_OK(SetColumnValue(kHostId, util::GetUuidValue(host_id), &row));

    // schema_version.
    Uuid schema_version;
    CHECK_OK(schema_version.FromString(master::kDefaultSchemaVersion));
    RETURN_NOT_OK(SetColumnValue(kSchemaVersion, util::GetUuidValue(schema_version), &row));

    // Tokens.
    RETURN_NOT_OK(SetColumnValue(kTokens, util::GetTokensValue(), &row));
  }
  return Status::OK();
}

Schema PeersVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kPeer, YQLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kDataCenter, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kHostId, YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kPreferredIp, YQLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kRack, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kReleaseVersion, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kRPCAddress, YQLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSchemaVersion, YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kTokens, YQLType::CreateTypeSet(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
