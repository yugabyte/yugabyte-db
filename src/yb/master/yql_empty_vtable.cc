// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_empty_vtable.h"

namespace yb {
namespace master {

YQLEmptyVTable::YQLEmptyVTable(const TableName& table_name,
                               const Master* const master,
                               const Schema& schema)
    : YQLVirtualTable(table_name, master, schema) {
}

Status YQLEmptyVTable::RetrieveData(std::unique_ptr<YQLRowBlock> *vtable) const {
  // Empty rowblock.
  vtable->reset(new YQLRowBlock(schema_));
  return Status::OK();
}

}  // namespace master
}  // namespace yb
