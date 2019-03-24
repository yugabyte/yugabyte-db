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

#include "yb/master/yql_local_vtable.h"
#include "yb/master/ts_descriptor.h"

namespace yb {
namespace master {

LocalVTable::LocalVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemLocalTableName, master, CreateSchema()) {
}

Status LocalVTable::RetrieveData(const QLReadRequestPB& request,
                                 std::unique_ptr<QLRowBlock>* vtable) const {
  vector<std::shared_ptr<TSDescriptor> > descs;
  GetSortedLiveDescriptors(&descs);
  vtable->reset(new QLRowBlock(schema_));

  InetAddress remote_endpoint;
  RETURN_NOT_OK(remote_endpoint.FromString(request.remote_endpoint().host()));

  size_t index = 0;
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    ++index;
    // This is thread safe since all operations are reads.
    TSInformationPB ts_info = desc->GetTSInformationPB();

    // The system.local table contains only a single entry for the host that we are connected
    // to and hence we need to look for the 'remote_endpoint' here.
    if (!request.proxy_uuid().empty()) {
      if (desc->permanent_uuid() != request.proxy_uuid()) {
        continue;
      }
    } else if (!util::RemoteEndpointMatchesTServer(ts_info, remote_endpoint)) {
      continue;
    }
    QLRow& row = (*vtable)->Extend();
    auto ips = VERIFY_RESULT(util::GetPublicPrivateIPs(ts_info));
    const CloudInfoPB& cloud_info = ts_info.registration().common().cloud_info();
    RETURN_NOT_OK(SetColumnValue(kSystemLocalKeyColumn, "local", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalBootstrappedColumn, "COMPLETED", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalBroadcastAddressColumn, ips.public_ip, &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalClusterNameColumn, "local cluster", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalCQLVersionColumn, "3.4.2", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalDataCenterColumn, cloud_info.placement_region(),
                                 &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalGossipGenerationColumn, 0, &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalListenAddressColumn, ips.private_ip, &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalNativeProtocolVersionColumn, "4", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalPartitionerColumn,
                                 "org.apache.cassandra.dht.Murmur3Partitioner", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalRackColumn, cloud_info.placement_zone(), &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalReleaseVersionColumn, "3.9-SNAPSHOT", &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalRpcAddressColumn, ips.public_ip, &row));

    Uuid schema_version;
    RETURN_NOT_OK(schema_version.FromString(master::kDefaultSchemaVersion));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalSchemaVersionColumn, schema_version, &row));
    RETURN_NOT_OK(SetColumnValue(kSystemLocalThriftVersionColumn, "20.1.0", &row));
    // setting tokens
    RETURN_NOT_OK(SetColumnValue(kSystemLocalTokensColumn,
                                 util::GetTokensValue(index - 1, descs.size()), &row));
    break;
  }

  return Status::OK();
}

Schema LocalVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kSystemLocalKeyColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalBootstrappedColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalBroadcastAddressColumn, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalClusterNameColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalCQLVersionColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalDataCenterColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalGossipGenerationColumn, QLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kSystemLocalHostIdColumn, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kSystemLocalListenAddressColumn, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalNativeProtocolVersionColumn,
                             QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalPartitionerColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalRackColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalReleaseVersionColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalRpcAddressColumn, QLType::Create(DataType::INET)));
  CHECK_OK(builder.AddColumn(kSystemLocalSchemaVersionColumn, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kSystemLocalThriftVersionColumn, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalTokensColumn, QLType::CreateTypeSet(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kSystemLocalTruncatedAtColumn,
                             QLType::CreateTypeMap(DataType::UUID, DataType::BINARY)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
