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
  explicit YQLColumnsVTable(const Master* const master);
  CHECKED_STATUS RetrieveData(const YQLReadRequestPB& request,
                              std::unique_ptr<YQLRowBlock>* vtable) const;
 protected:
  Schema CreateSchema() const;
 private:
  CHECKED_STATUS PopulateColumnInformation(const Schema& schema,
                                           const std::string& keyspace_name,
                                           const std::string& table_name,
                                           const size_t col_idx,
                                           YQLRow* const row) const;
  static constexpr const char* const kKeyspaceName = "keyspace_name";
  static constexpr const char* const kTableName = "table_name";
  static constexpr const char* const kColumnName = "column_name";
  static constexpr const char* const kClusteringOrder = "clustering_order";
  static constexpr const char* const kColumnNameBytes = "column_name_bytes";
  static constexpr const char* const kKind = "kind";
  static constexpr const char* const kPosition = "position";
  static constexpr const char* const kType = "type";
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_COLUMNS_VTABLE_H
