// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_PARTITIONS_VTABLE_H
#define YB_MASTER_YQL_PARTITIONS_VTABLE_H

#include "yb/master/master.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system.partitions.
class YQLPartitionsVTable : public YQLVirtualTable {
 public:
  explicit YQLPartitionsVTable(const Master* const master);
  CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                              std::unique_ptr<YQLRowBlock>* vtable) const;
 protected:
  Schema CreateSchema() const;
 private:
  static constexpr const char* const kKeyspaceName = "keyspace_name";
  static constexpr const char* const kTableName = "table_name";
  static constexpr const char* const kStartKey = "start_key";
  static constexpr const char* const kEndKey = "end_key";
  static constexpr const char* const kRpcAddresses = "rpc_addresses";
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_PARTITIONS_VTABLE_H
