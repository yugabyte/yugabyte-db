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
  using util::GetStringValue;
  using util::GetUuidValue;
  using util::GetInetValue;
  using util::GetIntValue;

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
  CHECK_OK(builder.AddKeyColumn(kSystemLocalKeyColumn, YQLType::Create(DataType::STRING)));
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
