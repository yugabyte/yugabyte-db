// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_YQL_VIRTUAL_TABLE_H
#define YB_MASTER_YQL_VIRTUAL_TABLE_H

#include "yb/common/entity_ids.h"
#include "yb/common/yql_rowblock.h"
#include "yb/common/yql_storage_interface.h"
#include "yb/master/master.h"

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
  virtual CHECKED_STATUS RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const = 0;

  CHECKED_STATUS GetIterator(const Schema& projection,
                             const Schema& schema,
                             HybridTime req_hybrid_time,
                             std::unique_ptr<common::YQLRowwiseIteratorIf>* iter) const override;
  CHECKED_STATUS BuildYQLScanSpec(const YQLReadRequestPB& request,
                                  const HybridTime& hybrid_time,
                                  const Schema& schema,
                                  std::unique_ptr<common::YQLScanSpec>* spec,
                                  HybridTime* req_hybrid_time) const override;
  const Schema& schema() const { return schema_; }

  const TableName& table_name() const { return table_name_; }

 protected:
  // Finds the given column name in the schema and updates the specified column in the given row
  // with the provided value.
  CHECKED_STATUS SetColumnValue(const std::string& col_name, const YQLValuePB& value_pb,
                                YQLRow* row) const;
  const Master* const master_;
  TableName table_name_;
  Schema schema_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_VIRTUAL_TABLE_H
