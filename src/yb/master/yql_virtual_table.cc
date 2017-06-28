// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_virtual_table.h"
#include "yb/master/yql_vtable_iterator.h"

namespace yb {
namespace master {

YQLVirtualTable::YQLVirtualTable(const TableName& table_name,
                                 const Master* const master,
                                 const Schema& schema)
    : master_(master),
      table_name_(table_name),
      schema_(schema) {
}

CHECKED_STATUS YQLVirtualTable::GetIterator(const YQLReadRequestPB& request,
                                            const Schema& projection,
                                            const Schema& schema,
                                            HybridTime req_hybrid_time,
                                            std::unique_ptr<common::YQLRowwiseIteratorIf>* iter)
                                            const {
  std::unique_ptr<YQLRowBlock> vtable;
  RETURN_NOT_OK(RetrieveData(request, &vtable));

  // If hashed column values are specified, filter by the hash key.
  if (!request.hashed_column_values().empty()) {

    std::vector<int> hashed_column_indices;
    for (const YQLColumnValuePB& hashed_column : request.hashed_column_values()) {
      const ColumnId column_id(hashed_column.column_id());
      hashed_column_indices.emplace_back(schema_.find_column_by_id(column_id));
    }
    const auto& hashed_column_values = request.hashed_column_values();

    std::vector<YQLRow>& rows = vtable->rows();
    auto excluded_rows = std::remove_if(
        rows.begin(), rows.end(),
        [&hashed_column_indices, &hashed_column_values](const YQLRow& row) -> bool {
          for (size_t i = 0; i < hashed_column_values.size(); i++) {
            if (hashed_column_values.Get(i).expr().value() !=
                row.column(hashed_column_indices[i])) {
              return true;
            }
          }
          return false;
        });
    rows.erase(excluded_rows, rows.end());
  }

  iter->reset(new YQLVTableIterator(std::move(vtable)));
  return Status::OK();
}

CHECKED_STATUS YQLVirtualTable::BuildYQLScanSpec(const YQLReadRequestPB& request,
                                                 const HybridTime& hybrid_time,
                                                 const Schema& schema,
                                                 const bool include_static_columns,
                                                 const Schema& static_projection,
                                                 std::unique_ptr<common::YQLScanSpec>* spec,
                                                 std::unique_ptr<common::YQLScanSpec>*
                                                 static_row_spec,
                                                 HybridTime* req_hybrid_time) const {
  // There should be no static columns in system tables so we are not handling it.
  if (include_static_columns) {
    return STATUS(IllegalState, "system table contains no static columns");
  }
  spec->reset(new common::YQLScanSpec(
      request.has_where_expr() ? &request.where_expr().condition() : nullptr));
  *req_hybrid_time = hybrid_time;
  return Status::OK();
}

}  // namespace master
}  // namespace yb
