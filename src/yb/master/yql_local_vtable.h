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

#ifndef YB_MASTER_YQL_LOCAL_VTABLE_H
#define YB_MASTER_YQL_LOCAL_VTABLE_H

#include "yb/master/master.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system.local.
class LocalVTable : public YQLVirtualTable {
 public:
  explicit LocalVTable(const Master* const master_);
  CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                              std::unique_ptr<YQLRowBlock>* vtable) const;

 protected:
  Schema CreateSchema() const;

 private:
  static constexpr const char* const kSystemLocalKeyColumn = "key";
  static constexpr const char* const kSystemLocalBootstrappedColumn = "bootstrapped";
  static constexpr const char* const kSystemLocalBroadcastAddressColumn = "broadcast_address";
  static constexpr const char* const kSystemLocalClusterNameColumn = "cluster_name";
  static constexpr const char* const kSystemLocalCQLVersionColumn = "cql_version";
  static constexpr const char* const kSystemLocalDataCenterColumn = "data_center";
  static constexpr const char* const kSystemLocalGossipGenerationColumn = "gossip_generation";
  static constexpr const char* const kSystemLocalHostIdColumn = "host_id";
  static constexpr const char* const kSystemLocalListenAddressColumn = "listen_address";
  static constexpr const char* const kSystemLocalNativeProtocolVersionColumn =
      "native_protocol_version";
  static constexpr const char* const kSystemLocalPartitionerColumn = "partitioner";
  static constexpr const char* const kSystemLocalRackColumn = "rack";
  static constexpr const char* const kSystemLocalReleaseVersionColumn = "release_version";
  static constexpr const char* const kSystemLocalRpcAddressColumn = "rpc_address";
  static constexpr const char* const kSystemLocalSchemaVersionColumn = "schema_version";
  static constexpr const char* const kSystemLocalThriftVersionColumn = "thrift_version";
  static constexpr const char* const kSystemLocalTokensColumn = "tokens";
  static constexpr const char* const kSystemLocalTruncatedAtColumn = "truncated_at";
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_LOCAL_VTABLE_H
