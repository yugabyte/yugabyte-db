// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_local_vtable.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

namespace yb {
namespace master {

LocalVTable::LocalVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemLocalTableName, master, CreateSchema()) {
}

Status LocalVTable::RetrieveData(const YQLReadRequestPB& request,
                                 std::unique_ptr<YQLRowBlock>* vtable) const {
  vector<std::shared_ptr<TSDescriptor> > descs;
  master_->ts_manager()->GetAllDescriptors(&descs);
  vtable->reset(new YQLRowBlock(schema_));
  for (const std::shared_ptr<TSDescriptor>& desc : descs) {
    TSInformationPB ts_info;
    // This is thread safe since all operations are reads.
    desc->GetTSInformationPB(&ts_info);

    for (HostPortPB rpc_address : ts_info.registration().common().rpc_addresses()) {
      // The system.local table contains only a single entry for the host that we are connected
      // to and hence we need to look for the 'remote_endpoint' here.
      if (rpc_address.host() == request.remote_endpoint().host()) {

        InetAddress addr;
        RETURN_NOT_OK(addr.FromString(rpc_address.host()));

        YQLRow& row = (*vtable)->Extend();
        RETURN_NOT_OK(SetColumnValue(kSystemLocalKeyColumn, util::GetStringValue("local"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalBootstrappedColumn,
                                     util::GetStringValue("COMPLETED"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalBroadcastAddressColumn,
                                     util::GetInetValue(addr), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalClusterNameColumn,
                                     util::GetStringValue("local cluster"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalCQLVersionColumn,
                                     util::GetStringValue("3.4.2"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalDataCenterColumn, util::GetStringValue(""), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalGossipGenerationColumn, util::GetIntValue(0),
                                     &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalListenAddressColumn, util::GetInetValue(addr),
                                     &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalNativeProtocolVersionColumn,
                                     util::GetStringValue("4"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalPartitionerColumn,
                                     util::GetStringValue(
                                         "org.apache.cassandra.dht.Murmur3Partitioner"),
                                     &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalRackColumn, util::GetStringValue("rack"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalReleaseVersionColumn,
                                     util::GetStringValue("3.9-SNAPSHOT"), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalRpcAddressColumn, util::GetInetValue(addr), &row));

        Uuid schema_version;
        RETURN_NOT_OK(schema_version.FromString(master::kDefaultSchemaVersion));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalSchemaVersionColumn,
                                     util::GetUuidValue(schema_version), &row));
        RETURN_NOT_OK(SetColumnValue(kSystemLocalThriftVersionColumn,
                                     util::GetStringValue("20.1.0"), &row));
        // setting tokens
        RETURN_NOT_OK(SetColumnValue(kSystemLocalTokensColumn, util::GetTokensValue(), &row));
        break;
      }
    }

    // Need only 1 row for system.local;
    if ((*vtable)->row_count() == 1) {
      break;
    }
  }

  if ((*vtable)->row_count() != 1) {
    return STATUS_SUBSTITUTE(IllegalState, "system.local should contain exactly 1 row, found $0",
                             (*vtable)->row_count());
  }

  return Status::OK();
}

Schema LocalVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kSystemLocalKeyColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalBootstrappedColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalBroadcastAddressColumn, DataType::INET));
  CHECK_OK(builder.AddColumn(kSystemLocalClusterNameColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalCQLVersionColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalDataCenterColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalGossipGenerationColumn, DataType::INT32));
  CHECK_OK(builder.AddColumn(kSystemLocalHostIdColumn, DataType::UUID));
  CHECK_OK(builder.AddColumn(kSystemLocalListenAddressColumn, DataType::INET));
  CHECK_OK(builder.AddColumn(kSystemLocalNativeProtocolVersionColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalPartitionerColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalRackColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalReleaseVersionColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalRpcAddressColumn, DataType::INET));
  CHECK_OK(builder.AddColumn(kSystemLocalSchemaVersionColumn, DataType::UUID));
  CHECK_OK(builder.AddColumn(kSystemLocalThriftVersionColumn, DataType::STRING));
  CHECK_OK(builder.AddColumn(kSystemLocalTokensColumn,
                                  YQLType(DataType::SET, { YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn(
      kSystemLocalTruncatedAtColumn,
      YQLType(DataType::MAP, { YQLType(DataType::UUID), YQLType(DataType::BINARY) })));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
