// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_empty_vtable.h"

namespace yb {
namespace master {

YQLEmptyVTable::YQLEmptyVTable(const Schema& schema)
    : YQLVirtualTable(schema) {
}

Status YQLEmptyVTable::RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const {
  // Empty rowblock.
  vtable->reset(new YQLRowBlock(schema_));
  return Status::OK();
}

}  // namespace master
}  // namespace yb
