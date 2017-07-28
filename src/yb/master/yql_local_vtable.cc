// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_local_vtable.h"
#include "yb/master/ts_descriptor.h"

namespace yb {
namespace master {

LocalVTable::LocalVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemLocalTableName, master, CreateSchema()) {
}

Status LocalVTable::RetrieveData(const YQLReadRequestPB& request,
                                 std::unique_ptr<YQLRowBlock>* vtable) const {
  vector<std::shared_ptr<TSDescriptor> > descs;
  GetSortedLiveDescriptors(&descs);
  vtable->reset(new YQLRowBlock(schema_));

  InetAddress remote_endpoint;
  RETURN_NOT_OK(remote_endpoint.FromString(request.remote_endpoint().host()));

  size_t index = 0;
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    TSInformationPB ts_info;
    // This is thread safe since all operations are reads.
    desc->GetTSInformationPB(&ts_info);

    // The system.local table contains only a single entry for the host that we are connected
    // to and hence we need to look for the 'remote_endpoint' here.
    if (util::RemoteEndpointMatchesTServer(ts_info, remote_endpoint)) {
      YQLRow& row = (*vtable)->Extend();
      CloudInfoPB cloud_info = ts_info.registration().common().cloud_info();
      RETURN_NOT_OK(SetColumnValue(kSystemLocalKeyColumn, "local", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalBootstrappedColumn, "COMPLETED", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalBroadcastAddressColumn, remote_endpoint, &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalClusterNameColumn, "local cluster", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalCQLVersionColumn, "3.4.2", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalDataCenterColumn, cloud_info.placement_region(),
                                   &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalGossipGenerationColumn, 0, &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalListenAddressColumn, remote_endpoint, &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalNativeProtocolVersionColumn, "4", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalPartitionerColumn,
                                   "org.apache.cassandra.dht.Murmur3Partitioner", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalRackColumn, cloud_info.placement_zone(), &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalReleaseVersionColumn, "3.9-SNAPSHOT", &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalRpcAddressColumn,
                                   remote_endpoint, &row));

      Uuid schema_version;
      RETURN_NOT_OK(schema_version.FromString(master::kDefaultSchemaVersion));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalSchemaVersionColumn, schema_version, &row));
      RETURN_NOT_OK(SetColumnValue(kSystemLocalThriftVersionColumn, "20.1.0", &row));
      // setting tokens
      RETURN_NOT_OK(SetColumnValue(kSystemLocalTokensColumn,
                                   util::GetTokensValue(index, descs.size()), &row));
      break;
    }
    index++;
  }

  return Status::OK();
}

Schema LocalVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kSystemLocalKeyColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalBootstrappedColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalBroadcastAddressColumn, YQLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalClusterNameColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalCQLVersionColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalDataCenterColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalGossipGenerationColumn, YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kSystemLocalHostIdColumn, YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kSystemLocalListenAddressColumn, YQLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalNativeProtocolVersionColumn,
                             YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalPartitionerColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalRackColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalReleaseVersionColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalRpcAddressColumn, YQLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalSchemaVersionColumn, YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kSystemLocalThriftVersionColumn, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalTokensColumn, YQLType::CreateTypeSet(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalTruncatedAtColumn,
                             YQLType::CreateTypeMap(DataType::UUID, DataType::BINARY)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
