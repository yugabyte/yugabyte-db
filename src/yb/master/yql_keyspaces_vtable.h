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
  YQLKeyspacesVTable(const Schema& schema, Master* master);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const override;
 private:
  const Master* const master_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_KEYSPACES_VTABLE_H
