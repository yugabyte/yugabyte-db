// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_VIRTUAL_TABLE_H
#define YB_MASTER_YQL_VIRTUAL_TABLE_H

#include "yb/common/entity_ids.h"
#include "yb/common/yql_rowblock.h"
#include "yb/common/yql_storage_interface.h"
#include "yb/master/master.h"
#include "yb/master/util/yql_vtable_helpers.h"

namespace yb {
namespace master {

// A YQL virtual table which is based on in memory data.
class YQLVirtualTable : public common::YQLStorageIf {
 public:
  explicit YQLVirtualTable(const TableName& table_name,
                           const Master* const master,
                           const Schema& schema);

  // Retrieves all the data for the yql virtual table in form of a YQLRowBlock. This data is then
  // used by the iterator.
  virtual CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                                      std::unique_ptr<YQLRowBlock>* vtable) const = 0;

  CHECKED_STATUS GetIterator(const YQLReadRequestPB& request,
                             const Schema& projection,
                             const Schema& schema,
                             HybridTime req_hybrid_time,
                             std::unique_ptr<common::YQLRowwiseIteratorIf>* iter) const override;
  CHECKED_STATUS BuildYQLScanSpec(const YQLReadRequestPB& request,
                                  const HybridTime& hybrid_time,
                                  const Schema& schema,
                                  bool include_static_columns,
                                  const Schema& static_projection,
                                  std::unique_ptr<common::YQLScanSpec>* spec,
                                  std::unique_ptr<common::YQLScanSpec>* static_row_spec,
                                  HybridTime* req_hybrid_time) const override;
  const Schema& schema() const { return schema_; }

  const TableName& table_name() const { return table_name_; }

 protected:
  // Finds the given column name in the schema and updates the specified column in the given row
  // with the provided value.
  template<class T>
  CHECKED_STATUS SetColumnValue(const std::string& col_name, const T& value, YQLRow* row) const {
    int column_index = schema_.find_column(col_name);
    if (column_index == Schema::kColumnNotFound) {
      return STATUS_SUBSTITUTE(NotFound, "Couldn't find column $0 in schema", col_name);
    }
    *(row->mutable_column(column_index)) = util::GetValue(value);
    return Status::OK();
  }
  const Master* const master_;
  TableName table_name_;
  Schema schema_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_VIRTUAL_TABLE_H
