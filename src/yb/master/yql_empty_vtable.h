// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_EMPTY_VTABLE_H
#define YB_MASTER_YQL_EMPTY_VTABLE_H

#include "yb/common/schema.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// Generic virtual table which we currently use for system tables that are empty. Although we are
// not sure when the class will be deleted since currently it does not look like we need to populate
// some system tables.
class YQLEmptyVTable : public YQLVirtualTable {
 public:
  explicit YQLEmptyVTable(const TableName& table_name,
                          const Master* const master,
                          const Schema& schema);
  CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                              std::unique_ptr<YQLRowBlock>* vtable) const;
};

}  // namespace master
}  // namespace yb

#endif // YB_MASTER_YQL_EMPTY_VTABLE_H
