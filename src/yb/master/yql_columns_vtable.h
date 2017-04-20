// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_COLUMNS_VTABLE_H
#define YB_MASTER_YQL_COLUMNS_VTABLE_H

#include "yb/master/master.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system_schema.columns.
class YQLColumnsVTable : public YQLVirtualTable {
 public:
  YQLColumnsVTable(const Schema& schema, Master* master);
  CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const override;
 private:
  CHECKED_STATUS PopulateColumnInformation(const Schema& schema,
                                           const YQLValuePB& keyspace_name,
                                           const YQLValuePB& table_name,
                                           const size_t col_idx,
                                           YQLRow* const row) const;
  const Master* const master_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_COLUMNS_VTABLE_H
