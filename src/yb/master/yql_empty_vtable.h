// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_EMPTY_VTABLE_H
#define YB_MASTER_YQL_EMPTY_VTABLE_H

#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// Generic virtual table which we currently use for system tables that are empty.
class YQLEmptyVTable : public YQLVirtualTable {
 public:
  explicit YQLEmptyVTable(const Schema& schema);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const override;
};

}  // namespace master
}  // namespace yb

#endif // YB_MASTER_YQL_EMPTY_VTABLE_H
