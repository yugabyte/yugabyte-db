// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_KEYSPACES_VTABLE_H
#define YB_MASTER_YQL_KEYSPACES_VTABLE_H

#include "yb/master/master.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system_schema.keyspaces.
class YQLKeyspacesVTable : public YQLVirtualTable {
 public:
  explicit YQLKeyspacesVTable(const Master* const master);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const override;
 protected:
  Schema CreateSchema(const std::string& table_name) const override;
 private:
  static constexpr const char* const kKeyspaceName = "keyspace_name";
  static constexpr const char* const kDurableWrites = "durable_writes";
  static constexpr const char* const kReplication = "replication";
  const Master* const master_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_KEYSPACES_VTABLE_H
