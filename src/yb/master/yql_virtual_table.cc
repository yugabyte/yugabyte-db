// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_virtual_table.h"
#include "yb/master/yql_vtable_iterator.h"

namespace yb {
namespace master {

YQLVirtualTable::YQLVirtualTable(const Schema& schema)
    : schema_(schema) {
}

CHECKED_STATUS YQLVirtualTable::GetIterator(const Schema& projection,
                                            const Schema& schema,
                                            HybridTime req_hybrid_time,
                                            std::unique_ptr<common::YQLRowwiseIteratorIf>* iter)
                                            const {
  std::unique_ptr<YQLRowBlock> vtable;
  RETURN_NOT_OK(RetrieveData(&vtable));
  iter->reset(new YQLVTableIterator(std::move(vtable)));
  return Status::OK();
}

CHECKED_STATUS YQLVirtualTable::BuildYQLScanSpec(const YQLReadRequestPB& request,
                                                 const HybridTime& hybrid_time,
                                                 const Schema& schema,
                                                 std::unique_ptr<common::YQLScanSpec>* spec,
                                                 HybridTime* req_hybrid_time) const {
  spec->reset(new common::YQLScanSpec(
      request.has_where_condition() ? &request.where_condition() : nullptr));
  *req_hybrid_time = hybrid_time;
  return Status::OK();
}

CHECKED_STATUS YQLVirtualTable::SetColumnValue(const std::string& col_name,
                                               const YQLValuePB& value_pb,
                                               YQLRow* row) const {
  int column_index = schema_.find_column(col_name);
  if (column_index == Schema::kColumnNotFound) {
    return STATUS_SUBSTITUTE(NotFound, "Couldn't find column $0 in schema", col_name);
  }
  *(row->mutable_column(column_index)) = value_pb;
  return Status::OK();
}

}  // namespace master
}  // namespace yb
